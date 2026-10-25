#include "TestConfiguracionAppService.h"
#include "TestContratoT003.h"
#include "TestContratoT007.h"
#include "TestOperacionExecutor.h"
#include "TestWorkerLocal.h"
#include "TestRecuperacionArchivos.h"
#include "TestCredencialesSat.h"
#include "TestPreparacionPerfiles.h"
#include "TestDemoPerfilesSatService.h"
#include "TestDemoSolicitudesService.h"
#include "TestDominio.h"
#include "TestOSIntegration.h"
#include "TestPuertos.h"
#include "TestAlmacenamientoPaquetes.h"
#include "TestRegexLogSanitizer.h"
#include "TestResultado.h"
#include "TestServiciosPersistidos.h"
#include "TestOperacionesSatProductivo.h"
#include "TestNotificaciones.h"
#include "TestAccesoPaquetes.h"
#include "TestVencimientoEFirma.h"
#include "TestEstadoAgregado.h"
#include "TestCfdiDominio.h"
#include "TestParserCfdi.h"
#include "TestIndiceCfdi.h"
#include "TestPrevisualizacionCfdi.h"
#include "TestCoberturaMensual.h"
#include "TestCatalogoMensualService.h"
#include "TestPlanDescargaGuiada.h"
#include "TestExportacionFacturas.h"
#include "TestPlanificadorExportacion.h"

#include <QCoreApplication>
#include <QTest>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    int fallos = 0;
    auto ejecutar = [&](QObject&& prueba) { fallos += QTest::qExec(&prueba, argc, argv); };
    ejecutar(TestDominio());
    ejecutar(TestPuertos());
    ejecutar(TestResultado());
    ejecutar(TestDemoSolicitudesService());
    ejecutar(TestDemoPerfilesSatService());
    ejecutar(TestContratoT003());
    ejecutar(TestRegexLogSanitizer());
    ejecutar(TestServiciosPersistidos());
    ejecutar(TestOSIntegration());
    ejecutar(TestConfiguracionAppService());
    ejecutar(TestCredencialesSat());
    ejecutar(TestPreparacionPerfiles());
    ejecutar(TestContratoT007());
    ejecutar(TestOperacionExecutor());
    ejecutar(TestWorkerLocal());
    ejecutar(TestRecuperacionArchivos());
    ejecutar(TestAlmacenamientoPaquetes()); // T008
    ejecutar(TestOperacionesSatProductivo()); // T009
    ejecutar(TestNotificaciones());           // T009
    ejecutar(TestAccesoPaquetes());           // T009.1
    ejecutar(TestVencimientoEFirma());        // T014.3
    ejecutar(TestEstadoAgregado());           // T014.4
    ejecutar(TestCfdiDominio());              // T015.1
    ejecutar(TestParserCfdi());               // T015.1
    ejecutar(TestIndiceCfdi());               // T015.2
    ejecutar(TestPrevisualizacionCfdi());     // T015.4
    ejecutar(TestCoberturaMensual());         // T023.2
    ejecutar(TestCatalogoMensualService());   // T023.2
    ejecutar(TestPlanDescargaGuiada());       // T023.3
    ejecutar(TestExportacionFacturas());      // T026.1
    ejecutar(TestPlanificadorExportacion());  // T026.1
    return fallos == 0 ? 0 : 1;
}
