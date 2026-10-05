#include "TestPuertos.h"

#include "ports/LogSanitizer.h"
#include "ports/OSIntegration.h"
#include "ports/PackageStorage.h"
#include "ports/SatGateway.h"
#include "ports/SecretStore.h"
#include "ports/repositories/ConfiguracionAppRepository.h"
#include "ports/repositories/LogSolicitudRepository.h"
#include "ports/repositories/PaqueteSolicitudRepository.h"
#include "ports/repositories/PerfilSatRepository.h"
#include "ports/repositories/SolicitudMasivaRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <QTest>

#include <type_traits>

using namespace satcfdi;

namespace {

// Que si prueba: cada puerto compila de forma aislada, es polimorfico, tiene
// destructor virtual y no tiene datos miembro (tamano igual al de una clase con
// solo vptr).
//
// Que NO prueba: la forma de los metodos. Desde T003 los repositorios,
// UnitOfWork y LogSanitizer tienen metodos virtuales puros (no cambian
// sizeof); SecretStore (T005), PackageStorage (T008) y SatGateway (T009)
// tambien tienen metodos y se prueban en sus suites (SatGateway con
// FakeSatGateway y el servidor HTTP local en tests/sat, y por
// OperacionesSatProductivo en TestOperacionesSatProductivo). Desde T004 OSIntegration es un
// QObject abstracto y su contrato se prueba en TestOSIntegration.
struct SoloVptr {
    virtual ~SoloVptr() = default;
};

template <typename Puerto>
constexpr bool esPuertoVacio()
{
    return std::has_virtual_destructor_v<Puerto> && std::is_polymorphic_v<Puerto>
           && sizeof(Puerto) == sizeof(SoloVptr);
}

// SatGateway (T009): interfaz abstracta con metodos; sin estado propio.
static_assert(std::is_abstract_v<SatGateway> && esPuertoVacio<SatGateway>());
static_assert(esPuertoVacio<SecretStore>());
static_assert(std::is_abstract_v<OSIntegration> && std::is_base_of_v<QObject, OSIntegration>);
// PackageStorage (T008): interfaz abstracta con metodos; sin estado propio.
static_assert(std::is_abstract_v<PackageStorage> && esPuertoVacio<PackageStorage>());
static_assert(esPuertoVacio<LogSanitizer>());
static_assert(esPuertoVacio<PerfilSatRepository>());
static_assert(esPuertoVacio<SolicitudMasivaRepository>());
static_assert(esPuertoVacio<PaqueteSolicitudRepository>());
static_assert(esPuertoVacio<LogSolicitudRepository>());
static_assert(esPuertoVacio<ConfiguracionAppRepository>());
static_assert(esPuertoVacio<UnitOfWork>());

} // namespace

void TestPuertos::puertosTienenDestructorVirtualYNingunEstado()
{
    // Comprobacion en compilacion (static_assert); aqui solo se registra la prueba.
    QVERIFY(true);
}
