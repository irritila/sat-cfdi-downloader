#include "AppViewModel.h"

#include "AccionesFinder.h"
#include "application/primeruso/ConsultaPrimerUso.h"
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

void AppViewModel::abrirCarpetaPaquetes()
{
    if (!m_finder) {
        return;
    }
    const quint64 generacion = ++m_genFinder;
    setMensajeFinder(QString());
    m_finder->abrirCarpetaPaquetes().then(this, [this, generacion](const ResultadoAccionFinder& r) {
        if (generacion == m_genFinder) {
            setMensajeFinder(r.estado == ResultadoAccionFinder::Estado::Mostrado ? QString() : r.mensaje);
        }
    });
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

void AppViewModel::setConsultaPrimerUso(ConsultaPrimerUso* consulta)
{
    m_consultaPrimerUso = consulta;
    ++m_genPrimerUso; // descarta respuestas de la consulta anterior
    m_primerUsoConocido = false;
    m_primerUsoError = false;
    m_primerUsoCargando = false;
    emit primerUsoChanged();
    refrescarPrimerUso();
}

void AppViewModel::refrescarPrimerUso()
{
    if (!m_consultaPrimerUso) {
        return;
    }
    const quint64 generacion = ++m_genPrimerUso;
    if (!m_primerUsoConocido && !m_primerUsoCargando) {
        m_primerUsoCargando = true;
        m_primerUsoError = false;
        emit primerUsoChanged();
    }
    m_consultaPrimerUso->consultar().then(this, [this, generacion](const ConsultaPrimerUso::ResultadoPrimerUso& r) {
        if (generacion != m_genPrimerUso) {
            return; // respuesta tardia
        }
        m_primerUsoCargando = false;
        if (r.esExito()) {
            const EstadoPrimerUso& e = r.valor();
            m_primerUsoConocido = true;
            m_primerUsoError = false;
            m_hayPerfiles = e.hayPerfiles;
            m_hayEFirmaLista = e.hayEFirmaLista;
            m_haySolicitudes = e.haySolicitudes;
        } else {
            m_primerUsoConocido = false;
            m_primerUsoError = true;
        }
        emit primerUsoChanged();
    });
}

QVariantList AppViewModel::primerUsoPasos() const
{
    return {m_hayPerfiles, m_hayEFirmaLista, m_haySolicitudes};
}

bool AppViewModel::mostrarGuiaPrimerUso() const
{
    return m_primerUsoConocido && !m_primerUsoCargando && !m_primerUsoError && !m_haySolicitudes;
}

void AppViewModel::limpiarFiltros()
{
    if (m_filtroTexto.isEmpty() && m_filtroEstado.isEmpty() && m_filtroTipo.isEmpty() && m_filtroMes.isEmpty()) {
        return;
    }
    m_filtroTexto.clear();
    m_filtroEstado.clear();
    m_filtroTipo.clear();
    m_filtroMes.clear();
    emit filtrosChanged();
}

} // namespace satcfdi
