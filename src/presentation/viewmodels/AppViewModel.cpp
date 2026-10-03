#include "AppViewModel.h"

#include "NuevaSolicitudViewModel.h"
#include "SolicitudDetailViewModel.h"

#include "domain/solicitudes/SolicitudId.h"

namespace satcfdi {

AppViewModel::AppViewModel(NuevaSolicitudViewModel* nuevaSolicitud,
                           SolicitudDetailViewModel* detalle,
                           QObject* parent)
    : QObject(parent)
    , m_nuevaSolicitud(nuevaSolicitud)
    , m_detalle(detalle)
{
    Q_ASSERT(nuevaSolicitud != nullptr);
    Q_ASSERT(detalle != nullptr);
    connect(nuevaSolicitud, &NuevaSolicitudViewModel::submitted, this,
            [this](const QString& id) { abrirDetalle(id); });
    // Tras eliminar desde el detalle se regresa a la lista (T003 2c).
    connect(detalle, &SolicitudDetailViewModel::eliminada, this,
            [this](const QString&) { mostrarLista(); });
}

void AppViewModel::mostrarLista()
{
    setPagina(Pagina::Lista);
}

void AppViewModel::mostrarNueva()
{
    if (m_nuevaSolicitud) {
        m_nuevaSolicitud->reiniciar();
    }
    setPagina(Pagina::Nueva);
}

bool AppViewModel::abrirDetalle(const QString& id)
{
    if (!SolicitudId::desdeTexto(id)) {
        return false;
    }
    if (m_solicitudSeleccionadaId != id) {
        m_solicitudSeleccionadaId = id;
        emit solicitudSeleccionadaIdChanged();
    }
    if (m_detalle) {
        m_detalle->cargar(id);
    }
    setPagina(Pagina::Detalle);
    return true;
}

void AppViewModel::setPagina(Pagina pagina)
{
    if (m_pagina != pagina) {
        m_pagina = pagina;
        emit paginaChanged();
    }
}

} // namespace satcfdi
