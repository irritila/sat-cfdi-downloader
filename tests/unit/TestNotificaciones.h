#pragma once

#include <QObject>

// T009 (unit): ServicioNotificaciones (D1, D9) con un Notificador falso:
// textos sin RFC completo, una notificacion por solicitud y transicion,
// "Descarga completa: N de N", credencial por perfil y permiso denegado sin
// reintentos ni efectos.
class TestNotificaciones : public QObject {
    Q_OBJECT

private slots:
    void textosPorTransicion();
    void dedupePorSolicitudYTransicion();
    void credencialPorPerfil();
    void permisoDenegadoNoReintenta();
};
