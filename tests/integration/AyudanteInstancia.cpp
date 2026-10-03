#include "app_core/SingleInstanceCoordinator.h"

#include <QCoreApplication>
#include <QTimer>

#include <cstdio>

// satcfdi_instance_helper: proceso auxiliar de TestInstanciaUnica (prueba
// multiproceso de la carrera de adquisicion).
// Uso: satcfdi_instance_helper <canal> <activacionesEsperadas>
// Imprime el rol en la primera linea. La primaria sigue viva hasta recibir
// <activacionesEsperadas> activaciones (tope de 20 s para no colgar ctest) e
// imprime "activaciones=<n>".
int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    if (args.size() != 3) {
        std::fprintf(stderr, "uso: %s <canal> <activacionesEsperadas>\n", argv[0]);
        return 64;
    }
    const int esperadas = args.at(2).toInt();

    satcfdi::SingleInstanceCoordinator coordinador(args.at(1));
    const auto rol = coordinador.adquirir();
    switch (rol) {
    case satcfdi::SingleInstanceCoordinator::Rol::SecondaryActivated:
        std::printf("SecondaryActivated\n");
        return 0;
    case satcfdi::SingleInstanceCoordinator::Rol::Error:
        std::printf("Error\n");
        return 1;
    case satcfdi::SingleInstanceCoordinator::Rol::Primary:
        break;
    }
    std::printf("Primary\n");
    std::fflush(stdout);

    int recibidas = 0;
    QObject::connect(&coordinador, &satcfdi::SingleInstanceCoordinator::activacionSolicitada, &app,
                     [&] {
                         if (++recibidas >= esperadas) {
                             QCoreApplication::exit(0);
                         }
                     });
    coordinador.habilitarEntrega();
    if (esperadas <= 0) {
        return 0;
    }
    QTimer::singleShot(20000, &app, [] { QCoreApplication::exit(2); });
    const int rc = QCoreApplication::exec();
    std::printf("activaciones=%d\n", recibidas);
    return rc;
}
