#pragma once

// E/S POSIX del directorio de contenedores (T005, DS3). Sin OpenSSL ni
// Security. Directorio 0700, archivos 0600, creacion con
// O_CREAT|O_EXCL|O_NOFOLLOW, fsync del archivo, rename atomico en el mismo
// directorio y fsync del directorio. Los errores no incluyen rutas (solo
// etiqueta y errno).
//
// Anti-TOCTOU (revision de seguridad 03): cada operacion abre el directorio
// con O_DIRECTORY|O_NOFOLLOW, valida con fstat (directorio, dueno = euid,
// 0700) y opera RELATIVO a ese descriptor (openat/renameat/unlinkat/fstatat/
// fdopendir). Si `credentials/` se sustituye por un enlace u otro directorio
// no valido, la operacion falla sin escribir ni borrar fuera. Limite: los
// componentes PADRE de la ruta (AppDataLocation) se consideran confiables.

#include "domain/common/Resultado.h"
#include "ports/persistence/Exito.h"
#include "ports/secrets/SecretStoreTypes.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstddef>

namespace satcfdi::secrets {

class DirectorioCredenciales {
public:
    explicit DirectorioCredenciales(QString ruta);

    const QString& ruta() const noexcept { return m_ruta; }

    // Crea el directorio (y padres) si falta y fuerza 0700. Falla si la ruta
    // es un enlace simbolico o no es directorio. Error: FalloEscritura.
    Resultado<Exito, ErrorSecretStore> asegurar() const;

    // Escribe `datos` en `nombre` de forma atomica via `nombreTemporal`.
    // Si falla, elimina el temporal. Error: FalloEscritura.
    Resultado<Exito, ErrorSecretStore> escribirAtomico(const QString& nombre, const QString& nombreTemporal,
                                                       const QByteArray& datos) const;

    enum class Lectura { Ok, NoExiste, Error };
    // Lee un archivo regular (sin seguir enlaces) de hasta `maximo` bytes.
    // Mayor que `maximo` o no regular -> Error.
    Lectura leer(const QString& nombre, std::size_t maximo, QByteArray& datos) const;

    // Elimina `nombre`; inexistente es exito. Error: FalloEscritura.
    Resultado<Exito, ErrorSecretStore> eliminar(const QString& nombre) const;

    // Nombres de las entradas del directorio. Directorio inexistente -> lista
    // vacia. Error (no se puede enumerar): FalloEscritura.
    Resultado<QStringList, ErrorSecretStore> listar() const;

private:
    // Descriptor validado del directorio o -1 (errno). `corregirPermisos`
    // solo en asegurar(): aplica fchmod 0700 sobre el descriptor.
    int abrirAnclado(bool corregirPermisos) const;

    QString m_ruta;
};

// Lee un archivo de entrada elegido por el usuario (.cer/.key) directamente
// en un BufferSecreto, sin QByteArray intermedio.
// - No legible / no regular -> ArchivoIlegible.
// - Vacio o mayor que `maximo` -> FormatoInvalido (antes de reservar).
Resultado<BufferSecreto, ErrorSecretStore> leerArchivoEntrada(const QString& ruta, std::size_t maximo);

} // namespace satcfdi::secrets
