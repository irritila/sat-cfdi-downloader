#pragma once

#include <QObject>

// Contrato de OSIntegration (T004) ejercitado con FakeOSIntegration:
// vocabulario de estados, intenciones, comandos asincronos y registro.
class TestOSIntegration : public QObject {
    Q_OBJECT

private slots:
    void enumsDelContrato();
    void intencionesSeEmitenConArgumentos();
    void contextoDeArranque();
    void loginItemHabilitarYDeshabilitar();
    void loginItemEstadosEfectivosDistintosDeLaPreferencia_data();
    void loginItemEstadosEfectivosDistintosDeLaPreferencia();
    void loginItemNoDisponiblePermaneceNoDisponible();
    void permisoNotificaciones_data();
    void permisoNotificaciones();
    void envioNotificacionSegunPermiso_data();
    void envioNotificacionSegunPermiso();
    void comandosPendientesSeCompletanBajoControl();
    void reflejarYPrepararSalidaSoloRegistran();
    void senalesDeEstadoCruzanConexionEncolada();
};
