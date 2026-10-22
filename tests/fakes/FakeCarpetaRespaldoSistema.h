#pragma once

// T025.4: CarpetaRespaldoSistema determinista. crearReferencia() acepta
// cualquier ruta absoluta (o la de `canonicas`, que simula enlaces como
// ~/Dropbox) y devuelve un bookmark sintetico "bm:<n>"; resolver() devuelve
// lo programado para ese bookmark (por omision, la ruta con la que se creo).
// Sin disco. Thread-safe.

#include "ports/respaldo/CarpetaRespaldoSistema.h"

#include <QMap>
#include <QMutex>
#include <QMutexLocker>

#include <optional>

namespace fakes {

class FakeCarpetaRespaldoSistema final : public satcfdi::respaldo::CarpetaRespaldoSistema {
public:
    using Error = satcfdi::respaldo::ErrorCarpetaRespaldo;
    using Referencia = satcfdi::respaldo::ReferenciaCarpetaRespaldo;
    using Resuelta = satcfdi::respaldo::CarpetaRespaldoResuelta;

    // ruta elegida -> ruta canonica (simula enlaces).
    void fijarCanonica(const QString& elegida, const QString& canonica)
    {
        QMutexLocker l(&m_mutex);
        m_canonicas.insert(elegida, canonica);
    }
    void fijarUuidVolumen(const QString& uuid)
    {
        QMutexLocker l(&m_mutex);
        m_uuid = uuid;
    }
    // Error de crearReferencia (nullopt = sin error).
    void fallarCrear(std::optional<Error::Categoria> c)
    {
        QMutexLocker l(&m_mutex);
        m_falloCrear = c;
    }
    // Lo que resolver(bookmark) devolvera desde ahora.
    void fijarResolucion(const QByteArray& bookmark, const Resuelta& r)
    {
        QMutexLocker l(&m_mutex);
        m_resoluciones.insert(bookmark, r);
        m_erroresResolver.remove(bookmark);
    }
    void fallarResolver(const QByteArray& bookmark, Error::Categoria c)
    {
        QMutexLocker l(&m_mutex);
        m_erroresResolver.insert(bookmark, c);
    }

    satcfdi::Resultado<Referencia, Error> crearReferencia(const QString& rutaElegida) const override
    {
        using R = satcfdi::Resultado<Referencia, Error>;
        QMutexLocker l(&m_mutex);
        ++m_llamadasCrear;
        if (m_falloCrear) {
            return R::fallo(Error::de(*m_falloCrear, QStringLiteral("fake")));
        }
        if (!rutaElegida.startsWith(QLatin1Char('/'))) {
            return R::fallo(Error::de(Error::Categoria::RutaInvalida));
        }
        Referencia r;
        r.rutaCanonica = m_canonicas.value(rutaElegida, rutaElegida);
        r.bookmark = QByteArray("bm:") + QByteArray::number(++m_contador);
        r.uuidVolumen = m_uuid;
        m_resoluciones.insert(r.bookmark, Resuelta{r.rutaCanonica, r.uuidVolumen, false});
        return R::exito(r);
    }

    satcfdi::Resultado<Resuelta, Error> resolver(const QByteArray& bookmark) const override
    {
        using R = satcfdi::Resultado<Resuelta, Error>;
        QMutexLocker l(&m_mutex);
        ++m_llamadasResolver;
        if (const auto it = m_erroresResolver.constFind(bookmark); it != m_erroresResolver.cend()) {
            return R::fallo(Error::de(*it, QStringLiteral("fake")));
        }
        const auto it = m_resoluciones.constFind(bookmark);
        if (it == m_resoluciones.cend()) {
            return R::fallo(Error::de(Error::Categoria::BookmarkInvalido));
        }
        return R::exito(*it);
    }

    int llamadasCrear() const
    {
        QMutexLocker l(&m_mutex);
        return m_llamadasCrear;
    }
    int llamadasResolver() const
    {
        QMutexLocker l(&m_mutex);
        return m_llamadasResolver;
    }

private:
    mutable QMutex m_mutex;
    QMap<QString, QString> m_canonicas;
    QString m_uuid = QStringLiteral("00000000-0000-4000-8000-000000000001");
    std::optional<Error::Categoria> m_falloCrear;
    mutable QMap<QByteArray, Resuelta> m_resoluciones;
    QMap<QByteArray, Error::Categoria> m_erroresResolver;
    mutable int m_contador = 0;
    mutable int m_llamadasCrear = 0;
    mutable int m_llamadasResolver = 0;
};

} // namespace fakes
