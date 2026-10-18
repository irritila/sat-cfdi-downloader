#include "SalidaFfmpeg.h"

#include "Guion.h"

#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>

namespace video {

QString SalidaFfmpeg::iniciar(const QString& ffmpeg, QSize tamano, const QString& metadatos, const QString& mp4,
                              const QString& log, bool rapido, const QString& pistaVoz)
{
    m_tamano = tamano;
    m_log = log;
    QStringList args{
        QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-y"),
        // Entrada 0: cuadros RGB24 por stdin.
        QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
        QStringLiteral("-video_size"), QStringLiteral("%1x%2").arg(tamano.width()).arg(tamano.height()),
        QStringLiteral("-framerate"), QString::number(kFps), QStringLiteral("-i"), QStringLiteral("pipe:0"),
        // Entrada 1: capitulos (FFMETADATA1).
        QStringLiteral("-f"), QStringLiteral("ffmetadata"), QStringLiteral("-i"), metadatos};
    if (!pistaVoz.isEmpty()) {
        // Entrada 2: pista de voz ya mezclada y alineada a los cuadros.
        args << QStringLiteral("-f") << QStringLiteral("s16le") << QStringLiteral("-ar") << QStringLiteral("48000")
             << QStringLiteral("-ac") << QStringLiteral("2") << QStringLiteral("-i") << pistaVoz;
    }
    args << QStringList{
        QStringLiteral("-map"), QStringLiteral("0:v:0"), QStringLiteral("-map_metadata"), QStringLiteral("1"),
        QStringLiteral("-map_chapters"), QStringLiteral("1"),
        QStringLiteral("-c:v"), QStringLiteral("libx264"),
        QStringLiteral("-preset"), rapido ? QStringLiteral("ultrafast") : QStringLiteral("medium"),
        QStringLiteral("-crf"), QStringLiteral("18"),
        // Cuantizacion local (sin mb-tree ni AQ): una diferencia minima en una
        // zona (texto de la app) no cambia la codificacion del resto del
        // cuadro; asi el fondo y la transcripcion se reproducen igual (D16).
        QStringLiteral("-x264-params"), QStringLiteral("mbtree=0:aq-mode=0"),
        // Cuadro clave cada segundo: una diferencia minima entre corridas no se
        // propaga mas de 30 cuadros por la prediccion (D16).
        QStringLiteral("-g"), QString::number(kFps), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        QStringLiteral("-r"), QString::number(kFps)};
    if (!pistaVoz.isEmpty()) {
        // D8: AAC-LC estereo 48 kHz 192 kb/s.
        args << QStringList{QStringLiteral("-map"), QStringLiteral("2:a:0"), QStringLiteral("-c:a"), QStringLiteral("aac"),
                            QStringLiteral("-profile:a"), QStringLiteral("aac_low"), QStringLiteral("-b:a"),
                            QStringLiteral("192k"), QStringLiteral("-ar"), QStringLiteral("48000"),
                            QStringLiteral("-ac"), QStringLiteral("2")};
    }
    args << QStringList{
        QStringLiteral("-movflags"), QStringLiteral("+faststart"),
        // Sin metadatos variables (version del codificador, fechas).
        QStringLiteral("-fflags"), QStringLiteral("+bitexact"), QStringLiteral("-flags:v"), QStringLiteral("+bitexact"),
        QStringLiteral("-flags:a"), QStringLiteral("+bitexact"), mp4};
    m_proceso.setProgram(ffmpeg);
    m_proceso.setArguments(args);
    m_proceso.setStandardOutputFile(QProcess::nullDevice());
    m_proceso.setStandardErrorFile(log);
    m_proceso.start(QIODevice::WriteOnly);
    if (!m_proceso.waitForStarted(10000)) {
        return QStringLiteral("no se pudo iniciar ffmpeg (%1): %2").arg(ffmpeg, m_proceso.errorString());
    }
    return {};
}

QString SalidaFfmpeg::escribir(const QImage& cuadro)
{
    if (cuadro.size() != m_tamano || cuadro.format() != QImage::Format_RGB888) {
        return QStringLiteral("cuadro con tamano o formato inesperado");
    }
    const qsizetype bytesFila = qsizetype(m_tamano.width()) * 3;
    for (int y = 0; y < m_tamano.height(); ++y) {
        const char* fila = reinterpret_cast<const char*>(cuadro.constScanLine(y));
        if (m_proceso.write(fila, bytesFila) != bytesFila) {
            return QStringLiteral("ffmpeg dejo de aceptar cuadros (ver ffmpeg.log)");
        }
    }
    while (m_proceso.bytesToWrite() > 0) {
        if (!m_proceso.waitForBytesWritten(60000)) {
            return QStringLiteral("ffmpeg no consumio el cuadro: %1").arg(m_proceso.errorString());
        }
    }
    return {};
}

QString SalidaFfmpeg::terminar(bool abortar)
{
    if (m_proceso.state() == QProcess::NotRunning) {
        return abortar ? QString() : QStringLiteral("ffmpeg no estaba en ejecucion");
    }
    m_proceso.closeWriteChannel();
    if (abortar) {
        m_proceso.kill();
    }
    m_proceso.waitForFinished(600000);
    if (abortar) {
        return {};
    }
    if (m_proceso.exitStatus() != QProcess::NormalExit || m_proceso.exitCode() != 0) {
        QFile log(m_log);
        const QString detalle = log.open(QIODevice::ReadOnly) ? QString::fromUtf8(log.readAll()).trimmed() : QString();
        return QStringLiteral("ffmpeg termino con codigo %1: %2").arg(m_proceso.exitCode()).arg(detalle);
    }
    return {};
}

QString medirAudioDecodificado(const QString& ffmpeg, const QString& mp4, double& lufs, double& picoVerdadero)
{
    QProcess p;
    p.setStandardOutputFile(QProcess::nullDevice());
    p.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-i"), mp4,
                     QStringLiteral("-map"), QStringLiteral("0:a:0"), QStringLiteral("-af"),
                     QStringLiteral("loudnorm=I=-16:TP=-2:LRA=11:print_format=json"), QStringLiteral("-f"),
                     QStringLiteral("null"), QStringLiteral("-")});
    if (!p.waitForStarted(10000) || !p.waitForFinished(600000)) {
        p.kill();
        p.waitForFinished(5000);
        return QStringLiteral("ffmpeg no pudo decodificar el audio del MP4");
    }
    const QString texto = QString::fromUtf8(p.readAllStandardError());
    const qsizetype i = texto.lastIndexOf(QLatin1Char('{'));
    const qsizetype j = i >= 0 ? texto.indexOf(QLatin1Char('}'), i) : -1;
    const QJsonObject m = j > i ? QJsonDocument::fromJson(texto.mid(i, j - i + 1).toUtf8()).object() : QJsonObject();
    bool okI = false;
    bool okTp = false;
    lufs = m.value(QStringLiteral("input_i")).toString().toDouble(&okI);
    picoVerdadero = m.value(QStringLiteral("input_tp")).toString().toDouble(&okTp);
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0 || !okI || !okTp) {
        return QStringLiteral("ffmpeg no pudo medir el audio decodificado del MP4");
    }
    return {};
}

} // namespace video
