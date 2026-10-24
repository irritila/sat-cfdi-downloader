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
    // T025.4: despertar del sistema, actividad de sistema y fakes del respaldo.
    void despertarSoloEntreInicializarYSalida();
    void actividadDeSistemaViveConElToken();
    void fakesDePlataformaDelRespaldo();
    // T025.5: relanzamiento tras salir.
    void relanzamientoContratoYFake();
    // T025.6 C4: impresion nativa de texto.
    void impresionContratoYFake();
    // T025.6 H1/M1: copia segura al portapapeles.
    void copiaSecretaContratoYFake();
};
