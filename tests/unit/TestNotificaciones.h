#pragma once

#include <QObject>

// T009 (unit): ServicioNotificaciones (D1, D9) con un Notificador falso:
// textos sin RFC completo, una notificacion por solicitud y transicion,
// matriz D4 de T024.1 (una por direccion, "Terminada" con archivos no
// notifica), credencial por perfil y permiso denegado sin reintentos ni efectos.
class TestNotificaciones : public QObject {
    Q_OBJECT

private slots:
    void textosPorTransicion_data();
    void textosPorTransicion();
    void terminadaConArchivosNoNotifica();
    void mesGuiadoProduceDosNotificaciones();
    void dedupePorSolicitudYTransicion();
    void credencialPorPerfil();
    void permisoDenegadoNoReintenta();
};
