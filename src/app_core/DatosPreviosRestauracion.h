#pragma once

#include <QString>
#include <QStringList>

// T025.5 (D22, ADR 0022): carpetas `pre-restauracion-*` que deja una
// restauracion aplicada. Solo cuentan (y solo se borran) las creadas por
// RestauracionArranque: directorio real (no enlace) del euid con el nombre
// `pre-restauracion-*` y, dentro, la marca `.restauracion.json` (archivo
// regular) cuyo id y rutas coinciden. La de datos (`<datos>/pre-restauracion-*`)
// es el registro: su marca apunta a la carpeta hermana junto a la raiz de
// paquetes, que puede estar en otro volumen y cerca de una raiz legada (T019).
// Ademas el id de la marca debe estar en el registro local de restauraciones
// aplicadas (`<datos>/restauracion/aplicadas.json`), que RestauracionArranque
// escribe al promover: una marca copiada o fabricada no cuenta.
// Una carpeta con ese nombre sin marca valida, o un enlace, es ajena: no
// cuenta ni se toca.
//
// Hilo: cualquiera menos el grafico (E/S).
namespace satcfdi::datos_previos_restauracion {

inline constexpr const char* kArchivoMarca = ".restauracion.json";
inline constexpr const char* kArchivoRegistro = "aplicadas.json";

// Registro de restauraciones aplicadas (escritura atomica y durable).
bool registrarAplicada(const QString& directorioDatos, const QString& id);
bool retirarAplicada(const QString& directorioDatos, const QString& id);

// Escribe (si falta) la marca en `carpeta` de forma atomica y durable.
bool escribirMarca(const QString& carpeta, const QString& id, const QString& preDatos, const QString& prePaquetes);
// Directorio real del euid con la marca del id `id`.
bool esCarpetaDe(const QString& carpeta, const QString& id);
// Directorio real del euid vacio, o que solo contiene la marca (y su
// temporal) del id `id`: lo unico que la restauracion puede adoptar.
bool vaciaOSoloMarcaDe(const QString& carpeta, const QString& id);

// Borra la marca de `carpeta` (al revertir, antes del rmdir).
void borrarMarca(const QString& carpeta);

// Carpetas propias pendientes de descartar (datos y paquetes), absolutas.
QStringList pendientes(const QString& directorioDatos);

// D22 (bloqueo de la reconciliacion de e.firma): true si existe alguna carpeta
// propia con marca valida, REGISTRADA O NO, o si el registro existe pero no se
// puede leer (enlace, no regular, demasiado grande o corrupto). Un registro
// perdido o danado nunca habilita el borrado de secretos.
bool bloqueanReconciliacion(const QString& directorioDatos);

// Descartar solo borra las carpetas registradas.
struct ResultadoDescarte {
    int borradas = 0;  // carpetas propias borradas
    int fallidas = 0;  // propias que no se pudieron borrar por completo
    int ajenas = 0;    // `pre-restauracion-*` sin marca valida o enlazadas (intactas)
};

// Borra recursivamente (sin seguir enlaces) las carpetas propias: primero la
// junto a la raiz y al final la de datos, que guarda la marca.
ResultadoDescarte descartar(const QString& directorioDatos);

} // namespace satcfdi::datos_previos_restauracion
