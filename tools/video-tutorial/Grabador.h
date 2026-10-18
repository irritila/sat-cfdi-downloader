#pragma once

// Grabacion cuadro a cuadro (T017 D4): reloj de animaciones propio
// (QAnimationDriver), QSG_RENDER_LOOP=basic, cursor simulado y overlays QML,
// grabWindow por cuadro y RGB24 por stdin a ffmpeg (H.264 yuv420p,
// +faststart, capitulos del guion). Sin red: un interceptor de URL de QML
// bloquea todo esquema que no sea local (D15).

#include "DatosDemo.h"
#include "Guion.h"

#include <QSet>
#include <QString>

namespace video {

struct OpcionesGrabacion {
    QString dirSalida;
    QString nombre = QStringLiteral("tutorial");
    QString ffmpeg;
    QString dirTemporal; // QTemporaryDir de la corrida
    QString pistaVoz;    // vacio = sin audio; PCM s16le estereo 48 kHz (Voz.h)
    QSet<QString> escenas; // vacio = todas; si no, "capitulo/escena" que se graban
    QString dirDepuracion; // vacio o subdirectorio de dirSalida (SATCFDI_VIDEO_DEPURAR_CUADROS)
    int depurarDesde = 0;          // SATCFDI_VIDEO_DEPURAR_RANGO=desde-hasta (cuadros)
    int depurarHasta = 1 << 30;
    bool rapido = false;
    bool offline = false;
};

// 0 = MP4, .srt y linea de tiempo escritos en dirSalida; 1 = error (ya
// impreso en stderr, con la escena cuando aplica).
// `salida`: linea de tiempo de los archivos (igual a `guion` o recortada a
// las escenas pedidas, recortarGuion).
int grabar(Guion& guion, const Guion& salida, DemoVideo& demo, const OpcionesGrabacion& opciones);

} // namespace video
