#pragma once

// Musica de fondo generada localmente (R4, 08-audio-musica.md): pad aditivo
// determinista "pad-fiscal-v1", EQ solo de la musica, calibracion de la cama,
// ducking a partir de los intervalos de voz del montaje y suma en flotante
// sobre la pista de voz ya normalizada (que no se toca). Sin red, sin
// aleatoriedad ni reloj; ffmpeg solo mide loudness.

#include "Guion.h"

#include <QList>
#include <QPair>
#include <QString>

#include <optional>
#include <utility>
#include <vector>

namespace video {

// Version del generador, la EQ y el ducking: entra en la clave del cache.
inline constexpr int kVersionMusica = 1;
// Preset pad-fiscal-v1.
inline constexpr qint64 kMuestrasPorAcorde = 320000; // 72 BPM, 8 pulsos
inline constexpr int kFundidoAcordeMs = 1500;         // medio coseno solapado
inline constexpr int kAnticipacionDuckMs = 50;         // ataque completado antes de la voz
inline constexpr int kHuecoUnionMs = 1500;             // huecos menores se unen
// Verificacion (08-audio-musica.md).
inline constexpr int kVentanaMargenMs = 400;
inline constexpr double kUmbralVozActivaDb = -35.0; // RMS de la voz en la ventana (dBFS)
inline constexpr double kMargenMinimoDb = 20.0;     // voz - musica, RMS
inline constexpr double kMargenBandaDb = 25.0;      // idem, 1-4 kHz
inline constexpr double kFraccionMargen = 0.95;
inline constexpr double kObjetivoMezclaLufs = -16.0;
inline constexpr double kToleranciaMezclaLu = 0.2;

using Intervalos = QList<QPair<qint64, qint64>>; // [inicio, fin) en muestras por canal

// Tramos de voz de la pista planificada (uno por linea, en su posicion).
Intervalos intervalosVoz(const Guion& planificado);
// Ordena y une los intervalos separados por menos de `hueco` muestras.
Intervalos unirIntervalos(Intervalos intervalos, qint64 hueco);

// Pad del preset, mono, `muestras` de largo, con los fundidos globales y ya
// ecualizado (filtros continuos en toda la duracion). Determinista.
// T024.4: con `salidaAnclada` = [a, b) (muestras), el fundido de salida va de
// a a b y despues hay silencio hasta `muestras` (cierre del video); sin el,
// el fundido global de `salidaMs` termina con la pista.
std::vector<float> generarCama(qint64 muestras, const ConfigMusica& config,
                               std::optional<std::pair<qint64, qint64>> salidaAnclada = std::nullopt);
// Ganancia lineal del ducking en la muestra `n` (intervalos ya unidos).
// Tras el ultimo intervalo la atenuacion se mantiene hasta el final.
class Ducking {
public:
    Ducking(const Intervalos& unidos, const ConfigMusica& config);
    double ganancia(qint64 n); // llamadas con n creciente
private:
    double db(qint64 n) const;
    Intervalos m_intervalos;
    qint64 m_ataque;
    qint64 m_anticipacion;
    qint64 m_retencion;
    qint64 m_liberacion;
    double m_duckDb;
    qsizetype m_actual = 0;
};

struct InformeMusica {
    QString clave;
    bool desdeCache = false;
    QString sha256Musica; // PCM f32 de la musica final (cama calibrada y con ducking)
    double lufsCama = 0;  // cama ecualizada antes de calibrar
    double gananciaCamaDb = 0;
    double ajusteDb = 0;  // ajuste de la musica para la mezcla (0 si no hizo falta)
    double lufsVoz = 0;
    double lufsMezcla = 0;
    double picoVerdaderoMezcla = 0; // dBTP de la suma en PCM (antes del AAC)
    qint64 saturadas = 0;
    int ventanasConVoz = 0;
    double fraccionMargen = 0;      // ventanas con voz y margen >= 20 dB
    double fraccionMargenBanda = 0; // idem 1-4 kHz, >= 25 dB
    double margenMedianoDb = 0;
    double margenP5Db = 0;      // percentil 5: >= 20 dB equivale al criterio del 95 %
    int ventanasBanda = 0;      // con voz activa y energia de voz en 1-4 kHz
    double margenBandaP5Db = 0;
    double duckCierreDb = 0; // ganancia maxima de la musica desde la ultima frase hasta el final
};

// Suma la musica a `pistaVoz` (s16le estereo 48 kHz, ya normalizada; no se
// modifica) y escribe `salida` con la misma longitud. Cache en `dirCache`.
QString mezclarMusica(const Guion& planificado, const QString& pistaVoz, const QString& dirCache, const QString& ffmpeg,
                      const QString& dirTrabajo, const QString& salida, InformeMusica& informe);

} // namespace video
