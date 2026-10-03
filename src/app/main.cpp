#include "OSIntegrationFactory.h"
#include "app_core/AppBootstrapper.h"
#include "app_core/ArranqueProceso.h"
#include "app_core/AppCompositionRoot.h"
#include "app_core/SingleInstanceCoordinator.h"

#include <QApplication>
#include <QMessageBox>

namespace {

// Error fatal de arranque: no se carga QML contra una base no lista.
int errorFatal(const QString& mensaje)
{
    qCritical("Error de arranque: %s", qUtf8Printable(mensaje));
    QMessageBox::critical(nullptr, QApplication::applicationName(), mensaje);
    return 2;
}

} // namespace

// Punto de entrada de satcfdi_app (T004). Orden obligatorio:
// 1. QApplication e identidad Qt.  2. Argumentos.  3. Instancia unica (la
// secundaria envia ActivateWindow y termina con 0 sin tocar SQLite).
// 4. Bootstrap SQLite.  5. Composition root, OSIntegration y
// AppLifecycleController.  Cerrar la ventana la oculta; solo Salir termina.
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    satcfdi::configurarIdentidadAplicacion();

    auto opciones = satcfdi::AppBootstrapper::opcionesDesdeArgumentos(QApplication::arguments());
    if (!opciones) {
        return errorFatal(opciones.error().mensaje);
    }

    // Vive todo el proceso: mientras exista, esta es la instancia primaria.
    satcfdi::SingleInstanceCoordinator instancia;
    const satcfdi::AppBootstrapper bootstrapper(std::move(opciones).valor());
    const auto preparacion = satcfdi::prepararProceso(instancia, bootstrapper);
    switch (preparacion.tipo) {
    case satcfdi::PreparacionProceso::Tipo::Lista:
        break;
    case satcfdi::PreparacionProceso::Tipo::Secundaria:
        return preparacion.codigoSalida();
    case satcfdi::PreparacionProceso::Tipo::Error:
        return errorFatal(preparacion.mensajeError);
    }

    // Adaptador de SO: solo en la primaria y ANTES de cualquier procesamiento
    // de eventos (lee el Apple Event de arranque para LaunchContext). Hasta
    // aqui no se proceso ningun evento: adquirir() solo hace listen() (o, en
    // la secundaria, E/S bloqueante de su propio socket) y el bootstrap espera
    // su hilo con wait(). Se declara despues de `app` y antes de `root`: vive
    // mas que el root/controlador y se destruye antes que QApplication.
    const std::unique_ptr<satcfdi::OSIntegration> os = satcfdi::crearOSIntegracion();
    satcfdi::AppCompositionRoot root(preparacion.arranque.rutaBase);
    if (!root.cargar()) {
        return 1;
    }

    QApplication::setQuitOnLastWindowClosed(false);
    root.iniciarCicloDeVida(*os, &instancia);

    return QApplication::exec();
}
