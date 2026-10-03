#pragma once

#include "ports/SecretStore.h"

#include <QString>

#include <memory>

namespace satcfdi {

// Punto de inyeccion del SecretStore de produccion (T005). Unico lugar de
// satcfdi_app que elige la implementacion concreta.
//
// `directorioDatos`: directorio de datos efectivo (AppDataLocation o
// --data-dir). Los contenedores cifrados viven en `<directorioDatos>/credentials`
// (el adaptador lo crea con 0700 en la primera escritura). El bundle id es el
// del Info.plist (SATCFDI_BUNDLE_IDENTIFIER) y prefija los servicios Keychain.
//
// Devuelve nullptr si la plataforma no tiene SecretStore (solo macOS lo tiene).
std::unique_ptr<SecretStore> crearSecretStore(const QString& directorioDatos);

} // namespace satcfdi
