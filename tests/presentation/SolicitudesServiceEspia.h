#pragma once

#include "application/requests/DemoSolicitudesService.h"
#include "application/requests/SolicitudesService.h"

#include <QList>

#include <utility>

// Doble de prueba (solo tests/presentation): delega en DemoSolicitudesService
// (futures ya completados) y registra las llamadas que llegan al servicio para
// comprobar el flujo evaluarDuplicado -> crear()/crearLocal(). No sobrescribe
// crear(): la fachada de SolicitudesService llama crearLocal(SinConfirmar).
class SolicitudesServiceEspia final : public satcfdi::SolicitudesService {
    Q_OBJECT

public:
    SolicitudesServiceEspia(QList<satcfdi::PerfilResumen> perfiles,
                            satcfdi::DemoSolicitudesService::Datos datos,
                            QObject* parent = nullptr)
        : SolicitudesService(parent)
        , m_demo(std::move(perfiles), datos)
    {
        connect(&m_demo, &SolicitudesService::listaCambiada, this, &SolicitudesService::listaCambiada);
        connect(&m_demo, &SolicitudesService::solicitudActualizada, this,
                &SolicitudesService::solicitudActualizada);
        connect(&m_demo, &SolicitudesService::solicitudEliminada, this,
                &SolicitudesService::solicitudEliminada);
    }

    void setPerfiles(QList<satcfdi::PerfilResumen> perfiles) { m_demo.setPerfiles(std::move(perfiles)); }

    QFuture<ResultadoLista> listar() override { return m_demo.listar(); }

    QFuture<ResultadoDetalle> obtener(const satcfdi::SolicitudId& id) override
    {
        return m_demo.obtener(id);
    }

    QFuture<ResultadoEvaluarDuplicado> evaluarDuplicado(const satcfdi::NuevaSolicitudRequest& request) override
    {
        ++llamadasEvaluar;
        return m_demo.evaluarDuplicado(request);
    }

    QFuture<ResultadoCrear> crearLocal(const satcfdi::NuevaSolicitudRequest& request,
                                       satcfdi::ConfirmacionDuplicado confirmacion) override
    {
        confirmaciones.append(confirmacion);
        return m_demo.crearLocal(request, confirmacion);
    }

    QFuture<ResultadoEliminar> eliminar(const satcfdi::SolicitudId& id) override
    {
        ++llamadasEliminar;
        return m_demo.eliminar(id);
    }

    // Llamadas a crearLocal (incluye las que llegan por la fachada crear()).
    int llamadasCrear() const { return int(confirmaciones.size()); }

    int llamadasEvaluar = 0;
    int llamadasEliminar = 0;
    QList<satcfdi::ConfirmacionDuplicado> confirmaciones;

private:
    satcfdi::DemoSolicitudesService m_demo;
};
