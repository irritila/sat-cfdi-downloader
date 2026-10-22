#pragma once

#include "ports/respaldo/CarpetaRespaldoSistema.h"

namespace satcfdi::macos {

// CarpetaRespaldoSistema sobre NSURL (T025.4). Header C++ puro; Foundation
// queda en el .mm.
//
// - crearReferencia(): exige ruta absoluta; realpath(3) resuelve enlaces
//   (incluido el ultimo componente) y la ruta debe ser un directorio. El
//   bookmark es normal (`bookmarkDataWithOptions:0`, sin security scope: la
//   app no usa App Sandbox). UUID de volumen con NSURLVolumeUUIDStringKey.
// - resolver(): `NSURLBookmarkResolutionWithoutUI |
//   NSURLBookmarkResolutionWithoutMounting` (el respaldo diario corre sin
//   usuario: nunca dialogos ni montajes de red). La ruta resuelta se vuelve a
//   canonicalizar con realpath y debe ser un directorio fuera de la Papelera.
//
// Sin estado; reentrante. Bloquea: nunca en el hilo grafico.
class CarpetaRespaldoMacOS final : public respaldo::CarpetaRespaldoSistema {
public:
    Resultado<respaldo::ReferenciaCarpetaRespaldo, respaldo::ErrorCarpetaRespaldo>
    crearReferencia(const QString& rutaElegida) const override;

    Resultado<respaldo::CarpetaRespaldoResuelta, respaldo::ErrorCarpetaRespaldo>
    resolver(const QByteArray& bookmark) const override;
};

} // namespace satcfdi::macos
