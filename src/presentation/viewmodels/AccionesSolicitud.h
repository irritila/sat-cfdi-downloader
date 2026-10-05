#pragma once

#include "domain/solicitudes/SolicitudId.h"

namespace satcfdi {

// Acciones de operacion SAT que la presentacion solicita (T007 D4, D17).
// Interfaz minima propia de presentacion: el composition root la implementa
// sobre WorkerLocal (que solo ENCOLA en OperacionExecutor). Sin ella (pruebas
// o shell sin worker) las acciones no se ofrecen. Hilo grafico; no bloquea.
class AccionesSolicitud {
public:
    virtual ~AccionesSolicitud() = default;
    // Envio tras crearLocal() (solo usuario). Con el monitoreo pausado tambien
    // se ejecuta (D17).
    virtual void enviar(const SolicitudId& id) = 0;
    // Con el monitoreo pausado quedan como intencion pendiente.
    virtual void verificarAhora(const SolicitudId& id) = 0;
    virtual void reintentarDescarga(const SolicitudId& id) = 0;
};

} // namespace satcfdi
