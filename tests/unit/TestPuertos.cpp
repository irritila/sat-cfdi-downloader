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
// Que NO prueba: la ausencia de metodos. Agregar metodos (virtuales o no) no
// cambia sizeof y C++20 no tiene reflexion para enumerarlos. La regla "sin
// metodos en T002" se verifica por revision de src/ports/.
struct SoloVptr {
    virtual ~SoloVptr() = default;
};

template <typename Puerto>
constexpr bool esPuertoVacio()
{
    return std::has_virtual_destructor_v<Puerto> && std::is_polymorphic_v<Puerto>
           && sizeof(Puerto) == sizeof(SoloVptr);
}

static_assert(esPuertoVacio<SatGateway>());
static_assert(esPuertoVacio<SecretStore>());
static_assert(esPuertoVacio<OSIntegration>());
static_assert(esPuertoVacio<PackageStorage>());
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
