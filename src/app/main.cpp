#include "app_core/AppBootstrapper.h"
#include "app_core/AppCompositionRoot.h"

#include <QApplication>
#include <QMessageBox>

namespace {

// Error fatal de arranque: no se carga QML contra una base no lista.
int errorFatal(const satcfdi::ErrorArranque& error)
{
    qCritical("Error de arranque: %s", qUtf8Printable(error.mensaje));
    QMessageBox::critical(nullptr, QApplication::applicationName(), error.mensaje);
    return 2;
}

} // namespace

// Punto de entrada de satcfdi_app. QApplication porque T004 agregara el icono
// de menu bar; cerrar la ultima ventana termina el proceso (valor por defecto
// de Qt) y no hay menu bar, instancia unica ni modo agente.
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    // Antes de resolver la ruta de datos (QStandardPaths::AppDataLocation).
    satcfdi::configurarIdentidadAplicacion();

    auto opciones = satcfdi::AppBootstrapper::opcionesDesdeArgumentos(QApplication::arguments());
    if (!opciones) {
        return errorFatal(opciones.error());
    }

    const satcfdi::AppBootstrapper bootstrapper(std::move(opciones).valor());
    // Inicializa SQLite en un hilo temporal y lo une antes de crear el grafo.
    const auto arranque = bootstrapper.preparar();
    if (!arranque) {
        return errorFatal(arranque.error());
    }

    satcfdi::AppCompositionRoot root(arranque.valor().rutaBase);
    if (!root.cargar()) {
        return 1;
    }

    return QApplication::exec();
}
