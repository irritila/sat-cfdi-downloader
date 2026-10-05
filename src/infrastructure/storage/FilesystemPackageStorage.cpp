#include "infrastructure/storage/FilesystemPackageStorage.h"

#include <QDir>
#include <QFile>
#include <QStringList>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <utility>

namespace satcfdi {

namespace {

constexpr unsigned kModoDirectorio = 0700;
constexpr unsigned kModoArchivo = 0600;
constexpr int kIntentosTemporal = 3;

using ResultadoGuardar = Resultado<ArchivoFinal, ErrorGuardarZip>;

QString textoErrno(int e)
{
    return QString::fromLocal8Bit(std::strerror(e));
}

// Diagnostico saneado: operacion, errno y ruta RELATIVA (nunca bytes).
QString diagnostico(const char* operacion, int e, const QString& rutaRelativa)
{
    return QStringLiteral("%1: %2 (%3)").arg(QString::fromLatin1(operacion), textoErrno(e), rutaRelativa);
}

ErrorAlmacenamiento clasificarEscritura(int e)
{
    switch (e) {
    case EACCES:
    case EPERM:
    case EROFS:
        return ErrorAlmacenamiento::Permiso;
    case ENOSPC:
    case EDQUOT:
        return ErrorAlmacenamiento::SinEspacio;
    default:
        return ErrorAlmacenamiento::Escritura;
    }
}

ResultadoGuardar falla(ErrorAlmacenamiento tipo, QString diag)
{
    return ResultadoGuardar::fallo(ErrorGuardarZip::deAlmacenamiento(tipo, std::move(diag)));
}

QString sufijoAleatorio(FileOps& ops)
{
    unsigned char bytes[8] = {};
    ops.aleatorio(bytes, sizeof bytes);
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(bytes), sizeof bytes).toHex());
}

} // namespace

FilesystemPackageStorage::FilesystemPackageStorage(QString raizAbsoluta, std::unique_ptr<FileOps> fileOps)
    : m_raiz(raizAbsoluta.isEmpty() ? QString() : QDir::cleanPath(raizAbsoluta))
    , m_ops(std::move(fileOps))
{
}

QString FilesystemPackageStorage::resolverRaiz(const std::optional<QString>& dataDir, const QString& home)
{
    if (dataDir && !dataDir->isEmpty()) {
        return QDir::cleanPath(*dataDir + QStringLiteral("/paquetes"));
    }
    return QDir::cleanPath(home + QStringLiteral("/SAT-CFDI-Downloader/paquetes"));
}

bool FilesystemPackageStorage::raizValida() const
{
    return !m_raiz.isEmpty() && QDir::isAbsolutePath(m_raiz) && m_ops;
}

QByteArray FilesystemPackageStorage::nativa(const QString& rutaRelativa) const
{
    return QFile::encodeName(rutaRelativa.isEmpty() ? m_raiz : m_raiz + QLatin1Char('/') + rutaRelativa);
}

Resultado<ArchivoFinal, ErrorGuardarZip> FilesystemPackageStorage::guardarAtomico(const UbicacionPaquete& ubicacion,
                                                                                    FuenteZipPorChunks& fuente,
                                                                                    const Cancelacion& cancelacion)
{
    // 1. Validacion pura: nada toca el filesystem si la entrada es invalida.
    const auto derivada = rutapaquete::derivarRutaRelativa(ubicacion);
    if (!derivada) {
        return falla(ErrorAlmacenamiento::EntradaInvalida, QStringLiteral("ubicacion de paquete invalida"));
    }
    if (!raizValida()) {
        return falla(ErrorAlmacenamiento::LecturaRaiz, QStringLiteral("raiz de paquetes invalida"));
    }
    if (cancelacion.solicitada()) {
        return falla(ErrorAlmacenamiento::Cancelada, QStringLiteral("cancelada antes de iniciar"));
    }
    const QString rel = derivada.valor();
    const QStringList partes = rel.split(QLatin1Char('/'));
    const QString nombreFinal = partes.last();
    const QString relDirectorio = partes.mid(0, 3).join(QLatin1Char('/'));

    // 2. Colision previa: no se descarga sobre un final existente.
    FileOps::TipoEntrada tipo{};
    if (m_ops->tipo(nativa(rel), tipo) == 0) {
        return falla(ErrorAlmacenamiento::ColisionDestino, QStringLiteral("destino existente (%1)").arg(rel));
    }

    // 3. Raiz (puede incluir enlaces del sistema, p. ej. /var) y estructura
    // propia (solo directorios reales, 0700).
    {
        const QStringList componentesRaiz = m_raiz.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QString acumulada;
        for (const QString& c : componentesRaiz) {
            acumulada += QLatin1Char('/') + c;
            const QByteArray ruta = QFile::encodeName(acumulada);
            const int e = m_ops->tipo(ruta, tipo);
            if (e == 0 && (tipo == FileOps::TipoEntrada::Directorio || tipo == FileOps::TipoEntrada::Enlace)) {
                continue;
            }
            if (e == 0) {
                return falla(ErrorAlmacenamiento::Escritura, QStringLiteral("raiz no es un directorio"));
            }
            if (e != ENOENT) {
                return falla(clasificarEscritura(e), diagnostico("raiz", e, QString()));
            }
            if (const int c2 = m_ops->crearDirectorio(ruta, kModoDirectorio); c2 != 0 && c2 != EEXIST) {
                return falla(clasificarEscritura(c2), diagnostico("crear raiz", c2, QString()));
            }
        }
        QString relAcumulada;
        for (int i = 0; i < 3; ++i) {
            relAcumulada += (i == 0 ? QString() : QStringLiteral("/")) + partes.at(i);
            const QByteArray ruta = nativa(relAcumulada);
            const int e = m_ops->tipo(ruta, tipo);
            if (e == 0) {
                if (tipo != FileOps::TipoEntrada::Directorio) {
                    return falla(ErrorAlmacenamiento::Escritura,
                                 QStringLiteral("componente no es directorio (%1)").arg(relAcumulada));
                }
                continue;
            }
            if (e != ENOENT) {
                return falla(clasificarEscritura(e), diagnostico("directorio", e, relAcumulada));
            }
            if (const int c2 = m_ops->crearDirectorio(ruta, kModoDirectorio); c2 != 0 && c2 != EEXIST) {
                return falla(clasificarEscritura(c2), diagnostico("crear directorio", c2, relAcumulada));
            }
        }
    }

    // 4. Temporal exclusivo en la MISMA carpeta (D6).
    int fd = -1;
    QString relTemporal;
    for (int intento = 0; intento < kIntentosTemporal; ++intento) {
        relTemporal = relDirectorio + QStringLiteral("/.") + nombreFinal + QLatin1Char('.') + sufijoAleatorio(*m_ops)
                      + QStringLiteral(".part");
        const int e = m_ops->abrirExclusivo(nativa(relTemporal), kModoArchivo, fd);
        if (e == 0) {
            break;
        }
        fd = -1;
        if (e != EEXIST || intento + 1 == kIntentosTemporal) {
            return falla(clasificarEscritura(e), diagnostico("crear temporal", e, relDirectorio));
        }
    }
    const QByteArray temporal = nativa(relTemporal);
    // Limpieza ante cualquier salida sin promover.
    auto abortar = [&](ErrorGuardarZip error) {
        if (fd >= 0) {
            m_ops->cerrar(fd);
            fd = -1;
        }
        m_ops->eliminarArchivo(temporal);
        return ResultadoGuardar::fallo(std::move(error));
    };

    // 5. Escritura por chunks, observando la cancelacion antes de cada uno.
    for (;;) {
        if (cancelacion.solicitada()) {
            return abortar(ErrorGuardarZip::deAlmacenamiento(ErrorAlmacenamiento::Cancelada,
                                                             QStringLiteral("cancelada durante la escritura")));
        }
        auto chunk = fuente.siguiente();
        if (!chunk) {
            return abortar(ErrorGuardarZip::deFuente(chunk.error().diagnostico));
        }
        if (!chunk.valor()) {
            break;
        }
        const QByteArray& datos = *chunk.valor();
        if (const int e = m_ops->escribir(fd, datos.constData(), std::size_t(datos.size())); e != 0) {
            return abortar(ErrorGuardarZip::deAlmacenamiento(clasificarEscritura(e),
                                                             diagnostico("escribir", e, relTemporal)));
        }
    }
    if (cancelacion.solicitada()) {
        return abortar(ErrorGuardarZip::deAlmacenamiento(ErrorAlmacenamiento::Cancelada,
                                                         QStringLiteral("cancelada antes de promover")));
    }

    // 6. Durabilidad antes de promover (D7): si falla, no se promueve.
    if (const int e = m_ops->sincronizarArchivo(fd); e != 0) {
        return abortar(ErrorGuardarZip::deAlmacenamiento(ErrorAlmacenamiento::Durabilidad,
                                                         diagnostico("fsync", e, relTemporal)));
    }
    const int errorCierre = m_ops->cerrar(fd);
    fd = -1;
    if (errorCierre != 0) {
        return abortar(ErrorGuardarZip::deAlmacenamiento(clasificarEscritura(errorCierre),
                                                         diagnostico("cerrar", errorCierre, relTemporal)));
    }

    // 7. Promocion sin reemplazo.
    if (const int e = m_ops->promoverSinReemplazo(temporal, nativa(rel)); e != 0) {
        return abortar(ErrorGuardarZip::deAlmacenamiento(
            e == EEXIST ? ErrorAlmacenamiento::ColisionDestino : ErrorAlmacenamiento::Promocion,
            diagnostico("promover", e, rel)));
    }

    // 8. fsync del directorio: el final ya existe; un fallo es advertencia (D7).
    ArchivoFinal final;
    final.rutaRelativa = rel;
    final.advertenciaDurabilidad = m_ops->sincronizarDirectorio(nativa(relDirectorio)) != 0;
    return ResultadoGuardar::exito(std::move(final));
}

Resultado<bool, ErrorAlmacenamiento> FilesystemPackageStorage::existeArchivoFinal(const QString& rutaRelativa)
{
    using R = Resultado<bool, ErrorAlmacenamiento>;
    if (!rutapaquete::esRutaFinalValida(rutaRelativa)) {
        return R::fallo(ErrorAlmacenamiento::EntradaInvalida);
    }
    if (!raizValida()) {
        return R::fallo(ErrorAlmacenamiento::LecturaRaiz);
    }
    FileOps::TipoEntrada tipo{};
    const int e = m_ops->tipo(nativa(rutaRelativa), tipo);
    if (e == ENOENT || e == ENOTDIR) {
        return R::exito(false);
    }
    if (e != 0) {
        return R::fallo(ErrorAlmacenamiento::LecturaRaiz);
    }
    return R::exito(tipo == FileOps::TipoEntrada::Archivo);
}

Resultado<QList<HallazgoFilesystem>, ErrorAlmacenamiento> FilesystemPackageStorage::escanearRecuperacion()
{
    using R = Resultado<QList<HallazgoFilesystem>, ErrorAlmacenamiento>;
    using Tipo = FileOps::TipoEntrada;
    if (!raizValida()) {
        return R::fallo(ErrorAlmacenamiento::LecturaRaiz);
    }
    Tipo tipoRaiz{};
    const int e = m_ops->tipo(nativa(QString()), tipoRaiz);
    if (e == ENOENT) {
        return R::exito({});
    }
    if (e != 0 || (tipoRaiz != Tipo::Directorio && tipoRaiz != Tipo::Enlace)) {
        return R::fallo(ErrorAlmacenamiento::LecturaRaiz);
    }

    // Solo subdirectorios reales cuyo nombre cumple el nivel; lo demas se ignora.
    auto subdirectorios = [&](const QString& rel, auto esValido, QStringList& salida) {
        QList<FileOps::EntradaDirectorio> entradas;
        if (m_ops->listar(nativa(rel), entradas) != 0) {
            return false;
        }
        for (const auto& entrada : entradas) {
            const QString nombre = QFile::decodeName(entrada.nombre);
            if (entrada.tipo == Tipo::Directorio && esValido(nombre)) {
                salida.append(rel.isEmpty() ? nombre : rel + QLatin1Char('/') + nombre);
            }
        }
        return true;
    };

    QStringList rfcs;
    if (!subdirectorios(QString(), [](const QString& n) { return rutapaquete::esRfcComponente(n); }, rfcs)) {
        return R::fallo(ErrorAlmacenamiento::LecturaRaiz);
    }
    QStringList periodos;
    for (const QString& r : std::as_const(rfcs)) {
        if (!subdirectorios(r, [](const QString& n) { return rutapaquete::esPeriodoComponente(n); }, periodos)) {
            return R::fallo(ErrorAlmacenamiento::LecturaRaiz);
        }
    }
    QStringList solicitudes;
    for (const QString& p : std::as_const(periodos)) {
        if (!subdirectorios(p, [](const QString& n) { return uuid::esCanonico(n); }, solicitudes)) {
            return R::fallo(ErrorAlmacenamiento::LecturaRaiz);
        }
    }

    QList<HallazgoFilesystem> hallazgos;
    for (const QString& relSolicitud : std::as_const(solicitudes)) {
        QList<FileOps::EntradaDirectorio> entradas;
        if (m_ops->listar(nativa(relSolicitud), entradas) != 0) {
            return R::fallo(ErrorAlmacenamiento::LecturaRaiz);
        }
        const auto solicitudId = SolicitudId::desdeTexto(relSolicitud.section(QLatin1Char('/'), 2, 2));
        for (const auto& entrada : entradas) {
            if (entrada.tipo != Tipo::Archivo || !solicitudId) {
                continue; // enlaces, directorios y especiales nunca son propios
            }
            const QString nombre = QFile::decodeName(entrada.nombre);
            HallazgoFilesystem h;
            h.rutaRelativa = relSolicitud + QLatin1Char('/') + nombre;
            h.solicitudId = *solicitudId;
            if (rutapaquete::esNombreFinal(nombre)) {
                h.tipo = TipoHallazgo::Final;
                h.archivoFinal = nombre;
            } else if (const auto asociado = rutapaquete::finalDeTemporal(nombre)) {
                h.tipo = TipoHallazgo::Temporal;
                h.archivoFinal = *asociado;
            } else {
                continue; // ajeno: ni se reporta ni se borra
            }
            hallazgos.append(std::move(h));
        }
    }
    std::sort(hallazgos.begin(), hallazgos.end(), [](const HallazgoFilesystem& a, const HallazgoFilesystem& b) {
        return a.rutaRelativa < b.rutaRelativa;
    });
    return R::exito(std::move(hallazgos));
}

Resultado<Exito, ErrorAlmacenamiento> FilesystemPackageStorage::eliminarTemporal(const HallazgoFilesystem& temporal)
{
    using R = Resultado<Exito, ErrorAlmacenamiento>;
    // Solo un temporal propio y coherente con sus campos; nunca un final.
    const auto partes = rutapaquete::separarRutaPropia(temporal.rutaRelativa);
    const auto asociado = partes ? rutapaquete::finalDeTemporal(partes->nombre) : std::nullopt;
    if (temporal.tipo != TipoHallazgo::Temporal || !asociado || *asociado != temporal.archivoFinal
        || partes->solicitudId != temporal.solicitudId.texto()) {
        return R::fallo(ErrorAlmacenamiento::EntradaInvalida);
    }
    if (!raizValida()) {
        return R::fallo(ErrorAlmacenamiento::LecturaRaiz);
    }
    const QByteArray ruta = nativa(temporal.rutaRelativa);
    FileOps::TipoEntrada tipo{};
    const int e = m_ops->tipo(ruta, tipo);
    if (e == ENOENT) {
        return R::exito(Exito{});
    }
    if (e != 0) {
        return R::fallo(ErrorAlmacenamiento::LecturaRaiz);
    }
    if (tipo != FileOps::TipoEntrada::Archivo) {
        return R::fallo(ErrorAlmacenamiento::EntradaInvalida);
    }
    if (const int u = m_ops->eliminarArchivo(ruta); u != 0 && u != ENOENT) {
        const ErrorAlmacenamiento c = clasificarEscritura(u);
        return R::fallo(c == ErrorAlmacenamiento::Permiso ? c : ErrorAlmacenamiento::Escritura);
    }
    return R::exito(Exito{});
}

} // namespace satcfdi
