#pragma once

#include "infrastructure/storage/FileOps.h"
#include "ports/PackageStorage.h"

#include <QString>

#include <memory>
#include <optional>

namespace satcfdi {

// PackageStorage sobre el filesystem POSIX/macOS (T008 D5-D10) con la costura
// FileOps inyectable.
//
// Raiz (D8): ruta ABSOLUTA inyectada por el composition root; se crea al
// primer guardado (componentes faltantes con 0700). Con una raiz relativa o
// vacia, toda operacion devuelve LecturaRaiz sin tocar el filesystem.
// Hilo: el del OperacionExecutor. Sin estado mutable propio: una instancia
// puede usarse en serie desde ese hilo.
class FilesystemPackageStorage final : public PackageStorage {
public:
    explicit FilesystemPackageStorage(QString raizAbsoluta,
                                      std::unique_ptr<FileOps> fileOps = std::make_unique<PosixFileOps>());

    // Raiz efectiva (D8), PURA: con `dataDir` -> <dataDir>/paquetes; sin el ->
    // <home>/SAT-CFDI-Downloader/paquetes. `home` se inyecta (QDir::homePath()
    // en produccion) para que las pruebas no dependan de la carpeta real.
    static QString resolverRaiz(const std::optional<QString>& dataDir, const QString& home);

    const QString& raiz() const noexcept { return m_raiz; }

    Resultado<ArchivoFinal, ErrorGuardarZip> guardarAtomico(const UbicacionPaquete& ubicacion,
                                                             FuenteZipPorChunks& fuente,
                                                             const Cancelacion& cancelacion) override;
    Resultado<bool, ErrorAlmacenamiento> existeArchivoFinal(const QString& rutaRelativa) override;
    Resultado<QList<HallazgoFilesystem>, ErrorAlmacenamiento> escanearRecuperacion() override;
    Resultado<Exito, ErrorAlmacenamiento> eliminarTemporal(const HallazgoFilesystem& temporal) override;

private:
    QByteArray nativa(const QString& rutaRelativa) const;
    bool raizValida() const;

    QString m_raiz;
    std::unique_ptr<FileOps> m_ops;
};

} // namespace satcfdi
