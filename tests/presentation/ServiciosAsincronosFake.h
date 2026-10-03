#pragma once

#include "application/profiles/PerfilesSatService.h"
#include "application/requests/SolicitudesService.h"

#include <QFuture>
#include <QList>
#include <QPromise>

#include <memory>
#include <utility>

// Fakes asincronos reales (solo tests/presentation). Cada llamada crea un
// QPromise pendiente; la prueba decide cuando y en que orden se completa con
// resolver(i, valor). Asi se reproducen respuestas tardias y desordenadas
// como las de PersistenceDispatcher. Las senales del contrato se emiten desde
// la prueba (son publicas) para simular commits.
template <typename T>
class Pendientes {
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

private:
    // Las promesas pendientes al destruir el fake se cancelan (QPromise).
    QList<std::shared_ptr<QPromise<T>>> m_promesas;
};

class SolicitudesServiceAsincrono final : public satcfdi::SolicitudesService {
    Q_OBJECT

public:
    using SolicitudesService::SolicitudesService;

    QFuture<ResultadoLista> listar() override { return lista.nueva(); }

    QFuture<ResultadoDetalle> obtener(const satcfdi::SolicitudId& id) override
    {
        idsObtener.append(id);
        return detalle.nueva();
    }

    QFuture<ResultadoEvaluarDuplicado> evaluarDuplicado(const satcfdi::NuevaSolicitudRequest& request) override
    {
        requestsEvaluar.append(request);
        return evaluacion.nueva();
    }

    QFuture<ResultadoCrear> crearLocal(const satcfdi::NuevaSolicitudRequest& request,
                                       satcfdi::ConfirmacionDuplicado confirmacion) override
    {
        requestsCrear.append(request);
        confirmaciones.append(confirmacion);
        return creacion.nueva();
    }

    QFuture<ResultadoEliminar> eliminar(const satcfdi::SolicitudId& id) override
    {
        idsEliminar.append(id);
        return eliminacion.nueva();
    }

    Pendientes<ResultadoLista> lista;
    Pendientes<ResultadoDetalle> detalle;
    Pendientes<ResultadoEvaluarDuplicado> evaluacion;
    Pendientes<ResultadoCrear> creacion;
    Pendientes<ResultadoEliminar> eliminacion;

    QList<satcfdi::SolicitudId> idsObtener;
    QList<satcfdi::NuevaSolicitudRequest> requestsEvaluar;
    QList<satcfdi::NuevaSolicitudRequest> requestsCrear;
    QList<satcfdi::ConfirmacionDuplicado> confirmaciones;
    QList<satcfdi::SolicitudId> idsEliminar;
};

class PerfilesSatServiceAsincrono final : public satcfdi::PerfilesSatService {
    Q_OBJECT

public:
    using PerfilesSatService::PerfilesSatService;

    QFuture<ResultadoLista> listarActivos() override { return lista.nueva(); }

    QFuture<ResultadoCrearPerfil> crearPerfilSimulado(const satcfdi::NuevoPerfilSimuladoRequest& request) override
    {
        requestsPerfil.append(request);
        return creacion.nueva();
    }

    Pendientes<ResultadoLista> lista;
    Pendientes<ResultadoCrearPerfil> creacion;
    QList<satcfdi::NuevoPerfilSimuladoRequest> requestsPerfil;
};
