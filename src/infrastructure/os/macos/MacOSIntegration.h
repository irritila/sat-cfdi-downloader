#pragma once

#include "ports/OSIntegration.h"

#include <memory>

namespace satcfdi {

// Adaptador macOS de OSIntegration (T004, ADR 0009, DA4). Header C++ puro:
// AppKit, ServiceManagement y UserNotifications quedan en el .mm (pimpl).
//
// Construccion (requisitos de orden para el composition root/main):
// 1. Despues de construir QApplication (necesita NSApp y QGuiApplication).
// 2. En el hilo grafico, ANTES de cualquier procesamiento de eventos:
//    antes de QApplication::exec(), de processEvents(), de mostrar ventanas o
//    dialogos y de cargar QML. El constructor completa el arranque de AppKit
//    (un ciclo acotado de processEvents que dispara finishLaunching) para leer
//    el Apple Event de apertura; si AppKit ya habia terminado de arrancar, no
//    puede saber el origen y reporta LaunchContext::Manual con un aviso.
// 3. Despues de adquirir la instancia unica: solo la primaria lo construye.
//    Nota: ese ciclo de eventos puede entregar conexiones pendientes al
//    QLocalServer antes de que exista el controlador.
// 4. Conectar senales y luego llamar inicializar() (crea el menu bar).
// 5. Al salir: prepararSalida() antes de QCoreApplication::exit(); destruir
//    el adaptador antes que QApplication.
//
// Comportamiento:
// - LaunchContext::LoginItem -> NSApplicationActivationPolicyAccessory desde
//   el constructor (sin Dock ni ventana). Cuando cualquier ventana de nivel
//   superior (Qt::Window/Qt::Dialog) se muestra, pasa a Regular y activa la
//   app. Sin LSUIElement.
// - kAEReopenApplication (clic en Dock, abrir el bundle con el proceso vivo)
//   -> activarVentanaSolicitada().
// - Cmd-Q / Salir del Dock / menu de la app (QEvent::Quit hacia qApp) ->
//   salirSolicitado(); la terminacion se cancela hasta prepararSalida().
// - Sin bundle (binario suelto) o macOS < 13: Login Item y notificaciones
//   Unavailable, sin crashear.
class MacOSIntegration final : public OSIntegration {
    Q_OBJECT

public:
    explicit MacOSIntegration(QObject* parent = nullptr);
    ~MacOSIntegration() override;

    void inicializar() override;

    LaunchContext launchContext() const override;
    LoginItemStatus loginItemStatus() const override;
    NotificationStatus notificationStatus() const override;

    void configurarLoginItem(bool habilitar) override;
    void solicitarPermisoNotificaciones() override;
    void enviarNotificacionPrueba(const QString& titulo, const QString& cuerpo) override;

    void reflejarPreferenciaLoginItem(bool habilitado) override;
    void reflejarMonitoreoPausado(bool pausado) override;
    void reflejarEstadoMonitoreo(const EstadoMonitoreo& estado) override;

    void prepararSalida() override;

protected:
    bool eventFilter(QObject* objeto, QEvent* evento) override;

private:
    struct Impl;

    void actualizarLoginItemStatus(LoginItemStatus estado);
    void actualizarNotificationStatus(NotificationStatus estado);
    void refrescarEstadosDelSistema();
    void actualizarMenu();
    void asegurarModoRegular();

    std::unique_ptr<Impl> d;
};

} // namespace satcfdi
