#pragma once

#include <QHashFunctions>
#include <QString>
#include <QStringView>

#include <optional>
#include <utility>

namespace satcfdi {

// Identidad local de un perfil SAT: UUID canonico (minusculas, sin llaves).
// Un valor construido por defecto es nulo y no identifica ninguna entidad.
// Nunca se usa el indice de fila como identidad.
class PerfilId {
public:
    PerfilId() = default;

    // Acepta solo la forma canonica; cualquier otro texto devuelve nullopt.
    static std::optional<PerfilId> desdeTexto(QStringView texto);

    // Genera un identificador nuevo en forma canonica.
    static PerfilId generar();

    bool esNulo() const noexcept { return m_valor.isEmpty(); }
    const QString& texto() const noexcept { return m_valor; }

    friend bool operator==(const PerfilId&, const PerfilId&) = default;

    friend size_t qHash(const PerfilId& id, size_t seed = 0) noexcept
    {
        return ::qHash(id.m_valor, seed);
    }

private:
    explicit PerfilId(QString valor) : m_valor(std::move(valor)) {}

    QString m_valor;
};

} // namespace satcfdi
