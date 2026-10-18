#include "infrastructure/cfdi/pdf/RenderizadorPdfCfdi.h"

#include "infrastructure/cfdi/pdf/UrlVerificacionCfdi.h"

#include <qrcodegen.hpp>

#include <QBuffer>
#include <QColor>
#include <QFont>
#include <QList>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPen>
#include <QPdfWriter>
#include <QTextLayout>
#include <QTextOption>

#include <algorithm>
#include <exception>
#include <memory>
#include <optional>
#include <utility>

namespace satcfdi {

namespace {

constexpr qreal kAnchoContenido = RenderizadorPdfCfdi::kAnchoPaginaPt - 2 * RenderizadorPdfCfdi::kMargenPt;
constexpr int kResolucion = 1200; // dpi del QPdfWriter; las medidas se escriben en pt

// Tamanos de letra (pt).
constexpr qreal kLetraTitulo = 13.0;
constexpr qreal kLetraSeccion = 9.5;
constexpr qreal kLetraEtiqueta = 6.8;
constexpr qreal kLetraValor = 8.8;
constexpr qreal kLetraTabla = 7.6;
constexpr qreal kLetraPie = 6.8;

// Espacios (pt).
constexpr qreal kSeparacionSeccion = 10.0;
constexpr qreal kRellenoCelda = 2.5;

const QColor kTexto(0x1d, 0x1d, 0x1f);
const QColor kTextoSecundario(0x5f, 0x63, 0x68);
const QColor kFondoSeccion(0xec, 0xee, 0xf1);
const QColor kFondoEncabezadoTabla(0xf4, 0xf5, 0xf7);
const QColor kLinea(0xc8, 0xcc, 0xd2);

QFont fuente(qreal puntos, bool negrita = false)
{
    QFont f(QStringLiteral("Helvetica"));
    f.setStyleHint(QFont::SansSerif);
    f.setPointSizeF(puntos);
    f.setBold(negrita);
    return f;
}

QString monto(const std::optional<ImporteCfdi>& importe)
{
    return importe ? importe->texto().trimmed() : QString();
}

QString fechaLegible(const QString& iso)
{
    QString f = iso.trimmed();
    if (f.size() > 10 && f.at(10) == QLatin1Char('T')) {
        f[10] = QLatin1Char(' ');
    }
    return f;
}

QString nombreTipo(TipoComprobante tipo)
{
    switch (tipo) {
    case TipoComprobante::Ingreso: return QStringLiteral("Ingreso");
    case TipoComprobante::Egreso: return QStringLiteral("Egreso");
    case TipoComprobante::Traslado: return QStringLiteral("Traslado");
    case TipoComprobante::Nomina: return QStringLiteral("Nómina");
    case TipoComprobante::Pago: return QStringLiteral("Pago");
    case TipoComprobante::Desconocido: break;
    }
    return QStringLiteral("Desconocido");
}

// c_Impuesto (claves documentadas en DetalleCfdi.h).
QString nombreImpuesto(const QString& clave)
{
    if (clave == u"001") return QStringLiteral("001 ISR");
    if (clave == u"002") return QStringLiteral("002 IVA");
    if (clave == u"003") return QStringLiteral("003 IEPS");
    return clave;
}

QString unir(const QString& a, const QString& b, const QString& separador = QStringLiteral(" "))
{
    if (a.isEmpty()) return b;
    if (b.isEmpty()) return a;
    return a + separador + b;
}

struct Campo {
    QString etiqueta;
    QString valor;
};

struct Columna {
    QString titulo;
    qreal ancho = 0; // pt; 0 = el resto
    Qt::Alignment alineacion = Qt::AlignLeft;
};

// Superficie de dibujo en pt sobre el QPdfWriter. En la pasada de medicion
// `m_pintor` es nulo: solo avanza el cursor y cuenta paginas.
class Lienzo {
public:
    Lienzo(QPdfWriter& writer, QPainter* pintor, int totalPaginas)
        : m_writer(writer)
        , m_pintor(pintor)
        , m_k(writer.resolution() / 72.0)
        , m_totalPaginas(totalPaginas)
    {
        m_altoPie = altoTexto(RenderizadorPdfCfdi::leyenda(), fuente(kLetraPie), kAnchoContenido) + 4.0
                    + altoTexto(QStringLiteral("Página"), fuente(kLetraPie), kAnchoContenido) + 6.0;
    }

    int pagina() const { return m_pagina; }
    qreal y() const { return m_y; }
    void avanzar(qreal dy) { m_y += dy; }
    void fijarY(qreal y) { m_y = y; }
    qreal limiteInferior() const
    {
        return RenderizadorPdfCfdi::kAltoPaginaPt - RenderizadorPdfCfdi::kMargenPt - m_altoPie;
    }
    bool cabe(qreal alto) const { return m_y + alto <= limiteInferior(); }

    // Garantiza `alto` pt libres; si no caben, cambia de pagina. Devuelve true
    // si hubo cambio de pagina.
    bool reservar(qreal alto)
    {
        if (cabe(alto) || m_y <= RenderizadorPdfCfdi::kMargenPt + 0.5) {
            return false;
        }
        nuevaPagina();
        return true;
    }

    void nuevaPagina()
    {
        dibujarPie();
        if (m_pintor && !m_writer.newPage()) {
            m_fallo = true;
        }
        ++m_pagina;
        m_y = RenderizadorPdfCfdi::kMargenPt;
    }

    void terminar() { dibujarPie(); }
    bool fallo() const { return m_fallo; }

    // Alto en pt de `texto` ajustado a `ancho` pt.
    qreal altoTexto(const QString& texto, const QFont& f, qreal ancho) const
    {
        if (texto.isEmpty()) {
            return 0;
        }
        QTextLayout layout(conSaltos(texto), f, &m_writer);
        return componerLineas(layout, ancho, Qt::AlignLeft) / m_k;
    }

    // Dibuja `texto` ajustado a `ancho` pt a partir de (x, y) pt. Devuelve su alto en pt.
    qreal texto(qreal x, qreal y, qreal ancho, const QString& texto, const QFont& f,
                const QColor& color = kTexto, Qt::Alignment alineacion = Qt::AlignLeft) const
    {
        if (texto.isEmpty()) {
            return 0;
        }
        QTextLayout layout(conSaltos(texto), f, &m_writer);
        const qreal alto = componerLineas(layout, ancho, alineacion);
        if (m_pintor) {
            m_pintor->setPen(color);
            layout.draw(m_pintor, QPointF(x * m_k, y * m_k));
        }
        return alto / m_k;
    }

    void rectangulo(const QRectF& pt, const QColor& relleno) const
    {
        if (m_pintor) {
            m_pintor->fillRect(escalar(pt), relleno);
        }
    }

    void linea(qreal x1, qreal y1, qreal x2, qreal y2, const QColor& color = kLinea) const
    {
        if (m_pintor) {
            m_pintor->setPen(QPen(color, 0.5 * m_k));
            m_pintor->drawLine(QPointF(x1 * m_k, y1 * m_k), QPointF(x2 * m_k, y2 * m_k));
        }
    }

    void marco(const QRectF& pt, const QColor& color = kLinea) const
    {
        if (m_pintor) {
            m_pintor->setPen(QPen(color, 0.6 * m_k));
            m_pintor->setBrush(Qt::NoBrush);
            m_pintor->drawRect(escalar(pt));
        }
    }

    // Simbolo QR: modulos oscuros como rectangulos (rachas horizontales
    // unidas), sin antialiasing para que no aparezcan juntas.
    void qr(const qrcodegen::QrCode& codigo, const QRectF& pt) const
    {
        if (!m_pintor) {
            return;
        }
        const int n = codigo.getSize();
        const qreal modulo = pt.width() / n;
        m_pintor->save();
        m_pintor->setRenderHint(QPainter::Antialiasing, false);
        m_pintor->fillRect(escalar(pt), Qt::white);
        for (int fila = 0; fila < n; ++fila) {
            int x = 0;
            while (x < n) {
                if (!codigo.getModule(x, fila)) {
                    ++x;
                    continue;
                }
                int fin = x;
                while (fin < n && codigo.getModule(fin, fila)) {
                    ++fin;
                }
                m_pintor->fillRect(escalar(QRectF(pt.x() + x * modulo, pt.y() + fila * modulo, (fin - x) * modulo,
                                                  modulo)),
                                   Qt::black);
                x = fin;
            }
        }
        m_pintor->restore();
    }

private:
    // QTextLayout solo parte lineas en U+2028.
    static QString conSaltos(QString texto)
    {
        texto.replace(QLatin1Char('\n'), QChar::LineSeparator);
        return texto;
    }

    QRectF escalar(const QRectF& pt) const
    {
        return QRectF(pt.x() * m_k, pt.y() * m_k, pt.width() * m_k, pt.height() * m_k);
    }

    // Compone las lineas en unidades del dispositivo; devuelve el alto.
    qreal componerLineas(QTextLayout& layout, qreal anchoPt, Qt::Alignment alineacion) const
    {
        QTextOption opcion(alineacion);
        opcion.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(opcion);
        layout.setCacheEnabled(true);
        const qreal ancho = anchoPt * m_k;
        qreal alto = 0;
        layout.beginLayout();
        for (;;) {
            QTextLine linea = layout.createLine();
            if (!linea.isValid()) {
                break;
            }
            linea.setLineWidth(ancho);
            linea.setPosition(QPointF(0, alto));
            alto += linea.height();
        }
        layout.endLayout();
        return alto;
    }

    void dibujarPie()
    {
        if (!m_pintor) {
            return;
        }
        const qreal base = RenderizadorPdfCfdi::kAltoPaginaPt - RenderizadorPdfCfdi::kMargenPt - m_altoPie + 6.0;
        linea(RenderizadorPdfCfdi::kMargenPt, base - 3.0, RenderizadorPdfCfdi::kMargenPt + kAnchoContenido,
              base - 3.0);
        const qreal alto = texto(RenderizadorPdfCfdi::kMargenPt, base, kAnchoContenido, RenderizadorPdfCfdi::leyenda(),
                                 fuente(kLetraPie), kTextoSecundario);
        texto(RenderizadorPdfCfdi::kMargenPt, base + alto + 2.0, kAnchoContenido,
              QStringLiteral("Página %1 de %2").arg(m_pagina).arg(std::max(m_totalPaginas, m_pagina)),
              fuente(kLetraPie), kTextoSecundario, Qt::AlignRight);
    }

    QPdfWriter& m_writer;
    QPainter* m_pintor;
    qreal m_k;
    int m_totalPaginas;
    int m_pagina = 1;
    qreal m_y = RenderizadorPdfCfdi::kMargenPt;
    qreal m_altoPie = 0;
    bool m_fallo = false;
};

// Composicion del documento sobre un Lienzo (misma secuencia en ambas pasadas).
class Composicion {
public:
    Composicion(Lienzo& lienzo, const ResultadoParseoCfdi& cfdi, const std::optional<qrcodegen::QrCode>& qr)
        : m_l(lienzo)
        , m_r(cfdi.resumen)
        , m_d(cfdi.detalle)
        , m_estado(cfdi.estado())
        , m_qr(qr)
    {
    }

    void componer()
    {
        encabezado();
        emisorReceptor();
        pago();
        conceptos();
        impuestos();
        totales();
        relacionados();
        pagos();
        nomina();
        complementosNoRepresentados();
        timbre();
        avisoParcial();
        m_l.terminar();
    }

private:
    static constexpr qreal x0 = RenderizadorPdfCfdi::kMargenPt;

    void encabezado()
    {
        const QRectF rectQr = RenderizadorPdfCfdi::rectanguloQrPt();
        const qreal anchoIzq = kAnchoContenido - rectQr.width() - 18.0;
        qreal y = m_l.y();
        y += m_l.texto(x0, y, anchoIzq, QStringLiteral("Comprobante Fiscal Digital por Internet"),
                       fuente(kLetraTitulo, true));
        y += 2.0;
        y += m_l.texto(x0, y, anchoIzq,
                       QStringLiteral("CFDI %1 · Tipo %2 (%3)")
                           .arg(m_r.version, nombreTipo(m_r.tipo), QString(codigoTipoComprobante(m_r.tipo))),
                       fuente(kLetraValor), kTextoSecundario);
        y += 6.0;
        const QList<Campo> campos{
            {QStringLiteral("Serie y folio"), unir(m_r.serie, m_r.folio, QStringLiteral("-"))},
            {QStringLiteral("Fecha de emisión"), fechaLegible(m_r.fecha)},
            {QStringLiteral("Lugar de expedición (CP)"), m_r.lugarExpedicion},
            {QStringLiteral("Folio fiscal (UUID)"), m_r.uuid},
        };
        m_l.fijarY(y);
        rejilla(campos, 2, anchoIzq);
        const qreal finIzq = m_l.y();

        // QR de verificacion (Anexo 20) o aviso de que no se pudo construir.
        if (m_qr) {
            m_l.qr(*m_qr, rectQr);
        } else {
            m_l.marco(rectQr);
            m_l.texto(rectQr.x() + 6, rectQr.y() + 26, rectQr.width() - 12,
                      QStringLiteral("Código QR no disponible: faltan datos del CFDI para construirlo."),
                      fuente(kLetraEtiqueta), kTextoSecundario, Qt::AlignHCenter);
        }
        m_l.texto(rectQr.x() - 10, rectQr.bottom() + 11, rectQr.width() + 20, // fuera de la zona de silencio (4 modulos)
                  QStringLiteral("Verificación en el portal del SAT"), fuente(kLetraEtiqueta), kTextoSecundario,
                  Qt::AlignHCenter);
        m_l.fijarY(std::max(finIzq, rectQr.bottom() + 23.0) + kSeparacionSeccion);
    }

    void emisorReceptor()
    {
        seccion(QStringLiteral("Emisor"));
        rejilla({{QStringLiteral("RFC"), m_r.emisor.rfc},
                 {QStringLiteral("Régimen fiscal"), m_r.emisor.regimenFiscal},
                 {QStringLiteral("Nombre o razón social"), m_r.emisor.nombre}},
                2, kAnchoContenido);
        m_l.avanzar(kSeparacionSeccion);
        seccion(QStringLiteral("Receptor"));
        rejilla({{QStringLiteral("RFC"), m_r.receptor.rfc},
                 {QStringLiteral("Uso del CFDI"), m_r.receptor.usoCfdi},
                 {QStringLiteral("Nombre o razón social"), m_r.receptor.nombre},
                 {QStringLiteral("Régimen fiscal"), m_r.receptor.regimenFiscal},
                 {QStringLiteral("Domicilio fiscal (CP)"), m_r.receptor.domicilioFiscal}},
                2, kAnchoContenido);
        m_l.avanzar(kSeparacionSeccion);
    }

    void pago()
    {
        seccion(QStringLiteral("Datos de pago"));
        rejilla({{QStringLiteral("Forma de pago"), m_r.formaPago},
                 {QStringLiteral("Método de pago"), m_r.metodoPago},
                 {QStringLiteral("Moneda"), m_r.moneda},
                 {QStringLiteral("Tipo de cambio"), monto(m_r.tipoCambio)},
                 {QStringLiteral("Exportación"), m_d.exportacion}},
                3, kAnchoContenido);
        m_l.avanzar(kSeparacionSeccion);
    }

    void conceptos()
    {
        if (m_d.conceptos.isEmpty()) {
            return;
        }
        const QList<Columna> columnas{
            {QStringLiteral("Clave"), 60},
            {QStringLiteral("Cantidad"), 46, Qt::AlignRight},
            {QStringLiteral("Unidad"), 50},
            {QStringLiteral("Descripción"), 0},
            {QStringLiteral("Valor unitario"), 66, Qt::AlignRight},
            {QStringLiteral("Importe"), 66, Qt::AlignRight},
            {QStringLiteral("Descuento"), 52, Qt::AlignRight},
        };
        QList<QStringList> filas;
        filas.reserve(m_d.conceptos.size());
        for (const ConceptoCfdi& c : m_d.conceptos) {
            QString clave = c.claveProdServ;
            if (!c.noIdentificacion.isEmpty()) {
                clave += QStringLiteral("\nNo. id. ") + c.noIdentificacion;
            }
            if (!c.objetoImp.isEmpty()) {
                clave += QStringLiteral("\nObj. imp. ") + c.objetoImp;
            }
            filas.append({clave, monto(c.cantidad), unir(c.claveUnidad, c.unidad), c.descripcion,
                          monto(c.valorUnitario), monto(c.importe), monto(c.descuento)});
        }
        seccion(QStringLiteral("Conceptos (%1)").arg(m_d.conceptos.size()));
        tabla(columnas, filas);
        m_l.avanzar(kSeparacionSeccion);
    }

    void impuestos()
    {
        const ImpuestosCfdi& i = m_d.impuestos;
        if (i.traslados.isEmpty() && i.retenciones.isEmpty() && !i.totalTrasladados && !i.totalRetenidos) {
            return;
        }
        seccion(QStringLiteral("Impuestos"));
        if (!i.traslados.isEmpty()) {
            subtitulo(QStringLiteral("Trasladados"));
            QList<QStringList> filas;
            for (const TrasladoCfdi& t : i.traslados) {
                filas.append({nombreImpuesto(t.impuesto), t.tipoFactor, monto(t.tasaOCuota), monto(t.base),
                              monto(t.importe)});
            }
            tabla({{QStringLiteral("Impuesto"), 0},
                   {QStringLiteral("Tipo factor"), 90},
                   {QStringLiteral("Tasa o cuota"), 90, Qt::AlignRight},
                   {QStringLiteral("Base"), 100, Qt::AlignRight},
                   {QStringLiteral("Importe"), 100, Qt::AlignRight}},
                  filas);
        }
        if (!i.retenciones.isEmpty()) {
            subtitulo(QStringLiteral("Retenidos"));
            QList<QStringList> filas;
            for (const RetencionCfdi& r : i.retenciones) {
                filas.append({nombreImpuesto(r.impuesto), monto(r.importe)});
            }
            tabla({{QStringLiteral("Impuesto"), 0}, {QStringLiteral("Importe"), 100, Qt::AlignRight}}, filas);
        }
        m_l.avanzar(kSeparacionSeccion);
    }

    void totales()
    {
        QList<Campo> filas{{QStringLiteral("Subtotal"), monto(m_r.subtotal)},
                           {QStringLiteral("Descuento"), monto(m_r.descuento)},
                           {QStringLiteral("Impuestos trasladados"), monto(m_d.impuestos.totalTrasladados)},
                           {QStringLiteral("Impuestos retenidos"), monto(m_d.impuestos.totalRetenidos)}};
        filas.removeIf([](const Campo& c) { return c.valor.isEmpty(); });
        const QString total = unir(monto(m_r.total), m_r.moneda);
        const qreal ancho = 230.0;
        const qreal x = x0 + kAnchoContenido - ancho;
        const qreal altoFila = m_l.altoTexto(QStringLiteral("0"), fuente(kLetraValor), ancho) + 3.0;
        m_l.reservar(altoFila * (filas.size() + 1) + 16.0);
        seccionEn(QStringLiteral("Totales"), x, ancho);
        for (const Campo& c : filas) {
            fila(x, ancho, c.etiqueta, c.valor, fuente(kLetraValor), altoFila);
        }
        m_l.linea(x, m_l.y(), x + ancho, m_l.y());
        m_l.avanzar(1.5);
        fila(x, ancho, QStringLiteral("Total"), total.isEmpty() ? QStringLiteral("(sin total)") : total,
             fuente(kLetraValor + 1.0, true), altoFila + 2.0);
        m_l.avanzar(kSeparacionSeccion);
    }

    void relacionados()
    {
        if (m_d.relacionados.isEmpty()) {
            return;
        }
        seccion(QStringLiteral("CFDI relacionados"));
        QList<QStringList> filas;
        for (const RelacionCfdi& r : m_d.relacionados) {
            filas.append({r.tipoRelacion, r.uuid});
        }
        tabla({{QStringLiteral("Tipo de relación"), 110}, {QStringLiteral("Folio fiscal (UUID)"), 0}}, filas);
        m_l.avanzar(kSeparacionSeccion);
    }

    void pagos()
    {
        if (!m_d.pagos) {
            return;
        }
        const PagosCfdi& p = *m_d.pagos;
        seccion(QStringLiteral("Complemento de pagos %1").arg(p.version));
        if (p.montoTotalPagos) {
            rejilla({{QStringLiteral("Monto total de pagos"), monto(p.montoTotalPagos)}}, 2, kAnchoContenido);
        }
        int n = 0;
        for (const PagoCfdi& pago : p.pagos) {
            ++n;
            subtitulo(QStringLiteral("Pago %1").arg(n));
            rejilla({{QStringLiteral("Fecha de pago"), fechaLegible(pago.fechaPago)},
                     {QStringLiteral("Forma de pago"), pago.formaDePago},
                     {QStringLiteral("Moneda"), pago.moneda},
                     {QStringLiteral("Tipo de cambio"), monto(pago.tipoCambio)},
                     {QStringLiteral("Monto"), monto(pago.monto)}},
                    3, kAnchoContenido);
            if (!pago.documentos.isEmpty()) {
                QList<QStringList> filas;
                for (const DocumentoRelacionadoPago& d : pago.documentos) {
                    filas.append({d.idDocumento, unir(d.serie, d.folio, QStringLiteral("-")), d.monedaDr,
                                  d.numParcialidad, monto(d.impSaldoAnt), monto(d.impPagado),
                                  monto(d.impSaldoInsoluto)});
                }
                tabla({{QStringLiteral("Documento relacionado (UUID)"), 0},
                       {QStringLiteral("Serie-folio"), 56},
                       {QStringLiteral("Moneda"), 38},
                       {QStringLiteral("Parcialidad"), 46, Qt::AlignRight},
                       {QStringLiteral("Saldo anterior"), 62, Qt::AlignRight},
                       {QStringLiteral("Importe pagado"), 62, Qt::AlignRight},
                       {QStringLiteral("Saldo insoluto"), 62, Qt::AlignRight}},
                      filas);
            }
        }
        m_l.avanzar(kSeparacionSeccion);
    }

    void nomina()
    {
        if (!m_d.nomina) {
            return;
        }
        const NominaCfdi& n = *m_d.nomina;
        const QString periodo = (n.fechaInicialPago.isEmpty() && n.fechaFinalPago.isEmpty())
                                    ? QString()
                                    : n.fechaInicialPago + QStringLiteral(" a ") + n.fechaFinalPago;
        seccion(QStringLiteral("Complemento de nómina %1").arg(n.version));
        rejilla({{QStringLiteral("Tipo de nómina"), n.tipoNomina},
                 {QStringLiteral("Fecha de pago"), n.fechaPago},
                 {QStringLiteral("Periodo de pago"), periodo},
                 {QStringLiteral("Días pagados"), monto(n.numDiasPagados)},
                 {QStringLiteral("Total de percepciones"), monto(n.totalPercepciones)},
                 {QStringLiteral("Total de deducciones"), monto(n.totalDeducciones)},
                 {QStringLiteral("Total de otros pagos"), monto(n.totalOtrosPagos)}},
                3, kAnchoContenido);
        m_l.avanzar(kSeparacionSeccion);
    }

    void complementosNoRepresentados()
    {
        if (m_r.complementosNoRepresentados.isEmpty()) {
            return;
        }
        seccion(QStringLiteral("Complementos no representados"));
        for (const ComplementoNoRepresentado& c : m_r.complementosNoRepresentados) {
            parrafo(QStringLiteral("Contiene el complemento %1 (%2), no representado en esta visualización.")
                        .arg(c.nombreLocal, c.espacioNombres));
        }
        m_l.avanzar(kSeparacionSeccion);
    }

    void timbre()
    {
        seccion(QStringLiteral("Timbre fiscal digital"));
        if (!m_d.timbre) {
            parrafo(QStringLiteral("El XML no contiene el timbre fiscal digital."));
        } else {
            const TimbreFiscalCfdi& t = *m_d.timbre;
            rejilla({{QStringLiteral("Folio fiscal (UUID)"), t.uuid},
                     {QStringLiteral("Fecha de timbrado"), fechaLegible(t.fechaTimbrado)},
                     {QStringLiteral("RFC del proveedor de certificación"), t.rfcProvCertif},
                     {QStringLiteral("No. de certificado del SAT"), t.noCertificadoSat},
                     {QStringLiteral("No. de certificado del emisor"), m_d.noCertificado},
                     {QStringLiteral("Versión del timbre"), t.version}},
                    2, kAnchoContenido);
        }
        rejilla({{QStringLiteral("Sello del emisor (últimos 8 caracteres)"), m_d.ultimosCaracteresSello(8)}}, 1,
                kAnchoContenido);
        m_l.avanzar(kSeparacionSeccion);
    }

    void avisoParcial()
    {
        if (m_estado != EstadoParseo::Parcial) {
            return;
        }
        seccion(QStringLiteral("Aviso de lectura"));
        parrafo(QStringLiteral("Algunos datos del XML no se pudieron representar: complementos no representados, "
                               "campos ausentes o inválidos, o listas recortadas por límite."));
        if (!m_r.camposInvalidos.isEmpty()) {
            parrafo(QStringLiteral("Campos ausentes o inválidos: ") + m_r.camposInvalidos.join(QStringLiteral(", ")));
        }
        if (m_d.elementosOmitidosPorLimite > 0) {
            parrafo(QStringLiteral("Elementos no mostrados por límite: %1.").arg(m_d.elementosOmitidosPorLimite));
        }
    }

    // --- Bloques ---------------------------------------------------------

    qreal altoSeccion() const { return m_l.altoTexto(QStringLiteral("X"), fuente(kLetraSeccion, true), 100) + 5.0; }

    // Titulo de seccion; se asegura de que quepa con al menos una fila despues.
    void seccion(const QString& titulo)
    {
        m_l.reservar(altoSeccion() + 28.0);
        seccionEn(titulo, x0, kAnchoContenido);
    }

    void seccionEn(const QString& titulo, qreal x, qreal ancho)
    {
        const qreal alto = altoSeccion();
        m_l.rectangulo(QRectF(x, m_l.y(), ancho, alto), kFondoSeccion);
        m_l.texto(x + 4.0, m_l.y() + 2.5, ancho - 8.0, titulo, fuente(kLetraSeccion, true));
        m_l.avanzar(alto + 3.0);
    }

    void subtitulo(const QString& texto)
    {
        const QFont f = fuente(kLetraValor, true);
        const qreal alto = m_l.altoTexto(texto, f, kAnchoContenido);
        m_l.reservar(alto + 24.0);
        m_l.avanzar(2.0);
        m_l.texto(x0, m_l.y(), kAnchoContenido, texto, f);
        m_l.avanzar(alto + 2.0);
    }

    void parrafo(const QString& texto)
    {
        const QFont f = fuente(kLetraValor);
        const qreal alto = m_l.altoTexto(texto, f, kAnchoContenido);
        m_l.reservar(alto);
        m_l.texto(x0, m_l.y(), kAnchoContenido, texto, f);
        m_l.avanzar(alto + 2.0);
    }

    void fila(qreal x, qreal ancho, const QString& etiqueta, const QString& valor, const QFont& f, qreal alto)
    {
        m_l.texto(x + 4.0, m_l.y(), ancho * 0.55, etiqueta, f, kTextoSecundario);
        m_l.texto(x + ancho * 0.45, m_l.y(), ancho * 0.55 - 4.0, valor, f, kTexto, Qt::AlignRight);
        m_l.avanzar(alto);
    }

    // Campos etiqueta/valor en `columnas` columnas (omite los vacios).
    void rejilla(QList<Campo> campos, int columnas, qreal ancho)
    {
        campos.removeIf([](const Campo& c) { return c.valor.trimmed().isEmpty(); });
        const qreal anchoCelda = (ancho - (columnas - 1) * 12.0) / columnas;
        const QFont fEtiqueta = fuente(kLetraEtiqueta);
        const QFont fValor = fuente(kLetraValor);
        for (qsizetype i = 0; i < campos.size(); i += columnas) {
            qreal alto = 0;
            for (qsizetype j = i; j < std::min(campos.size(), i + columnas); ++j) {
                alto = std::max(alto, m_l.altoTexto(campos.at(j).etiqueta, fEtiqueta, anchoCelda)
                                          + m_l.altoTexto(campos.at(j).valor, fValor, anchoCelda));
            }
            m_l.reservar(alto + 4.0);
            for (qsizetype j = i; j < std::min(campos.size(), i + columnas); ++j) {
                const qreal x = x0 + (j - i) * (anchoCelda + 12.0);
                const qreal hEtiqueta =
                    m_l.texto(x, m_l.y(), anchoCelda, campos.at(j).etiqueta, fEtiqueta, kTextoSecundario);
                m_l.texto(x, m_l.y() + hEtiqueta, anchoCelda, campos.at(j).valor, fValor);
            }
            m_l.avanzar(alto + 4.0);
        }
    }

    // Tabla con encabezado repetido en cada pagina y filas que no se parten.
    void tabla(const QList<Columna>& columnas, const QList<QStringList>& filas)
    {
        QList<qreal> anchos;
        qreal fijo = 0;
        int flexibles = 0;
        for (const Columna& c : columnas) {
            fijo += c.ancho;
            flexibles += c.ancho <= 0 ? 1 : 0;
        }
        const qreal resto = flexibles > 0 ? std::max<qreal>(40.0, (kAnchoContenido - fijo) / flexibles) : 0;
        for (const Columna& c : columnas) {
            anchos.append(c.ancho > 0 ? c.ancho : resto);
        }
        const QFont fEncabezado = fuente(kLetraTabla, true);
        const QFont fCelda = fuente(kLetraTabla);

        const auto altoFila = [&](const QStringList& celdas, const QFont& f) {
            qreal alto = 0;
            for (qsizetype i = 0; i < columnas.size(); ++i) {
                alto = std::max(alto, m_l.altoTexto(celdas.value(i), f, anchos.at(i) - 2 * kRellenoCelda));
            }
            return alto + 2 * kRellenoCelda;
        };
        const auto dibujarFila = [&](const QStringList& celdas, const QFont& f, qreal alto, bool encabezado) {
            if (encabezado) {
                m_l.rectangulo(QRectF(x0, m_l.y(), kAnchoContenido, alto), kFondoEncabezadoTabla);
            }
            qreal x = x0;
            for (qsizetype i = 0; i < columnas.size(); ++i) {
                m_l.texto(x + kRellenoCelda, m_l.y() + kRellenoCelda, anchos.at(i) - 2 * kRellenoCelda,
                          celdas.value(i), f, encabezado ? kTextoSecundario : kTexto, columnas.at(i).alineacion);
                x += anchos.at(i);
            }
            m_l.avanzar(alto);
            m_l.linea(x0, m_l.y(), x0 + kAnchoContenido, m_l.y());
        };

        QStringList titulos;
        for (const Columna& c : columnas) {
            titulos.append(c.titulo);
        }
        const qreal altoEncabezado = altoFila(titulos, fEncabezado);
        const qreal primeraFila = filas.isEmpty() ? 0 : altoFila(filas.constFirst(), fCelda);
        m_l.reservar(altoEncabezado + primeraFila);
        dibujarFila(titulos, fEncabezado, altoEncabezado, true);
        for (const QStringList& celdas : filas) {
            const qreal alto = altoFila(celdas, fCelda);
            if (m_l.reservar(alto)) {
                dibujarFila(titulos, fEncabezado, altoEncabezado, true);
            }
            dibujarFila(celdas, fCelda, alto, false);
        }
    }

    Lienzo& m_l;
    const ResumenCfdi& m_r;
    const DetalleCfdi& m_d;
    EstadoParseo m_estado;
    const std::optional<qrcodegen::QrCode>& m_qr;
};

std::optional<qrcodegen::QrCode> codigoQr(const ResultadoParseoCfdi& cfdi)
{
    const QString uuid = cfdi.detalle.timbre && !cfdi.detalle.timbre->uuid.isEmpty() ? cfdi.detalle.timbre->uuid
                                                                                     : cfdi.resumen.uuid;
    const auto url = urlVerificacionCfdi(DatosQrCfdi{uuid, cfdi.resumen.emisor.rfc, cfdi.resumen.receptor.rfc,
                                                     cfdi.resumen.total ? cfdi.resumen.total->texto() : QString(),
                                                     cfdi.detalle.selloEmisor});
    if (!url) {
        return std::nullopt;
    }
    try {
        // Nivel de correccion M (el Anexo 20 en el repositorio no fija uno);
        // qrcodegen lo sube si cabe sin crecer la version.
        return qrcodegen::QrCode::encodeText(url->toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
    } catch (const std::exception&) {
        return std::nullopt; // datos demasiado largos para un QR
    }
}

void configurar(QPdfWriter& writer)
{
    writer.setResolution(kResolucion);
    writer.setPageLayout(QPageLayout(QPageSize(QPageSize::Letter), QPageLayout::Portrait, QMarginsF(0, 0, 0, 0),
                                     QPageLayout::Point));
    // Metadatos sin datos fiscales.
    writer.setCreator(QStringLiteral("Archivo Fiscal"));
    writer.setTitle(QStringLiteral("Visualización de CFDI"));
}

} // namespace

QString RenderizadorPdfCfdi::leyenda()
{
    return QStringLiteral("Visualización generada localmente a partir del XML del CFDI. No es la representación "
                          "impresa emitida por el contribuyente ni valida sello, vigencia o cancelación. El XML es el "
                          "comprobante fiscal.");
}

Resultado<PdfRenderizado, ErrorRenderPdf> RenderizadorPdfCfdi::renderizar(const ResultadoParseoCfdi& cfdi)
{
    using R = Resultado<PdfRenderizado, ErrorRenderPdf>;
    if (cfdi.estado() != EstadoParseo::Completo && cfdi.estado() != EstadoParseo::Parcial) {
        return R::fallo(ErrorRenderPdf{ErrorRenderPdf::Tipo::NoRepresentable});
    }
    const std::optional<qrcodegen::QrCode> qr = codigoQr(cfdi);

    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return R::fallo(ErrorRenderPdf{ErrorRenderPdf::Tipo::Fallo});
    }
    QPdfWriter writer(&buffer);
    configurar(writer);

    // Pasada 1: solo medir (numero de paginas para el pie).
    int paginas = 1;
    {
        Lienzo medicion(writer, nullptr, 0);
        Composicion(medicion, cfdi, qr).componer();
        paginas = medicion.pagina();
    }

    // Pasada 2: dibujar.
    QPainter pintor;
    if (!pintor.begin(&writer)) {
        return R::fallo(ErrorRenderPdf{ErrorRenderPdf::Tipo::Fallo});
    }
    pintor.setRenderHint(QPainter::Antialiasing, true);
    pintor.setRenderHint(QPainter::TextAntialiasing, true);
    Lienzo lienzo(writer, &pintor, paginas);
    Composicion(lienzo, cfdi, qr).componer();
    const bool fallo = lienzo.fallo();
    const int dibujadas = lienzo.pagina();
    if (!pintor.end() || fallo || bytes.isEmpty()) {
        return R::fallo(ErrorRenderPdf{ErrorRenderPdf::Tipo::Fallo});
    }
    buffer.close();
    return R::exito(PdfRenderizado{std::move(bytes), dibujadas});
}

} // namespace satcfdi
