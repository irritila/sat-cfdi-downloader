#include "domain/cfdi/ExportacionFacturas.h"

#include "domain/cfdi/UuidCfdi.h"

#include <QRegularExpression>

#include <algorithm>
#include <tuple>

namespace satcfdi {

namespace {

const QString kTalloPorOmision = QStringLiteral("Archivo-Fiscal");
const QString kExtensionZip = QStringLiteral(".zip");
constexpr qsizetype kMaxBytesNombre = 255;

template <class... F>
struct Visitante : F... {
    using F::operator()...;
};

QString carpetaDe(TipoDescarga direccion, MesCalendario mes)
{
    return QString(carpetaDireccion(direccion)) + QLatin1Char('/') + mes.texto();
}

// Quita espacios en los extremos, puntos iniciales y extensiones `.zip`
// finales hasta que el tallo no cambie.
QString estabilizarTallo(QString tallo)
{
    for (;;) {
        const QString antes = tallo;
        tallo = tallo.trimmed();
        while (tallo.endsWith(kExtensionZip, Qt::CaseInsensitive)) {
            tallo.chop(kExtensionZip.size());
            tallo = tallo.trimmed();
        }
        qsizetype i = 0;
        while (i < tallo.size() && (tallo.at(i) == QLatin1Char('.') || tallo.at(i).isSpace())) {
            ++i;
        }
        tallo.remove(0, i);
        if (tallo == antes) {
            return tallo;
        }
    }
}

// Recorta `tallo` a lo sumo `maxBytes` bytes UTF-8 sin partir un punto de
// codigo (los pares sustitutos se conservan o se quitan juntos).
QString recortarUtf8(const QString& tallo, qsizetype maxBytes)
{
    qsizetype bytes = 0;
    qsizetype i = 0;
    while (i < tallo.size()) {
        const QChar c = tallo.at(i);
        qsizetype unidades = 1;
        qsizetype n = 0;
        if (c.isHighSurrogate() && i + 1 < tallo.size() && tallo.at(i + 1).isLowSurrogate()) {
            unidades = 2;
            n = 4;
        } else if (c.unicode() < 0x80) {
            n = 1;
        } else if (c.unicode() < 0x800) {
            n = 2;
        } else {
            n = 3; // BMP (un sustituto aislado se codifica como U+FFFD: 3 bytes)
        }
        if (bytes + n > maxBytes) {
            break;
        }
        bytes += n;
        i += unidades;
    }
    return tallo.left(i);
}

// UUID canonico o base del asignador (`sin-uuid-<n>`), con sufijo de
// colision opcional.
bool esBaseAsignada(const QString& identificador)
{
    static const QRegularExpression kBase(QStringLiteral(
        "^(?:[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}|sin-uuid-[1-9][0-9]*)"
        "(?:-(?:[2-9]|[1-9][0-9]+))?$"));
    return kBase.match(identificador).hasMatch();
}

// Identificador validado (UUID canonico o base asignada); nullopt si no lo es.
std::optional<QString> identificadorValidado(const QString& identificador)
{
    if (const auto uuid = cfdi::normalizarUuid(identificador)) {
        return *uuid;
    }
    if (esBaseAsignada(identificador)) {
        return identificador;
    }
    return std::nullopt;
}

} // namespace

const PerfilId& perfilDe(const AlcanceExportacion& alcance)
{
    return std::visit([](const auto& a) -> const PerfilId& { return a.perfil; }, alcance);
}

QList<MesCalendario> mesesDe(const AlcanceExportacion& alcance)
{
    return std::visit(
        Visitante{
            [](const AlcanceMesExportacion& a) { return QList<MesCalendario>{a.mes}; },
            [](const AlcanceRangoExportacion& a) {
                QList<MesCalendario> meses;
                for (std::optional<MesCalendario> m = a.desde; m && *m <= a.hasta; m = m->siguiente()) {
                    meses.append(*m);
                }
                return meses;
            },
            [](const AlcanceSeleccionExportacion& a) {
                QList<MesCalendario> meses;
                for (const FacturaSeleccionada& f : a.facturas) {
                    meses.append(f.mes);
                }
                std::sort(meses.begin(), meses.end());
                meses.erase(std::unique(meses.begin(), meses.end()), meses.end());
                return meses;
            },
        },
        alcance);
}

QString textoMotivo(MotivoOmisionFactura motivo)
{
    switch (motivo) {
    case MotivoOmisionFactura::NoDisponibleEnEsteEquipo:
        return QStringLiteral("no disponible en este equipo");
    case MotivoOmisionFactura::Ilegible:
        return QStringLiteral("ilegible");
    case MotivoOmisionFactura::CambioDesdeIndexacion:
        return QString::fromUtf8("cambió desde que se indexó");
    case MotivoOmisionFactura::YaNoDisponible:
        return QStringLiteral("ya no disponible");
    case MotivoOmisionFactura::SinPdf:
        return QStringLiteral("sin PDF");
    }
    return QStringLiteral("ya no disponible");
}

QString textoMotivo(MotivoOmisionMes motivo)
{
    switch (motivo) {
    case MotivoOmisionMes::DescargasPendientes:
        return QStringLiteral("descargas pendientes");
    case MotivoOmisionMes::CoberturaIncompleta:
        return QStringLiteral("cobertura incompleta");
    }
    return QStringLiteral("cobertura incompleta");
}

QLatin1StringView carpetaDireccion(TipoDescarga direccion)
{
    return direccion == TipoDescarga::Emitidos ? QLatin1StringView("emitidas") : QLatin1StringView("recibidas");
}

QString nombreSugeridoZip(const AlcanceExportacion& alcance, QDate fecha)
{
    const QString prefijo = kTalloPorOmision + QLatin1Char('-');
    return std::visit(Visitante{
                          [&](const AlcanceMesExportacion& a) { return prefijo + a.mes.texto() + kExtensionZip; },
                          [&](const AlcanceRangoExportacion& a) {
                              if (a.desde == a.hasta) {
                                  return prefijo + a.desde.texto() + kExtensionZip;
                              }
                              return prefijo + a.desde.texto() + QLatin1Char('_') + a.hasta.texto() + kExtensionZip;
                          },
                          [&](const AlcanceSeleccionExportacion&) {
                              return prefijo + QStringLiteral("seleccion-") + fecha.toString(u"yyyy-MM-dd")
                                     + kExtensionZip;
                          },
                      },
                      alcance);
}

QString sanearNombreZip(QStringView nombre)
{
    const QString nfc = nombre.toString().normalized(QString::NormalizationForm_C);
    QString limpio;
    limpio.reserve(nfc.size());
    for (const QChar c : nfc) {
        if (c == QLatin1Char('/') || c == QLatin1Char(':')) {
            limpio.append(QLatin1Char('-'));
        } else if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format) {
            continue; // controles y formato (p. ej. U+202E) no aportan al nombre visible
        } else {
            limpio.append(c);
        }
    }
    QString tallo = estabilizarTallo(limpio);
    const qsizetype maxTallo = kMaxBytesNombre - kExtensionZip.size();
    if (tallo.toUtf8().size() > maxTallo) {
        tallo = estabilizarTallo(recortarUtf8(tallo, maxTallo));
    }
    if (tallo.isEmpty()) {
        tallo = kTalloPorOmision;
    }
    return tallo + kExtensionZip;
}

NombreEntradaFactura AsignadorNombresExportacion::siguiente(TipoDescarga direccion, MesCalendario mes,
                                                             QStringView uuid)
{
    NombreEntradaFactura nombre;
    nombre.carpeta = carpetaDe(direccion, mes);
    Carpeta& carpeta = m_carpetas[nombre.carpeta];
    const std::optional<QString> canonico = cfdi::normalizarUuid(uuid);
    const QString base = canonico ? *canonico : QStringLiteral("sin-uuid-%1").arg(++carpeta.sinUuid);
    nombre.base = base;
    for (int n = 2; carpeta.usadas.contains(nombre.base); ++n) {
        nombre.base = base + QLatin1Char('-') + QString::number(n);
    }
    carpeta.usadas.insert(nombre.base);
    return nombre;
}

std::optional<QByteArray> formatearOmitidas(const QList<OmisionExportacion>& omisiones)
{
    // Clave de orden: direccion, mes, clase (0 mes, 1 CFDI), identificador,
    // motivo y, para un identificador no validado, su posicion en la entrada
    // (asi no se fusiona con otra omision distinta; 0 = deduplicable).
    using Linea = std::tuple<TipoDescarga, MesCalendario, int, QString, int, qsizetype, QString>;
    QList<Linea> lineas;
    lineas.reserve(omisiones.size());
    for (qsizetype i = 0; i < omisiones.size(); ++i) {
        std::visit(Visitante{
                       [&](const OmisionMesExportacion& m) {
                           lineas.append(Linea{m.direccion, m.mes, 0, QString(), int(m.motivo), 0,
                                               carpetaDe(m.direccion, m.mes) + QStringLiteral(": ")
                                                   + textoMotivo(m.motivo)});
                       },
                       [&](const OmisionFacturaExportacion& f) {
                           const std::optional<QString> validado = identificadorValidado(f.identificador);
                           const QString id = validado.value_or(QStringLiteral("sin-uuid"));
                           lineas.append(Linea{f.direccion, f.mes, 1, id, int(f.motivo), validado ? 0 : i + 1,
                                               carpetaDe(f.direccion, f.mes) + QLatin1Char('/') + id
                                                   + QStringLiteral(": ") + textoMotivo(f.motivo)});
                       },
                   },
                   omisiones.at(i));
    }
    if (lineas.isEmpty()) {
        return std::nullopt;
    }
    std::sort(lineas.begin(), lineas.end());
    lineas.erase(std::unique(lineas.begin(), lineas.end()), lineas.end());
    QString texto;
    for (const Linea& l : std::as_const(lineas)) {
        texto += std::get<6>(l);
        texto += QLatin1Char('\n');
    }
    return texto.toUtf8();
}

} // namespace satcfdi
