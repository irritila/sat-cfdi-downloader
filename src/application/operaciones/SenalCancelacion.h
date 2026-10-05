#pragma once

#include <chrono>
#include <functional>
#include <memory>

namespace satcfdi {

// Senal de cancelacion cooperativa (T007 D12), segura entre hilos. Copiable:
// las copias comparten el mismo estado. La solicita el OperacionExecutor (p.
// ej. al vencer el plazo de salida de 10 s); la observa el adaptador de
// OperacionesSat en su hilo, consultando solicitada(), esperando con
// esperar() o registrando una notificacion con alSolicitar().
//
// Un SenalCancelacion construido por defecto es valido (nunca cancelado hasta
// que alguien llame solicitar()).
class SenalCancelacion {
public:
    SenalCancelacion();

    // Idempotente. Despierta a quien espere y ejecuta (una vez, en el hilo que
    // llama) las notificaciones registradas.
    void solicitar() const;
    bool solicitada() const noexcept;

    // Bloquea hasta que se solicite la cancelacion o pase `maximo`. true si
    // fue solicitada. Para adaptadores que esperan (red, reintentos internos).
    bool esperar(std::chrono::milliseconds maximo) const;

    // Ejecuta `notificar` cuando se solicite la cancelacion (de inmediato si ya
    // lo fue). Debe ser rapida, no bloqueante y segura en cualquier hilo
    // (p. ej. abortar un QNetworkReply via QMetaObject::invokeMethod).
    void alSolicitar(std::function<void()> notificar) const;

private:
    struct Estado;
    std::shared_ptr<Estado> m_estado;
};

} // namespace satcfdi
