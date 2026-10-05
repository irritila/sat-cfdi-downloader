#pragma once

#include <QDateTime>

#include <functional>

namespace satcfdi {

// Programador inyectable (T007 D8): ejecuta acciones en un instante del
// reloj logico. Produccion: app_core/ProgramadorQt (QTimer en el hilo que lo
// usa). Pruebas: FakeProgramador (tests/fakes) que dispara al avanzar el
// reloj, sin esperar tiempo real.
//
// Hilo: se usa desde un solo hilo (el del WorkerLocal / ejecutor que lo
// posee); la accion se ejecuta en ese mismo hilo. Instantes en el pasado se
// ejecutan en la siguiente vuelta del event loop (o en el siguiente avance
// del fake), nunca de forma sincrona dentro de programar().
// El reloj es satcfdi::RelojUtc (application/persistence/PuertosPersistencia.h).
class Programador {
public:
    using Id = quint64;

    virtual ~Programador() = default;

    // Programa `accion` para `instanteUtc`. Devuelve un id > 0.
    virtual Id programar(const QDateTime& instanteUtc, std::function<void()> accion) = 0;

    // Cancela una accion pendiente (sin efecto si ya corrio o no existe).
    virtual void cancelar(Id id) = 0;

    // Cancela todas las acciones pendientes (salida, D1).
    virtual void cancelarTodo() = 0;
};

} // namespace satcfdi
