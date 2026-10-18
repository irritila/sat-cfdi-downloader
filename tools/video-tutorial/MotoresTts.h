#pragma once

// Motores de sintesis del paso de voz (T017 D5, D9, D12).
// - OpenAI: POST https://api.openai.com/v1/audio/speech (unico codigo con
//   cliente HTTP de la herramienta; solo lo enlaza satcfdi_video_voz).
// - Prueba: audio sintetico local y determinista, para ctest (sin red ni clave).
// Los errores son mensajes seguros: categoria y codigo HTTP; nunca la clave,
// cabeceras ni el cuerpo de la respuesta.

#include "Voz.h"

#include <QByteArray>
#include <QString>

#include <memory>

namespace video {

class MotorTts {
public:
    virtual ~MotorTts() = default;
    // Audio del texto (WAV) o error seguro.
    virtual QString sintetizar(const ConfigVoz& config, const QString& texto, QByteArray& audio) = 0;
};

// `clave`: valor de OPENAI_API_KEY (no se copia a ningun otro lugar).
std::unique_ptr<MotorTts> motorOpenAi(QByteArray clave);
// SATCFDI_VOZ_PRUEBA_FALLAR_EN=N: la N-esima llamada de la corrida falla
// (simula un error a mitad del lote).
std::unique_ptr<MotorTts> motorPrueba();

} // namespace video
