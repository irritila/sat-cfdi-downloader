#pragma once

#include <QByteArray>
#include <QString>

// T025.5: utilidades POSIX de la restauracion (marcador, stagings y arranque).
// Nunca siguen enlaces al recorrer ni al borrar. Hilo: el del llamador (nunca
// el grafico). Sin rutas en los diagnosticos.
namespace satcfdi::archivos_restauracion {

// fsync + F_FULLFSYNC (F_FULLFSYNC no soportado no es error si fsync tuvo
// exito). Sin seguir enlaces (O_NOFOLLOW).
bool sincronizarArchivo(const QString& ruta);
bool sincronizarDirectorio(const QString& ruta);

// tmp (O_CREAT|O_TRUNC|O_NOFOLLOW, 0600) -> write -> fsync -> rename -> fsync
// del directorio.
bool escribirAtomico(const QString& ruta, const QByteArray& datos);

// lstat: existe la entrada (sin seguir el ultimo componente).
bool existe(const QString& ruta);
// Directorio real (no enlace), del euid y con modo exacto 0700.
bool esDirectorioPrivado(const QString& ruta);
// mkdir 0700 exclusivo (falla si ya existe) + chmod.
bool crearDirectorioPrivado(const QString& ruta);
// mkdir 0700 si falta; acepta uno existente solo si es privado.
bool asegurarDirectorioPrivado(const QString& ruta);

// Borra recursivamente sin seguir enlaces, por descriptor: openat con
// O_DIRECTORY|O_NOFOLLOW, fdopendir y unlinkat sobre el padre (un enlace se
// borra como entrada). true si al final no existe.
bool borrarArbol(const QString& ruta);

// fsync de cada archivo y carpeta bajo `ruta`, recorrido por descriptor
// (openat O_NOFOLLOW). Falla ante un enlace u otra clase de entrada.
bool sincronizarArbol(const QString& ruta);

// Excluye `ruta` del respaldo de Time Machine (equivale a
// NSURLIsExcludedFromBackupKey; xattr sin seguir enlaces). Fuera de macOS: true.
inline constexpr const char* kXattrExcluirRespaldo = "com.apple.metadata:com_apple_backup_excludeItem";
bool excluirDeRespaldoDelSistema(const QString& ruta);

// Bytes libres para el usuario en el volumen de `ruta` (statfs), o -1.
qint64 espacioLibre(const QString& ruta);

// Copia `origen` a `destino` (nuevo, O_EXCL, 0600) con fsync. Sin seguir
// enlaces. Se conserva el mtime.
bool copiarDuradero(const QString& origen, const QString& destino);

// st_dev de la entrada (stat, sigue enlaces) o -1.
qint64 dispositivo(const QString& ruta);

// SHA-256 del contenido (archivo regular, sin seguir enlaces); vacio si falla.
QByteArray sha256(const QString& ruta);

// Agrega a `acumulado` una linea por entrada bajo `raiz` (sin la propia
// raiz), en orden estable: carpetas por ruta; archivos con tamano, inodo,
// mtime ns y ctime ns (lstat). No depende de la ubicacion de `raiz`, asi que
// sirve antes y despues de renombrarla.
// El llamador lo resume con SHA-256. false si no se pudo recorrer o hay algo
// que no es archivo regular ni carpeta.
bool acumularArbol(const QString& raiz, const QString& etiqueta, QByteArray& acumulado);

} // namespace satcfdi::archivos_restauracion
