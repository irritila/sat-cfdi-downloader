#include "TestC14nFirma.h"
#include "TestClienteHttpSat.h"
#include "TestFakeSatGateway.h"
#include "TestSatGatewayProductivo.h"
#include "TestSobresRespuestas.h"

#include <QCoreApplication>
#include <QStringList>
#include <QTest>

// satcfdi_sat_spike_tests [--suite unit|infrastructure] [--actualizar-goldens] [args de QTest]
// --actualizar-goldens solo actua con SATCFDI_ACTUALIZAR_GOLDENS=1 (nunca en ctest).
// Sin --suite ejecuta ambas.
int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QString suite;
    QStringList args;
    const QStringList entrada = app.arguments();
    for (qsizetype i = 0; i < entrada.size(); ++i) {
        if (entrada.at(i) == QStringLiteral("--actualizar-goldens")) {
            satpruebas::actualizarGoldens = true;
        } else if (entrada.at(i) == QStringLiteral("--suite") && i + 1 < entrada.size()) {
            suite = entrada.at(++i);
        } else {
            args.append(entrada.at(i));
        }
    }
    int fallos = 0;
    auto ejecutar = [&](QObject&& prueba) { fallos += QTest::qExec(&prueba, args); };
    if (suite.isEmpty() || suite == QStringLiteral("unit")) {
        ejecutar(TestC14nFirma());
        ejecutar(TestSobresRespuestas());
        ejecutar(TestFakeSatGateway()); // T009
    }
    if (suite.isEmpty() || suite == QStringLiteral("infrastructure")) {
        ejecutar(TestClienteHttpSat());
        ejecutar(TestSatGatewayProductivo()); // T009
    }
    return fallos == 0 ? 0 : 1;
}
