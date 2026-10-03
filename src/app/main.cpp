#include "AppCompositionRoot.h"

#include <QApplication>

// Punto de entrada de satcfdi_app (T002). QApplication porque T004 agregara el
// icono de menu bar; en T002 cerrar la ultima ventana termina el proceso (valor
// por defecto de Qt) y no hay menu bar, instancia unica ni modo agente.
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Adenium"));
    QApplication::setApplicationName(QStringLiteral("SAT CFDI Downloader"));

    satcfdi::AppCompositionRoot root;
    if (!root.cargar()) {
        return 1;
    }

    return QApplication::exec();
}
