#pragma once

#include <QObject>

// T014.4 (unit): estado agregado del icono (D2) como funcion pura,
// clasificacion de atencion, consulta ligera de solicitudes/paquetes con
// fakes de persistencia, MonitorEstadoAgregado (emite solo al cambiar,
// coalesce consultas, conserva el valor ante fallos, se detiene en la salida)
// y destino tipado de las notificaciones (D1).
class TestEstadoAgregado : public QObject {
    Q_OBJECT

private slots:
    void prioridad_data();
    void prioridad();
    void solicitudRequiereAtencion();
    void perfilRequiereAtencion();
    void consultaSolicitudes();
    void consultaSolicitudesFallidaEsNula();
    void monitorEmiteSoloAlCambiar();
    void monitorCoalesceConsultas();
    void monitorConservaValorAnteFallo();
    void monitorDetenidoNoEmite();
    void destinoDeNotificaciones();
};
