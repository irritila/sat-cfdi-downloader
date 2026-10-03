#pragma once

#include "domain/configuracion/ConfiguracionApp.h"
#include "ports/OSIntegration.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QWindow>

#include <functional>
#include <optional>

namespace satcfdi {

class AppViewModel;
class ConfiguracionAppService;
class ExtensionCicloDeVida;

// Orquestador de runtime de T004 (DA2). Conecta intenciones de OSIntegration
// (menu bar, Dock, segunda instancia) con la ventana, la navegacion
// (AppViewModel) y ConfiguracionAppService. Refleja en el adaptador solo
// estado CONFIRMADO en SQLite. No toca SQL ni solicitudes.
//
// - Close de la ventana: se consume y la ventana se oculta (el proceso, el
//   menu bar y el worker siguen). Requiere setQuitOnLastWindowClosed(false).
// - QEvent::Quit de la aplicacion (Cmd-Q, Salir del Dock) se redirige al
//   mismo flujo de salida explicita.
// - Salir: extension.detener() -> registrarUltimoCierre() confirmado (o
//   fallido, registrado en log) -> os.prepararSalida() -> salida().
//
// Ownership/hilo: hilo grafico. No posee nada; os, configuracion y
// appViewModel deben vivir mas que el controlador. La ventana es no
// propietaria (QPointer).
class AppLifecycleController final : public QObject {
    Q_OBJECT

public:
    AppLifecycleController(OSIntegration& os, ConfiguracionAppService& configuracion,
                           AppViewModel& appViewModel, QWindow* ventana,
                           QObject* parent = nullptr);
    ~AppLifecycleController() override;

    // Accion final de la salida. Por defecto QCoreApplication::exit(0).
    void setSalida(std::function<void()> salida);
    // Opcional (T007).
    void setExtension(ExtensionCicloDeVida* extension) { m_extension = extension; }

    // Conecta senales, inicializa el adaptador, lee la configuracion
    // persistida para reflejarla y, en arranque Manual, muestra y enfoca la
    // ventana. En arranque por LoginItem no la muestra. Una sola vez.
    void iniciar();

    bool saliendo() const noexcept { return m_saliendo; }
    // Ultima configuracion confirmada (vacio hasta la lectura inicial).
    const std::optional<ConfiguracionApp>& configuracionConfirmada() const noexcept
    {
        return m_confirmada;
    }
    OSIntegration::LoginItemStatus loginItemStatus() const noexcept { return m_loginItemStatus; }
    OSIntegration::NotificationStatus notificationStatus() const noexcept
    {
        return m_notificationStatus;
    }

public slots:
    void mostrarVentana();
    void ocultarVentana();
    void enfocarVentana();
    void nuevaSolicitud();
    void solicitarSalida();

signals:
    // Una escritura de configuracion fallo; el adaptador conserva el ultimo
    // estado confirmado. `operacion`: "monitoreo", "inicioAutomatico",
    // "ultimoCierre" o "lectura".
    void operacionFallida(const QString& operacion, const QString& mensaje);
    void configuracionReflejada(const satcfdi::ConfiguracionApp& configuracion);
    void loginItemStatusChanged(satcfdi::OSIntegration::LoginItemStatus estado);
    void notificationStatusChanged(satcfdi::OSIntegration::NotificationStatus estado);
    void notificacionPruebaTerminada(satcfdi::OSIntegration::NotificationSendResult resultado);
    void salidaIniciada();

protected:
    bool eventFilter(QObject* objeto, QEvent* evento) override;

private:
    void cambiarMonitoreo(bool pausar);
    void cambiarInicioAutomatico(bool habilitar);
    void reflejar(const ConfiguracionApp& configuracion);
    void terminarSalida();

    OSIntegration& m_os;
    ConfiguracionAppService& m_configuracion;
    AppViewModel& m_appViewModel;
    QPointer<QWindow> m_ventana;
    ExtensionCicloDeVida* m_extension = nullptr;
    std::function<void()> m_salida;

    std::optional<ConfiguracionApp> m_confirmada;
    OSIntegration::LoginItemStatus m_loginItemStatus;
    OSIntegration::NotificationStatus m_notificationStatus;
    bool m_iniciado = false;
    bool m_saliendo = false;
};

} // namespace satcfdi
