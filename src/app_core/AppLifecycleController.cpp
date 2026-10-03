#include "AppLifecycleController.h"

#include "ExtensionCicloDeVida.h"
#include "application/configuration/ConfiguracionAppService.h"
#include "presentation/viewmodels/AppViewModel.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFuture>
#include <QLoggingCategory>

namespace satcfdi {

namespace {
Q_LOGGING_CATEGORY(lcCiclo, "satcfdi.ciclovida")
} // namespace

AppLifecycleController::AppLifecycleController(OSIntegration& os,
                                               ConfiguracionAppService& configuracion,
                                               AppViewModel& appViewModel, QWindow* ventana,
                                               QObject* parent)
    : QObject(parent)
    , m_os(os)
    , m_configuracion(configuracion)
    , m_appViewModel(appViewModel)
    , m_ventana(ventana)
    , m_salida([] { QCoreApplication::exit(0); })
    , m_loginItemStatus(os.loginItemStatus())
    , m_notificationStatus(os.notificationStatus())
{
}

AppLifecycleController::~AppLifecycleController()
{
    if (m_ventana) {
        m_ventana->removeEventFilter(this);
    }
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->removeEventFilter(this);
    }
}

void AppLifecycleController::setSalida(std::function<void()> salida)
{
    m_salida = std::move(salida);
}

void AppLifecycleController::iniciar()
{
    Q_ASSERT(!m_iniciado);
    m_iniciado = true;

    if (m_ventana) {
        m_ventana->installEventFilter(this);
    }
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }

    connect(&m_os, &OSIntegration::mostrarVentanaSolicitada, this,
            &AppLifecycleController::mostrarVentana);
    connect(&m_os, &OSIntegration::activarVentanaSolicitada, this,
            &AppLifecycleController::mostrarVentana);
    connect(&m_os, &OSIntegration::enfocarVentanaSolicitada, this,
            &AppLifecycleController::enfocarVentana);
    connect(&m_os, &OSIntegration::ocultarVentanaSolicitada, this,
            &AppLifecycleController::ocultarVentana);
    connect(&m_os, &OSIntegration::nuevaSolicitudSolicitada, this,
            &AppLifecycleController::nuevaSolicitud);
    connect(&m_os, &OSIntegration::cambioMonitoreoSolicitado, this,
            &AppLifecycleController::cambiarMonitoreo);
    connect(&m_os, &OSIntegration::cambioInicioAutomaticoSolicitado, this,
            &AppLifecycleController::cambiarInicioAutomatico);
    connect(&m_os, &OSIntegration::permisoNotificacionesSolicitado, this,
            [this] { m_os.solicitarPermisoNotificaciones(); });
    connect(&m_os, &OSIntegration::notificacionPruebaSolicitada, this, [this] {
        m_os.enviarNotificacionPrueba(QStringLiteral("SAT CFDI Downloader"),
                                      QStringLiteral("Notificacion de prueba."));
    });
    connect(&m_os, &OSIntegration::salirSolicitado, this,
            &AppLifecycleController::solicitarSalida);

    connect(&m_os, &OSIntegration::loginItemStatusChanged, this,
            [this](OSIntegration::LoginItemStatus estado) {
                m_loginItemStatus = estado;
                emit loginItemStatusChanged(estado);
            });
    connect(&m_os, &OSIntegration::notificationStatusChanged, this,
            [this](OSIntegration::NotificationStatus estado) {
                m_notificationStatus = estado;
                emit notificationStatusChanged(estado);
            });
    connect(&m_os, &OSIntegration::notificacionPruebaTerminada, this,
            &AppLifecycleController::notificacionPruebaTerminada);

    m_os.inicializar();

    // Estado persistido -> menu bar y extension.
    m_configuracion.obtener().then(this, [this](const ConfiguracionAppService::ResultadoConfiguracion& r) {
        if (!r) {
            qCWarning(lcCiclo, "No se pudo leer la configuracion local: %s",
                      qUtf8Printable(r.error().mensaje));
            emit operacionFallida(QStringLiteral("lectura"), r.error().mensaje);
            return;
        }
        reflejar(r.valor());
    });

    if (m_os.launchContext() == OSIntegration::LaunchContext::Manual) {
        mostrarVentana();
    }
}

void AppLifecycleController::mostrarVentana()
{
    if (!m_ventana || m_saliendo) {
        return;
    }
    m_ventana->show();
    enfocarVentana();
}

void AppLifecycleController::ocultarVentana()
{
    if (m_ventana) {
        m_ventana->hide();
    }
}

void AppLifecycleController::enfocarVentana()
{
    if (!m_ventana || m_saliendo) {
        return;
    }
    if (!m_ventana->isVisible()) {
        m_ventana->show();
    }
    m_ventana->raise();
    m_ventana->requestActivate();
}

void AppLifecycleController::nuevaSolicitud()
{
    if (m_saliendo) {
        return;
    }
    mostrarVentana();
    m_appViewModel.mostrarNueva();
}

void AppLifecycleController::cambiarMonitoreo(bool pausar)
{
    if (m_saliendo) {
        return;
    }
    m_configuracion.actualizarMonitoreoPausado(pausar).then(
        this, [this](const ConfiguracionAppService::ResultadoConfiguracion& r) {
            if (!r) {
                emit operacionFallida(QStringLiteral("monitoreo"), r.error().mensaje);
                if (m_confirmada) {
                    m_os.reflejarMonitoreoPausado(m_confirmada->monitoreoPausado);
                }
                return;
            }
            m_confirmada = r.valor();
            m_os.reflejarMonitoreoPausado(r.valor().monitoreoPausado);
            if (m_extension) {
                m_extension->aplicarMonitoreoPausado(r.valor().monitoreoPausado);
            }
        });
}

void AppLifecycleController::cambiarInicioAutomatico(bool habilitar)
{
    if (m_saliendo) {
        return;
    }
    m_configuracion.actualizarInicioAutomatico(habilitar).then(
        this, [this](const ConfiguracionAppService::ResultadoConfiguracion& r) {
            if (!r) {
                emit operacionFallida(QStringLiteral("inicioAutomatico"), r.error().mensaje);
                if (m_confirmada) {
                    m_os.reflejarPreferenciaLoginItem(m_confirmada->inicioAutomaticoHabilitado);
                }
                return;
            }
            m_confirmada = r.valor();
            const bool habilitado = r.valor().inicioAutomaticoHabilitado;
            m_os.configurarLoginItem(habilitado);
            m_os.reflejarPreferenciaLoginItem(habilitado);
        });
}

void AppLifecycleController::reflejar(const ConfiguracionApp& configuracion)
{
    m_confirmada = configuracion;
    m_os.reflejarPreferenciaLoginItem(configuracion.inicioAutomaticoHabilitado);
    m_os.reflejarMonitoreoPausado(configuracion.monitoreoPausado);
    if (m_extension) {
        m_extension->aplicarMonitoreoPausado(configuracion.monitoreoPausado);
    }
    emit configuracionReflejada(configuracion);
}

void AppLifecycleController::solicitarSalida()
{
    if (m_saliendo) {
        return;
    }
    m_saliendo = true;
    emit salidaIniciada();
    if (m_extension) {
        m_extension->detener();
    }
    m_configuracion.registrarUltimoCierre().then(
        this, [this](const ConfiguracionAppService::ResultadoConfiguracion& r) {
            if (!r) {
                // No se bloquea la salida: se registra y se continua.
                qCWarning(lcCiclo, "No se pudo registrar el ultimo cierre: %s",
                          qUtf8Printable(r.error().mensaje));
                emit operacionFallida(QStringLiteral("ultimoCierre"), r.error().mensaje);
            } else {
                m_confirmada = r.valor();
            }
            terminarSalida();
        });
}

void AppLifecycleController::terminarSalida()
{
    m_os.prepararSalida();
    if (m_salida) {
        m_salida();
    }
}

bool AppLifecycleController::eventFilter(QObject* objeto, QEvent* evento)
{
    if (!m_saliendo) {
        if (evento->type() == QEvent::Close && objeto == m_ventana.data()) {
            evento->ignore();
            ocultarVentana();
            return true;
        }
        if (evento->type() == QEvent::Quit && objeto == QCoreApplication::instance()) {
            // ignore(): cancela la terminacion de AppKit (applicationShouldTerminate)
            // hasta que el flujo de salida confirmado llame exit(0).
            evento->ignore();
            solicitarSalida();
            return true;
        }
    }
    return QObject::eventFilter(objeto, evento);
}

} // namespace satcfdi
