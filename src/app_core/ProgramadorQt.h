#pragma once

#include "application/operaciones/Programador.h"
#include "application/persistence/PuertosPersistencia.h"

#include <QHash>
#include <QPointer>
#include <QTimer>

namespace satcfdi {

// Programador de produccion (T007 D8) sobre QTimer de un solo disparo.
//
// Hilo: se usa desde UN solo hilo (el que llama programar(); debe tener event
// loop). Cada QTimer se crea en ese hilo, asi que la accion corre en el mismo
// hilo. Un instante pasado se ejecuta en la siguiente vuelta del event loop
// (intervalo 0), nunca dentro de programar(). Lo crea y posee el composition
// root; se destruye despues de su usuario (WorkerLocal / OperacionExecutor).
class ProgramadorQt final : public Programador {
public:
    explicit ProgramadorQt(RelojUtc reloj = relojSistema());
    ~ProgramadorQt() override;

    ProgramadorQt(const ProgramadorQt&) = delete;
    ProgramadorQt& operator=(const ProgramadorQt&) = delete;

    Id programar(const QDateTime& instanteUtc, std::function<void()> accion) override;
    void cancelar(Id id) override;
    void cancelarTodo() override;

    int pendientes() const { return static_cast<int>(m_timers.size()); }

private:
    RelojUtc m_reloj;
    Id m_siguiente = 0;
    QHash<Id, QPointer<QTimer>> m_timers;
};

} // namespace satcfdi
