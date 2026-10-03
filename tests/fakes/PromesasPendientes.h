#pragma once

// Promesas manuales para fakes de servicios (T005.1). Cada llamada crea un
// QPromise pendiente; la prueba decide cuando y en que orden se completa
// (respuestas tardias y desordenadas como las de PersistenceDispatcher).
// Solo hilo grafico.

#include <QFuture>
#include <QList>
#include <QPromise>

#include <memory>
#include <utility>

namespace fakes {

template <typename T>
class PromesasPendientes {
public:
    QFuture<T> nueva()
    {
        auto promesa = std::make_shared<QPromise<T>>();
        promesa->start();
        m_promesas.append(promesa);
        return promesa->future();
    }

    int size() const { return int(m_promesas.size()); }

    // Completa la llamada `indice` (orden de llegada, desde 0).
    void resolver(int indice, T valor)
    {
        Q_ASSERT(indice >= 0 && indice < m_promesas.size());
        QPromise<T>& promesa = *m_promesas.at(indice);
        promesa.addResult(std::move(valor));
        promesa.finish();
    }

    void resolverUltima(T valor) { resolver(size() - 1, std::move(valor)); }

    // Cancela la llamada `indice` (p. ej. dispatcher cerrado).
    void cancelar(int indice)
    {
        Q_ASSERT(indice >= 0 && indice < m_promesas.size());
        m_promesas.at(indice)->future().cancel();
        m_promesas.at(indice)->finish();
    }

private:
    // Las promesas pendientes al destruir el fake se cancelan (QPromise).
    QList<std::shared_ptr<QPromise<T>>> m_promesas;
};

} // namespace fakes
