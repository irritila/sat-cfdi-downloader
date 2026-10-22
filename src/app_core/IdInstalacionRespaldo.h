#pragma once

#include <QString>
#include <QUuid>

namespace satcfdi {

// T025.3 (D17): id de esta instalacion para reconocer sus propios respaldos y
// `.partial`. Se guarda en `<directorioDatos>/preferencias.ini`, clave
// `respaldo/idInstalacion` (UUID canonico):
// - es local del equipo y de la app (como el resto de la configuracion del
//   respaldo, D14, seccion `respaldo`);
// - NUNCA entra al respaldo: la captura (T025.2) solo copia la lista blanca de
//   claves exactas (`facturas/ultimoPerfil`), y no esta en SQLite, que si se
//   respalda. Asi, restaurar en otra Mac no hace que esa Mac depure las copias
//   de esta.
// Si falta o no es un UUID canonico se genera uno nuevo y se escribe. Hilo:
// cualquiera, sin concurrencia con otra escritura del mismo ini.
QUuid obtenerOCrearIdInstalacion(const QString& rutaPreferencias);

inline constexpr const char* kClaveIdInstalacion = "respaldo/idInstalacion";

} // namespace satcfdi
