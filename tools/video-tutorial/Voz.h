#pragma once

// Voz del video por bloques (T017 D5, D13, D19; respuestas/03-audio.md y
// 04-audio-refinamiento.md): configuracion TTS del guion, claves de las tres
// capas del cache, lectura de la generacion publicada y mezcla de la pista.
// Solo Qt Core (y ffmpeg local por QProcess para normalizar): lo comparten el
// grabador (sin red, D15) y el paso de voz (satcfdi_video_voz, unico
// ejecutable con cliente HTTP).
//
// Cache (por omision build/video/tts-cache/):
//   actual.json                         puntero unico: {"generacion": "g000007"}
//   generaciones/<id>/manifiesto.json   capas sintesis, montaje y alineacion
//   generaciones/<id>/sintesis/<clave>.wav  audio tal como lo entrego el motor
//   generaciones/<id>/montaje/<clave>.s16   bloque recortado: PCM s16le estereo 48 kHz
//   alineacion/correcciones.json        marcas corregidas a mano (las edita el usuario)
//   .bloqueo                            bloqueo exclusivo del paso de voz
//   .preparando-*/                      staging (se borra al terminar)
// La grabacion solo lee la generacion a la que apunta actual.json.

#include "Guion.h"

#include <QHash>
#include <QString>

namespace video {

inline constexpr int kEsquemaCacheVoz = 2;
// Versiones de las capas derivadas: cambiarlas rehace la capa sin llamar a TTS.
inline constexpr int kVersionMontaje = 3;   // recorte por bloque (D19) + compresion de pausas (R1d)
inline constexpr int kVersionAlineacion = 4; // R1c por frase; R1d: pausas del audio sin comprimir + mapa

struct ConfigVoz {
    QString motor = QStringLiteral("openai"); // openai | prueba
    QString modelo;
    QString voz;
    QString instrucciones;
    QString formato = QStringLiteral("wav");
    double velocidad = 1.0;
    double tempo = 1.0; // montaje local (atempo); no entra en la sintesis
    int pausaMaxima = 0; // montaje local (ms; 0 = sin compresion de pausas)
};

// Configuracion desde `voz` de la raiz del guion; lo que falte toma los
// valores de especialista-audio-voz (04-audio-refinamiento.md).
ConfigVoz configVoz(const Guion& guion, const QString& motor);
// Error si la configuracion no sirve para llamar a OpenAI.
QString validarConfigOpenAi(const ConfigVoz& config);

// Capa de sintesis: SHA-256 del JSON canonico con el texto hablado del
// bloque, modelo, voz, instrucciones, formato y velocidad (y el motor si no
// es openai, para que el audio de prueba nunca se confunda con el real).
QString claveSintesis(const ConfigVoz& config, const QString& textoBloque);
// Entrada de alineacion de un bloque: su sintesis + el texto de cada linea
// (el mismo audio dividido en otras lineas es otra alineacion) + el tempo
// de montaje si no es 1.0 (otro audio montado, otras marcas).
QString claveBloque(const QString& claveSintesis, const QStringList& textosLineas, const ConfigVoz& config);
// Capa de montaje: audio sintetizado + version + tempo (si no es 1.0) +
// pausa maxima (si la hay).
QString claveMontaje(const QString& sha256Audio, const ConfigVoz& config);

// Bloque del guion con ids para el paso de voz y los mensajes.
struct BloqueVoz {
    QString id;       // "capitulo/escena" o "capitulo/escena/<linea>"
    QString escena;   // nombreEscena(...)
    QString capitulo; // id
    QString idEscena; // id corto de la escena
    QStringList idsLineas;   // "capitulo/escena/linea"
    QStringList textosLineas;
    QString texto;    // texto hablado del bloque
};
QString bloquesVoz(const Guion& guion, QList<BloqueVoz>& bloques);

// Audio listo para montar: PCM recortado, su loudness y las marcas.
struct AudioCacheado {
    QString archivoPcm; // absoluto
    qint64 muestras = 0;
    double lufs = -20.0;
    QList<qint64> marcas;
    // Diagnostico de la alineacion (--validar --detalle).
    QString metodo;
    int pausasEncontradas = 0;
    int pausasEsperadas = 0;
    int pausasAcortadas = 0; // montaje (pausaMaxima)
};
// Lee la generacion publicada, indexada por claveBloque (sin actual.json o
// con un cache por linea anterior: vacio, no es error).
QString leerCacheVoz(const QString& dirCache, QHash<QString, AudioCacheado>& bloques, QString* generacion = nullptr);

// Pista de voz completa del guion planificado (PCM s16le estereo 48 kHz,
// totalCuadros * 1600 muestras): cada tramo de linea en su posicion, ajuste
// moderado por bloque (+-3 dB hacia la mediana) y normalizacion de la pista
// completa en dos pasadas (I=-16 LUFS, TP=-2 dBTP) con ffmpeg.
QString escribirMezcla(const Guion& guion, const ConfigVoz& config, const QHash<QString, AudioCacheado>& bloques,
                       const QString& ffmpeg, const QString& dirTrabajo, const QString& ruta);

} // namespace video
