#include "infrastructure/cfdi/zip/ArchivosZipCfdiFs.h"

#include "ports/storage/RutaPaquete.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cerrno>
#include <sys/stat.h>

#include <utility>

namespace satcfdi {

namespace {

constexpr qint64 kTrozo = 1024 * 1024;

} // namespace

ArchivosZipCfdiFs::ArchivosZipCfdiFs(QString raizPaquetes)
    : m_raiz(std::move(raizPaquetes))
{
}

ArchivoZipLocal ArchivosZipCfdiFs::consultar(const QString& rutaRelativa)
{
    ArchivoZipLocal r;
    if (m_raiz.isEmpty() || !QDir::isAbsolutePath(m_raiz) || !rutapaquete::esRutaFinalValida(rutaRelativa)) {
        r.estado = ArchivoZipLocal::Estado::Invalido;
        return r;
    }
    const QString ruta = QDir(m_raiz).filePath(rutaRelativa);
    struct stat info {};
    if (::lstat(QFile::encodeName(ruta).constData(), &info) != 0) {
        r.estado = (errno == ENOENT || errno == ENOTDIR) ? ArchivoZipLocal::Estado::NoExiste
                                                         : ArchivoZipLocal::Estado::ErrorLectura;
        return r;
    }
    if (!S_ISREG(info.st_mode)) {
        r.estado = ArchivoZipLocal::Estado::Invalido; // enlace, directorio u otro
        return r;
    }
    r.estado = ArchivoZipLocal::Estado::Existe;
    r.rutaAbsoluta = ruta;
    r.tamano = static_cast<qint64>(info.st_size);
#if defined(__APPLE__)
    const struct timespec& mtime = info.st_mtimespec;
#else
    const struct timespec& mtime = info.st_mtim;
#endif
    r.modificadoMs = static_cast<qint64>(mtime.tv_sec) * 1000 + static_cast<qint64>(mtime.tv_nsec / 1000000);
    return r;
}

std::optional<QString> ArchivosZipCfdiFs::sha256(const QString& rutaAbsoluta, const std::function<bool()>& continuar)
{
    QFile archivo(rutaAbsoluta);
    if (!archivo.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray trozo;
    while (!archivo.atEnd()) {
        if (continuar && !continuar()) {
            return std::nullopt;
        }
        trozo = archivo.read(kTrozo);
        if (trozo.isEmpty() && archivo.error() != QFileDevice::NoError) {
            return std::nullopt;
        }
        hash.addData(trozo);
    }
    if (archivo.error() != QFileDevice::NoError) {
        return std::nullopt;
    }
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace satcfdi
