#pragma once

#include <QtGlobal>

#include <utility>
#include <variant>

namespace satcfdi {

// Resultado de una operacion: contiene exactamente un valor T (exito) o un
// error E (fallo). Tipo propio C++20; no depende de std::expected.
//
// Precondicion de acceso: valor() solo si esExito(); error() solo si
// !esExito(). Violarla es un error de programacion (Q_ASSERT en debug).
template <typename T, typename E>
class Resultado {
public:
    using TipoValor = T;
    using TipoError = E;

    static Resultado exito(T valor)
    {
        return Resultado(std::in_place_index<0>, std::move(valor));
    }

    static Resultado fallo(E error)
    {
        return Resultado(std::in_place_index<1>, std::move(error));
    }

    bool esExito() const noexcept { return m_dato.index() == 0; }
    explicit operator bool() const noexcept { return esExito(); }

    const T& valor() const&
    {
        Q_ASSERT(esExito());
        return std::get<0>(m_dato);
    }

    T&& valor() &&
    {
        Q_ASSERT(esExito());
        return std::get<0>(std::move(m_dato));
    }

    const E& error() const&
    {
        Q_ASSERT(!esExito());
        return std::get<1>(m_dato);
    }

    E&& error() &&
    {
        Q_ASSERT(!esExito());
        return std::get<1>(std::move(m_dato));
    }

private:
    template <std::size_t I, typename U>
    Resultado(std::in_place_index_t<I> indice, U&& dato)
        : m_dato(indice, std::forward<U>(dato))
    {
    }

    std::variant<T, E> m_dato;
};

} // namespace satcfdi
