#pragma once

// Acciones extra de una notificacion local (T014.4 D1) para la ilustracion de
// notificaciones del video (T017 D10). COPIA de la regla de
// notificacionLocalDe (src/app_core/IntegracionWorker.cpp): la herramienta no
// puede enlazar satcfdi_app_core (arrastra satcfdi_sat) y T017 no cambia el
// binario de satcfdi_app. La prueba satcfdi_video_sync_notificaciones falla si
// el original cambia; entonces hay que revisar esta copia y actualizar la
// huella en tools/video-tutorial/CMakeLists.txt.

#include "application/notificaciones/Notificador.h"
#include "ports/OSIntegration.h"

#include <QList>

namespace video {

QList<satcfdi::OSIntegration::AccionNotificacion> accionesExtraNotificacion(const satcfdi::Notificacion& n);

} // namespace video
