#include "app_core/ConfiguracionRespaldoIni.h"

#include <QFileInfo>
#include <QSettings>
#include <QTimeZone>

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
    s.sync();
    if (s.status() != QSettings::NoError) {
        return R::fallo(ErrorConfiguracionRespaldo::de(QStringLiteral("configuracion.escribir")));
    }
    return R::exito(Exito{});
}

} // namespace satcfdi
