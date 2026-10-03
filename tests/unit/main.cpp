#include "TestDemoPerfilesSatService.h"
#include "TestDemoSolicitudesService.h"
#include "TestDominio.h"
#include "TestPuertos.h"
#include "TestResultado.h"

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
    return fallos == 0 ? 0 : 1;
}
