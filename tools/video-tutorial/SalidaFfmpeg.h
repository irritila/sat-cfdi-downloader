#pragma once

// Salida de cuadros a ffmpeg (T017 D4, D8): RGB24 por stdin, H.264 yuv420p,
// 30 fps, +faststart, bitexact y capitulos desde un FFMETADATA1.

#include <QProcess>
#include <QSize>
#include <QString>

class QImage;

namespace video {

// D25: pico verdadero maximo del audio AAC decodificado (-2 dBTP + 0.2 dB).
inline constexpr double kPicoVerdaderoMaximoAac = -1.8;

// Loudness integrado y pico verdadero del audio del MP4, decodificado
// (ffmpeg loudnorm en modo analisis). Error o vacio.
QString medirAudioDecodificado(const QString& ffmpeg, const QString& mp4, double& lufs, double& picoVerdadero);

class SalidaFfmpeg {
public:
    // Error o vacio. `log`: stderr de ffmpeg (en el temporal).
    // `pistaVoz`: vacio = sin audio; si no, PCM s16le estereo 48 kHz que se
    // codifica en AAC-LC 192 kb/s (D8).
    QString iniciar(const QString& ffmpeg, QSize tamano, const QString& metadatos, const QString& mp4,
                    const QString& log, bool rapido, const QString& pistaVoz = {});
    // Envia un cuadro RGB888 del tamano pactado; espera a que ffmpeg lo consuma.
    QString escribir(const QImage& cuadro);
    // Cierra stdin y espera. Con `abortar` mata el proceso. Error o vacio.
    QString terminar(bool abortar);

private:
    QProcess m_proceso;
    QSize m_tamano;
    QString m_log;
};

} // namespace video
