#pragma once

#include "ports/respaldo/AlmacenKekRespaldo.h"
#include "ports/respaldo/CarpetaRespaldoSistema.h"

#include <QUuid>

#include <memory>

namespace satcfdi {

// T025.4: adaptadores de plataforma del respaldo (unico lugar de satcfdi_app
// que los elige). macOS: KEK en el data protection keychain (servicio
// `<bundleId>.respaldo.v1.kek.<idInstalacion>`, no sincronizable,
// ThisDeviceOnly) y carpeta por bookmark (NSURL). Otras plataformas: nullptr
// (el gestor informa NoDisponible).
//
// `idInstalacion` (obtenerOCrearIdInstalacion) es el ambito de las KEK: cada
// instalacion (otro --data-dir) solo ve y borra las suyas. Un id nulo da un
// almacen que no opera (AlmacenMalConfigurado, "ambito.invalido").
std::unique_ptr<respaldo::AlmacenKekRespaldo> crearAlmacenKekRespaldo(const QUuid& idInstalacion);
std::unique_ptr<respaldo::CarpetaRespaldoSistema> crearCarpetaRespaldoSistema();

} // namespace satcfdi
