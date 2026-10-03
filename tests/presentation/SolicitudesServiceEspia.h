#pragma once

#include "application/requests/DemoSolicitudesService.h"
#include "application/requests/SolicitudesService.h"

// Doble de prueba (solo tests/presentation): delega en DemoSolicitudesService
// y cuenta llamadas a crear() para comprobar que un formulario invalido no
// llega al servicio.
class SolicitudesServiceEspia final : public satcfdi::SolicitudesService {
    Q_OBJECT

public:
    SolicitudesServiceEspia(QList<satcfdi::PerfilResumen> perfiles,
                            satcfdi::DemoSolicitudesService::Datos datos,
                            QObject* parent = nullptr)
        : SolicitudesService(parent)
        , m_demo(std::move(perfiles), datos)
    {
        connect(&m_demo, &SolicitudesService::solicitudActualizada, this,
                &SolicitudesService::solicitudActualizada);
    }

    QFuture<ResultadoLista> listar() override { return m_demo.listar(); }

    QFuture<ResultadoDetalle> obtener(const satcfdi::SolicitudId& id) override
    {
        return m_demo.obtener(id);
    }

    QFuture<ResultadoCrear> crear(const satcfdi::NuevaSolicitudRequest& request) override
    {
        ++llamadasCrear;
        return m_demo.crear(request);
    }

    int llamadasCrear = 0;

private:
    satcfdi::DemoSolicitudesService m_demo;
};
