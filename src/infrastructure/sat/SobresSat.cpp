#include "infrastructure/sat/SobresSat.h"

#include "domain/common/Rfc.h"

#include <QTimeZone>
#include <QUuid>

#include <utility>

namespace satcfdi::sat {

namespace {

const QByteArray kNsSoap = "http://schemas.xmlsoap.org/soap/envelope/";
const QByteArray kNsDsig = "http://www.w3.org/2000/09/xmldsig#";
const QByteArray kNsWsu = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd";
const QByteArray kNsWsse = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd";
const QByteArray kValueTypeX509 =
    "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-x509-token-profile-1.0#X509v3";
const QByteArray kEncodingBase64 =
    "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-soap-message-security-1.0#Base64Binary";
const QByteArray kRsaSha1 = "http://www.w3.org/2000/09/xmldsig#rsa-sha1";
const QByteArray kSha1 = "http://www.w3.org/2000/09/xmldsig#sha1";
const QByteArray kEnveloped = "http://www.w3.org/2000/09/xmldsig#enveloped-signature";
const QByteArray kMarcaFirma = "FIRMA_PENDIENTE_SATCFDI";

using R = Resultado<SobreSat, ErrorSobre>;

ErrorSobre error(ErrorSobre::Tipo tipo, const char* diagnostico)
{
    return ErrorSobre{tipo, QString::fromLatin1(diagnostico)};
}

QByteArray escapar(const QString& texto)
{
    QByteArray r;
    const QByteArray utf8 = texto.toUtf8();
    r.reserve(utf8.size());
    for (char c : utf8) {
        switch (c) {
        case '&': r.append("&amp;"); break;
        case '<': r.append("&lt;"); break;
        case '>': r.append("&gt;"); break;
        case '"': r.append("&quot;"); break;
        default: r.append(c); break;
        }
    }
    return r;
}

QByteArray uri(const QString& u)
{
    return u.toLatin1();
}

// Atributos en el orden dado, omitiendo los vacios.
QByteArray atributos(const QList<std::pair<const char*, QString>>& lista)
{
    QByteArray r;
    for (const auto& [nombre, valor] : lista) {
        if (!valor.isEmpty()) {
            r.append(' ').append(nombre).append("=\"").append(escapar(valor)).append('"');
        }
    }
    return r;
}

QString textoFechaSolicitud(const QDateTime& f)
{
    // Fecha y hora tal cual, sin zona (xs:dateTime local, doc §5.3).
    return f.date().toString(QStringLiteral("yyyy-MM-dd")) + QLatin1Char('T')
           + f.time().toString(QStringLiteral("HH:mm:ss"));
}

std::optional<QString> rfcValido(const QString& texto)
{
    return rfc::normalizarYValidar(texto);
}

QByteArray signedInfo(VarianteC14n declarada, const QByteArray& referencia, const QByteArray& transforms,
                      const QByteArray& digest)
{
    const QByteArray c14n = uri(uriAlgoritmo(declarada));
    return "<SignedInfo><CanonicalizationMethod Algorithm=\"" + c14n + "\"/><SignatureMethod Algorithm=\""
           + kRsaSha1 + "\"/><Reference URI=\"" + referencia + "\"><Transforms>" + transforms
           + "</Transforms><DigestMethod Algorithm=\"" + kSha1 + "\"/><DigestValue>" + digest
           + "</DigestValue></Reference></SignedInfo>";
}

// Canonicaliza SignedInfo dentro de `documento`, firma y reemplaza la marca.
Resultado<QByteArray, ErrorSobre> firmarDocumento(QByteArray documento, const MaterialFirma& material,
                                                  VarianteC14n variante)
{
    using RF = Resultado<QByteArray, ErrorSobre>;
    auto canon = canonicalizar(documento, variante, SelectorNodo::porNombre(kNsDsig, "SignedInfo"));
    if (!canon) {
        return RF::fallo(ErrorSobre{ErrorSobre::Tipo::Xml, canon.error().diagnostico});
    }
    auto firma = firmarRsaSha1(material, canon.valor());
    if (!firma) {
        return RF::fallo(ErrorSobre{ErrorSobre::Tipo::Firma, firma.error().diagnostico});
    }
    if (documento.count(kMarcaFirma) != 1) {
        return RF::fallo(error(ErrorSobre::Tipo::Xml, "sobre.marca_firma"));
    }
    documento.replace(kMarcaFirma, firma.valor().toBase64());
    return RF::exito(std::move(documento));
}

// Sobre con firma enveloped dentro de des:<nodo>.
R sobreEnveloped(Operacion operacion, const char* operacionXml, const char* nodo, const QByteArray& atributosNodo,
                 const QByteArray& hijos, const MaterialFirma& material, const OpcionesFirma& opciones)
{
    const QByteArray ns = uri(descriptor(operacion).espacioNombres);
    const QByteArray prefijo = "<s:Envelope xmlns:s=\"" + kNsSoap + "\" xmlns:des=\"" + ns + "\" xmlns:xd=\""
                               + kNsDsig + "\"><s:Header/><s:Body><des:" + operacionXml + "><des:" + nodo
                               + atributosNodo + ">" + hijos;
    const QByteArray sufijo =
        QByteArray("</des:") + nodo + "></des:" + operacionXml + "></s:Body></s:Envelope>";

    // 1. Digest del nodo de peticion sin firma (transform enveloped).
    auto canon = canonicalizar(prefijo + sufijo, opciones.c14n, SelectorNodo::porNombre(ns, nodo, true));
    if (!canon) {
        return R::fallo(ErrorSobre{ErrorSobre::Tipo::Xml, canon.error().diagnostico});
    }
    const QByteArray digest = sha1Base64(canon.valor());

    auto cert = leerDatosCertificado(material.certificadoDer(), opciones.formatoIssuer);
    if (!cert) {
        return R::fallo(ErrorSobre{ErrorSobre::Tipo::Firma, cert.error().diagnostico});
    }
    QByteArray transforms = "<Transform Algorithm=\"" + kEnveloped + "\"/>";
    if (opciones.declarada() == VarianteC14n::Exclusiva) {
        transforms += "<Transform Algorithm=\"" + uri(uriAlgoritmo(VarianteC14n::Exclusiva)) + "\"/>";
    }
    const QByteArray firma = "<Signature xmlns=\"" + kNsDsig + "\">"
                             + signedInfo(opciones.declarada(), QByteArray(), transforms, digest)
                             + "<SignatureValue>" + kMarcaFirma + "</SignatureValue><KeyInfo><X509Data>"
                               "<X509IssuerSerial><X509IssuerName>"
                             + escapar(cert.valor().issuer) + "</X509IssuerName><X509SerialNumber>"
                             + cert.valor().serialDecimal.toLatin1()
                             + "</X509SerialNumber></X509IssuerSerial><X509Certificate>"
                             + cert.valor().certificadoBase64 + "</X509Certificate></X509Data></KeyInfo></Signature>";

    auto firmado = firmarDocumento(prefijo + firma + sufijo, material, opciones.c14n);
    if (!firmado) {
        return R::fallo(std::move(firmado).error());
    }
    return R::exito(SobreSat{operacion, std::move(firmado).valor(), digest});
}

R solicitud(Operacion operacion, const ParametrosSolicitud& p, const MaterialFirma& material,
            const OpcionesFirma& opciones)
{
    const bool emitidos = operacion == Operacion::SolicitaDescargaEmitidos;
    const auto solicitante = rfcValido(p.rfcSolicitante);
    if (!solicitante) {
        return R::fallo(error(ErrorSobre::Tipo::Parametros, "solicitud.rfc_solicitante"));
    }
    if (!p.fechaInicial.isValid() || !p.fechaFinal.isValid() || p.fechaFinal < p.fechaInicial) {
        return R::fallo(error(ErrorSobre::Tipo::Parametros, "solicitud.fechas"));
    }
    if (p.tipoSolicitud.isEmpty()) {
        return R::fallo(error(ErrorSobre::Tipo::Parametros, "solicitud.tipo_solicitud"));
    }
    QString emisor;
    QString receptor;
    if (!p.rfcEmisor.isEmpty()) {
        const auto r = rfcValido(p.rfcEmisor);
        if (!r) {
            return R::fallo(error(ErrorSobre::Tipo::Parametros, "solicitud.rfc_emisor"));
        }
        emisor = *r;
    }
    if (!p.rfcReceptor.isEmpty()) {
        const auto r = rfcValido(p.rfcReceptor);
        if (!r) {
            return R::fallo(error(ErrorSobre::Tipo::Parametros, "solicitud.rfc_receptor"));
        }
        receptor = *r;
    }
    if (emitidos && (emisor.isEmpty() || !receptor.isEmpty())) {
        return R::fallo(error(ErrorSobre::Tipo::Parametros, "solicitud.emitidos_rfc"));
    }
    if (!emitidos && (receptor.isEmpty() || !p.rfcReceptores.isEmpty())) {
        return R::fallo(error(ErrorSobre::Tipo::Parametros, "solicitud.recibidos_rfc"));
    }
    QByteArray hijos;
    if (!p.rfcReceptores.isEmpty()) {
        hijos = "<des:RfcReceptores>";
        for (const QString& c : p.rfcReceptores) {
            const auto r = rfcValido(c);
            if (!r) {
                return R::fallo(error(ErrorSobre::Tipo::Parametros, "solicitud.rfc_receptores"));
            }
            hijos += "<des:RfcReceptor>" + escapar(*r) + "</des:RfcReceptor>";
        }
        hijos += "</des:RfcReceptores>";
    }
    // Orden doc §5.6 (RfcReceptor, propio de Recibidos, entre RfcEmisor y
    // RfcSolicitante: supuesto alfabetico).
    const QByteArray attrs = atributos({
        {"Complemento", p.complemento},
        {"EstadoComprobante", p.estadoComprobante},
        {"FechaInicial", textoFechaSolicitud(p.fechaInicial)},
        {"FechaFinal", textoFechaSolicitud(p.fechaFinal)},
        {"RfcACuentaTerceros", p.rfcACuentaTerceros},
        {"RfcEmisor", emisor},
        {"RfcReceptor", receptor},
        {"RfcSolicitante", *solicitante},
        {"TipoComprobante", p.tipoComprobante},
        {"TipoSolicitud", p.tipoSolicitud},
    });
    return sobreEnveloped(operacion, emitidos ? "SolicitaDescargaEmitidos" : "SolicitaDescargaRecibidos",
                          "solicitud", attrs, hijos, material, opciones);
}

} // namespace

OpcionesFirma OpcionesFirma::porDefecto(Operacion operacion)
{
    OpcionesFirma o;
    o.c14n = descriptor(operacion).c14nPorDefecto;
    return o;
}

QString textoTimestamp(const QDateTime& instante)
{
    return instante.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz")) + QLatin1Char('Z');
}

Resultado<SobreSat, ErrorSobre> construirAutentica(const MaterialFirma& material, const ContextoSobre& contexto,
                                                   const OpcionesFirma& opciones)
{
    if (!contexto.ahoraUtc.isValid() || contexto.vigencia.count() <= 0) {
        return R::fallo(error(ErrorSobre::Tipo::Parametros, "autentica.reloj"));
    }
    const QDateTime creado = contexto.ahoraUtc.toUTC();
    const QDateTime expira = creado.addSecs(contexto.vigencia.count());
    const QString idToken = contexto.idToken.isEmpty()
                                ? QStringLiteral("uuid-") + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral("-1")
                                : contexto.idToken;
    if (idToken.contains(QLatin1Char('"')) || idToken.contains(QLatin1Char('<')) || idToken == QStringLiteral("_0")) {
        return R::fallo(error(ErrorSobre::Tipo::Parametros, "autentica.id_token"));
    }
    auto cert = leerDatosCertificado(material.certificadoDer(), opciones.formatoIssuer);
    if (!cert) {
        return R::fallo(ErrorSobre{ErrorSobre::Tipo::Firma, cert.error().diagnostico});
    }
    const QByteArray id = idToken.toLatin1();
    const QByteArray prefijo = "<s:Envelope xmlns:s=\"" + kNsSoap + "\" xmlns:u=\"" + kNsWsu
                               + "\"><s:Header><o:Security s:mustUnderstand=\"1\" xmlns:o=\"" + kNsWsse
                               + "\"><u:Timestamp u:Id=\"_0\"><u:Created>" + textoTimestamp(creado).toLatin1()
                               + "</u:Created><u:Expires>" + textoTimestamp(expira).toLatin1()
                               + "</u:Expires></u:Timestamp><o:BinarySecurityToken u:Id=\"" + id + "\" ValueType=\""
                               + kValueTypeX509 + "\" EncodingType=\"" + kEncodingBase64 + "\">"
                               + cert.valor().certificadoBase64 + "</o:BinarySecurityToken>";
    QByteArray addressing;
    if (opciones.wsAddressing) {
        const QByteArray nsNone = "http://schemas.microsoft.com/ws/2005/05/addressing/none";
        addressing = "<To s:mustUnderstand=\"1\" xmlns=\"" + nsNone + "\">"
                     + escapar(descriptor(Operacion::Autentica).endpoint.toString()) + "</To><Action s:mustUnderstand=\"1\" xmlns=\""
                     + nsNone + "\">" + escapar(descriptor(Operacion::Autentica).soapAction) + "</Action>";
    }
    const QByteArray sufijo = "</o:Security>" + addressing + "</s:Header><s:Body><Autentica xmlns=\""
                              + uri(descriptor(Operacion::Autentica).espacioNombres) + "\"/></s:Body></s:Envelope>";

    auto canon = canonicalizar(prefijo + sufijo, opciones.c14n, SelectorNodo::porId("_0"));
    if (!canon) {
        return R::fallo(ErrorSobre{ErrorSobre::Tipo::Xml, canon.error().diagnostico});
    }
    const QByteArray digest = sha1Base64(canon.valor());
    const QByteArray transforms = "<Transform Algorithm=\"" + uri(uriAlgoritmo(opciones.declarada())) + "\"/>";
    const QByteArray firma = "<Signature xmlns=\"" + kNsDsig + "\">"
                             + signedInfo(opciones.declarada(), "#_0", transforms, digest) + "<SignatureValue>"
                             + kMarcaFirma + "</SignatureValue><KeyInfo><o:SecurityTokenReference><o:Reference ValueType=\""
                             + kValueTypeX509 + "\" URI=\"#" + id
                             + "\"/></o:SecurityTokenReference></KeyInfo></Signature>";
    auto firmado = firmarDocumento(prefijo + firma + sufijo, material, opciones.c14n);
    if (!firmado) {
        return R::fallo(std::move(firmado).error());
    }
    return R::exito(SobreSat{Operacion::Autentica, std::move(firmado).valor(), digest});
}

Resultado<SobreSat, ErrorSobre> construirSolicitudEmitidos(const ParametrosSolicitud& parametros,
                                                           const MaterialFirma& material,
                                                           const OpcionesFirma& opciones)
{
    return solicitud(Operacion::SolicitaDescargaEmitidos, parametros, material, opciones);
}

Resultado<SobreSat, ErrorSobre> construirSolicitudRecibidos(const ParametrosSolicitud& parametros,
                                                            const MaterialFirma& material,
                                                            const OpcionesFirma& opciones)
{
    return solicitud(Operacion::SolicitaDescargaRecibidos, parametros, material, opciones);
}

Resultado<SobreSat, ErrorSobre> construirVerificacion(const QString& idSolicitud, const QString& rfcSolicitante,
                                                      const MaterialFirma& material, const OpcionesFirma& opciones)
{
    const auto rfc = rfcValido(rfcSolicitante);
    if (!rfc || idSolicitud.trimmed().isEmpty()) {
        return R::fallo(error(ErrorSobre::Tipo::Parametros, "verificacion.parametros"));
    }
    const QByteArray attrs = atributos({{"IdSolicitud", idSolicitud.trimmed()}, {"RfcSolicitante", *rfc}});
    return sobreEnveloped(Operacion::VerificaSolicitudDescarga, "VerificaSolicitudDescarga", "solicitud", attrs,
                          {}, material, opciones);
}

Resultado<SobreSat, ErrorSobre> construirDescarga(const QString& idPaquete, const QString& rfcSolicitante,
                                                  const MaterialFirma& material, const OpcionesFirma& opciones)
{
    const auto rfc = rfcValido(rfcSolicitante);
    if (!rfc || idPaquete.trimmed().isEmpty()) {
        return R::fallo(error(ErrorSobre::Tipo::Parametros, "descarga.parametros"));
    }
    // SUPUESTO (WSDL no publicado): PeticionDescargaMasivaTercerosEntrada/
    // peticionDescarga, como la referencia secundaria phpcfdi.
    const QByteArray attrs = atributos({{"IdPaquete", idPaquete.trimmed()}, {"RfcSolicitante", *rfc}});
    return sobreEnveloped(Operacion::Descargar, "PeticionDescargaMasivaTercerosEntrada", "peticionDescarga", attrs,
                          {}, material, opciones);
}

} // namespace satcfdi::sat
