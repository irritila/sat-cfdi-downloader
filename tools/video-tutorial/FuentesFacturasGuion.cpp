#include "FuentesFacturasGuion.h"

#include "DatosDemo.h"

#include <algorithm>

namespace video {

FacturasGuion::FacturasGuion(const PerfilesGuion& perfiles, const CredencialesGuion& credenciales)
    : m_perfiles(perfiles)
    , m_credenciales(credenciales)
{
}

QFuture<FacturasGuion::ResultadoPerfiles> FacturasGuion::perfiles()
{
    QList<PerfilFacturas> lista;
    for (const PerfilResumen& p : m_perfiles.perfiles) {
        if (m_perfiles.ocultos.contains(p.id.texto()) || !p.activo) {
            continue;
        }
        const auto it = m_credenciales.resumenes.constFind(p.id.texto());
        const EstadoCredencial estado = it == m_credenciales.resumenes.cend() ? EstadoCredencial::SinCredencial
                                                                              : it->estado;
        lista.append(PerfilFacturas{p.id, p.rfc, p.nombre, estado});
    }
    return QtFuture::makeReadyValueFuture(ResultadoPerfiles::exito(lista));
}

QFuture<FacturasGuion::ResultadoCatalogo> FacturasGuion::consultar(const ConsultaCatalogoMensual&)
{
    return QtFuture::makeReadyValueFuture(ResultadoCatalogo::exito(CatalogoMensual{}));
}

QFuture<FacturasGuion::ResultadoDisponibilidad> FacturasGuion::disponibilidad(const PerfilId& perfil,
                                                                              MesCalendario mes, bool)
{
    return QtFuture::makeReadyValueFuture(
        ResultadoDisponibilidad::exito(evaluarCoberturaMensual(perfil, mes, m_ahora, {})));
}

QFuture<FacturasGuion::ResultadoPreplan> FacturasGuion::planificar(const PerfilId& perfil, MesCalendario mes,
                                                                    AccionGuiada accion)
{
    PreplanGuiado p;
    p.perfil = perfil;
    p.mes = mes;
    p.accion = accion;
    return QtFuture::makeReadyValueFuture(ResultadoPreplan::exito(p));
}

QFuture<FacturasGuion::ResultadoEjecucion> FacturasGuion::ejecutar(const PreplanGuiado&, const DecisionesGuiadas&)
{
    return QtFuture::makeReadyValueFuture(ResultadoEjecucion::exito({}));
}

HistorialGuion::HistorialGuion(const PerfilesGuion& perfiles, const SolicitudesGuion& solicitudes)
    : m_perfiles(perfiles)
    , m_solicitudes(solicitudes)
{
}

namespace {

ResultadoHistorial resultadoDe(const SolicitudResumen& r)
{
    if (r.paquetesDescargados > 0) {
        return ResultadoHistorial::Completa;
    }
    if (r.estadoLocal == EstadoLocal::EnvioFallido || r.estadoSat == EstadoSolicitudSat::Error
        || r.estadoSat == EstadoSolicitudSat::Rechazada || r.estadoSat == EstadoSolicitudSat::Vencida) {
        return ResultadoHistorial::RequiereAtencion;
    }
    return ResultadoHistorial::EnPreparacion;
}

} // namespace

// Mas reciente primero (creadaEn), como el historial real.
QFuture<HistorialGuion::ResultadoEntradas> HistorialGuion::historial(const PerfilId& perfil)
{
    QString rfc;
    for (const PerfilResumen& p : m_perfiles.perfiles) {
        if (p.id == perfil) {
            rfc = p.rfc;
        }
    }
    QList<EntradaHistorial> entradas;
    for (const SolicitudDetalle& d : m_solicitudes.detalles) {
        const SolicitudResumen& r = d.resumen;
        if (rfc.isEmpty() || r.perfilRfc != rfc || m_solicitudes.ocultas.contains(r.id.texto())) {
            continue;
        }
        EntradaHistorial e;
        e.tipo = EntradaHistorial::Tipo::Anterior;
        e.perfil = perfil;
        const std::optional<MesCalendario> mes = MesCalendario::de(r.fechaInicial);
        if (mes && MesCalendario::de(r.fechaFinal) == mes) {
            e.mes = mes;
        }
        e.desde = r.fechaInicial;
        e.hasta = r.fechaFinal;
        e.creadoEn = r.creadaEn;
        e.resultado = resultadoDe(r);
        const bool emitidos = r.tipoDescarga == TipoDescarga::Emitidos;
        e.tieneEmitidas = emitidos;
        e.tieneRecibidas = !emitidos;
        (emitidos ? e.diagnosticoEmitidas : e.diagnosticoRecibidas) = r.id;
        entradas.append(e);
    }
    std::stable_sort(entradas.begin(), entradas.end(),
                     [](const EntradaHistorial& a, const EntradaHistorial& b) { return a.creadoEn > b.creadoEn; });
    return QtFuture::makeReadyValueFuture(ResultadoEntradas::exito(entradas));
}

} // namespace video
