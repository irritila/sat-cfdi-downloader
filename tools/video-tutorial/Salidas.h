#pragma once

// Salidas derivadas del guion planificado (T017 D3, D13): subtitulos .srt
// (un cue por linea con texto, en el cuadro planificado), capitulos para
// ffmpeg (FFMETADATA1, TIMEBASE 1/30) y linea de tiempo JSON (para los
// cortes 3 y 4: voz, mezcla y manifiesto de verificacion).

#include "Guion.h"

#include <QByteArray>

namespace video {

QString tiempoSrt(int cuadro);
QByteArray subtitulosSrt(const Guion& guion);
QByteArray metadatosCapitulos(const Guion& guion);
// D16 (T024.4): "satcfdi-d16 guion=<sha256> voz=<sha256|ninguna>" (etiqueta
// comment del MP4).
QString comentarioHuellas(const Guion& guion);
QByteArray lineaDeTiempoJson(const Guion& guion, bool rapido);

// Archivo a publicar: nombre final en el directorio de salida y su origen
// (ruta a copiar) o su contenido.
struct ArchivoPublicado {
    QString nombre;
    QString origen;     // si no esta vacio, se copia
    QByteArray contenido;
};

// Publica cada archivo con renombre atomico (rename(2) dentro del mismo
// directorio): se prepara en un subdirectorio temporal oculto de `dirSalida`
// y se renombra sobre el destino sin borrarlo antes; un lector ve el archivo
// anterior completo o el nuevo completo. Error o vacio.
QString publicar(const QString& dirSalida, const QList<ArchivoPublicado>& archivos);

} // namespace video
