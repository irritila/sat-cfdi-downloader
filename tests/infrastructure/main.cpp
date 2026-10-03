#include "TestSqliteCentinelas.h"
#include "TestSqliteConexion.h"
#include "TestSqliteMigraciones.h"
#include "TestSqliteRepositorios.h"

#include <QCoreApplication>
#include <QStandardPaths>
#include <QTest>

// satcfdi_infrastructure_tests: SQLite real sobre QTemporaryDir (T003).
int main(int argc, char* argv[])
{
    // Red de seguridad: ninguna prueba debe tocar rutas reales del usuario.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);

    int fallos = 0;
    auto ejecutar = [&](QObject&& prueba) { fallos += QTest::qExec(&prueba, argc, argv); };
    ejecutar(TestSqliteMigraciones());
    ejecutar(TestSqliteConexion());
    ejecutar(TestSqliteRepositorios());
    ejecutar(TestSqliteCentinelas());
    return fallos == 0 ? 0 : 1;
}
