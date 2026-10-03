#pragma once

namespace satcfdi {

// Punto de extension del ciclo de vida para el worker de T007. T004 no tiene
// worker: AppLifecycleController funciona sin extension (nullptr).
//
// Hilo grafico. Las llamadas llegan solo con estado ya CONFIRMADO en SQLite.
class ExtensionCicloDeVida {
public:
    virtual ~ExtensionCicloDeVida() = default;

    // Configuracion persistida leida al arrancar o pausa/reanudacion confirmada.
    virtual void aplicarMonitoreoPausado(bool pausado) = 0;

    // Salida explicita: detener trabajo en curso ANTES de registrar el cierre
    // y retirar el menu bar. Debe volver sin bloquear el hilo grafico.
    virtual void detener() = 0;
};

} // namespace satcfdi
