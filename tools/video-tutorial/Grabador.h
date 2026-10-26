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
    QString rutaDiagnosticoClic; // T022: SATCFDI_VIDEO_DIAGNOSTICO_CLIC=1 -> <salida>/<nombre>.diag.log
    // T022: solo diagnostico (SATCFDI_VIDEO_DIAGNOSTICO_DESACTIVAR=N): tras el
    // press del N-esimo clic con efecto, la ventana pierde el foco de Qt como
    // cuando otra app toma el foco de macOS. 0 = nunca.
    int desactivarEnClic = 0;
    // T022: solo pruebas (SATCFDI_VIDEO_DIAGNOSTICO_PERMITIR_DESACTIVAR=1):
    // no descartar WindowDeactivate, para probar la deteccion de agarre perdido.
    bool permitirDesactivacion = false;
    bool rapido = false;
    bool offline = false;
};

// 0 = MP4, .srt y linea de tiempo escritos en dirSalida; 1 = error (ya
// impreso en stderr, con la escena cuando aplica).
// `salida`: linea de tiempo de los archivos (igual a `guion` o recortada a
// las escenas pedidas, recortarGuion).
int grabar(Guion& guion, const Guion& salida, DemoVideo& demo, const OpcionesGrabacion& opciones);

} // namespace video
