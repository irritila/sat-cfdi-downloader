#include "application/cfdi/DescargaGuiadaService.h"

#include "application/cfdi/ProgresoIndexacionCfdi.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "application/requests/CreacionSolicitudLocal.h"
#include "domain/common/UuidCanonico.h"
#include "domain/solicitudes/SolicitudCanonica.h"
#include "ports/repositories/CatalogoMensualRepository.h"
#include "ports/repositories/LoteDescargaGuiadaRepository.h"
#include "ports/repositories/PerfilSatRepository.h"
#include "ports/repositories/SolicitudMasivaRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <utility>

namespace satcfdi {

namespace {

using Tipo = ErrorDescargaGuiada::Tipo;
using ResultadoPreplan = DescargaGuiadaService::ResultadoPreplan;
using ResultadoEjecucion = DescargaGuiadaService::ResultadoEjecucion;

ErrorDescargaGuiada error(Tipo tipo, QString mensaje)
{
    ErrorDescargaGuiada e;
    e.tipo = tipo;
    e.mensaje = std::move(mensaje);
    return e;
}

ErrorDescargaGuiada errorPersistencia(ErrorPersistencia causa)
{
    ErrorDescargaGuiada e = error(Tipo::Persistencia, causa.mensaje);
    e.causa = std::move(causa);
    return e;
}

ErrorDescargaGuiada errorCreacion(ErrorCrear creacion)
{
    ErrorDescargaGuiada e = error(Tipo::Creacion, creacion.mensaje);
    e.creacion = std::move(creacion);
    return e;
}

ErrorDescargaGuiada errorEFirma(std::optional<EstadoCredencial> estado)
{
    ErrorDescargaGuiada e = error(Tipo::EFirmaNoLista, QStringLiteral("La e.firma del perfil no esta lista."));
    e.estadoCredencial = estado;
    return e;
}

// Datos comunes de planificar() y ejecutar(), capturados por copia.
struct Entrada {
    PerfilId perfil;
    MesCalendario mes;
    AccionGuiada accion;
    QDateTime instante;
    QList<SolicitudId> indexando;
    int versionParser;
};

// Lee el perfil y la evidencia, evalua, planifica y clasifica duplicados de
// los candidatos. Hilo del dispatcher; dentro o fuera de una transaccion.
ResultadoPreplan preparar(const PuertosPersistencia& p, CatalogoMensualRepository& catalogo, const Entrada& in)
{
    auto perfil = p.perfiles.obtener(in.perfil);
    if (!perfil) {
        return ResultadoPreplan::fallo(errorPersistencia(std::move(perfil).error()));
    }
    if (!perfil.valor() || !perfil.valor()->activo || perfil.valor()->eliminadoEn) {
        return ResultadoPreplan::fallo(
            error(Tipo::PerfilNoDisponible, QStringLiteral("El perfil no existe, esta eliminado o inactivo.")));
    }
    const QString rfc = perfil.valor()->rfc;
    auto evidencia = catalogo.leerEvidencia(in.perfil, in.mes, in.versionParser);
    if (!evidencia) {
        return ResultadoPreplan::fallo(errorPersistencia(std::move(evidencia).error()));
    }
    QList<EvidenciaSolicitudMensual> lista = std::move(evidencia).valor();
    for (EvidenciaSolicitudMensual& e : lista) {
        e.indexacionProgramada = in.indexando.contains(e.solicitud.id);
    }

    PreplanGuiado pre;
    pre.perfil = in.perfil;
    pre.mes = in.mes;
    pre.accion = in.accion;
    pre.instante = in.instante;
    pre.indexando = in.indexando;
    pre.evaluacion = evaluarCoberturaMensual(in.perfil, in.mes, in.instante, lista);
    pre.oferta = ofertaDescargaGuiada(pre.evaluacion);
    auto plan = planificarDescargaGuiada(pre.evaluacion, in.accion);
    if (!plan && plan.error() == ErrorPlanGuiado::MesFuturo) {
        return ResultadoPreplan::fallo(error(Tipo::MesFuturo, QStringLiteral("El mes aun no empieza.")));
    }
    if (!plan) {
        ErrorDescargaGuiada e = error(Tipo::AccionNoDisponible,
                                      QStringLiteral("La accion no esta disponible para este mes en este momento."));
        e.preplanActual = pre; // evaluacion y oferta vigentes, sin plan ni candidatos
        return ResultadoPreplan::fallo(std::move(e));
    }
    pre.plan = std::move(plan).valor();

    for (TipoDescarga d : {TipoDescarga::Emitidos, TipoDescarga::Recibidos}) {
        const PlanDireccion& pd = pre.plan.direccion(d);
        for (const EvidenciaSolicitudMensual& e : std::as_const(lista)) {
            if (pd.reutilizar.contains(e.solicitud.id) && e.solicitud.estadoLocal == EstadoLocal::Creada) {
                pre.reutilizadasSinEnviar.append(e.solicitud.id);
            }
        }
        for (const TramoMensual& t : pd.crear) {
            CandidatoGuiado c;
            c.direccion = d;
            c.fechaInicialSat = centro::fechaSat(t.inicio);
            c.fechaFinalSat = centro::fechaSat(t.fin);
            auto canonica = SolicitudCanonica::normalizarTramo(d, rfc, c.fechaInicialSat, c.fechaFinalSat);
            if (!canonica) {
                return ResultadoPreplan::fallo(errorCreacion(ErrorCrear::filtroInvalido(std::move(canonica).error())));
            }
            c.dedupKey = canonica.valor().dedupKey();
            c.canonica = std::move(canonica).valor();
            auto duplicado = p.solicitudes.clasificarDuplicado(c.dedupKey);
            if (!duplicado) {
                return ResultadoPreplan::fallo(errorPersistencia(std::move(duplicado).error()));
            }
            c.duplicado = std::move(duplicado).valor();
            pre.candidatos.append(std::move(c));
        }
    }
    return ResultadoPreplan::exito(std::move(pre));
}

// Mismo plan y misma clasificacion de duplicados.
bool mismaHuella(const PreplanGuiado& a, const PreplanGuiado& b)
{
    return a.plan == b.plan && a.candidatos == b.candidatos;
}

} // namespace

QList<TipoDescarga> PreplanGuiado::direccionesPorConfirmar() const
{
    QList<TipoDescarga> r;
    for (const CandidatoGuiado& c : candidatos) {
        if (c.duplicado.clasificacion == ClasificacionDuplicado::RequiereConfirmacion && !r.contains(c.direccion)) {
            r.append(c.direccion);
        }
    }
    return r;
}

bool PreplanGuiado::creaAlgo() const
{
    for (const CandidatoGuiado& c : candidatos) {
        if (c.duplicado.clasificacion != ClasificacionDuplicado::Bloqueado) {
            return true;
        }
    }
    return false;
}

DescargaGuiadaService::DescargaGuiadaService(PuertosDescargaGuiada puertos, QObject* parent)
    : QObject(parent)
    , m_p(std::move(puertos))
{
    Q_ASSERT(m_p.reloj && m_p.estadoCredencial && m_p.encolarEnvio);
}

QFuture<DescargaGuiadaService::ResultadoPreplan>
DescargaGuiadaService::planificar(const PerfilId& perfil, MesCalendario mes, AccionGuiada accion)
{
    // D19: el instante de la accion se captura una vez, aqui.
    Entrada in{perfil, mes, accion, m_p.reloj(), {}, m_p.indexacion.versionParser()};
    if (perfil.esNulo()) {
        return QtFuture::makeReadyValueFuture(ResultadoPreplan::fallo(
            error(Tipo::PerfilNoDisponible, QStringLiteral("El perfil no existe, esta eliminado o inactivo."))));
    }
    CatalogoMensualRepository* catalogo = &m_p.catalogo;
    return m_p.estadoCredencial(perfil)
        .then(this,
              [this, in, catalogo](std::optional<EstadoCredencial> estado) -> QFuture<ResultadoPreplan> {
                  if (estado != EstadoCredencial::Lista) {
                      return QtFuture::makeReadyValueFuture(ResultadoPreplan::fallo(errorEFirma(estado)));
                  }
                  // 1. Solicitudes del mes, para tomar del indexador (en su
                  //    hilo, este) cuales tienen indexacion programada.
                  using Ids = Resultado<QList<SolicitudId>, ErrorPersistencia>;
                  return m_p.dispatcher
                      .despachar<Ids>([catalogo, in]() -> Ids {
                          auto e = catalogo->leerEvidencia(in.perfil, in.mes, in.versionParser);
                          if (!e) {
                              return Ids::fallo(std::move(e).error());
                          }
                          QList<SolicitudId> ids;
                          for (const EvidenciaSolicitudMensual& s : e.valor()) {
                              ids.append(s.solicitud.id);
                          }
                          return Ids::exito(ids);
                      })
                      .then(this, [this, in, catalogo](Ids ids) -> QFuture<ResultadoPreplan> {
                          if (!ids) {
                              return QtFuture::makeReadyValueFuture(
                                  ResultadoPreplan::fallo(errorPersistencia(std::move(ids).error())));
                          }
                          Entrada conIndice = in;
                          for (const SolicitudId& id : ids.valor()) {
                              if (m_p.indexacion.cubre(id)) {
                                  conIndice.indexando.append(id);
                              }
                          }
                          // 2. Plan y duplicados (solo lectura).
                          const PuertosPersistencia p = m_p.persistencia;
                          return m_p.dispatcher.despachar<ResultadoPreplan>(
                              [p, catalogo, conIndice] { return preparar(p, *catalogo, conIndice); });
                      })
                      .unwrap();
              })
        .unwrap();
}

QFuture<DescargaGuiadaService::ResultadoEjecucion>
DescargaGuiadaService::ejecutar(const PreplanGuiado& preplan, const DecisionesGuiadas& decisiones)
{
    const QDateTime ahora = m_p.reloj(); // creada_en de las solicitudes y del lote
    CatalogoMensualRepository* catalogo = &m_p.catalogo;
    LoteDescargaGuiadaRepository* lotes = &m_p.lotes;
    const PuertosPersistencia p = m_p.persistencia;
    const Entrada in{preplan.perfil, preplan.mes, preplan.accion, preplan.instante, preplan.indexando,
                     m_p.indexacion.versionParser()};

    auto tarea = [p, catalogo, lotes, in, preplan, decisiones, ahora]() -> ResultadoEjecucion {
        using R = ResultadoEjecucion;
        auto begin = p.unidadDeTrabajo.begin();
        if (!begin) {
            return R::fallo(errorPersistencia(std::move(begin).error()));
        }
        auto abortar = [&p](ErrorDescargaGuiada e) {
            (void)p.unidadDeTrabajo.rollback(); // prevalece el error original
            return R::fallo(std::move(e));
        };

        // 1. Plan autoritativo bajo BEGIN IMMEDIATE, con el MISMO instante.
        auto actual = preparar(p, *catalogo, in);
        if (!actual) {
            return abortar(std::move(actual).error());
        }
        const PreplanGuiado& nuevo = actual.valor();
        if (!mismaHuella(nuevo, preplan)) {
            ErrorDescargaGuiada e = error(Tipo::PlanDesactualizado,
                                          QStringLiteral("La evidencia del mes cambio; revisa el plan de nuevo."));
            e.preplanActual = nuevo;
            return abortar(std::move(e));
        }
        QList<TipoDescarga> pendientes;
        for (TipoDescarga d : nuevo.direccionesPorConfirmar()) {
            if (!decisiones.de(d)) {
                pendientes.append(d);
            }
        }
        if (!pendientes.isEmpty()) {
            ErrorDescargaGuiada e = error(Tipo::ConfirmacionPendiente,
                                          QStringLiteral("Falta confirmar una solicitud equivalente."));
            e.direcciones = pendientes;
            return abortar(std::move(e));
        }

        // 2. Solicitudes nuevas.
        EjecucionGuiada ej;
        QList<TramoLoteGuiado> tramos;
        const QList<TipoDescarga> confirmables = nuevo.direccionesPorConfirmar();
        for (const CandidatoGuiado& c : nuevo.candidatos) {
            if (c.duplicado.clasificacion == ClasificacionDuplicado::Bloqueado) {
                // Equivalente existente: se reutiliza, no se crea otra.
                if (c.duplicado.solicitudReferencia && !ej.reutilizadas.contains(*c.duplicado.solicitudReferencia)) {
                    ej.reutilizadas.append(*c.duplicado.solicitudReferencia);
                }
                continue;
            }
            if (confirmables.contains(c.direccion)
                && decisiones.de(c.direccion) == DecisionConfirmacion::Cancelar) {
                if (!ej.canceladas.contains(c.direccion)) {
                    ej.canceladas.append(c.direccion);
                }
                continue; // cancelar no crea nada para esta direccion
            }
            const SolicitudId id = SolicitudId::generar();
            const ConfirmacionDuplicado confirmacion =
                c.duplicado.clasificacion == ClasificacionDuplicado::RequiereConfirmacion
                    ? ConfirmacionDuplicado::Confirmada
                    : ConfirmacionDuplicado::SinConfirmar;
            auto creada = crearSolicitudLocalEnTransaccion(p, id, in.perfil, *c.canonica, confirmacion, ahora);
            if (!creada) {
                return abortar(errorCreacion(std::move(creada).error()));
            }
            ej.creadas.append(id);
            tramos.append(TramoLoteGuiado{c.direccion, id, c.fechaInicialSat, c.fechaFinalSat});
        }
        for (TipoDescarga d : {TipoDescarga::Emitidos, TipoDescarga::Recibidos}) {
            for (const SolicitudId& id : nuevo.plan.direccion(d).reutilizar) {
                if (!ej.reutilizadas.contains(id)) {
                    ej.reutilizadas.append(id);
                }
            }
        }

        // 3. Lote solo con las solicitudes NUEVAS (una reutilizada puede
        //    pertenecer ya a otro lote).
        if (!tramos.isEmpty()) {
            NuevoLoteGuiado lote;
            lote.id = uuid::generarCanonico();
            lote.perfil = in.perfil;
            lote.mes = in.mes;
            lote.creadoEn = ahora;
            lote.tramos = tramos;
            auto insertado = lotes->insertar(lote);
            if (!insertado) {
                return abortar(errorPersistencia(std::move(insertado).error()));
            }
            ej.loteId = lote.id;
        }
        auto commit = p.unidadDeTrabajo.commit();
        if (!commit) {
            return abortar(errorPersistencia(std::move(commit).error()));
        }
        ej.encoladas = ej.creadas;
        for (const SolicitudId& id : nuevo.reutilizadasSinEnviar) {
            if (!ej.encoladas.contains(id)) {
                ej.encoladas.append(id);
            }
        }
        return R::exito(std::move(ej));
    };

    const PerfilId perfil = preplan.perfil;
    const QString mes = preplan.mes.texto();
    return m_p.estadoCredencial(perfil)
        .then(this,
              [this, tarea](std::optional<EstadoCredencial> estado) -> QFuture<ResultadoEjecucion> {
                  if (estado != EstadoCredencial::Lista) {
                      return QtFuture::makeReadyValueFuture(ResultadoEjecucion::fallo(errorEFirma(estado)));
                  }
                  return m_p.dispatcher.despachar<ResultadoEjecucion>(tarea);
              })
        .unwrap()
        .then(this, [this, perfil, mes](ResultadoEjecucion r) {
            if (r) {
                for (const SolicitudId& id : r.valor().encoladas) {
                    m_p.encolarEnvio(id);
                }
                if (!r.valor().creadas.isEmpty()) {
                    emit mesActualizado(perfil, mes);
                }
            }
            return r;
        });
}

} // namespace satcfdi
