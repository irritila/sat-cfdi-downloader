#include "TestContratoT003.h"
#include "TestDemoPerfilesSatService.h"
#include "TestDemoSolicitudesService.h"
#include "TestDominio.h"
#include "TestPuertos.h"
#include "TestRegexLogSanitizer.h"
#include "TestResultado.h"
#include "TestServiciosPersistidos.h"

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
    return fallos == 0 ? 0 : 1;
}
