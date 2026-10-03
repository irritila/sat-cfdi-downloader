#pragma once

// FakeOSIntegration: OSIntegration determinista para pruebas (T004, DA8).
// Header-only. Reutilizacion: agregar este header a las fuentes del target de
// prueba (AUTOMOC genera su moc) e incluirlo como "fakes/FakeOSIntegration.h"
// con ${PROJECT_SOURCE_DIR}/tests en los include directories (o con ruta
// relativa). Enlazar satcfdi_ports.
//
// - Estado configurable con setters (sin emitir) o con fijar*/cambiar* (que
//   emiten la senal de estado como lo haria el adaptador real).
// - Cada comando se registra en `comandos` ("inicializar",
//   "configurarLoginItem(true)", "reflejarMonitoreoPausado(false)", ...).
// - Los comandos asincronos responden de inmediato (sincrono, en el mismo
//   hilo) con el estado programado, o quedan pendientes si
//   `responderInmediato == false` hasta llamar completar*().
// - emitir*() simula cada intencion del menu bar/SO.
// Sin AppKit, permisos reales, timers ni sleeps.

#include "ports/OSIntegration.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <optional>

namespace fakes {

class FakeOSIntegration final : public satcfdi::OSIntegration {
    Q_OBJECT

public:
    using OSIntegration::OSIntegration;

    // --- Configuracion del estado (no emite). ---
    void setLaunchContext(LaunchContext c) { m_launchContext = c; }
    void setLoginItemStatus(LoginItemStatus s) { m_loginItemStatus = s; }
    void setNotificationStatus(NotificationStatus s) { m_notificationStatus = s; }

    // Estado que tomara el Login Item al configurarLoginItem(habilitar). Por
    // defecto: habilitar -> Enabled, deshabilitar -> Disabled.
    std::optional<LoginItemStatus> resultadoHabilitarLoginItem;
    std::optional<LoginItemStatus> resultadoDeshabilitarLoginItem;
    // Estado que devolvera el SO al solicitar permiso (por defecto Granted si
    // estaba NotDetermined; si no, el estado actual).
    std::optional<NotificationStatus> resultadoPermiso;
    // Resultado forzado de enviarNotificacionPrueba (por defecto, derivado
    // del estado de permiso).
    std::optional<NotificationSendResult> resultadoEnvio;
    // false: los comandos asincronos quedan pendientes hasta completar*().
    bool responderInmediato = true;

    // --- Cambios de estado espontaneos (emiten si cambian). ---
    void cambiarLoginItemStatus(LoginItemStatus s)
    {
        if (m_loginItemStatus != s) {
            m_loginItemStatus = s;
            emit loginItemStatusChanged(s);
        }
    }
    void cambiarNotificationStatus(NotificationStatus s)
    {
        if (m_notificationStatus != s) {
            m_notificationStatus = s;
            emit notificationStatusChanged(s);
        }
    }

    // --- Intenciones simuladas. ---
    void emitirMostrarVentana() { emit mostrarVentanaSolicitada(); }
    void emitirOcultarVentana() { emit ocultarVentanaSolicitada(); }
    void emitirEnfocarVentana() { emit enfocarVentanaSolicitada(); }
    void emitirNuevaSolicitud() { emit nuevaSolicitudSolicitada(); }
    void emitirCambioMonitoreo(bool pausar) { emit cambioMonitoreoSolicitado(pausar); }
    void emitirCambioInicioAutomatico(bool habilitar) { emit cambioInicioAutomaticoSolicitado(habilitar); }
    void emitirPermisoNotificaciones() { emit permisoNotificacionesSolicitado(); }
    void emitirNotificacionPrueba() { emit notificacionPruebaSolicitada(); }
    void emitirSalir() { emit salirSolicitado(); }
    void emitirActivarVentana() { emit activarVentanaSolicitada(); }

    // --- Registro observable. ---
    QStringList comandos;
    QList<QPair<QString, QString>> notificacionesEnviadas; // (titulo, cuerpo) con resultado Sent
    bool inicializado = false;
    bool salidaPreparada = false;
    std::optional<bool> preferenciaReflejada;
    std::optional<bool> monitoreoPausadoReflejado;

    int pendientesLoginItem() const { return int(m_pendientesLoginItem.size()); }
    int pendientesPermiso() const { return m_pendientesPermiso; }
    int pendientesEnvio() const { return int(m_pendientesEnvio.size()); }

    // Completa el comando pendiente mas antiguo con el resultado programado.
    void completarLoginItem()
    {
        if (!m_pendientesLoginItem.isEmpty()) {
            resolverLoginItem(m_pendientesLoginItem.takeFirst());
        }
    }
    void completarPermiso()
    {
        if (m_pendientesPermiso > 0) {
            --m_pendientesPermiso;
            resolverPermiso();
        }
    }
    void completarEnvio()
    {
        if (!m_pendientesEnvio.isEmpty()) {
            const auto p = m_pendientesEnvio.takeFirst();
            resolverEnvio(p.first, p.second);
        }
    }

    // --- OSIntegration. ---
    void inicializar() override
    {
        comandos.append(QStringLiteral("inicializar"));
        inicializado = true;
    }

    LaunchContext launchContext() const override { return m_launchContext; }
    LoginItemStatus loginItemStatus() const override { return m_loginItemStatus; }
    NotificationStatus notificationStatus() const override { return m_notificationStatus; }

    void configurarLoginItem(bool habilitar) override
    {
        comandos.append(QStringLiteral("configurarLoginItem(%1)").arg(texto(habilitar)));
        if (responderInmediato) {
            resolverLoginItem(habilitar);
        } else {
            m_pendientesLoginItem.append(habilitar);
        }
    }

    void solicitarPermisoNotificaciones() override
    {
        comandos.append(QStringLiteral("solicitarPermisoNotificaciones"));
        if (responderInmediato) {
            resolverPermiso();
        } else {
            ++m_pendientesPermiso;
        }
    }

    void enviarNotificacionPrueba(const QString& titulo, const QString& cuerpo) override
    {
        comandos.append(QStringLiteral("enviarNotificacionPrueba"));
        if (responderInmediato) {
            resolverEnvio(titulo, cuerpo);
        } else {
            m_pendientesEnvio.append({titulo, cuerpo});
        }
    }

    void reflejarPreferenciaLoginItem(bool habilitado) override
    {
        comandos.append(QStringLiteral("reflejarPreferenciaLoginItem(%1)").arg(texto(habilitado)));
        preferenciaReflejada = habilitado;
    }

    void reflejarMonitoreoPausado(bool pausado) override
    {
        comandos.append(QStringLiteral("reflejarMonitoreoPausado(%1)").arg(texto(pausado)));
        monitoreoPausadoReflejado = pausado;
    }

    void prepararSalida() override
    {
        comandos.append(QStringLiteral("prepararSalida"));
        salidaPreparada = true;
    }

private:
    static QString texto(bool b) { return b ? QStringLiteral("true") : QStringLiteral("false"); }

    void resolverLoginItem(bool habilitar)
    {
        LoginItemStatus nuevo = habilitar ? resultadoHabilitarLoginItem.value_or(LoginItemStatus::Enabled)
                                          : resultadoDeshabilitarLoginItem.value_or(LoginItemStatus::Disabled);
        if (m_loginItemStatus == LoginItemStatus::Unavailable) {
            nuevo = LoginItemStatus::Unavailable;
        }
        cambiarLoginItemStatus(nuevo);
        emit loginItemConfigurado(habilitar, m_loginItemStatus);
    }

    void resolverPermiso()
    {
        NotificationStatus nuevo = m_notificationStatus;
        if (resultadoPermiso) {
            nuevo = *resultadoPermiso;
        } else if (m_notificationStatus == NotificationStatus::NotDetermined) {
            nuevo = NotificationStatus::Granted;
        }
        cambiarNotificationStatus(nuevo);
        emit permisoNotificacionesResuelto(m_notificationStatus);
    }

    void resolverEnvio(const QString& titulo, const QString& cuerpo)
    {
        NotificationSendResult r = NotificationSendResult::Sent;
        if (resultadoEnvio) {
            r = *resultadoEnvio;
        } else {
            switch (m_notificationStatus) {
            case NotificationStatus::Granted:
                r = NotificationSendResult::Sent;
                break;
            case NotificationStatus::Denied:
                r = NotificationSendResult::PermissionDenied;
                break;
            case NotificationStatus::NotDetermined:
                r = NotificationSendResult::PermissionNotDetermined;
                break;
            case NotificationStatus::Unavailable:
                r = NotificationSendResult::Unavailable;
                break;
            }
        }
        if (r == NotificationSendResult::Sent) {
            notificacionesEnviadas.append({titulo, cuerpo});
        }
        emit notificacionPruebaTerminada(r);
    }

    LaunchContext m_launchContext = LaunchContext::Manual;
    LoginItemStatus m_loginItemStatus = LoginItemStatus::Disabled;
    NotificationStatus m_notificationStatus = NotificationStatus::NotDetermined;
    QList<bool> m_pendientesLoginItem;
    int m_pendientesPermiso = 0;
    QList<QPair<QString, QString>> m_pendientesEnvio;
};

} // namespace fakes
