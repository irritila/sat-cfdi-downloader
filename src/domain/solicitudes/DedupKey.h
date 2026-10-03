#pragma once

#include <QByteArrayView>
#include <QHashFunctions>
#include <QString>
#include <QStringView>

#include <optional>
#include <utility>

namespace satcfdi {

// Clave de deduplicacion persistida en solicitud_masiva.dedup_key.
//
// Formato general `v<n>:<cuerpo>` (CHECK `v[1-9]*:?*` de T001). T003 solo
// produce `v1:<sha256-hex-lowercase>` (64 hex). Un valor construido por
// defecto es nulo. Las claves leidas de SQLite se reconstruyen con
// desdeTexto(); las nuevas se calculan con SolicitudCanonica.
class DedupKey {
public:
    DedupKey() = default;

    // Acepta `v<n>:<cuerpo no vacio>` con n >= 1 sin ceros a la izquierda. Si
    // la version es 1 exige ademas 64 hex en minusculas.
    static std::optional<DedupKey> desdeTexto(QStringView texto);

    // `v1:` + SHA-256 hex en minusculas de `serializacionCanonica` (bytes
    // UTF-8 producidos por SolicitudCanonica::serializacionCanonicaV1()).
    static DedupKey calcularV1(QByteArrayView serializacionCanonica);

    bool esNula() const noexcept { return m_valor.isEmpty(); }
    // 0 si es nula.
    int version() const noexcept { return m_version; }
    const QString& texto() const noexcept { return m_valor; }

    friend bool operator==(const DedupKey& a, const DedupKey& b) noexcept
    {
        return a.m_valor == b.m_valor;
    }

    friend size_t qHash(const DedupKey& clave, size_t seed = 0) noexcept
    {
        return ::qHash(clave.m_valor, seed);
    }

private:
    DedupKey(int version, QString valor) : m_version(version), m_valor(std::move(valor)) {}

    int m_version = 0;
    QString m_valor;
};

} // namespace satcfdi
