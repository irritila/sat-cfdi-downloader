#include "OSIntegrationFactory.h"

#if defined(Q_OS_MACOS)
#include "infrastructure/os/macos/MacOSIntegration.h"
#endif

namespace satcfdi {

#if !defined(Q_OS_MACOS)
namespace {

// Fallback minimo fuera de macOS (plataforma no soportada; solo para que el
// arbol compile): sin menu bar, Login Item ni notificaciones.
class OSIntegracionNoDisponible final : public OSIntegration {
public:
    using OSIntegration::OSIntegration;
    void inicializar() override {}
    LaunchContext launchContext() const override { return LaunchContext::Manual; }
    LoginItemStatus loginItemStatus() const override { return LoginItemStatus::Unavailable; }
    NotificationStatus notificationStatus() const override { return NotificationStatus::Unavailable; }
    void configurarLoginItem(bool h) override { emit loginItemConfigurado(h, LoginItemStatus::Unavailable); }
    void solicitarPermisoNotificaciones() override
    {
        emit permisoNotificacionesResuelto(NotificationStatus::Unavailable);
    }
    void enviarNotificacionPrueba(const QString&, const QString&) override
    {
        emit notificacionPruebaTerminada(NotificationSendResult::Unavailable);
    }
    void notificar(const NotificacionLocal& n) override
    {
        emit notificacionTerminada(n.id, NotificationSendResult::Unavailable);
    }
    void mostrarEnFinder(const QString& peticionId, const QString&) override
    {
        emit finderTerminado(peticionId, ResultadoFinder::Fallido);
    }
    void abrirCarpetaEnFinder(const QString& peticionId, const QString&) override
    {
        emit finderTerminado(peticionId, ResultadoFinder::Fallido);
    }
    void reflejarPreferenciaLoginItem(bool) override {}
    void reflejarMonitoreoPausado(bool) override {}
    void reflejarEstadoMonitoreo(const EstadoMonitoreo&) override {}
    void prepararSalida() override {}
};

} // namespace
#endif

// Requisitos de MacOSIntegration: crear despues de QApplication, en el hilo
// grafico, solo en la primaria y antes de cualquier procesamiento de eventos
// (exec, processEvents, dialogos, ventanas o QML); ver main.cpp.
std::unique_ptr<OSIntegration> crearOSIntegracion()
{
#if defined(Q_OS_MACOS)
    return std::make_unique<MacOSIntegration>();
#else
    return std::make_unique<OSIntegracionNoDisponible>();
#endif
}

} // namespace satcfdi
