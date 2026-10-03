#include "AppViewModel.h"

#include "EFirmaFormViewModel.h"
#include "NuevaSolicitudViewModel.h"
#include "PerfilesSatViewModel.h"
#include "SolicitudDetailViewModel.h"

#include "domain/solicitudes/SolicitudId.h"

namespace satcfdi {

AppViewModel::AppViewModel(NuevaSolicitudViewModel* nuevaSolicitud,
                           SolicitudDetailViewModel* detalle,
                           PerfilesSatViewModel* perfiles,
                           EFirmaFormViewModel* eFirma,
                           QObject* parent)
    : QObject(parent)
    , m_nuevaSolicitud(nuevaSolicitud)
    , m_detalle(detalle)
    , m_perfiles(perfiles)
    , m_eFirma(eFirma)
{
    Q_ASSERT(nuevaSolicitud != nullptr);
    Q_ASSERT(detalle != nullptr);
    Q_ASSERT(perfiles != nullptr);
    Q_ASSERT(eFirma != nullptr);
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

void AppViewModel::mostrarPerfiles()
{
    if (m_perfiles) {
        m_perfiles->cargar();
    }
    setPagina(Pagina::Perfiles);
}

void AppViewModel::setPagina(Pagina pagina)
{
    if (m_pagina == Pagina::Perfiles && pagina != Pagina::Perfiles) {
        // Salir de Perfiles descarta capturas: nada sensible sobrevive a la
        // navegacion.
        if (m_eFirma) {
            m_eFirma->abandonar(); // tambien si esta Validando
        }
        if (m_perfiles) {
            m_perfiles->cerrarFormulario();
        }
    }
    if (m_pagina != pagina) {
        m_pagina = pagina;
        emit paginaChanged();
    }
}

} // namespace satcfdi
