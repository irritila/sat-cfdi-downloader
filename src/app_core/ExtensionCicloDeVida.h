#pragma once

#include <QFuture>

namespace satcfdi {

// Punto de extension del ciclo de vida para el worker (T007). Lo implementa
// ExtensionWorker sobre WorkerLocal y OperacionExecutor; sin extension
// (nullptr) AppLifecycleController funciona igual.
//
// Hilo grafico. Las llamadas llegan solo con estado ya CONFIRMADO en SQLite.
class ExtensionCicloDeVida {
public:
    virtual ~ExtensionCicloDeVida() = default;

    // Configuracion persistida leida al arrancar o pausa/reanudacion confirmada.
    virtual void aplicarMonitoreoPausado(bool pausado) = 0;

    // Salida explicita (D1): detener timers, no aceptar operaciones nuevas y
    // esperar la actual hasta el plazo; despues cancelacion cooperativa. NO
    // bloquea: el future se completa cuando el trabajo termino. El
    // controlador registra el ultimo cierre y retira el menu bar DESPUES.
    virtual QFuture<void> detener() = 0;
};

} // namespace satcfdi
