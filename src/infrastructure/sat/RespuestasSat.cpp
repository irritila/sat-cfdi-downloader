#include "infrastructure/sat/RespuestasSat.h"

#include <QList>
#include <QXmlStreamReader>

#include <functional>
#include <utility>

namespace satcfdi::sat {

namespace {

const QString kNsSoap = QStringLiteral("http://schemas.xmlsoap.org/soap/envelope/");
const QString kNsWsu =
    QStringLiteral("http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd");
const QString kNsAutenticacion = QStringLiteral("http://DescargaMasivaTerceros.gob.mx");
const QString kNsServicios = QStringLiteral("http://DescargaMasivaTerceros.sat.gob.mx");

ErrorRespuesta error(ErrorRespuesta::Tipo tipo, const char* diagnostico)
{
    ErrorRespuesta e;
    e.tipo = tipo;
    e.diagnostico = QString::fromLatin1(diagnostico);
    return e;
}

struct Nodo {
    QString ns;
    QString nombre;
};

// Pila de elementos abiertos; `pila.last()` es el elemento actual.
using Pila = QList<Nodo>;

bool es(const Nodo& n, const QString& ns, QStringView nombre)
{
    return n.ns == ns && n.nombre == nombre;
}

// Padre del elemento actual (o nullptr).
const Nodo* padre(const Pila& pila)
{
    return pila.size() >= 2 ? &pila.at(pila.size() - 2) : nullptr;
}

// Texto completo del elemento actual (lee hasta su EndElement).
QString textoElemento(QXmlStreamReader& xml, Pila& pila)
{
    QString t = xml.readElementText(QXmlStreamReader::SkipChildElements);
    pila.removeLast();
    return t;
}

// Recorre el documento con una pila de elementos; captura un s:Fault SOAP
// 1.1 (namespace de sobre). `elemento` se llama en cada StartElement con la
// pila ya actualizada; si consume el elemento (readElementText) debe hacer
// pop de la pila (textoElemento lo hace).
std::optional<ErrorRespuesta> recorrer(const QByteArray& cuerpo,
                                       const std::function<void(QXmlStreamReader&, Pila&)>& elemento)
{
    QXmlStreamReader xml(cuerpo);
    std::optional<FaultSoap> fault;
    Pila pila;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.tokenType() == QXmlStreamReader::DTD) {
            return error(ErrorRespuesta::Tipo::XmlMalFormado, "respuesta.dtd_no_permitido");
        }
        if (xml.isEndElement()) {
            if (!pila.isEmpty()) {
                pila.removeLast();
            }
            continue;
        }
        if (!xml.isStartElement()) {
            continue;
        }
        pila.append(Nodo{xml.namespaceUri().toString(), xml.name().toString()});
        if (es(pila.last(), kNsSoap, u"Fault")) {
            FaultSoap f;
            int profundidad = 1;
            QString actual;
            while (!xml.atEnd() && profundidad > 0) {
                xml.readNext();
                if (xml.isStartElement()) {
                    ++profundidad;
                    if (profundidad == 2) {
                        actual = xml.name().toString();
                    }
                } else if (xml.isEndElement()) {
                    --profundidad;
                } else if (xml.isCharacters() && !xml.isWhitespace()) {
                    const QString texto = xml.text().toString().trimmed();
                    if (actual == u"faultcode") {
                        f.codigo += texto;
                    } else if (actual == u"faultstring") {
                        f.mensaje += texto;
                    } else if (actual == u"detail" && f.detalle.size() < 500) {
                        f.detalle += (f.detalle.isEmpty() ? QString() : QStringLiteral(" ")) + texto;
                    }
                }
            }
            pila.removeLast();
            f.detalle.truncate(500);
            fault = f;
            continue;
        }
        elemento(xml, pila);
    }
    if (xml.hasError()) {
        return error(ErrorRespuesta::Tipo::XmlMalFormado, "respuesta.xml_mal_formado");
    }
    if (fault) {
        ErrorRespuesta e = error(ErrorRespuesta::Tipo::Fault, "respuesta.fault");
        e.fault = std::move(fault);
        return e;
    }
    return std::nullopt;
}

QString atributo(const QXmlStreamReader& xml, QStringView nombre)
{
    return xml.attributes().value(nombre).toString();
}

bool entero(const QString& texto, std::optional<int>& destino)
{
    if (texto.isEmpty()) {
        return true; // ausente
    }
    bool ok = false;
    const int v = texto.trimmed().toInt(&ok);
    if (!ok) {
        return false;
    }
    destino = v;
    return true;
}

// Lee el texto del elemento actual directo a un BufferSecreto: sin QString
// intermedio si llega en un solo bloque; si llega fragmentado, acumula y
// sobrescribe el acumulado.
BufferSecreto textoSecreto(QXmlStreamReader& xml, Pila& pila, bool& valido)
{
    BufferSecreto r;
    QString acumulado;
    int bloques = 0;
    valido = true;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isCharacters()) {
            const QStringView t = xml.text();
            if (bloques == 0) {
                r = BufferSecreto::desdeTexto(t.trimmed());
            } else {
                if (bloques == 1) {
                    acumulado = QString::fromUtf8(reinterpret_cast<const char*>(r.datos()),
                                                  static_cast<qsizetype>(r.tamano()));
                }
                acumulado += t;
            }
            ++bloques;
        } else if (xml.isEndElement()) {
            break;
        } else if (xml.isStartElement()) {
            valido = false; // el token no lleva hijos
            xml.skipCurrentElement();
        }
    }
    pila.removeLast();
    if (bloques > 1) {
        r = BufferSecreto::desdeTexto(QStringView(acumulado).trimmed());
        acumulado.fill(QLatin1Char('\0'));
    }
    return r;
}

} // namespace

std::optional<FaultSoap> parsearFault(const QByteArray& cuerpo)
{
    auto e = recorrer(cuerpo, [](QXmlStreamReader&, Pila&) {});
    if (e && e->tipo == ErrorRespuesta::Tipo::Fault) {
        return e->fault;
    }
    return std::nullopt;
}

Resultado<RespuestaAutentica, ErrorRespuesta> parsearAutentica(const QByteArray& cuerpo)
{
    using R = Resultado<RespuestaAutentica, ErrorRespuesta>;
    BufferSecreto token;
    bool hayResultado = false;
    bool tokenValido = true;
    QString creado;
    QString expira;
    auto e = recorrer(cuerpo, [&](QXmlStreamReader& xml, Pila& pila) {
        const Nodo& n = pila.last();
        const Nodo* p = padre(pila);
        if (!hayResultado && es(n, kNsAutenticacion, u"AutenticaResult") && p != nullptr
            && es(*p, kNsAutenticacion, u"AutenticaResponse")) {
            hayResultado = true;
            token = textoSecreto(xml, pila, tokenValido);
        } else if (p != nullptr && es(*p, kNsWsu, u"Timestamp") && es(n, kNsWsu, u"Created") && creado.isEmpty()) {
            creado = textoElemento(xml, pila).trimmed();
        } else if (p != nullptr && es(*p, kNsWsu, u"Timestamp") && es(n, kNsWsu, u"Expires") && expira.isEmpty()) {
            expira = textoElemento(xml, pila).trimmed();
        }
    });
    if (e) {
        return R::fallo(std::move(*e));
    }
    if (!hayResultado) {
        return R::fallo(error(ErrorRespuesta::Tipo::SinResultado, "autentica.sin_resultado"));
    }
    if (token.vacio() || !tokenValido) {
        return R::fallo(error(ErrorRespuesta::Tipo::ValorInvalido, "autentica.token_invalido"));
    }
    return R::exito(RespuestaAutentica{TokenSat(std::move(token), QDateTime::fromString(creado, Qt::ISODateWithMs),
                                                QDateTime::fromString(expira, Qt::ISODateWithMs))});
}

Resultado<RespuestaAutentica, ErrorRespuesta> parsearAutenticaYLimpiar(QByteArray& cuerpo)
{
    auto r = parsearAutentica(cuerpo);
    if (!cuerpo.isDetached()) {
        cuerpo = QByteArray(); // compartido: solo se suelta esta referencia
    } else {
        secretos::limpiarMemoria(cuerpo.data(), static_cast<std::size_t>(cuerpo.size()));
        cuerpo.clear();
    }
    return r;
}

Resultado<RespuestaSolicitud, ErrorRespuesta> parsearSolicitud(const QByteArray& cuerpo)
{
    using R = Resultado<RespuestaSolicitud, ErrorRespuesta>;
    std::optional<RespuestaSolicitud> r;
    auto e = recorrer(cuerpo, [&](QXmlStreamReader& xml, Pila& pila) {
        const Nodo& n = pila.last();
        const Nodo* p = padre(pila);
        if (r || p == nullptr || n.ns != kNsServicios || p->ns != kNsServicios) {
            return;
        }
        for (QStringView op : {u"SolicitaDescargaEmitidos", u"SolicitaDescargaRecibidos", u"SolicitaDescargaFolio"}) {
            if (n.nombre == op.toString() + u"Result" && p->nombre == op.toString() + u"Response") {
                r = RespuestaSolicitud{atributo(xml, u"IdSolicitud"), atributo(xml, u"RfcSolicitante"),
                                       atributo(xml, u"CodEstatus"), atributo(xml, u"Mensaje")};
                return;
            }
        }
    });
    if (e) {
        return R::fallo(std::move(*e));
    }
    if (!r) {
        return R::fallo(error(ErrorRespuesta::Tipo::SinResultado, "solicitud.sin_resultado"));
    }
    return R::exito(std::move(*r));
}

Resultado<RespuestaVerificacion, ErrorRespuesta> parsearVerificacion(const QByteArray& cuerpo)
{
    using R = Resultado<RespuestaVerificacion, ErrorRespuesta>;
    std::optional<RespuestaVerificacion> r;
    bool valido = true;
    auto e = recorrer(cuerpo, [&](QXmlStreamReader& xml, Pila& pila) {
        const Nodo& n = pila.last();
        const Nodo* p = padre(pila);
        if (p == nullptr) {
            return;
        }
        if (!r && es(n, kNsServicios, u"VerificaSolicitudDescargaResult")
            && es(*p, kNsServicios, u"VerificaSolicitudDescargaResponse")) {
            RespuestaVerificacion v;
            v.codEstatus = atributo(xml, u"CodEstatus");
            v.mensaje = atributo(xml, u"Mensaje");
            v.codigoEstadoSolicitud = atributo(xml, u"CodigoEstadoSolicitud");
            valido = entero(atributo(xml, u"EstadoSolicitud"), v.estadoSolicitud)
                     && entero(atributo(xml, u"NumeroCFDIs"), v.numeroCfdis);
            r = std::move(v);
        } else if (r && es(n, kNsServicios, u"IdsPaquetes") && es(*p, kNsServicios, u"VerificaSolicitudDescargaResult")) {
            const QString id = textoElemento(xml, pila).trimmed();
            if (!id.isEmpty()) {
                r->idsPaquetes.append(id);
            }
        }
    });
    if (e) {
        return R::fallo(std::move(*e));
    }
    if (!r) {
        return R::fallo(error(ErrorRespuesta::Tipo::SinResultado, "verificacion.sin_resultado"));
    }
    if (!valido) {
        return R::fallo(error(ErrorRespuesta::Tipo::ValorInvalido, "verificacion.entero"));
    }
    return R::exito(std::move(*r));
}

Resultado<RespuestaDescarga, ErrorRespuesta> parsearDescarga(const QByteArray& cuerpo)
{
    using R = Resultado<RespuestaDescarga, ErrorRespuesta>;
    RespuestaDescarga r;
    bool hayEstatus = false;
    bool hayPaquete = false;
    bool base64Valido = true;
    auto e = recorrer(cuerpo, [&](QXmlStreamReader& xml, Pila& pila) {
        const Nodo& n = pila.last();
        const Nodo* p = padre(pila);
        if (p == nullptr) {
            return;
        }
        // SUPUESTO (phpcfdi): s:Header/h:respuesta con CodEstatus y Mensaje.
        if (!hayEstatus && es(n, kNsServicios, u"respuesta") && es(*p, kNsSoap, u"Header")) {
            hayEstatus = true;
            r.codEstatus = atributo(xml, u"CodEstatus");
            r.mensaje = atributo(xml, u"Mensaje");
        } else if (!hayPaquete && es(n, kNsServicios, u"Paquete")
                   && es(*p, kNsServicios, u"RespuestaDescargaMasivaTercerosSalida")) {
            hayPaquete = true;
            const QByteArray b64 = textoElemento(xml, pila).toLatin1();
            auto decodificado = QByteArray::fromBase64Encoding(b64, QByteArray::AbortOnBase64DecodingErrors);
            if (!decodificado) {
                base64Valido = false;
                return;
            }
            r.paquete = std::move(*decodificado);
            r.tamanoPaquete = r.paquete.size();
        }
    });
    if (e) {
        return R::fallo(std::move(*e));
    }
    if (!hayEstatus) {
        return R::fallo(error(ErrorRespuesta::Tipo::SinResultado, "descarga.sin_estatus"));
    }
    if (!base64Valido) {
        return R::fallo(error(ErrorRespuesta::Tipo::ValorInvalido, "descarga.paquete_base64"));
    }
    return R::exito(std::move(r));
}

} // namespace satcfdi::sat
