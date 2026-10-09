// Pruebas de los mapeos puros de MacOSIntegration (T004, corte C). Sin
// AppKit, permisos, bundle firmado ni sesion grafica.

#include "infrastructure/os/macos/MacOSMapeos.h"
#include "infrastructure/os/MenuBarDefinicion.h"

#include <QTest>

using namespace satcfdi::macos;
using LoginItemStatus = satcfdi::OSIntegration::LoginItemStatus;
using NotificationStatus = satcfdi::OSIntegration::NotificationStatus;
using NotificationSendResult = satcfdi::OSIntegration::NotificationSendResult;
using LaunchContext = satcfdi::OSIntegration::LaunchContext;

class TestMacOSMapeos : public QObject {
    Q_OBJECT

private slots:
    void estadoLoginItem()
    {
        QCOMPARE(mapearEstadoLoginItem(kSmStatusNotRegistered), LoginItemStatus::Disabled);
        QCOMPARE(mapearEstadoLoginItem(kSmStatusNotFound), LoginItemStatus::Disabled);
        QCOMPARE(mapearEstadoLoginItem(kSmStatusEnabled), LoginItemStatus::Enabled);
        QCOMPARE(mapearEstadoLoginItem(kSmStatusRequiresApproval), LoginItemStatus::RequiresApproval);
        QCOMPARE(mapearEstadoLoginItem(42), LoginItemStatus::Unavailable);
        QCOMPARE(mapearEstadoLoginItem(-1), LoginItemStatus::Unavailable);
    }

    void resultadoLoginItem()
    {
        // Exito o ya en el estado pedido: manda el status leido.
        QCOMPARE(mapearResultadoLoginItem(true, ErrorLoginItem::Ninguno, kSmStatusEnabled), LoginItemStatus::Enabled);
        QCOMPARE(mapearResultadoLoginItem(true, ErrorLoginItem::Ninguno, kSmStatusRequiresApproval),
                 LoginItemStatus::RequiresApproval);
        QCOMPARE(mapearResultadoLoginItem(false, ErrorLoginItem::Ninguno, kSmStatusNotRegistered),
                 LoginItemStatus::Disabled);
        QCOMPARE(mapearResultadoLoginItem(true, ErrorLoginItem::YaEnEstadoSolicitado, kSmStatusEnabled),
                 LoginItemStatus::Enabled);
        QCOMPARE(mapearResultadoLoginItem(false, ErrorLoginItem::YaEnEstadoSolicitado, kSmStatusNotFound),
                 LoginItemStatus::Disabled);
        // Rechazo y no disponible ganan sobre el status.
        QCOMPARE(mapearResultadoLoginItem(true, ErrorLoginItem::RechazadoPorUsuario, kSmStatusRequiresApproval),
                 LoginItemStatus::Rejected);
        QCOMPARE(mapearResultadoLoginItem(true, ErrorLoginItem::NoDisponible, kSmStatusNotFound),
                 LoginItemStatus::Unavailable);
        // Otro error al habilitar sin quedar habilitado -> Rejected.
        QCOMPARE(mapearResultadoLoginItem(true, ErrorLoginItem::Otro, kSmStatusNotRegistered),
                 LoginItemStatus::Rejected);
        QCOMPARE(mapearResultadoLoginItem(true, ErrorLoginItem::Otro, kSmStatusRequiresApproval),
                 LoginItemStatus::RequiresApproval);
        // Otro error al deshabilitar: el status leido.
        QCOMPARE(mapearResultadoLoginItem(false, ErrorLoginItem::Otro, kSmStatusEnabled), LoginItemStatus::Enabled);
    }

    void autorizacionNotificaciones()
    {
        QCOMPARE(mapearAutorizacionNotificaciones(kUnAuthNotDetermined), NotificationStatus::NotDetermined);
        QCOMPARE(mapearAutorizacionNotificaciones(kUnAuthDenied), NotificationStatus::Denied);
        QCOMPARE(mapearAutorizacionNotificaciones(kUnAuthAuthorized), NotificationStatus::Granted);
        QCOMPARE(mapearAutorizacionNotificaciones(kUnAuthProvisional), NotificationStatus::Granted);
        QCOMPARE(mapearAutorizacionNotificaciones(kUnAuthEphemeral), NotificationStatus::Granted);
        QCOMPARE(mapearAutorizacionNotificaciones(99), NotificationStatus::Unavailable);
    }

    void resultadoEnvio()
    {
        QCOMPARE(resultadoEnvioSegunEstado(NotificationStatus::Granted), NotificationSendResult::Sent);
        QCOMPARE(resultadoEnvioSegunEstado(NotificationStatus::Denied), NotificationSendResult::PermissionDenied);
        QCOMPARE(resultadoEnvioSegunEstado(NotificationStatus::NotDetermined),
                 NotificationSendResult::PermissionNotDetermined);
        QCOMPARE(resultadoEnvioSegunEstado(NotificationStatus::Unavailable), NotificationSendResult::Unavailable);
    }

    void contextoDeArranque()
    {
        QCOMPARE(launchContextDesdeEventoApertura(true, kAeClaseCore, kAeAbrirAplicacion, kAeLanzadoComoLoginItem),
                 LaunchContext::LoginItem);
        QCOMPARE(launchContextDesdeEventoApertura(true, kAeClaseCore, kAeAbrirAplicacion, 0), LaunchContext::Manual);
        QCOMPARE(launchContextDesdeEventoApertura(false, kAeClaseCore, kAeAbrirAplicacion, kAeLanzadoComoLoginItem),
                 LaunchContext::Manual);
        // 'odoc' (abrir documentos) o 'lgsi' (service item) no son Login Item.
        QCOMPARE(launchContextDesdeEventoApertura(true, kAeClaseCore, 0x6F646F63, kAeLanzadoComoLoginItem),
                 LaunchContext::Manual);
        QCOMPARE(launchContextDesdeEventoApertura(true, kAeClaseCore, kAeAbrirAplicacion, 0x6C677369),
                 LaunchContext::Manual);
    }

    void textosDistinguenEstados()
    {
        QStringList textos;
        for (auto e : {LoginItemStatus::Disabled, LoginItemStatus::Enabled, LoginItemStatus::RequiresApproval,
                       LoginItemStatus::Rejected, LoginItemStatus::Unavailable}) {
            textos << textoEstadoLoginItem(e);
        }
        textos.removeDuplicates();
        QCOMPARE(textos.size(), 5);

        QStringList notif;
        for (auto e : {NotificationStatus::NotDetermined, NotificationStatus::Granted, NotificationStatus::Denied,
                       NotificationStatus::Unavailable}) {
            notif << textoEstadoNotificaciones(e);
        }
        notif.removeDuplicates();
        QCOMPARE(notif.size(), 4);

        QVERIFY(textoAccionMonitoreo(false) != textoAccionMonitoreo(true));
    }

    // T012 D6: definicion compartida del menu (orden, separadores, textos y
    // reglas de visibilidad que antes vivian en MacOSIntegration.mm).
    void definicionMenuBar()
    {
        using namespace satcfdi::menubar;
        EstadoMenu estado;
        estado.monitoreo.fase = satcfdi::OSIntegration::EstadoMonitoreo::Fase::ActivoEnEspera;
        QStringList visibles;
        for (const Entrada& e : entradas(estado)) {
            if (e.visible) {
                visibles.append(e.separador ? QStringLiteral("---") : e.texto);
            }
        }
        QCOMPARE(visibles, (QStringList{
                               QStringLiteral("Mostrar ventana"), QStringLiteral("Nueva solicitud…"),
                               QStringLiteral("Abrir carpeta de paquetes"), QStringLiteral("---"),
                               QStringLiteral("Monitoreo activo"), QStringLiteral("Pausar monitoreo"),
                               QStringLiteral("---"), QStringLiteral("Abrir al iniciar sesión"),
                               textoEstadoLoginItem(satcfdi::OSIntegration::LoginItemStatus::Disabled),
                               QStringLiteral("---"),
                               textoEstadoNotificaciones(satcfdi::OSIntegration::NotificationStatus::NotDetermined),
                               QStringLiteral("Solicitar permiso de notificaciones"), QStringLiteral("---"),
                               QStringLiteral("Salir de SAT CFDI Downloader")}));
        QVERIFY(!entrada(Id::EstadoMonitoreo, estado).habilitada);
        estado.monitoreo.pendientes = 2;
        estado.monitoreoPausado = true;
        estado.preferenciaLoginItem = true;
        estado.loginItem = satcfdi::OSIntegration::LoginItemStatus::RequiresApproval;
        estado.notificaciones = satcfdi::OSIntegration::NotificationStatus::Denied;
        QCOMPARE(entrada(Id::PendientesMonitoreo, estado).texto, QStringLiteral("Pendientes: 2"));
        QVERIFY(entrada(Id::PendientesMonitoreo, estado).visible);
        QCOMPARE(entrada(Id::AccionMonitoreo, estado).texto, QStringLiteral("Reanudar monitoreo"));
        QVERIFY(entrada(Id::InicioAutomatico, estado).marcada);
        QVERIFY(entrada(Id::AbrirAjustesLoginItem, estado).visible);
        QVERIFY(!entrada(Id::SolicitarPermiso, estado).visible);
        QVERIFY(entrada(Id::EnviarPrueba, estado).visible);
        QVERIFY(entrada(Id::AbrirAjustesNotificaciones, estado).visible);
        // Textos que abren ventana o dialogo llevan "…" (UX-37).
        QCOMPARE(entrada(Id::AbrirAjustesLoginItem, estado).texto, QStringLiteral("Abrir ajustes de inicio de sesión…"));
        QCOMPARE(entrada(Id::EnviarPrueba, estado).texto, QStringLiteral("Enviar notificación de prueba"));
        QCOMPARE(entrada(Id::AbrirAjustesNotificaciones, estado).texto,
                 QStringLiteral("Abrir ajustes de notificaciones…"));
        QCOMPARE(entrada(Id::EstadoLoginItem, estado).texto, QStringLiteral("Estado en macOS: pendiente de aprobación"));
    }

    // T013 D6/UX-37: ningun texto del menu usa "..." ASCII ni palabras sin
    // acento conocidas, en ningun estado.
    void ortografiaMenuBar()
    {
        using namespace satcfdi::menubar;
        using E = satcfdi::OSIntegration::EstadoMonitoreo;
        using L = satcfdi::OSIntegration::LoginItemStatus;
        using N = satcfdi::OSIntegration::NotificationStatus;
        const QStringList prohibidas{QStringLiteral("..."), QStringLiteral("sesion"), QStringLiteral("notificacion "),
                                     QStringLiteral("aprobacion"), QStringLiteral("pausado")};
        for (E::Fase f : {E::Fase::Pausado, E::Fase::ActivoEnEspera, E::Fase::Ejecutando, E::Fase::Deteniendo,
                          E::Fase::Detenido}) {
            for (E::Actividad a : {E::Actividad::Ninguna, E::Actividad::Enviando, E::Actividad::Verificando,
                                   E::Actividad::Descargando, E::Actividad::Otra}) {
                for (L l : {L::Disabled, L::Enabled, L::RequiresApproval, L::Rejected, L::Unavailable}) {
                    for (N n : {N::NotDetermined, N::Granted, N::Denied, N::Unavailable}) {
                        EstadoMenu s;
                        s.monitoreo.fase = f;
                        s.monitoreo.actividad = a;
                        s.monitoreo.pendientes = 1;
                        s.loginItem = l;
                        s.notificaciones = n;
                        for (const Entrada& e : entradas(s)) {
                            for (const QString& p : prohibidas) {
                                QVERIFY2(!(e.texto + QLatin1Char(' ')).contains(p), qPrintable(e.texto));
                            }
                        }
                    }
                }
            }
        }
    }

    // UX-38: iconos de las lineas informativas y existencia de los SVG.
    void iconosMenuBar()
    {
        using namespace satcfdi::menubar;
        using F = satcfdi::OSIntegration::EstadoMonitoreo::Fase;
        EstadoMenu s;
        s.monitoreo.pendientes = 3;
        s.monitoreo.fase = F::ActivoEnEspera;
        QCOMPARE(entrada(Id::EstadoMonitoreo, s).icono, QStringLiteral("check-circle"));
        s.monitoreo.fase = F::Pausado;
        QCOMPARE(entrada(Id::EstadoMonitoreo, s).icono, QStringLiteral("clock"));
        s.monitoreo.fase = F::Ejecutando;
        QCOMPARE(entrada(Id::EstadoMonitoreo, s).icono, QStringLiteral("arrow-down-circle-dotted"));
        s.monitoreo.fase = F::Detenido;
        QVERIFY(entrada(Id::EstadoMonitoreo, s).icono.isEmpty());
        QCOMPARE(entrada(Id::PendientesMonitoreo, s).icono, QStringLiteral("clock"));
        // Las acciones no llevan icono.
        for (const Entrada& e : entradas(s)) {
            if (e.habilitada && !e.separador) {
                QVERIFY2(e.icono.isEmpty(), qPrintable(e.texto));
            }
        }
        for (const char* n : {"check-circle", "clock", "arrow-down-circle-dotted"}) {
            const QString ruta = QFINDTESTDATA(QStringLiteral("../../../../presentation/qml/assets/icons/%1.svg")
                                                   .arg(QLatin1String(n)));
            QVERIFY2(!ruta.isEmpty(), n);
        }
    }

    // T007 D2: textos de la linea de estado del worker.
    void textosEstadoMonitoreo()
    {
        using E = satcfdi::OSIntegration::EstadoMonitoreo;
        auto texto = [](E::Fase f, E::Actividad a = E::Actividad::Ninguna) {
            E e;
            e.fase = f;
            e.actividad = a;
            return textoEstadoMonitoreo(e);
        };
        QCOMPARE(texto(E::Fase::ActivoEnEspera), QStringLiteral("Monitoreo activo"));
        QCOMPARE(texto(E::Fase::Pausado), QStringLiteral("Monitoreo en pausa"));
        QCOMPARE(texto(E::Fase::Ejecutando, E::Actividad::Enviando), QStringLiteral("Trabajando: enviando…"));
        QCOMPARE(texto(E::Fase::Ejecutando, E::Actividad::Verificando), QStringLiteral("Trabajando: verificando…"));
        QCOMPARE(texto(E::Fase::Ejecutando, E::Actividad::Descargando), QStringLiteral("Trabajando: descargando…"));
        QCOMPARE(texto(E::Fase::Ejecutando, E::Actividad::Otra), QStringLiteral("Trabajando…"));
        QVERIFY(!texto(E::Fase::Deteniendo).isEmpty());
        QVERIFY(!texto(E::Fase::Detenido).isEmpty());
        QCOMPARE(textoPendientes(0), QString());
        QCOMPARE(textoPendientes(3), QStringLiteral("Pendientes: 3"));
    }
};

QTEST_GUILESS_MAIN(TestMacOSMapeos)
#include "TestMacOSMapeos.moc"
