#include "app_core/AppBootstrapper.h"

#include <QGuiApplication>
#include <QStandardPaths>

int ejecutarTestComposicionPersistida(int argc, char* argv[]);
int ejecutarTestInstanciaUnica(int argc, char* argv[]);
int ejecutarTestCicloDeVida(int argc, char* argv[]);
int ejecutarTestCredencialesArranque(int argc, char* argv[]);
int ejecutarTestPreparacionPerfiles(int argc, char* argv[]);

// satcfdi_integration_tests (T003 + T004). Offscreen (ver CMakeLists).
int main(int argc, char* argv[])
{
    // Red de seguridad: ninguna prueba toca rutas reales del usuario.
    QStandardPaths::setTestModeEnabled(true);
    QGuiApplication app(argc, argv);
    satcfdi::configurarIdentidadAplicacion();

    int fallos = 0;
    fallos += ejecutarTestComposicionPersistida(argc, argv);
    fallos += ejecutarTestInstanciaUnica(argc, argv);
    fallos += ejecutarTestCicloDeVida(argc, argv);
    fallos += ejecutarTestCredencialesArranque(argc, argv);
    fallos += ejecutarTestPreparacionPerfiles(argc, argv);
    return fallos == 0 ? 0 : 1;
}
