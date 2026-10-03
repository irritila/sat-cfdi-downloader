#pragma once

// KeychainApi falsa en memoria (T005). Determinista; permite inyectar un
// OSStatus por operacion (opcionalmente solo para un servicio) y observa los
// items. No toca Keychain real.

#include "infrastructure/secrets/macos/KeychainApi.h"

#include <map>
#include <optional>
#include <utility>

namespace fakes {

class FakeKeychainApi final : public satcfdi::secrets::KeychainApi {
public:
    enum class Operacion { Agregar, Copiar, Existe, Eliminar, Listar };

    struct Fallo {
        std::int32_t status;
        std::optional<QString> soloServicio; // nullopt = cualquier servicio
    };

    std::map<Operacion, Fallo> fallos;

    std::int32_t agregar(const QString& servicio, const QString& cuenta, const satcfdi::BufferSecreto& dato) override
    {
        if (auto s = falloPara(Operacion::Agregar, servicio)) {
            return *s;
        }
        const auto clave = std::make_pair(servicio, cuenta);
        if (m_items.count(clave) != 0) {
            return satcfdi::secrets::osstatus::kDuplicateItem;
        }
        m_items.emplace(clave, satcfdi::BufferSecreto::desdeBytes(dato.datos(), dato.tamano()));
        return satcfdi::secrets::osstatus::kSuccess;
    }

    std::int32_t copiar(const QString& servicio, const QString& cuenta, satcfdi::BufferSecreto& salida) override
    {
        if (auto s = falloPara(Operacion::Copiar, servicio)) {
            return *s;
        }
        const auto it = m_items.find(std::make_pair(servicio, cuenta));
        if (it == m_items.end()) {
            return satcfdi::secrets::osstatus::kItemNotFound;
        }
        salida = satcfdi::BufferSecreto::desdeBytes(it->second.datos(), it->second.tamano());
        return satcfdi::secrets::osstatus::kSuccess;
    }

    std::int32_t existe(const QString& servicio, const QString& cuenta) override
    {
        if (auto s = falloPara(Operacion::Existe, servicio)) {
            return *s;
        }
        return m_items.count(std::make_pair(servicio, cuenta)) != 0 ? satcfdi::secrets::osstatus::kSuccess
                                                                    : satcfdi::secrets::osstatus::kItemNotFound;
    }

    std::int32_t eliminar(const QString& servicio, const QString& cuenta) override
    {
        if (auto s = falloPara(Operacion::Eliminar, servicio)) {
            return *s;
        }
        return m_items.erase(std::make_pair(servicio, cuenta)) != 0 ? satcfdi::secrets::osstatus::kSuccess
                                                                    : satcfdi::secrets::osstatus::kItemNotFound;
    }

    std::int32_t listarCuentas(const QString& servicio, QStringList& cuentas) override
    {
        cuentas.clear();
        if (auto s = falloPara(Operacion::Listar, servicio)) {
            return *s;
        }
        for (const auto& [clave, valor] : m_items) {
            if (clave.first == servicio) {
                cuentas.append(clave.second);
            }
        }
        return satcfdi::secrets::osstatus::kSuccess;
    }

    // --- Observacion / manipulacion directa. ---
    std::size_t cantidad() const { return m_items.size(); }
    bool contiene(const QString& servicio, const QString& cuenta) const
    {
        return m_items.count(std::make_pair(servicio, cuenta)) != 0;
    }
    const satcfdi::BufferSecreto* dato(const QString& servicio, const QString& cuenta) const
    {
        const auto it = m_items.find(std::make_pair(servicio, cuenta));
        return it == m_items.end() ? nullptr : &it->second;
    }
    void quitar(const QString& servicio, const QString& cuenta) { m_items.erase(std::make_pair(servicio, cuenta)); }
    void poner(const QString& servicio, const QString& cuenta, const QByteArray& bytes)
    {
        m_items.erase(std::make_pair(servicio, cuenta));
        m_items.emplace(std::make_pair(servicio, cuenta),
                        satcfdi::BufferSecreto::desdeBytes(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    }

private:
    std::optional<std::int32_t> falloPara(Operacion op, const QString& servicio) const
    {
        const auto it = fallos.find(op);
        if (it == fallos.end()) {
            return std::nullopt;
        }
        if (it->second.soloServicio && *it->second.soloServicio != servicio) {
            return std::nullopt;
        }
        return it->second.status;
    }

    std::map<std::pair<QString, QString>, satcfdi::BufferSecreto> m_items;
};

} // namespace fakes
