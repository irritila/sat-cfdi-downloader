#pragma once

namespace satcfdi::presentacion {

// T013 D1: estilo Qt Quick Controls "Basic" en toda la app, personalizado con
// los tokens de Theme.qml. Llamar despues de crear QGuiApplication/QApplication
// y ANTES de cargar cualquier QML que importe QtQuick.Controls (app, herramienta
// de capturas y pruebas de presentacion). El menu bar y FileDialog siguen nativos.
void fijarEstiloBasico();

} // namespace satcfdi::presentacion
