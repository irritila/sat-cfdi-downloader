#pragma once

// Ilustraciones del video (T017 D10, D14): QML de tools/capturas-manual/qml
// alimentado con los textos y dibujos reales (menu bar, icono, notificaciones)
// o con los parametros del guion (Finder, selector de archivos).

#include "DatosDemo.h"
#include "Guion.h"

#include <QUrl>
#include <QVariantMap>

namespace video {

// Archivo QML y propiedades de la ilustracion. `dirTemporal` recibe los PNG
// del icono. Error o vacio.
QString prepararIlustracion(const Accion& accion, const DemoVideo& demo, const QString& dirTemporal, QUrl& archivo,
                            QVariantMap& propiedades);

} // namespace video
