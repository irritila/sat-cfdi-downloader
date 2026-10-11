#pragma once

// Puerto ArchivosZipCfdi (T015.2, T015 D2/D10): resuelve la ruta relativa de
// un paquete (ruta_local) bajo la raiz de paquetes y calcula su huella, para
// que el indexador (application) no toque el filesystem directamente.
//
// Hilo: el hilo de trabajo del indexador (nunca el grafico). Solo lectura:
// nunca crea, mueve ni borra archivos. Ninguna ruta sale a logs (D11).

#include <QString>

#include <functional>
#include <optional>

namespace satcfdi {

struct ArchivoZipLocal {
    enum class Estado {
        Existe,       // archivo regular bajo la raiz
        NoExiste,     // la ruta no existe
        Invalido,     // ruta_local con forma no propia (T008 D5), enlace o no regular
        ErrorLectura, // stat fallo por otra causa (permiso, E/S)
    };
    Estado estado = Estado::NoExiste;
    QString rutaAbsoluta; // solo con Existe
    qint64 tamano = 0;    // bytes, solo con Existe
    qint64 modificadoMs = 0; // ms desde epoch UTC, solo con Existe
};

class ArchivosZipCfdi {
public:
    virtual ~ArchivosZipCfdi() = default;

    // Metadatos del ZIP `rutaRelativa` (forma exacta de T008 D5).
    virtual ArchivoZipLocal consultar(const QString& rutaRelativa) = 0;

    // SHA-256 (hex minusculo) leyendo por trozos. nullopt si la lectura falla
    // o si `continuar` devuelve false (cancelacion entre trozos).
    virtual std::optional<QString> sha256(const QString& rutaAbsoluta, const std::function<bool()>& continuar) = 0;
};

} // namespace satcfdi
