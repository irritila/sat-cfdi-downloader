#pragma once

// AccesoCredencialSat falso para OperacionesSatProductivo (T009). Thread-safe
// (QMutex): la prueba lo programa en el hilo grafico y el adaptador lo usa en
// el hilo del ejecutor. El material es FICTICIO (bytes fijos de prueba).
//
//   fakes::FakeAccesoCredencialSat credenciales;
//   credenciales.fijarEstado(EstadoCredencial::Vencida);
//   credenciales.fijarErrorEstado(ErrorCredencialSat::almacen(Categoria::AlmacenBloqueado));

#include "application/profiles/AccesoCredencialSat.h"

#include <QMutex>
#include <QMutexLocker>

#include <optional>

namespace fakes {

class FakeAccesoCredencialSat final : public satcfdi::AccesoCredencialSat {
public:
    void fijarEstado(satcfdi::EstadoCredencial estado)
    {
        QMutexLocker l(&m_mutex);
        m_estado = estado;
    }
    void fijarErrorEstado(std::optional<satcfdi::ErrorCredencialSat> error)
    {
        QMutexLocker l(&m_mutex);
        m_errorEstado = std::move(error);
    }
    void fijarErrorMaterial(std::optional<satcfdi::ErrorCredencialSat> error)
    {
        QMutexLocker l(&m_mutex);
        m_errorMaterial = std::move(error);
    }
    int consultasEstado() const
    {
        QMutexLocker l(&m_mutex);
        return m_consultasEstado;
    }
    int lecturasMaterial() const
    {
        QMutexLocker l(&m_mutex);
        return m_lecturasMaterial;
    }

    satcfdi::Resultado<satcfdi::EstadoCredencial, satcfdi::ErrorCredencialSat>
    estadoEnHiloDeTrabajo(const satcfdi::PerfilId&) override
    {
        using R = satcfdi::Resultado<satcfdi::EstadoCredencial, satcfdi::ErrorCredencialSat>;
        QMutexLocker l(&m_mutex);
        ++m_consultasEstado;
        if (m_errorEstado) {
            return R::fallo(*m_errorEstado);
        }
        return R::exito(m_estado);
    }

    satcfdi::Resultado<satcfdi::MaterialFirma, satcfdi::ErrorCredencialSat>
    materialEnHiloDeTrabajo(const satcfdi::PerfilId&) override
    {
        using R = satcfdi::Resultado<satcfdi::MaterialFirma, satcfdi::ErrorCredencialSat>;
        QMutexLocker l(&m_mutex);
        ++m_lecturasMaterial;
        if (m_errorMaterial) {
            return R::fallo(*m_errorMaterial);
        }
        return R::exito(satcfdi::MaterialFirma(satcfdi::BufferSecreto::desdeTexto(u"cert-ficticio"),
                                               satcfdi::BufferSecreto::desdeTexto(u"llave-ficticia"),
                                               satcfdi::BufferSecreto::desdeTexto(u"contrasena-ficticia")));
    }

private:
    mutable QMutex m_mutex;
    satcfdi::EstadoCredencial m_estado = satcfdi::EstadoCredencial::Lista;
    std::optional<satcfdi::ErrorCredencialSat> m_errorEstado;
    std::optional<satcfdi::ErrorCredencialSat> m_errorMaterial;
    int m_consultasEstado = 0;
    int m_lecturasMaterial = 0;
};

} // namespace fakes
