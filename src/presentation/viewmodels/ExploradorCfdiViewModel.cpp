#include "ExploradorCfdiViewModel.h"

#include "AccionesFinder.h"
#include "CfdiListModel.h"
#include "ConsultaCatalogoCfdi.h"

#include "domain/cfdi/TipoComprobante.h"

#include <QVariantMap>

#include <utility>

namespace satcfdi {

namespace {

bool mismoFiltro(const FiltroListaCfdi& a, const FiltroListaCfdi& b)
{
    return a.texto == b.texto && a.tipo == b.tipo && a.orden == b.orden && a.limite == b.limite;
}

// "1 paquete" / "N paquetes" con el resto del texto en singular o plural.
QString conteo(int n, const QString& singular, const QString& plural)
{
    return n == 1 ? singular : plural.arg(n);
}

QVariantMap linea(const QString& clave, int n, const QString& texto, bool accionCarpeta)
{
    return {
        {QStringLiteral("clave"), clave},
        {QStringLiteral("conteo"), n},
        {QStringLiteral("texto"), texto},
        {QStringLiteral("accionCarpeta"), accionCarpeta},
    };
}

} // namespace

template <typename F>
void ExploradorCfdiViewModel::notificar(F&& cambio)
{
    if (m_profundidad > 0) {
        std::forward<F>(cambio)(); // anidado: notifica el externo
        return;
    }
    ++m_profundidad;
    const Estado estadoAntes = estado();
    const QString vistaAntes = vista();
    const bool actualizandoAntes = actualizando();
    std::forward<F>(cambio)();
    --m_profundidad;
    if (estado() != estadoAntes) {
        emit estadoChanged();
    }
    if (vista() != vistaAntes) {
        emit vistaChanged();
    }
    if (actualizando() != actualizandoAntes) {
        emit actualizandoChanged();
    }
}

ExploradorCfdiViewModel::ExploradorCfdiViewModel(QObject* parent)
    : QObject(parent)
    , m_modelo(new CfdiListModel(this))
{
    m_retardo.setSingleShot(true);
    m_retardo.setInterval(kIntervaloBusquedaMs);
    connect(&m_retardo, &QTimer::timeout, this, [this] { notificar([this] { consultarLista(); }); });
    // "lista" frente a los estados vacios depende del numero de filas.
    connect(m_modelo, &CfdiListModel::countChanged, this, [this] { notificar([] {}); });
}

void ExploradorCfdiViewModel::setConsulta(ConsultaCatalogoCfdi* consulta)
{
    if (m_consulta) {
        disconnect(m_consulta, nullptr, this, nullptr);
    }
    m_consulta = consulta;
    if (consulta) {
        connect(consulta, &ConsultaCatalogoCfdi::indiceCambiado, this, &ExploradorCfdiViewModel::alCambiarIndice);
        connect(consulta, &ConsultaCatalogoCfdi::actividadCambiada, this,
                &ExploradorCfdiViewModel::alCambiarActividad);
    }
}

void ExploradorCfdiViewModel::setAccionesFinder(AccionesFinder* acciones)
{
    m_finder = acciones;
    emit mensajeFinderChanged();
}

QString ExploradorCfdiViewModel::solicitudId() const
{
    return m_solicitud ? m_solicitud->texto() : QString();
}

ExploradorCfdiViewModel::Estado ExploradorCfdiViewModel::estado() const
{
    if (!m_solicitud) {
        return Estado::Ninguno;
    }
    if (m_errorLista || m_errorResumen) {
        return Estado::Error;
    }
    if (!m_listaConocida || !m_resumen) {
        return Estado::Cargando;
    }
    return Estado::ConDatos;
}

QString ExploradorCfdiViewModel::vista() const
{
    switch (estado()) {
    case Estado::Ninguno:
        return {};
    case Estado::Error:
        return QStringLiteral("error");
    case Estado::Cargando:
        return QStringLiteral("cargando");
    case Estado::ConDatos:
        break;
    }
    if (m_modelo->count() > 0) {
        return QStringLiteral("lista");
    }
    if (m_listaConFiltros) {
        return QStringLiteral("sinCoincidencias");
    }
    if (indexando()) {
        return QStringLiteral("indexando");
    }
    return QStringLiteral("sinFacturas");
}

QString ExploradorCfdiViewModel::errorMessage() const
{
    return estado() == Estado::Error ? tr("No se pudo leer la lista de facturas.") : QString();
}

void ExploradorCfdiViewModel::setTexto(const QString& texto)
{
    if (m_texto == texto) {
        return;
    }
    m_texto = texto;
    emit filtrosChanged();
    if (m_solicitud && m_activo) {
        m_retardo.start(); // debounce: reinicia el retardo
    }
}

void ExploradorCfdiViewModel::setTipo(const QString& tipo)
{
    if (m_tipo == tipo) {
        return;
    }
    m_tipo = tipo;
    emit filtrosChanged();
    if (m_solicitud && m_activo) {
        m_retardo.stop(); // la consulta incluye ya el texto actual
        notificar([this] { consultarLista(); });
    }
}

bool ExploradorCfdiViewModel::hayFiltros() const
{
    return !m_texto.trimmed().isEmpty() || !m_tipo.isEmpty();
}

QVariantList ExploradorCfdiViewModel::lineasEstado() const
{
    QVariantList r;
    if (!m_resumen) {
        return r;
    }
    const ResumenIndiceSolicitud& s = *m_resumen;
    if (s.indexando && s.paquetesPendientes > 0) {
        r.append(linea(QStringLiteral("indexando"), s.paquetesPendientes,
                       conteo(s.paquetesPendientes, tr("Indexando 1 paquete…"), tr("Indexando %1 paquetes…")), false));
    }
    if (s.paquetesNoDisponibles > 0) {
        r.append(linea(QStringLiteral("noDisponibles"), s.paquetesNoDisponibles,
                       conteo(s.paquetesNoDisponibles, tr("1 paquete no disponible (ZIP faltante)"),
                              tr("%1 paquetes no disponibles (ZIP faltante)")),
                       true));
    }
    if (s.paquetesIlegibles > 0) {
        r.append(linea(QStringLiteral("ilegibles"), s.paquetesIlegibles,
                       conteo(s.paquetesIlegibles, tr("1 paquete ilegible"), tr("%1 paquetes ilegibles")), true));
    }
    const int cfdiConProblema = s.cfdiIlegibles + s.cfdiNoSoportados;
    if (cfdiConProblema > 0) {
        r.append(linea(QStringLiteral("cfdiIlegibles"), cfdiConProblema,
                       conteo(cfdiConProblema, tr("1 CFDI ilegible o no soportado"),
                              tr("%1 CFDI ilegibles o no soportados")),
                       true));
    }
    return r;
}

bool ExploradorCfdiViewModel::abrir(const QString& id)
{
    const std::optional<SolicitudId> solicitud = SolicitudId::desdeTexto(id);
    if (!solicitud) {
        return false;
    }
    notificar([&] {
        const bool otra = !m_solicitud || *m_solicitud != *solicitud;
        if (otra) {
            m_solicitud = solicitud;
            m_modelo->limpiar();
            m_listaConocida = false;
            m_listaConFiltros = false;
            m_resumen.reset();
            const bool conFiltros = !m_texto.isEmpty() || !m_tipo.isEmpty();
            m_texto.clear();
            m_tipo.clear();
            if (conFiltros) {
                emit filtrosChanged();
            }
            emit resumenChanged();
            emit solicitudIdChanged();
        }
        setMensajeFinder(QString());
        ++m_genFinder;
        m_activo = true;
        m_errorLista = false;
        m_errorResumen = false;
        m_retardo.stop();
        m_filtroSolicitado.reset();
        consultarLista();
        consultarResumen();
    });
    if (m_consulta) {
        // (c) revalida huellas; los cambios llegan con indiceCambiado.
        m_consulta->revisarSolicitud(*solicitud);
    }
    return true;
}

void ExploradorCfdiViewModel::recargar()
{
    if (!m_solicitud) {
        return;
    }
    notificar([this] {
        m_activo = true;
        m_errorLista = false;
        m_errorResumen = false;
        m_listaConocida = false; // Reintentar: de vuelta a Cargando
        m_resumen.reset();
        emit resumenChanged();
        m_retardo.stop();
        m_filtroSolicitado.reset();
        consultarLista();
        consultarResumen();
    });
}

void ExploradorCfdiViewModel::limpiarFiltros()
{
    if (m_texto.isEmpty() && m_tipo.isEmpty()) {
        return;
    }
    m_texto.clear();
    m_tipo.clear();
    emit filtrosChanged();
    m_retardo.stop();
    if (m_solicitud && m_activo) {
        notificar([this] { consultarLista(); });
    }
}

void ExploradorCfdiViewModel::abrirCarpetaSolicitud()
{
    if (!puedeAbrirCarpeta()) {
        return;
    }
    const quint64 generacion = ++m_genFinder;
    setMensajeFinder(QString());
    auto aplicar = [this, generacion](const ResultadoAccionFinder& r) {
        if (generacion == m_genFinder) {
            setMensajeFinder(r.estado == ResultadoAccionFinder::Estado::Mostrado ? QString() : r.mensaje);
        }
    };
    m_finder->abrirCarpetaSolicitud(*m_solicitud).then(this, aplicar);
}

bool ExploradorCfdiViewModel::previsualizar(int fila)
{
    const FilaCfdi* f = m_modelo->filaEn(fila);
    if (!f || !m_solicitud) {
        return false;
    }
    emit previsualizacionSolicitada(m_solicitud->texto(), f->paqueteId, f->posicion);
    return true;
}

void ExploradorCfdiViewModel::desactivar()
{
    notificar([this] {
        m_activo = false;
        m_retardo.stop();
        ++m_generacion;
        m_genListaPendiente = 0;
        m_genResumenPendiente = 0;
        m_filtroSolicitado.reset();
        ++m_genFinder;
    });
}

FiltroListaCfdi ExploradorCfdiViewModel::filtroActual() const
{
    FiltroListaCfdi f;
    f.texto = m_texto.trimmed();
    if (!m_tipo.isEmpty()) {
        const TipoComprobante tipo = tipoComprobanteDesdeTexto(m_tipo);
        if (tipo != TipoComprobante::Desconocido) {
            f.tipo = tipo;
        }
    }
    f.orden = OrdenListaCfdi::FechaDescendente;
    return f;
}

void ExploradorCfdiViewModel::consultarLista()
{
    if (!m_solicitud) {
        return;
    }
    const FiltroListaCfdi filtro = filtroActual();
    if (m_genListaPendiente == 0 && m_listaConocida && m_filtroSolicitado && mismoFiltro(*m_filtroSolicitado, filtro)) {
        return; // misma consulta ya aplicada (p. ej. espacios al final)
    }
    m_filtroSolicitado = filtro;
    if (!m_consulta) {
        m_genListaPendiente = 0;
        m_modelo->limpiar();
        m_listaConocida = true;
        m_listaConFiltros = !filtro.texto.isEmpty() || filtro.tipo.has_value();
        return;
    }
    const quint64 generacion = ++m_generacion;
    m_genListaPendiente = generacion;
    const bool conFiltros = !filtro.texto.isEmpty() || filtro.tipo.has_value();
    auto aplicar = [this, generacion, conFiltros](const std::optional<ConsultaCatalogoCfdi::ResultadoLista>& r) {
        if (generacion != m_genListaPendiente) {
            return; // respuesta tardia: la ultima gana
        }
        notificar([&] {
            m_genListaPendiente = 0;
            if (r && r->esExito()) {
                QList<FilaCfdi> filas;
                filas.reserve(r->valor().size());
                for (const CfdiListado& c : r->valor()) {
                    filas.append(FilaCfdi::desde(c));
                }
                m_modelo->reemplazar(std::move(filas));
                m_listaConocida = true;
                m_listaConFiltros = conFiltros;
                m_errorLista = false;
            } else {
                m_errorLista = true;
                m_filtroSolicitado.reset();
            }
        });
    };
    m_consulta->listar(*m_solicitud, filtro)
        .then(this, [aplicar](const ConsultaCatalogoCfdi::ResultadoLista& r) { aplicar(r); })
        .onCanceled(this, [aplicar] { aplicar(std::nullopt); })
        .onFailed(this, [aplicar] { aplicar(std::nullopt); });
}

void ExploradorCfdiViewModel::consultarResumen()
{
    if (!m_solicitud) {
        return;
    }
    if (!m_consulta) {
        m_genResumenPendiente = 0;
        m_resumen = ResumenIndiceSolicitud{};
        emit resumenChanged();
        return;
    }
    const quint64 generacion = ++m_generacion;
    m_genResumenPendiente = generacion;
    auto aplicar = [this, generacion](const std::optional<ConsultaCatalogoCfdi::ResultadoResumen>& r) {
        if (generacion != m_genResumenPendiente) {
            return;
        }
        notificar([&] {
            m_genResumenPendiente = 0;
            if (r && r->esExito()) {
                const bool cambio = !m_resumen || !(*m_resumen == r->valor());
                m_resumen = r->valor();
                m_errorResumen = false;
                if (cambio) {
                    emit resumenChanged();
                }
            } else {
                m_errorResumen = true;
            }
        });
    };
    m_consulta->resumen(*m_solicitud)
        .then(this, [aplicar](const ConsultaCatalogoCfdi::ResultadoResumen& r) { aplicar(r); })
        .onCanceled(this, [aplicar] { aplicar(std::nullopt); })
        .onFailed(this, [aplicar] { aplicar(std::nullopt); });
}

void ExploradorCfdiViewModel::refrescar()
{
    // Sin volver a Cargando: la lista visible se conserva hasta la respuesta.
    notificar([this] {
        m_filtroSolicitado.reset(); // fuerza la consulta aunque el filtro no cambio
        consultarLista();
        consultarResumen();
    });
}

void ExploradorCfdiViewModel::alCambiarIndice(const SolicitudId& id)
{
    if (m_activo && m_solicitud && *m_solicitud == id && estado() != Estado::Error) {
        refrescar();
    }
}

void ExploradorCfdiViewModel::alCambiarActividad()
{
    if (m_activo && m_solicitud && estado() != Estado::Error) {
        notificar([this] { consultarResumen(); });
    }
}

void ExploradorCfdiViewModel::setMensajeFinder(const QString& mensaje)
{
    if (m_mensajeFinder != mensaje) {
        m_mensajeFinder = mensaje;
        emit mensajeFinderChanged();
    }
}

} // namespace satcfdi
