#pragma once

// AvisoVencimientoRepository en memoria (T014.3). Thread-safe: la prueba lo
// observa en el hilo grafico y el dispatcher lo usa en su hilo. Sobrevive a
// varias instancias de AvisoVencimientoEFirma (simula el reinicio de la app).

#include "ports/repositories/AvisoVencimientoRepository.h"

#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QStringList>

namespace fakes {

class FakeAvisoVencimientoRepository final : public satcfdi::AvisoVencimientoRepository {
public:
    satcfdi::Resultado<bool, satcfdi::ErrorPersistencia> registrarSiNuevo(const satcfdi::PerfilId& perfil,
                                                                          const QDateTime& vigenteHasta,
                                                                          int umbralDias,
                                                                          const QDateTime&) override
    {
        QMutexLocker l(&m_mutex);
        const QString clave = QStringLiteral("%1|%2|%3")
                                  .arg(perfil.texto(), vigenteHasta.toUTC().toString(Qt::ISODate))
                                  .arg(umbralDias);
        if (m_claves.contains(clave)) {
            return satcfdi::Resultado<bool, satcfdi::ErrorPersistencia>::exito(false);
        }
        m_claves.insert(clave);
        return satcfdi::Resultado<bool, satcfdi::ErrorPersistencia>::exito(true);
    }

    int registrados() const
    {
        QMutexLocker l(&m_mutex);
        return static_cast<int>(m_claves.size());
    }

private:
    mutable QMutex m_mutex;
    QSet<QString> m_claves;
};

} // namespace fakes
