#pragma once

#include "ports/secrets/SecretStoreTypes.h"

#include <QByteArray>
#include <QDateTime>
#include <QStringView>

namespace satcfdi::sat {

// Token de AutenticaResult (T006, D8): opaco, move-only, SOLO en memoria. No
// se persiste, no se registra y no tiene conversion a texto ni QDebug; el
// unico uso es construir el header Authorization dentro del cliente HTTP.
// Limpieza best effort (BufferSecreto); el valor del header que recibe Qt
// (QByteArray) es una copia inevitable que vive solo durante la peticion.
class TokenSat {
public:
    TokenSat() = default;
    TokenSat(QStringView texto, QDateTime creado, QDateTime expira)
        : TokenSat(BufferSecreto::desdeTexto(texto), std::move(creado), std::move(expira))
    {
    }
    // Toma el buffer UTF-8 del token (sin copias intermedias).
    TokenSat(BufferSecreto valor, QDateTime creado, QDateTime expira)
        : m_valor(std::move(valor))
        , m_creado(std::move(creado))
        , m_expira(std::move(expira))
    {
    }

    TokenSat(const TokenSat&) = delete;
    TokenSat& operator=(const TokenSat&) = delete;
    TokenSat(TokenSat&&) noexcept = default;
    TokenSat& operator=(TokenSat&&) noexcept = default;

    bool vacio() const noexcept { return m_valor.vacio(); }
    std::size_t tamano() const noexcept { return m_valor.tamano(); }
    // Created/Expires del Timestamp de la respuesta (UTC; invalidos si faltan).
    const QDateTime& creado() const noexcept { return m_creado; }
    const QDateTime& expira() const noexcept { return m_expira; }
    // Vigente en `ahoraUtc` (sin margen); sin Expires se considera vencido.
    bool vigenteEn(const QDateTime& ahoraUtc) const { return !vacio() && m_expira.isValid() && ahoraUtc < m_expira; }

    // Valor del header: WRAP access_token="<token>" (doc §6.1).
    QByteArray encabezadoAutorizacion() const
    {
        QByteArray h("WRAP access_token=\"");
        h.append(reinterpret_cast<const char*>(m_valor.datos()), static_cast<qsizetype>(m_valor.tamano()));
        h.append('"');
        return h;
    }

private:
    BufferSecreto m_valor;
    QDateTime m_creado;
    QDateTime m_expira;
};

QDebug operator<<(QDebug, const TokenSat&) = delete;

} // namespace satcfdi::sat
