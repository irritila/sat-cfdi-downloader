#include "application/estadoagregado/MonitorEstadoAgregado.h"

#include "application/operaciones/WorkerLocal.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "application/profiles/ConsultaPreparacionPerfiles.h"
#include "domain/paquetes/PaquetePersistido.h"
#include "domain/solicitudes/SolicitudPersistida.h"
#include "ports/repositories/PaqueteSolicitudRepository.h"
#include "ports/repositories/SolicitudMasivaRepository.h"

namespace satcfdi {

QFuture<std::optional<bool>> consultarAtencionSolicitudes(PersistenceDispatcher& dispatcher,
                                                          SolicitudMasivaRepository& solicitudes,
                                                          PaqueteSolicitudRepository& paquetes)
{
    SolicitudMasivaRepository* s = &solicitudes;
    PaqueteSolicitudRepository* p = &paquetes;
    return dispatcher.despachar<std::optional<bool>>([s, p]() -> std::optional<bool> {
        auto filas = s->listarVisibles();
        if (!filas.esExito()) {
            return std::nullopt;
        }
        for (const SolicitudPersistida& fila : filas.valor()) {
            if (solicitudRequiereAtencion(derivarEstadoResumen(fila.estadoLocal, fila.estadoSolicitudSat))) {
                return true;
            }
        }
        auto conteos = p->contarVisiblesPorSolicitudYEstado();
        if (!conteos.esExito()) {
            return std::nullopt;
        }
        for (auto it = conteos.valor().cbegin(); it != conteos.valor().cend(); ++it) {
            if (it.value().pendientesDescarga <= 0) {
                continue; // sin Disponible ni Error: no puede haber Error
            }
            auto lista = p->listarVisiblesPorSolicitud(it.key());
            if (!lista.esExito()) {
                return std::nullopt;
            }
            for (const PaquetePersistido& paquete : lista.valor()) {
                if (paqueteRequiereAtencion(paquete.estadoDescarga)) {
                    return true;
                }
            }
        }
        return false;
    });
}

QFuture<std::optional<bool>> consultarAtencionPerfiles(ConsultaPreparacionPerfiles& consulta)
{
    return consulta.listarVerificados().then([](const ConsultaPreparacionPerfiles::ResultadoLista& r)
                                                 -> std::optional<bool> {
        if (!r.esExito()) {
            return std::nullopt;
        }
        for (const PerfilConPreparacion& p : r.valor()) {
            if (perfilRequiereAtencion(p)) {
                return true;
            }
        }
        return false;
    });
}

MonitorEstadoAgregado::MonitorEstadoAgregado(Consulta solicitudes, Consulta perfiles, QObject* parent)
    : QObject(parent)
{
    m_solicitudes.consulta = std::move(solicitudes);
    m_solicitudes.campo = &EntradasEstadoAgregado::solicitudesRequierenAtencion;
    m_perfiles.consulta = std::move(perfiles);
    m_perfiles.campo = &EntradasEstadoAgregado::perfilesRequierenAtencion;
}

void MonitorEstadoAgregado::iniciar()
{
    if (m_iniciado || m_detenido) {
        return;
    }
    m_iniciado = true;
    publicar();
    lanzar(m_solicitudes);
    lanzar(m_perfiles);
}

void MonitorEstadoAgregado::detener()
{
    m_detenido = true;
}

void MonitorEstadoAgregado::alCambiarWorker(const InstantaneaWorker& instantanea)
{
    if (m_detenido) {
        return;
    }
    m_entradas.trabajando = instantanea.estado == EstadoWorker::Ejecutando;
    m_entradas.pausado = instantanea.estado == EstadoWorker::Pausado;
    if (m_iniciado) {
        publicar();
    }
}

void MonitorEstadoAgregado::refrescarSolicitudes()
{
    refrescar(m_solicitudes);
}

void MonitorEstadoAgregado::refrescarPerfiles()
{
    refrescar(m_perfiles);
}

void MonitorEstadoAgregado::refrescar(Fuente& fuente)
{
    if (!m_iniciado || m_detenido) {
        return; // iniciar() lanza la primera consulta
    }
    if (fuente.enCurso) {
        fuente.sucia = true;
        return;
    }
    lanzar(fuente);
}

void MonitorEstadoAgregado::lanzar(Fuente& fuente)
{
    if (!fuente.consulta) {
        return;
    }
    fuente.enCurso = true;
    fuente.sucia = false;
    Fuente* f = &fuente;
    QFuture<std::optional<bool>> futuro;
    try {
        futuro = fuente.consulta();
    } catch (...) {
        terminar(fuente, std::nullopt);
        return;
    }
    futuro.then(this, [this, f](const std::optional<bool>& valor) { terminar(*f, valor); })
        .onFailed(this, [this, f] { terminar(*f, std::nullopt); })
        .onCanceled(this, [this, f] { terminar(*f, std::nullopt); });
}

void MonitorEstadoAgregado::terminar(Fuente& fuente, std::optional<bool> valor)
{
    fuente.enCurso = false;
    if (m_detenido) {
        return;
    }
    if (valor) {
        m_entradas.*(fuente.campo) = *valor;
        publicar();
    }
    if (fuente.sucia) {
        lanzar(fuente);
    }
}

void MonitorEstadoAgregado::publicar()
{
    const EstadoAgregado actual = estado();
    if (m_publicado && *m_publicado == actual) {
        return;
    }
    m_publicado = actual;
    emit estadoCambiado(actual);
}

} // namespace satcfdi
