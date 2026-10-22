#include "app_core/DatosPreviosRestauracion.h"

#include "app_core/ArchivosRestauracion.h"
#include "app_core/MarcadorRestauracion.h"
#include "domain/common/UuidCanonico.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

#include <optional>

namespace satcfdi::datos_previos_restauracion {

namespace {

namespace ar = archivos_restauracion;

constexpr int kFormato = 1;
constexpr qint64 kMaxBytesMarca = 16 * 1024;

struct Marca {
    QString id;
    QString preDatos;
    QString prePaquetes;
};

bool nombreValido(const QString& ruta)
{
    return QFileInfo(ruta).fileName().startsWith(QLatin1String(marcador_restauracion::kPrefijoPre));
}

// Directorio real (no enlace) del euid.
bool carpetaPropia(const QString& ruta)
{
    struct stat st {};
    return ::lstat(QFile::encodeName(ruta).constData(), &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == ::geteuid();
}

std::optional<Marca> leerMarca(const QString& carpeta)
{
    const QString ruta = QDir(carpeta).filePath(QLatin1String(kArchivoMarca));
    struct stat st {};
    if (::lstat(QFile::encodeName(ruta).constData(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > kMaxBytesMarca) {
        return std::nullopt;
    }
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.read(kMaxBytesMarca));
    if (!doc.isObject() || doc.object().value(QStringLiteral("formato")).toInt(-1) != kFormato) {
        return std::nullopt;
    }
    const QJsonObject o = doc.object();
    Marca m{o.value(QStringLiteral("id")).toString(), o.value(QStringLiteral("preDatos")).toString(),
            o.value(QStringLiteral("prePaquetes")).toString()};
    const auto absoluta = [](const QString& r) { return QDir::isAbsolutePath(r) && QDir::cleanPath(r) == r; };
    if (!uuid::esCanonico(m.id) || !absoluta(m.preDatos) || !absoluta(m.prePaquetes) || !nombreValido(m.preDatos)
        || !nombreValido(m.prePaquetes)) {
        return std::nullopt;
    }
    return m;
}

QString rutaRegistro(const QString& directorioDatos)
{
    return QDir(marcador_restauracion::rutaCarpeta(directorioDatos)).filePath(QLatin1String(kArchivoRegistro));
}

enum class EstadoRegistro { Ausente, Leido, Ilegible };

// Ausente: no existe. Ilegible: existe pero es enlace, no regular, demasiado
// grande o no es JSON del formato esperado.
EstadoRegistro leerRegistro(const QString& directorioDatos, QStringList& ids)
{
    ids.clear();
    const QString ruta = rutaRegistro(directorioDatos);
    struct stat st {};
    if (::lstat(QFile::encodeName(ruta).constData(), &st) != 0) {
        return errno == ENOENT ? EstadoRegistro::Ausente : EstadoRegistro::Ilegible;
    }
    if (!S_ISREG(st.st_mode) || st.st_size > 1024 * 1024) {
        return EstadoRegistro::Ilegible;
    }
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) {
        return EstadoRegistro::Ilegible;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()
        || doc.object().value(QStringLiteral("formato")).toInt(-1) != kFormato
        || !doc.object().value(QStringLiteral("ids")).isArray()) {
        return EstadoRegistro::Ilegible;
    }
    for (const QJsonValue& v : doc.object().value(QStringLiteral("ids")).toArray()) {
        if (!uuid::esCanonico(v.toString())) {
            return EstadoRegistro::Ilegible;
        }
        ids.append(v.toString());
    }
    return EstadoRegistro::Leido;
}

QStringList leerRegistro(const QString& directorioDatos)
{
    QStringList ids;
    return leerRegistro(directorioDatos, ids) == EstadoRegistro::Leido ? ids : QStringList();
}

bool guardarRegistro(const QString& directorioDatos, const QStringList& ids)
{
    if (!marcador_restauracion::asegurarCarpeta(directorioDatos)) {
        return false;
    }
    QJsonObject o;
    o.insert(QStringLiteral("formato"), kFormato);
    o.insert(QStringLiteral("ids"), QJsonArray::fromStringList(ids));
    return ar::escribirAtomico(rutaRegistro(directorioDatos), QJsonDocument(o).toJson(QJsonDocument::Compact));
}

struct Propia {
    QString id;
    QString datos;
    std::optional<QString> paquetes; // si existe y su marca coincide
};

// Escanea `<datos>/pre-restauracion-*`.
QList<Propia> escanear(const QString& directorioDatos, int* ajenas, bool exigirRegistro = true)
{
    QList<Propia> propias;
    const QString datos = QDir::cleanPath(QDir(directorioDatos).absolutePath());
    const QStringList aplicadas = leerRegistro(datos);
    const QStringList nombres = QDir(datos).entryList(
        QStringList{QLatin1String(marcador_restauracion::kPrefijoPre) + QLatin1Char('*')},
        QDir::AllEntries | QDir::System | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QString& nombre : nombres) {
        const QString carpeta = QDir(datos).filePath(nombre);
        const auto marca = carpetaPropia(carpeta) ? leerMarca(carpeta) : std::nullopt;
        if (!marca || marca->preDatos != carpeta || (exigirRegistro && !aplicadas.contains(marca->id))) {
            if (ajenas) {
                ++*ajenas;
            }
            continue;
        }
        Propia p{marca->id, carpeta, std::nullopt};
        if (marca->prePaquetes != carpeta && carpetaPropia(marca->prePaquetes)) {
            const auto hermana = leerMarca(marca->prePaquetes);
            if (hermana && hermana->id == marca->id && hermana->prePaquetes == marca->prePaquetes) {
                p.paquetes = marca->prePaquetes;
            } else if (ajenas) {
                ++*ajenas;
            }
        }
        propias.append(p);
    }
    return propias;
}

} // namespace

bool escribirMarca(const QString& carpeta, const QString& id, const QString& preDatos, const QString& prePaquetes)
{
    const QString ruta = QDir(carpeta).filePath(QLatin1String(kArchivoMarca));
    if (ar::existe(ruta)) {
        return true;
    }
    QJsonObject o;
    o.insert(QStringLiteral("formato"), kFormato);
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("preDatos"), preDatos);
    o.insert(QStringLiteral("prePaquetes"), prePaquetes);
    return ar::escribirAtomico(ruta, QJsonDocument(o).toJson(QJsonDocument::Compact));
}

bool registrarAplicada(const QString& directorioDatos, const QString& id)
{
    QStringList ids = leerRegistro(directorioDatos);
    if (ids.contains(id)) {
        return true;
    }
    ids.append(id);
    return guardarRegistro(directorioDatos, ids);
}

bool retirarAplicada(const QString& directorioDatos, const QString& id)
{
    QStringList ids = leerRegistro(directorioDatos);
    if (!ids.removeAll(id)) {
        return true;
    }
    return guardarRegistro(directorioDatos, ids);
}

void borrarMarca(const QString& carpeta)
{
    ::unlink(QFile::encodeName(QDir(carpeta).filePath(QLatin1String(kArchivoMarca))).constData());
}

QStringList pendientes(const QString& directorioDatos)
{
    QStringList r;
    for (const Propia& p : escanear(directorioDatos, nullptr)) {
        r.append(p.datos);
        if (p.paquetes) {
            r.append(*p.paquetes);
        }
    }
    return r;
}

bool esCarpetaDe(const QString& carpeta, const QString& id)
{
    if (!carpetaPropia(carpeta)) {
        return false;
    }
    const auto marca = leerMarca(carpeta);
    return marca && marca->id == id;
}

bool vaciaOSoloMarcaDe(const QString& carpeta, const QString& id)
{
    if (!carpetaPropia(carpeta)) {
        return false;
    }
    const QString marca = QLatin1String(kArchivoMarca);
    const QString tmp = marca + QStringLiteral(".tmp");
    const QStringList hay =
        QDir(carpeta).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QString& h : hay) {
        if (h != marca && h != tmp) {
            return false;
        }
    }
    return !hay.contains(marca) || esCarpetaDe(carpeta, id);
}

bool bloqueanReconciliacion(const QString& directorioDatos)
{
    QStringList ids;
    if (leerRegistro(directorioDatos, ids) == EstadoRegistro::Ilegible) {
        return true; // sin registro confiable no se borra ningun secreto
    }
    return !escanear(directorioDatos, nullptr, false).isEmpty();
}

ResultadoDescarte descartar(const QString& directorioDatos)
{
    ResultadoDescarte r;
    for (const Propia& p : escanear(directorioDatos, &r.ajenas)) {
        bool ok = true;
        if (p.paquetes) {
            ok = ar::borrarArbol(*p.paquetes);
            r.borradas += ok ? 1 : 0;
            r.fallidas += ok ? 0 : 1;
        }
        // La de datos (con la marca registro) al final: si algo falla, el
        // siguiente descarte la vuelve a encontrar.
        if (ok && ar::borrarArbol(p.datos)) {
            ++r.borradas;
            retirarAplicada(directorioDatos, p.id);
        } else if (ok) {
            ++r.fallidas;
        }
    }
    return r;
}

} // namespace satcfdi::datos_previos_restauracion
