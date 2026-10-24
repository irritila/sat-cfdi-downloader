#include "AppViewModel.h"

#include "AccionesFinder.h"
#include "EFirmaFormViewModel.h"
#include "ExploradorCfdiViewModel.h"
#include "FacturasViewModel.h"
#include "FuenteHistorial.h"
#include "HistorialViewModel.h"
#include "PerfilesSatViewModel.h"
#include "RespaldoViewModel.h"
#include "SolicitudDetailViewModel.h"

#include "application/notificaciones/DestinoNavegacion.h"
#include "application/primeruso/ConsultaPrimerUso.h"
#include "domain/solicitudes/SolicitudId.h"

#include <optional>
#include <utility>

namespace satcfdi {

namespace {

bool esRaiz(AppViewModel::Pagina p)
{
    return p == AppViewModel::Pagina::Facturas || p == AppViewModel::Pagina::Perfiles;
}

} // namespace

AppViewModel::AppViewModel(SolicitudDetailViewModel* detalle,
                           PerfilesSatViewModel* perfiles,
                           EFirmaFormViewModel* eFirma,
                           QObject* parent)
    : QObject(parent)
    , m_detalle(detalle)
    , m_perfiles(perfiles)
    , m_eFirma(eFirma)
{
    Q_ASSERT(detalle != nullptr);
    Q_ASSERT(perfiles != nullptr);
    Q_ASSERT(eFirma != nullptr);
    // Tras eliminar desde el detalle se regresa al origen (T003 2c, T023.5).
    connect(detalle, &SolicitudDetailViewModel::eliminada, this, [this](const QString&) {
        if (m_pagina == Pagina::Detalle) {
            regresar();
        }
    });
    connect(detalle, &SolicitudDetailViewModel::estadoChanged, this, &AppViewModel::alCambiarEstadoDetalle);
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

// ---- Pila de "Atras" ----

bool AppViewModel::puedeRegresar() const
{
    // Sin pantalla Facturas (pruebas, shell) Diagnostico hace de raiz.
    return !esRaiz(m_pagina) && !(m_pagina == Pagina::Diagnostico && m_origenes.isEmpty() && !m_facturas);
}

QString AppViewModel::textoRegresar() const
{
    switch (paginaOrigen()) {
    case Pagina::Historial: return QStringLiteral("Historial");
    case Pagina::Respaldo: return QStringLiteral("Respaldo");
    case Pagina::Perfiles: return QStringLiteral("Perfiles SAT");
    case Pagina::Diagnostico: return QStringLiteral("Diagnóstico");
    case Pagina::Detalle: return QStringLiteral("Detalles técnicos");
    case Pagina::Facturas:
    case Pagina::FacturasSolicitud: break;
    }
    return QStringLiteral("Facturas");
}

void AppViewModel::irARaiz(Pagina pagina)
{
    ++m_genDestino; // una navegacion explicita descarta un destino pendiente
    m_origenes.clear();
    setPagina(pagina);
}

void AppViewModel::irASecundaria(Pagina pagina)
{
    ++m_genDestino;
    const qsizetype i = m_origenes.indexOf(pagina);
    if (i >= 0) {
        m_origenes.resize(i); // destino ya en la pila: se recorta (sin ciclos)
    } else if (m_pagina != pagina) {
        m_origenes.append(m_pagina);
    }
    setPagina(pagina);
    emit paginaChanged(); // el origen pudo cambiar aunque la pagina no
}

void AppViewModel::regresar()
{
    if (!puedeRegresar()) {
        return;
    }
    ++m_genDestino;
    m_detalleConRespaldoLista.clear();
    m_avisoFinderPendiente.clear();
    Pagina destino = m_origenes.isEmpty() ? Pagina::Facturas : m_origenes.takeLast();
    if (destino == Pagina::Facturas && !m_facturas) {
        destino = Pagina::Diagnostico; // sin pantalla Facturas (pruebas, shell)
        m_origenes.clear();
    }
    if (destino == Pagina::Detalle && m_detalle && m_detalle->solicitudId() != m_solicitudSeleccionadaId) {
        m_detalle->cargar(m_solicitudSeleccionadaId);
    }
    if (destino == Pagina::Historial && m_historial) {
        m_historial->recargar();
    }
    setPagina(destino);
    emit paginaChanged();
}

// ---- Paginas ----

void AppViewModel::mostrarFacturas()
{
    if (m_facturas) {
        irARaiz(Pagina::Facturas);
    }
}

void AppViewModel::mostrarPerfiles()
{
    if (m_perfiles) {
        m_perfiles->cargar();
    }
    irARaiz(Pagina::Perfiles);
}

void AppViewModel::mostrarHistorial()
{
    if (!m_historial) {
        return;
    }
    if (m_facturas) {
        m_historial->mostrarPerfil(m_facturas->perfilId(), m_facturas->perfilRfc(), m_facturas->perfilNombre());
    }
    irASecundaria(Pagina::Historial);
}

void AppViewModel::mostrarRespaldo()
{
    if (m_respaldoVm) {
        irASecundaria(Pagina::Respaldo);
    }
}

void AppViewModel::setRespaldo(RespaldoViewModel* respaldo)
{
    m_respaldoVm = respaldo;
}

void AppViewModel::mostrarDiagnostico()
{
    irASecundaria(Pagina::Diagnostico);
}

void AppViewModel::cargarDetalle(const QString& id)
{
    if (m_solicitudSeleccionadaId != id) {
        m_solicitudSeleccionadaId = id;
        emit solicitudSeleccionadaIdChanged();
    }
    if (m_detalle) {
        m_detalle->cargar(id);
    }
}

bool AppViewModel::abrirDetalle(const QString& id)
{
    if (!SolicitudId::desdeTexto(id)) {
        return false;
    }
    // Una navegacion explicita sustituye cualquier respaldo de notificacion.
    m_detalleConRespaldoLista.clear();
    m_avisoFinderPendiente.clear();
    cargarDetalle(id);
    irASecundaria(Pagina::Detalle);
    return true;
}

void AppViewModel::abrirDiagnosticoDe(const QString& solicitudId)
{
    if (!abrirDetalle(solicitudId)) {
        mostrarDiagnostico();
    }
}

void AppViewModel::setExplorador(ExploradorCfdiViewModel* explorador)
{
    m_explorador = explorador;
}

bool AppViewModel::abrirFacturas(const QString& id)
{
    if (!m_explorador || !m_explorador->abrir(id)) {
        return false;
    }
    m_detalleConRespaldoLista.clear();
    m_avisoFinderPendiente.clear();
    if (m_solicitudSeleccionadaId != id) {
        cargarDetalle(id); // el regreso muestra el detalle de esta solicitud
    }
    if (m_pagina != Pagina::Detalle && m_pagina != Pagina::FacturasSolicitud) {
        // Sin detalle de origen: "Atras" regresa igualmente a su detalle.
        ++m_genDestino;
        m_origenes.removeAll(Pagina::Detalle);
        m_origenes.append(m_pagina);
        m_origenes.append(Pagina::Detalle);
        setPagina(Pagina::FacturasSolicitud);
        emit paginaChanged();
        return true;
    }
    irASecundaria(Pagina::FacturasSolicitud);
    return true;
}

void AppViewModel::regresarDeFacturas()
{
    if (m_pagina == Pagina::FacturasSolicitud) {
        regresar();
    }
}

void AppViewModel::setHistorial(HistorialViewModel* historial)
{
    if (m_historial) {
        disconnect(m_historial, nullptr, this, nullptr);
    }
    m_historial = historial;
    if (!historial) {
        return;
    }
    connect(historial, &HistorialViewModel::facturasMesSolicitadas, this,
            [this](const QString& perfil, int anio, int mes) { abrirFacturasMes(perfil, anio, mes); });
    connect(historial, &HistorialViewModel::reintentoSolicitado, this,
            [this](const QString& perfil, int anio, int mes) { abrirFacturasMes(perfil, anio, mes, true); });
    connect(historial, &HistorialViewModel::diagnosticoSolicitado, this, &AppViewModel::abrirDiagnosticoDe);
    if (m_pagina == Pagina::Historial) {
        historial->activar();
    }
}

void AppViewModel::setFacturas(FacturasViewModel* facturas)
{
    if (m_facturas) {
        disconnect(m_facturas, nullptr, this, nullptr);
    }
    m_facturas = facturas;
    if (!facturas) {
        return;
    }
    connect(facturas, &FacturasViewModel::registrarEFirmaSolicitado, this,
            [this](const QString& perfilId) { mostrarPerfilSeleccionado(perfilId); });
    connect(facturas, &FacturasViewModel::detallesTecnicosSolicitados, this,
            [this](const QString&, int, int, const QString& solicitudId) { abrirDiagnosticoDe(solicitudId); });
    if (m_pagina == Pagina::Facturas) {
        facturas->activar();
    }
}

void AppViewModel::setResolucionDestinos(ResolucionDestinoNotificacion* resolucion)
{
    m_resolucion = resolucion;
}

void AppViewModel::abrirFacturasMes(const QString& perfilId, int anio, int mes, bool reintentar)
{
    if (!m_facturas) {
        return;
    }
    mostrarFacturas();
    m_facturas->irAMes(perfilId, anio, mes,
                       reintentar ? FacturasViewModel::AlLlegar::Reintentar : FacturasViewModel::AlLlegar::Nada);
}

// ---- Notificaciones (T014.4, T023.5) ----

void AppViewModel::abrirDestino(const DestinoNavegacion& destino)
{
    switch (destino.tipo) {
    case DestinoNavegacion::Tipo::FacturasMes:
        if (destino.mes && m_facturas) {
            abrirFacturasMes(destino.perfil.texto(), destino.mes->anio(), destino.mes->mes());
            return;
        }
        break;
    case DestinoNavegacion::Tipo::Diagnostico:
        navegarADetalleConRespaldo(destino.solicitud.texto(), QString(), Pagina::Facturas);
        return;
    case DestinoNavegacion::Tipo::Facturas:
        break;
    }
    if (m_facturas) {
        mostrarFacturas();
    } else {
        irARaiz(Pagina::Diagnostico); // sin pantalla Facturas (pruebas, shell)
    }
}

void AppViewModel::abrirSolicitudNotificada(const QString& id)
{
    const std::optional<SolicitudId> solicitud = SolicitudId::desdeTexto(id);
    if (!solicitud) {
        abrirDestino(DestinoNavegacion::facturas());
        return;
    }
    if (!m_resolucion) {
        abrirDestino(DestinoNavegacion::diagnostico(*solicitud));
        return;
    }
    const quint64 generacion = ++m_genDestino;
    m_resolucion->resolver(*solicitud).then(this, [this, generacion](const DestinoNavegacion& destino) {
        if (generacion == m_genDestino) {
            abrirDestino(destino);
        }
    });
}

void AppViewModel::navegarADetalleConRespaldo(const QString& id, const QString& avisoFinder, Pagina respaldo)
{
    if (respaldo == Pagina::Facturas && !m_facturas) {
        respaldo = Pagina::Diagnostico;
    }
    if (!abrirDetalle(id)) {
        if (respaldo == Pagina::Facturas) {
            mostrarFacturas();
        } else {
            mostrarDiagnostico();
        }
        if (!avisoFinder.isEmpty()) {
            setMensajeFinder(avisoFinder);
        }
        return;
    }
    if (respaldo == Pagina::Facturas) {
        // Notificacion con destino Diagnostico: el origen es Facturas (raiz),
        // no la pagina donde estaba el usuario.
        m_origenes = {Pagina::Facturas};
        emit paginaChanged();
    }
    if (!avisoFinder.isEmpty() && m_detalle) {
        m_detalle->mostrarAvisoFinder(avisoFinder);
    }
    m_detalleConRespaldoLista = id;
    m_avisoFinderPendiente = avisoFinder;
    m_respaldo = respaldo;
    alCambiarEstadoDetalle(); // la carga pudo resolverse sin esperar
}

void AppViewModel::alCambiarEstadoDetalle()
{
    using Estado = SolicitudDetailViewModel::Estado;
    // T015.3: la solicitud del explorador ya no existe -> detalle "no encontrada".
    if (m_pagina == Pagina::FacturasSolicitud && m_detalle && m_detalle->estado() == Estado::NoEncontrada
        && m_detalle->solicitudId() == m_solicitudSeleccionadaId) {
        regresar();
    }
    if (m_detalleConRespaldoLista.isEmpty() || !m_detalle) {
        return;
    }
    const Estado estado = m_detalle->estado();
    if (estado == Estado::Cargando || estado == Estado::Ninguno) {
        return;
    }
    const QString id = std::exchange(m_detalleConRespaldoLista, QString());
    const QString aviso = std::exchange(m_avisoFinderPendiente, QString());
    if (estado != Estado::NoEncontrada || m_pagina != Pagina::Detalle || m_solicitudSeleccionadaId != id
        || m_detalle->solicitudId() != id) {
        return; // cargo (o fallo la lectura) o el usuario ya navego a otro lado
    }
    if (m_respaldo == Pagina::Facturas && m_facturas) {
        mostrarFacturas();
    } else {
        irARaiz(Pagina::Diagnostico);
    }
    if (!aviso.isEmpty()) {
        setMensajeFinder(aviso);
    }
}

void AppViewModel::mostrarPerfilSeleccionado(const QString& perfilId)
{
    if (m_pagina == Pagina::Perfiles && m_eFirma) {
        m_eFirma->abandonar(); // la captura en curso era de otra seleccion
    }
    mostrarPerfiles();
    if (m_perfiles) {
        m_perfiles->seleccionarAlCargar(perfilId);
    }
}

void AppViewModel::mostrarCarpetaSolicitudEnFinder(const QString& id)
{
    const std::optional<SolicitudId> solicitud = SolicitudId::desdeTexto(id);
    if (!solicitud || !m_finder) {
        return;
    }
    const quint64 generacion = ++m_genFinderSolicitud;
    m_finder->abrirCarpetaSolicitud(*solicitud).then(this, [this, generacion, id](const ResultadoAccionFinder& r) {
        if (generacion != m_genFinderSolicitud || r.estado == ResultadoAccionFinder::Estado::Mostrado) {
            return;
        }
        // Accion tecnica: el aviso se muestra en el detalle o, si la
        // solicitud ya no existe, en Diagnostico.
        navegarADetalleConRespaldo(id, r.mensaje, Pagina::Diagnostico);
        emit avisoFinderSolicitudPresentado();
    });
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
    if (m_pagina == Pagina::FacturasSolicitud && pagina != Pagina::FacturasSolicitud && m_explorador) {
        m_explorador->desactivar();
    }
    if (m_pagina == Pagina::Facturas && pagina != Pagina::Facturas && m_facturas) {
        m_facturas->desactivar();
    }
    if (m_pagina == Pagina::Historial && pagina != Pagina::Historial && m_historial) {
        m_historial->desactivar();
    }
    const bool entraFacturas = m_pagina != Pagina::Facturas && pagina == Pagina::Facturas;
    const bool entraHistorial = m_pagina != Pagina::Historial && pagina == Pagina::Historial;
    const bool entraRespaldo = m_pagina != Pagina::Respaldo && pagina == Pagina::Respaldo;
    if (m_pagina != pagina) {
        m_pagina = pagina;
        emit paginaChanged();
    }
    if (entraFacturas && m_facturas) {
        m_facturas->activar();
    }
    if (entraHistorial && m_historial) {
        m_historial->activar();
    }
    if (entraRespaldo && m_respaldoVm) {
        m_respaldoVm->activar();
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
