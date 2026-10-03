// Pruebas de los mapeos puros de MacOSIntegration (T004, corte C). Sin
// AppKit, permisos, bundle firmado ni sesion grafica.

#include "infrastructure/os/macos/MacOSMapeos.h"

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
};

QTEST_GUILESS_MAIN(TestMacOSMapeos)
#include "TestMacOSMapeos.moc"
