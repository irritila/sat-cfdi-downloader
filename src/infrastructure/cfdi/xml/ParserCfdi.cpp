#include "infrastructure/cfdi/xml/ParserCfdi.h"

#include "domain/cfdi/UuidCfdi.h"

#include <QByteArray>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>

#include <initializer_list>
#include <vector>

// Parser de CFDI (T015.1, T015 D5/D6/D11). Sin logs: ni contenido, ni RFC,
// ni nombres, ni UUID, ni montos, ni sellos. El llamador registra, si acaso,
// el estado y la CausaIlegible, que no llevan datos del XML.

namespace satcfdi {

using namespace Qt::StringLiterals;

namespace {

constexpr auto kNsCfdi4 = "http://www.sat.gob.mx/cfd/4"_L1;
constexpr auto kNsCfdi3 = "http://www.sat.gob.mx/cfd/3"_L1;
constexpr auto kNsTimbre = "http://www.sat.gob.mx/TimbreFiscalDigital"_L1;
constexpr auto kNsPagos20 = "http://www.sat.gob.mx/Pagos20"_L1;
constexpr auto kNsPagos10 = "http://www.sat.gob.mx/Pagos"_L1;
constexpr auto kNsNomina12 = "http://www.sat.gob.mx/nomina12"_L1;

// --- Validaciones previas sobre bytes crudos -------------------------------

bool empiezaConBomUtf8(QByteArrayView d)
{
    return d.size() >= 3 && static_cast<unsigned char>(d[0]) == 0xEF && static_cast<unsigned char>(d[1]) == 0xBB
           && static_cast<unsigned char>(d[2]) == 0xBF;
}

// UTF-8 estricto (RFC 3629): sin sobrelargas, sin sustitutos, <= U+10FFFF.
// Rechaza tambien NUL, que nunca es un caracter XML valido y delata UTF-16/32.
bool esUtf8Valido(QByteArrayView d)
{
    const auto* p = reinterpret_cast<const unsigned char*>(d.data());
    const qsizetype n = d.size();
    qsizetype i = 0;
    while (i < n) {
        const unsigned char c = p[i];
        if (c == 0) {
            return false;
        }
        if (c < 0x80) {
            ++i;
            continue;
        }
        qsizetype largo = 0;
        char32_t cp = 0;
        if (c >= 0xC2 && c <= 0xDF) {
            largo = 2;
            cp = c & 0x1Fu;
        } else if ((c & 0xF0u) == 0xE0u) {
            largo = 3;
            cp = c & 0x0Fu;
        } else if (c >= 0xF0 && c <= 0xF4) {
            largo = 4;
            cp = c & 0x07u;
        } else {
            return false; // continuacion suelta, C0/C1 o F5-FF
        }
        if (n - i < largo) {
            return false;
        }
        for (qsizetype k = 1; k < largo; ++k) {
            const unsigned char cc = p[i + k];
            if ((cc & 0xC0u) != 0x80u) {
                return false;
            }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (largo == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) {
            return false;
        }
        if (largo == 4 && (cp < 0x10000 || cp > 0x10FFFF)) {
            return false;
        }
        i += largo;
    }
    return true;
}

// Cualquier "<!DOCTYPE" (sin distinguir mayusculas) en cualquier parte. Es
// deliberadamente conservador: un CFDI no usa DTD y asi ninguna declaracion de
// entidad llega a QXmlStreamReader.
bool contieneDoctype(QByteArrayView d)
{
    constexpr QByteArrayView kPatron("<!DOCTYPE");
    qsizetype desde = 0;
    while (true) {
        const qsizetype pos = d.indexOf("<!", desde);
        if (pos < 0 || d.size() - pos < kPatron.size()) {
            return false;
        }
        if (d.sliced(pos, kPatron.size()).compare(kPatron, Qt::CaseInsensitive) == 0) {
            return true;
        }
        desde = pos + 2;
    }
}

// --- Parser -----------------------------------------------------------------

enum class Contexto {
    Otro,
    Comprobante,
    Relacionados,
    Conceptos,
    Concepto,
    ComplementoConcepto,
    Impuestos,
    Traslados,
    Retenciones,
    Complemento,
    Pagos,
    Pago,
};

enum class Modo {
    Completo,     // 4.0 o 3.3
    Minimo,       // 3.2 u otra version: solo datos minimos
};

ResultadoParseoCfdi ilegible(CausaIlegible causa)
{
    ResultadoParseoCfdi r;
    r.resumen.estado = EstadoParseo::Ilegible;
    r.causaIlegible = causa;
    return r;
}

ResultadoParseoCfdi noEsCfdi()
{
    ResultadoParseoCfdi r;
    r.resumen.estado = EstadoParseo::NoEsCfdi;
    return r;
}

class Parser {
public:
    ResultadoParseoCfdi ejecutar(QByteArrayView xml);

private:
    // Primer atributo presente entre `nombres` (3.2 usa otra capitalizacion).
    static QString texto(const QXmlStreamAttributes& a, std::initializer_list<QLatin1StringView> nombres)
    {
        for (const QLatin1StringView nombre : nombres) {
            if (a.hasAttribute(nombre)) {
                return a.value(nombre).toString();
            }
        }
        return QString();
    }

    static QString texto(const QXmlStreamAttributes& a, QLatin1StringView nombre)
    {
        return a.value(nombre).toString();
    }

    std::optional<ImporteCfdi> decimal(const QXmlStreamAttributes& a, QLatin1StringView elemento,
        std::initializer_list<QLatin1StringView> nombres, bool requerido = false)
    {
        for (const QLatin1StringView nombre : nombres) {
            if (!a.hasAttribute(nombre)) {
                continue;
            }
            auto valor = ImporteCfdi::desdeTexto(a.value(nombre));
            if (!valor) {
                marcarInvalido(elemento, *nombres.begin());
            }
            return valor;
        }
        if (requerido) {
            marcarInvalido(elemento, *nombres.begin());
        }
        return std::nullopt;
    }

    std::optional<ImporteCfdi> decimal(const QXmlStreamAttributes& a, QLatin1StringView elemento,
        QLatin1StringView nombre, bool requerido = false)
    {
        return decimal(a, elemento, {nombre}, requerido);
    }

    void marcarInvalido(QLatin1StringView elemento, QLatin1StringView atributo)
    {
        const QString campo = QString(elemento) + QChar(u'@') + atributo;
        if (!m_resumen.camposInvalidos.contains(campo)
            && m_resumen.camposInvalidos.size() < LimitesParserCfdi::kMaxCamposInvalidos) {
            m_resumen.camposInvalidos.append(campo);
        }
    }

    void registrarComplemento(QStringView ns, QStringView local)
    {
        ComplementoNoRepresentado c{ns.toString(), local.toString()};
        if (!m_resumen.complementosNoRepresentados.contains(c)
            && m_resumen.complementosNoRepresentados.size() < LimitesParserCfdi::kMaxComplementosNombrados) {
            m_resumen.complementosNoRepresentados.append(std::move(c));
        }
    }

    // true si cabe uno mas; si no, cuenta el omitido.
    bool cabe(qsizetype actuales, int limite)
    {
        if (actuales < limite) {
            return true;
        }
        ++m_detalle.elementosOmitidosPorLimite;
        return false;
    }

    // Devuelve false si la raiz no es un cfdi:Comprobante.
    bool procesarRaiz(QStringView ns, QStringView local, const QXmlStreamAttributes& a);
    Contexto procesarHijo(Contexto padre, QStringView ns, QStringView local, const QXmlStreamAttributes& a);
    void leerEmisor(const QXmlStreamAttributes& a);
    void leerReceptor(const QXmlStreamAttributes& a);
    void leerConcepto(const QXmlStreamAttributes& a);
    void leerTimbre(const QXmlStreamAttributes& a);
    void leerNomina(const QXmlStreamAttributes& a);
    Contexto procesarComplemento(QStringView ns, QStringView local, const QXmlStreamAttributes& a);
    ResultadoParseoCfdi finalizar();

    Modo m_modo = Modo::Completo;
    QString m_nsRaiz;
    ResumenCfdi m_resumen;
    DetalleCfdi m_detalle;
    QString m_tipoRelacionActual;
    bool m_vioEmisor = false;
    bool m_vioReceptor = false;
    int m_documentosPago = 0;
};

ResultadoParseoCfdi Parser::ejecutar(QByteArrayView xml)
{
    if (xml.isEmpty()) {
        return ilegible(CausaIlegible::MalFormado);
    }
    if (xml.size() > LimitesParserCfdi::kMaxBytes) {
        return ilegible(CausaIlegible::ExcedeLimites);
    }
    if (empiezaConBomUtf8(xml)) {
        xml = xml.sliced(3);
    }
    if (contieneDoctype(xml)) {
        return ilegible(CausaIlegible::Doctype);
    }
    if (!esUtf8Valido(xml)) {
        return ilegible(CausaIlegible::Codificacion);
    }

    // fromRawData no copia; `xml` vive durante toda la funcion.
    QXmlStreamReader lector(QByteArray::fromRawData(xml.data(), xml.size()));
    lector.setNamespaceProcessing(true);

    std::vector<Contexto> pila;
    pila.reserve(LimitesParserCfdi::kProfundidadMaxima);
    int elementos = 0;

    while (!lector.atEnd()) {
        switch (lector.readNext()) {
        case QXmlStreamReader::StartDocument: {
            const QStringView codificacion = lector.documentEncoding();
            if (!codificacion.isEmpty() && codificacion.compare(u"UTF-8", Qt::CaseInsensitive) != 0) {
                return ilegible(CausaIlegible::Codificacion);
            }
            break;
        }
        case QXmlStreamReader::DTD:
            // Inalcanzable tras contieneDoctype; defensa en profundidad.
            return ilegible(CausaIlegible::Doctype);
        case QXmlStreamReader::StartElement: {
            if (static_cast<int>(pila.size()) >= LimitesParserCfdi::kProfundidadMaxima) {
                return ilegible(CausaIlegible::Profundidad);
            }
            if (++elementos > LimitesParserCfdi::kMaxElementos) {
                return ilegible(CausaIlegible::ExcedeLimites);
            }
            const QStringView ns = lector.namespaceUri();
            const QStringView local = lector.name();
            const QXmlStreamAttributes atributos = lector.attributes();
            if (pila.empty()) {
                if (!procesarRaiz(ns, local, atributos)) {
                    return noEsCfdi();
                }
                pila.push_back(Contexto::Comprobante);
            } else {
                pila.push_back(procesarHijo(pila.back(), ns, local, atributos));
            }
            break;
        }
        case QXmlStreamReader::EndElement:
            if (!pila.empty()) {
                pila.pop_back();
            }
            break;
        default:
            break;
        }
    }

    if (lector.hasError() || elementos == 0) {
        return ilegible(CausaIlegible::MalFormado);
    }
    return finalizar();
}

bool Parser::procesarRaiz(QStringView ns, QStringView local, const QXmlStreamAttributes& a)
{
    if (local != u"Comprobante" || (ns != kNsCfdi4 && ns != kNsCfdi3)) {
        return false;
    }
    m_nsRaiz = ns.toString();

    const QString versionMayuscula = texto(a, "Version"_L1);
    const bool es40 = ns == kNsCfdi4 && versionMayuscula == u"4.0";
    const bool es33 = ns == kNsCfdi3 && versionMayuscula == u"3.3";
    m_modo = (es40 || es33) ? Modo::Completo : Modo::Minimo;
    m_resumen.version = a.hasAttribute("Version"_L1) ? versionMayuscula : texto(a, "version"_L1);

    constexpr auto kElemento = "Comprobante"_L1;
    if (m_modo == Modo::Minimo) {
        // CFDI 3.2 (y anteriores): atributos en camelCase con minuscula inicial.
        m_resumen.tipo = tipoComprobanteDesdeTexto(texto(a, {"tipoDeComprobante"_L1, "TipoDeComprobante"_L1}));
        m_resumen.fecha = texto(a, {"fecha"_L1, "Fecha"_L1});
        m_resumen.serie = texto(a, {"serie"_L1, "Serie"_L1});
        m_resumen.folio = texto(a, {"folio"_L1, "Folio"_L1});
        m_resumen.lugarExpedicion = texto(a, "LugarExpedicion"_L1);
        m_resumen.subtotal = decimal(a, kElemento, {"subTotal"_L1, "SubTotal"_L1});
        m_resumen.descuento = decimal(a, kElemento, {"descuento"_L1, "Descuento"_L1});
        m_resumen.total = decimal(a, kElemento, {"total"_L1, "Total"_L1});
        m_resumen.moneda = texto(a, "Moneda"_L1);
        m_resumen.tipoCambio = decimal(a, kElemento, "TipoCambio"_L1);
        m_resumen.metodoPago = texto(a, {"metodoDePago"_L1, "MetodoPago"_L1});
        m_resumen.formaPago = texto(a, {"formaDePago"_L1, "FormaPago"_L1});
        return true;
    }

    const QString tipo = texto(a, "TipoDeComprobante"_L1);
    m_resumen.tipo = tipoComprobanteDesdeTexto(tipo);
    if (m_resumen.tipo == TipoComprobante::Desconocido || tipo.size() != 1) {
        m_resumen.tipo = TipoComprobante::Desconocido;
        marcarInvalido(kElemento, "TipoDeComprobante"_L1);
    }
    m_resumen.fecha = texto(a, "Fecha"_L1);
    if (m_resumen.fecha.isEmpty()) {
        marcarInvalido(kElemento, "Fecha"_L1);
    }
    m_resumen.serie = texto(a, "Serie"_L1);
    m_resumen.folio = texto(a, "Folio"_L1);
    m_resumen.lugarExpedicion = texto(a, "LugarExpedicion"_L1);
    m_resumen.subtotal = decimal(a, kElemento, "SubTotal"_L1, true);
    m_resumen.descuento = decimal(a, kElemento, "Descuento"_L1);
    m_resumen.total = decimal(a, kElemento, "Total"_L1, true);
    m_resumen.moneda = texto(a, "Moneda"_L1);
    if (m_resumen.moneda.isEmpty()) {
        marcarInvalido(kElemento, "Moneda"_L1);
    }
    m_resumen.tipoCambio = decimal(a, kElemento, "TipoCambio"_L1);
    m_resumen.metodoPago = texto(a, "MetodoPago"_L1);
    m_resumen.formaPago = texto(a, "FormaPago"_L1);

    m_detalle.noCertificado = texto(a, "NoCertificado"_L1);
    m_detalle.exportacion = texto(a, "Exportacion"_L1);
    m_detalle.selloEmisor = texto(a, "Sello"_L1);
    return true;
}

Contexto Parser::procesarHijo(Contexto padre, QStringView ns, QStringView local, const QXmlStreamAttributes& a)
{
    const bool nsCfdi = ns == m_nsRaiz;

    if (m_modo == Modo::Minimo) {
        // Solo emisor, receptor y timbre (para el UUID).
        if (padre == Contexto::Comprobante && nsCfdi) {
            if (local == u"Emisor") {
                leerEmisor(a);
            } else if (local == u"Receptor") {
                leerReceptor(a);
            } else if (local == u"Complemento") {
                return Contexto::Complemento;
            }
        } else if (padre == Contexto::Complemento && ns == kNsTimbre && local == u"TimbreFiscalDigital") {
            leerTimbre(a);
        }
        return Contexto::Otro;
    }

    switch (padre) {
    case Contexto::Comprobante:
        if (!nsCfdi) {
            return Contexto::Otro;
        }
        if (local == u"CfdiRelacionados") {
            m_tipoRelacionActual = texto(a, "TipoRelacion"_L1);
            return Contexto::Relacionados;
        }
        if (local == u"Emisor") {
            leerEmisor(a);
        } else if (local == u"Receptor") {
            leerReceptor(a);
        } else if (local == u"Conceptos") {
            return Contexto::Conceptos;
        } else if (local == u"Impuestos") {
            constexpr auto kElemento = "Impuestos"_L1;
            m_detalle.impuestos.totalTrasladados = decimal(a, kElemento, "TotalImpuestosTrasladados"_L1);
            m_detalle.impuestos.totalRetenidos = decimal(a, kElemento, "TotalImpuestosRetenidos"_L1);
            return Contexto::Impuestos;
        } else if (local == u"Complemento") {
            return Contexto::Complemento;
        }
        // InformacionGlobal, Addenda y otros: se ignoran sin afectar el estado.
        return Contexto::Otro;

    case Contexto::Relacionados:
        if (nsCfdi && local == u"CfdiRelacionado") {
            if (!cabe(m_detalle.relacionados.size(), LimitesParserCfdi::kMaxRelacionados)) {
                return Contexto::Otro;
            }
            const auto uuid = cfdi::normalizarUuid(a.value("UUID"_L1));
            if (!uuid) {
                marcarInvalido("CfdiRelacionado"_L1, "UUID"_L1);
            } else {
                m_detalle.relacionados.append(RelacionCfdi{m_tipoRelacionActual, *uuid});
            }
        }
        return Contexto::Otro;

    case Contexto::Conceptos:
        if (nsCfdi && local == u"Concepto") {
            if (!cabe(m_detalle.conceptos.size(), LimitesParserCfdi::kMaxConceptos)) {
                return Contexto::Otro;
            }
            leerConcepto(a);
            return Contexto::Concepto;
        }
        return Contexto::Otro;

    case Contexto::Concepto:
        // Impuestos, InformacionAduanera, CuentaPredial, Parte, ACuentaTerceros
        // del concepto no se representan (no son complementos).
        if (nsCfdi && local == u"ComplementoConcepto") {
            return Contexto::ComplementoConcepto;
        }
        return Contexto::Otro;

    case Contexto::ComplementoConcepto:
        registrarComplemento(ns, local);
        return Contexto::Otro;

    case Contexto::Impuestos:
        if (nsCfdi && local == u"Traslados") {
            return Contexto::Traslados;
        }
        if (nsCfdi && local == u"Retenciones") {
            return Contexto::Retenciones;
        }
        return Contexto::Otro;

    case Contexto::Traslados:
        if (nsCfdi && local == u"Traslado") {
            auto& imp = m_detalle.impuestos;
            if (cabe(imp.traslados.size() + imp.retenciones.size(), LimitesParserCfdi::kMaxImpuestos)) {
                constexpr auto kElemento = "Traslado"_L1;
                TrasladoCfdi t;
                t.base = decimal(a, kElemento, "Base"_L1);
                t.impuesto = texto(a, "Impuesto"_L1);
                t.tipoFactor = texto(a, "TipoFactor"_L1);
                t.tasaOCuota = decimal(a, kElemento, "TasaOCuota"_L1);
                t.importe = decimal(a, kElemento, "Importe"_L1);
                imp.traslados.append(std::move(t));
            }
        }
        return Contexto::Otro;

    case Contexto::Retenciones:
        if (nsCfdi && local == u"Retencion") {
            auto& imp = m_detalle.impuestos;
            if (cabe(imp.traslados.size() + imp.retenciones.size(), LimitesParserCfdi::kMaxImpuestos)) {
                RetencionCfdi r;
                r.impuesto = texto(a, "Impuesto"_L1);
                r.importe = decimal(a, "Retencion"_L1, "Importe"_L1);
                imp.retenciones.append(std::move(r));
            }
        }
        return Contexto::Otro;

    case Contexto::Complemento:
        return procesarComplemento(ns, local, a);

    case Contexto::Pagos: {
        if (ns != kNsPagos20 && ns != kNsPagos10) {
            return Contexto::Otro;
        }
        PagosCfdi& pagos = *m_detalle.pagos;
        if (local == u"Totales") {
            pagos.montoTotalPagos = decimal(a, "Totales"_L1, "MontoTotalPagos"_L1);
            return Contexto::Otro;
        }
        if (local == u"Pago") {
            if (!cabe(pagos.pagos.size(), LimitesParserCfdi::kMaxPagos)) {
                return Contexto::Otro;
            }
            constexpr auto kElemento = "Pago"_L1;
            PagoCfdi p;
            p.fechaPago = texto(a, "FechaPago"_L1);
            p.formaDePago = texto(a, "FormaDePagoP"_L1);
            p.moneda = texto(a, "MonedaP"_L1);
            p.tipoCambio = decimal(a, kElemento, "TipoCambioP"_L1);
            p.monto = decimal(a, kElemento, "Monto"_L1, true);
            pagos.pagos.append(std::move(p));
            return Contexto::Pago;
        }
        return Contexto::Otro;
    }

    case Contexto::Pago:
        if ((ns == kNsPagos20 || ns == kNsPagos10) && local == u"DoctoRelacionado") {
            if (!cabe(m_documentosPago, LimitesParserCfdi::kMaxDocumentosPago)) {
                return Contexto::Otro;
            }
            ++m_documentosPago;
            constexpr auto kElemento = "DoctoRelacionado"_L1;
            DocumentoRelacionadoPago d;
            const auto uuid = cfdi::normalizarUuid(a.value("IdDocumento"_L1));
            if (uuid) {
                d.idDocumento = *uuid;
            } else {
                // IdDocumento admite otros folios en pagos de documentos no CFDI.
                d.idDocumento = texto(a, "IdDocumento"_L1);
            }
            d.serie = texto(a, "Serie"_L1);
            d.folio = texto(a, "Folio"_L1);
            d.monedaDr = texto(a, "MonedaDR"_L1);
            d.equivalenciaDr = decimal(a, kElemento, "EquivalenciaDR"_L1);
            d.tipoCambioDr = decimal(a, kElemento, "TipoCambioDR"_L1);
            d.metodoDePagoDr = texto(a, "MetodoDePagoDR"_L1);
            d.numParcialidad = texto(a, "NumParcialidad"_L1);
            d.impSaldoAnt = decimal(a, kElemento, "ImpSaldoAnt"_L1);
            d.impPagado = decimal(a, kElemento, "ImpPagado"_L1);
            d.impSaldoInsoluto = decimal(a, kElemento, "ImpSaldoInsoluto"_L1);
            m_detalle.pagos->pagos.last().documentos.append(std::move(d));
        }
        return Contexto::Otro;

    case Contexto::Otro:
        break;
    }
    return Contexto::Otro;
}

Contexto Parser::procesarComplemento(QStringView ns, QStringView local, const QXmlStreamAttributes& a)
{
    if (ns == kNsTimbre && local == u"TimbreFiscalDigital") {
        leerTimbre(a);
        return Contexto::Otro;
    }
    if ((ns == kNsPagos20 || ns == kNsPagos10) && local == u"Pagos") {
        if (!m_detalle.pagos) {
            m_detalle.pagos = PagosCfdi{};
            m_detalle.pagos->version = texto(a, "Version"_L1);
        }
        return Contexto::Pagos;
    }
    if (ns == kNsNomina12 && local == u"Nomina") {
        leerNomina(a);
        return Contexto::Otro;
    }
    registrarComplemento(ns, local);
    return Contexto::Otro;
}

void Parser::leerEmisor(const QXmlStreamAttributes& a)
{
    if (m_vioEmisor) {
        return;
    }
    m_vioEmisor = true;
    m_resumen.emisor.rfc = texto(a, {"Rfc"_L1, "rfc"_L1});
    m_resumen.emisor.nombre = texto(a, {"Nombre"_L1, "nombre"_L1});
    m_resumen.emisor.regimenFiscal = texto(a, "RegimenFiscal"_L1);
}

void Parser::leerReceptor(const QXmlStreamAttributes& a)
{
    if (m_vioReceptor) {
        return;
    }
    m_vioReceptor = true;
    m_resumen.receptor.rfc = texto(a, {"Rfc"_L1, "rfc"_L1});
    m_resumen.receptor.nombre = texto(a, {"Nombre"_L1, "nombre"_L1});
    m_resumen.receptor.regimenFiscal = texto(a, "RegimenFiscalReceptor"_L1);
    m_resumen.receptor.usoCfdi = texto(a, "UsoCFDI"_L1);
    m_resumen.receptor.domicilioFiscal = texto(a, "DomicilioFiscalReceptor"_L1);
}

void Parser::leerConcepto(const QXmlStreamAttributes& a)
{
    constexpr auto kElemento = "Concepto"_L1;
    ConceptoCfdi c;
    c.claveProdServ = texto(a, "ClaveProdServ"_L1);
    c.noIdentificacion = texto(a, "NoIdentificacion"_L1);
    c.cantidad = decimal(a, kElemento, "Cantidad"_L1, true);
    c.claveUnidad = texto(a, "ClaveUnidad"_L1);
    c.unidad = texto(a, "Unidad"_L1);
    c.descripcion = texto(a, "Descripcion"_L1);
    c.valorUnitario = decimal(a, kElemento, "ValorUnitario"_L1, true);
    c.importe = decimal(a, kElemento, "Importe"_L1, true);
    c.descuento = decimal(a, kElemento, "Descuento"_L1);
    c.objetoImp = texto(a, "ObjetoImp"_L1);
    m_detalle.conceptos.append(std::move(c));
}

void Parser::leerTimbre(const QXmlStreamAttributes& a)
{
    if (m_detalle.timbre) {
        return; // un solo timbre; los siguientes se ignoran
    }
    TimbreFiscalCfdi t;
    t.version = texto(a, {"Version"_L1, "version"_L1});
    const auto uuid = cfdi::normalizarUuid(a.value("UUID"_L1));
    if (uuid) {
        t.uuid = *uuid;
    }
    t.fechaTimbrado = texto(a, "FechaTimbrado"_L1);
    t.rfcProvCertif = texto(a, "RfcProvCertif"_L1);
    t.noCertificadoSat = texto(a, {"NoCertificadoSAT"_L1, "noCertificadoSAT"_L1});
    t.selloCfd = texto(a, {"SelloCFD"_L1, "selloCFD"_L1});
    m_resumen.uuid = t.uuid;
    m_detalle.timbre = std::move(t);
}

void Parser::leerNomina(const QXmlStreamAttributes& a)
{
    if (m_detalle.nomina) {
        return; // solo el primer complemento de nomina (D6, basico)
    }
    constexpr auto kElemento = "Nomina"_L1;
    NominaCfdi n;
    n.version = texto(a, "Version"_L1);
    n.tipoNomina = texto(a, "TipoNomina"_L1);
    n.fechaPago = texto(a, "FechaPago"_L1);
    n.fechaInicialPago = texto(a, "FechaInicialPago"_L1);
    n.fechaFinalPago = texto(a, "FechaFinalPago"_L1);
    n.numDiasPagados = decimal(a, kElemento, "NumDiasPagados"_L1);
    n.totalPercepciones = decimal(a, kElemento, "TotalPercepciones"_L1);
    n.totalDeducciones = decimal(a, kElemento, "TotalDeducciones"_L1);
    n.totalOtrosPagos = decimal(a, kElemento, "TotalOtrosPagos"_L1);
    m_detalle.nomina = std::move(n);
}

ResultadoParseoCfdi Parser::finalizar()
{
    ResultadoParseoCfdi r;
    if (m_modo == Modo::Minimo) {
        m_resumen.estado = EstadoParseo::VersionNoSoportada;
        // Solo datos minimos: el detalle no se entrega para versiones no soportadas.
        r.resumen = std::move(m_resumen);
        return r;
    }

    if (!m_vioEmisor || m_resumen.emisor.rfc.isEmpty()) {
        marcarInvalido("Emisor"_L1, "Rfc"_L1);
    }
    if (!m_vioReceptor || m_resumen.receptor.rfc.isEmpty()) {
        marcarInvalido("Receptor"_L1, "Rfc"_L1);
    }
    if (m_resumen.uuid.isEmpty()) {
        marcarInvalido("TimbreFiscalDigital"_L1, "UUID"_L1);
    }

    const bool parcial = !m_resumen.complementosNoRepresentados.isEmpty() || !m_resumen.camposInvalidos.isEmpty()
                         || m_detalle.elementosOmitidosPorLimite > 0;
    m_resumen.estado = parcial ? EstadoParseo::Parcial : EstadoParseo::Completo;
    r.resumen = std::move(m_resumen);
    r.detalle = std::move(m_detalle);
    return r;
}

} // namespace

ResultadoParseoCfdi parsearCfdi(QByteArrayView xml)
{
    Parser parser;
    return parser.ejecutar(xml);
}

} // namespace satcfdi
