#include "app_core/ConfiguracionRespaldoIni.h"

#include <QFileInfo>
#include <QSettings>
#include <QTimeZone>

#include <utility>

namespace satcfdi {

using respaldo::ConfiguracionRespaldo;
using respaldo::ErrorConfiguracionRespaldo;
using respaldo::ReferenciaKek;

namespace {

const QString kRuta = QStringLiteral("respaldo/carpeta/ruta");
const QString kBookmark = QStringLiteral("respaldo/carpeta/bookmark");
const QString kUuid = QStringLiteral("respaldo/carpeta/uuidVolumen");
const QString kDiario = QStringLiteral("respaldo/diario");
const QString kKekContrasena = QStringLiteral("respaldo/kek/contrasena");
const QString kKekRecuperacion = QStringLiteral("respaldo/kek/recuperacion");
const QString kIntento = QStringLiteral("respaldo/ultimo/intento");
const QString kExito = QStringLiteral("respaldo/ultimo/exito");
const QString kResultado = QStringLiteral("respaldo/ultimo/resultado");
// T025.6 (C6): resumen saneado del ultimo respaldo.
const QString kResumen = QStringLiteral("respaldo/ultimo/resumen/");
const QString kRFecha = kResumen + QStringLiteral("fecha");
const QString kREstado = kResumen + QStringLiteral("estado");
const QString kRBytes = kResumen + QStringLiteral("bytesDatos");
const QString kRFaltantes = kResumen + QStringLiteral("faltantes");
const QString kROmitidos = kResumen + QStringLiteral("omitidos");
const QString kRDepurados = kResumen + QStringLiteral("depurados");
const QString kRSinRegistro = kResumen + QStringLiteral("copiasSinRegistro");
const QString kRDuplicados = kResumen + QStringLiteral("duplicadosProveedor");
const QString kRAvisoDepuracion = kResumen + QStringLiteral("avisoDepuracion");
const QString kRAvisoRegistro = kResumen + QStringLiteral("avisoRegistro");
const QString kRErrorDestino = kResumen + QStringLiteral("errorDestino");
const QString kRErrorCaptura = kResumen + QStringLiteral("errorCaptura");
const QString kRErrorContenedor = kResumen + QStringLiteral("errorContenedor");
constexpr int kMaxConteo = 10'000'000;

std::optional<QDateTime> fecha(const QSettings& s, const QString& clave)
{
    QDateTime f = QDateTime::fromString(s.value(clave).toString(), Qt::ISODateWithMs);
    if (!f.isValid()) {
        return std::nullopt;
    }
    return f.toUTC();
}

void ponerFecha(QSettings& s, const QString& clave, const std::optional<QDateTime>& f)
{
    if (f) {
        s.setValue(clave, f->toUTC().toString(Qt::ISODateWithMs));
    } else {
        s.remove(clave);
    }
}

// nullopt si la clave falta; ReferenciaKek nula si esta presente pero no es
// una referencia valida (la lectura deja de ser integra).
std::optional<ReferenciaKek> referencia(const QSettings& s, const QString& clave)
{
    if (!s.contains(clave)) {
        return std::nullopt;
    }
    return ReferenciaKek::desdeTexto(s.value(clave).toString()).value_or(ReferenciaKek());
}

// Clave estable: [a-z_], 1..48. Cualquier otra cosa se lee como invalida.
bool esClaveEstable(const QString& v)
{
    if (v.isEmpty() || v.size() > 48) {
        return false;
    }
    for (const QChar c : v) {
        if (!((c >= QLatin1Char('a') && c <= QLatin1Char('z')) || c == QLatin1Char('_'))) {
            return false;
        }
    }
    return true;
}

// Conteo entero no negativo y acotado; ausente o corrupto -> 0.
int conteo(const QSettings& s, const QString& clave)
{
    bool ok = false;
    const int v = s.value(clave).toString().toInt(&ok);
    return ok && v >= 0 && v <= kMaxConteo ? v : 0;
}

bool bandera(const QSettings& s, const QString& clave)
{
    return s.value(clave).toString() == QLatin1String("true");
}

QString claveOpcional(const QSettings& s, const QString& clave)
{
    const QString v = s.value(clave).toString();
    return esClaveEstable(v) ? v : QString();
}

// Tolerante: sin fecha valida o sin estado valido no hay resumen; un campo
// corrupto se lee como 0/false/vacio. Nunca afecta `integra`.
std::optional<respaldo::ResumenUltimoRespaldo> leerResumen(const QSettings& s)
{
    const auto f = fecha(s, kRFecha);
    const QString estado = s.value(kREstado).toString();
    if (!f || !esClaveEstable(estado)) {
        return std::nullopt;
    }
    respaldo::ResumenUltimoRespaldo r;
    r.fechaUtc = *f;
    r.estado = estado;
    bool ok = false;
    const qulonglong bytes = s.value(kRBytes).toString().toULongLong(&ok);
    r.bytesDatos = ok ? bytes : 0;
    r.faltantes = conteo(s, kRFaltantes);
    r.omitidos = conteo(s, kROmitidos);
    r.depurados = conteo(s, kRDepurados);
    r.copiasSinRegistro = conteo(s, kRSinRegistro);
    r.duplicadosProveedor = conteo(s, kRDuplicados);
    r.avisoDepuracion = bandera(s, kRAvisoDepuracion);
    r.avisoRegistro = bandera(s, kRAvisoRegistro);
    r.errorDestino = claveOpcional(s, kRErrorDestino);
    r.errorCaptura = claveOpcional(s, kRErrorCaptura);
    r.errorContenedor = claveOpcional(s, kRErrorContenedor);
    return r;
}

void ponerResumen(QSettings& s, const std::optional<respaldo::ResumenUltimoRespaldo>& r)
{
    if (!r) {
        s.remove(QStringLiteral("respaldo/ultimo/resumen"));
        return;
    }
    s.setValue(kRFecha, r->fechaUtc.toUTC().toString(Qt::ISODateWithMs));
    s.setValue(kREstado, r->estado);
    s.setValue(kRBytes, QString::number(r->bytesDatos));
    s.setValue(kRFaltantes, QString::number(r->faltantes));
    s.setValue(kROmitidos, QString::number(r->omitidos));
    s.setValue(kRDepurados, QString::number(r->depurados));
    s.setValue(kRSinRegistro, QString::number(r->copiasSinRegistro));
    s.setValue(kRDuplicados, QString::number(r->duplicadosProveedor));
    s.setValue(kRAvisoDepuracion, r->avisoDepuracion ? QStringLiteral("true") : QStringLiteral("false"));
    s.setValue(kRAvisoRegistro, r->avisoRegistro ? QStringLiteral("true") : QStringLiteral("false"));
    for (const auto& [clave, valor] : {std::pair{kRErrorDestino, r->errorDestino}, std::pair{kRErrorCaptura, r->errorCaptura},
                                       std::pair{kRErrorContenedor, r->errorContenedor}}) {
        if (esClaveEstable(valor)) {
            s.setValue(clave, valor);
        } else {
            s.remove(clave);
        }
    }
}

void ponerTexto(QSettings& s, const QString& clave, const QString& valor)
{
    if (valor.isEmpty()) {
        s.remove(clave);
    } else {
        s.setValue(clave, valor);
    }
}

} // namespace

Resultado<respaldo::ConfiguracionLeida, ErrorConfiguracionRespaldo> ConfiguracionRespaldoIni::cargar()
{
    using R = Resultado<respaldo::ConfiguracionLeida, ErrorConfiguracionRespaldo>;
    const bool existe = QFileInfo(m_ruta).isFile();
    QSettings s(m_ruta, QSettings::IniFormat);
    if (s.status() != QSettings::NoError) {
        return R::fallo(ErrorConfiguracionRespaldo::de(QStringLiteral("configuracion.leer")));
    }
    respaldo::ConfiguracionLeida leida;
    ConfiguracionRespaldo& c = leida.configuracion;
    c.rutaCanonica = s.value(kRuta).toString();
    c.bookmark = QByteArray::fromBase64(s.value(kBookmark).toString().toLatin1());
    c.uuidVolumen = s.value(kUuid).toString();
    c.diarioActivado = s.value(kDiario, false).toBool();
    const auto pw = referencia(s, kKekContrasena);
    const auto rec = referencia(s, kKekRecuperacion);
    c.kekContrasena = pw.value_or(ReferenciaKek());
    c.kekRecuperacion = rec.value_or(ReferenciaKek());
    leida.integra = existe && pw && rec && !pw->nula() && !rec->nula();
    c.ultimoIntentoUtc = fecha(s, kIntento);
    c.ultimoExitoUtc = fecha(s, kExito);
    c.ultimoResultado = s.value(kResultado).toString();
    c.ultimoResumen = leerResumen(s);
    return R::exito(std::move(leida));
}

Resultado<Exito, ErrorConfiguracionRespaldo> ConfiguracionRespaldoIni::guardar(const ConfiguracionRespaldo& c)
{
    using R = Resultado<Exito, ErrorConfiguracionRespaldo>;
    QSettings s(m_ruta, QSettings::IniFormat);
    ponerTexto(s, kRuta, c.rutaCanonica);
    ponerTexto(s, kBookmark, QString::fromLatin1(c.bookmark.toBase64()));
    ponerTexto(s, kUuid, c.uuidVolumen);
    s.setValue(kDiario, c.diarioActivado);
    ponerTexto(s, kKekContrasena, c.kekContrasena.texto());
    ponerTexto(s, kKekRecuperacion, c.kekRecuperacion.texto());
    ponerFecha(s, kIntento, c.ultimoIntentoUtc);
    ponerFecha(s, kExito, c.ultimoExitoUtc);
    ponerTexto(s, kResultado, c.ultimoResultado);
    ponerResumen(s, c.ultimoResumen);
    s.sync();
    if (s.status() != QSettings::NoError) {
        return R::fallo(ErrorConfiguracionRespaldo::de(QStringLiteral("configuracion.escribir")));
    }
    return R::exito(Exito{});
}

} // namespace satcfdi
