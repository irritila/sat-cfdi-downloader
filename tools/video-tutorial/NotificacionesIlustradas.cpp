#include "NotificacionesIlustradas.h"

namespace video {

using namespace satcfdi;

// Fuente: notificacionLocalDe, src/app_core/IntegracionWorker.cpp (T014.4 D1).
QList<OSIntegration::AccionNotificacion> accionesExtraNotificacion(const Notificacion& n)
{
    switch (n.destino.tipo) {
    case DestinoNotificacion::Tipo::Ninguno:
        break;
    case DestinoNotificacion::Tipo::Solicitud:
        if (n.tipo == QLatin1String("descarga_completa")) {
            return {OSIntegration::AccionNotificacion::MostrarEnFinder};
        }
        break;
    case DestinoNotificacion::Tipo::Perfil:
        if (n.tipo == QLatin1String("credencial") || n.tipo == QLatin1String("efirma_por_vencer")) {
            return {OSIntegration::AccionNotificacion::AbrirPerfiles};
        }
        break;
    }
    return {};
}

} // namespace video
