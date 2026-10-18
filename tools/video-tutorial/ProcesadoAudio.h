#pragma once

// Procesado local de un bloque de voz (D19; 04-audio-refinamiento.md), con
// ffmpeg/ffprobe del equipo y sin red:
// - Montaje: recorte de los extremos (-50 dBFS; conserva ~65 ms antes y
//   ~150 ms despues; pausas internas intactas), fundidos de 5 ms, estereo
//   48 kHz y medicion de loudness. Sin normalizar: la pista se normaliza
//   completa al grabar (Voz.h).
// - Alineacion por indice de frase (R1c): se cuentan las frases de cada
//   linea (contarFrases) y se detectan las pausas del bloque; si hay al
//   menos frases-1 pausas y las frases-1 mas largas se distinguen del resto
//   (>= 20 % y 40 ms mas largas que la siguiente), esas son las fronteras
//   entre frases y la frontera del cue i es la pausa que cierra su ultima
//   frase ("frases"). Si no se distinguen: cada frontera entre frases va a la
//   pausa mas cercana a su posicion estimada por caracteres
//   ("respaldo-frases"). Si hay menos pausas que frases-1: solo fronteras de
//   linea por su posicion estimada ("respaldo-proporcional"). Se valida orden,
//   cobertura y duracion minima de cue. Las correcciones manuales reemplazan
//   marcas calculadas.

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace video {

struct Herramientas {
    QString ffmpeg;
    QString ffprobe;
};

// Compresion de pausas internas (R1d): cada corte quita `muestras` del audio
// sin comprimir a partir de `inicio` (centro del silencio largo).
struct MapaCompresion {
    QList<QPair<qint64, qint64>> cortes; // (inicio en el audio sin comprimir, muestras quitadas)
    // Posicion en el audio comprimido de la muestra `t` del audio sin comprimir.
    qint64 aplicar(qint64 t) const;
    QJsonArray aJson() const;
    static MapaCompresion desdeJson(const QJsonArray& a);
};

// Aplica el tempo (atempo, conserva el tono; 1.0 = sin cambio) y recorta los
// extremos: eso queda en `pcmSinComprimir` (la alineacion detecta ahi las
// pausas). Si `pausaMaximaMs` > 0, cada silencio interno (-45 dB, el umbral de
// la alineacion) mas largo se acorta a `pausaMaximaMs` cortando en su centro
// con fundidos de 5 ms; el resultado va a `pcm` y `mapa` registra los cortes.
// Sin compresion, `pcm` es una copia de `pcmSinComprimir`. Loudness y
// muestras se miden sobre `pcm`. Rellena `meta`. Error o vacio.
QString prepararBloque(const Herramientas& h, const QString& wav, double tempo, int pausaMaximaMs, const QString& pcm,
                       const QString& pcmSinComprimir, QJsonObject& meta, MapaCompresion& mapa);

struct Alineacion {
    QList<qint64> marcas;               // inicio de cada linea (muestras); marcas[0] = 0
    QList<QPair<qint64, qint64>> pausas; // candidatas detectadas (muestras)
    // "frases" (exacto) | "respaldo-frases" | "respaldo-proporcional" |
    // "una-linea" | "correcciones"
    QString metodo;
    int pausasEsperadas = 0; // frases del bloque - 1
};

// Frases de un texto hablado: corta en . ? ! y en puntos suspensivos ("…" o
// "...", una sola frase) seguidos de espacio o del fin; en ":" solo si sigue
// una mayuscula (o es el final). "¿...?" y "¡...!" cuentan como una frase que
// termina en ? o !. Un punto sin espacio despues ("e.firma", "1.5") no corta.
int contarFrases(const QString& texto);
QList<int> largosFrases(const QString& texto); // caracteres de cada frase

// Alinea las lineas (texto hablado de cada una) sobre el PCM del bloque.
// `correcciones[i]`: inicio corregido de la linea i (muestras), si existe.
// Error (que nombra la linea) o vacio.
// `pausaMinimaMs`: duracion minima de una pausa detectable (por omision 120).
// Las pausas se detectan en `pcmDeteccion` (audio sin comprimir, `muestras`);
// las marcas se pasan al audio montado con `mapa` (`muestrasFinal`). Las
// correcciones ya estan en el audio montado.
QString alinearBloque(const Herramientas& h, const QString& pcmDeteccion, qint64 muestras,
                      const QStringList& textosLineas, const QStringList& idsLineas,
                      const QList<std::optional<qint64>>& correcciones, int pausaMinimaMs, const MapaCompresion& mapa,
                      qint64 muestrasFinal, Alineacion& salida);

// Version de ffmpeg (primera linea de -version), para el manifiesto.
QString versionFfmpeg(const Herramientas& h);

} // namespace video
