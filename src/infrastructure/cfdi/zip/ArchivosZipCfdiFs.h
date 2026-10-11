#pragma once

// ArchivosZipCfdiFs (T015.2): implementacion de ArchivosZipCfdi sobre la raiz
// de paquetes (T008 D8). Solo lectura: lstat/stat y lectura por trozos para el
// SHA-256; nunca crea, mueve ni borra. Acepta solo rutas relativas con la
// forma exacta de T008 D5 (sin "..", absolutas ni componentes vacios) y
// rechaza enlaces simbolicos en el archivo final. Sin estado mutable: segura
// desde el hilo del indexador. Sin logs (D11).

#include "ports/cfdi/ArchivosZipCfdi.h"

namespace satcfdi {

class ArchivosZipCfdiFs final : public ArchivosZipCfdi {
public:
    explicit ArchivosZipCfdiFs(QString raizPaquetes);

    ArchivoZipLocal consultar(const QString& rutaRelativa) override;
    std::optional<QString> sha256(const QString& rutaAbsoluta, const std::function<bool()>& continuar) override;

private:
    QString m_raiz;
};

} // namespace satcfdi
