#include "application/cfdi/CatalogoMensualCfdiService.h"

#include "application/cfdi/ProgresoIndexacionCfdi.h"
#include "application/persistence/PersistenceDispatcher.h"

#include <QFuture>
#include <QHash>

#include <algorithm>

#include <utility>

namespace satcfdi {

namespace {

bool tieneDescargados(const EvidenciaSolicitudMensual& e)
{
    for (const EvidenciaPaqueteMensual& p : e.paquetes) {
        if (p.estadoDescarga == EstadoDescarga::Descargado) {
            return true;
        }
    }
    return false;
}

} // namespace

CatalogoMensualCfdiService::CatalogoMensualCfdiService(PersistenceDispatcher& dispatcher,
                                                       CatalogoMensualRepository& catalogo,
                                                       LoteDescargaGuiadaRepository& lotes,
                                                       ProgresoIndexacionCfdi& indexacion, RelojUtc reloj,
                                                       QObject* parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
    , m_catalogo(catalogo)
    , m_lotes(lotes)
    , m_indexacion(indexacion)
    , m_reloj(std::move(reloj))
{
    Q_ASSERT(m_reloj);
}

QFuture<CatalogoMensualCfdiService::ResultadoCatalogo>
CatalogoMensualCfdiService::consultar(const ConsultaCatalogoMensual& consulta)
{
    if (consulta.perfil.esNulo()) {
        return QtFuture::makeReadyValueFuture(ResultadoCatalogo::exito({}));
    }
    CatalogoMensualRepository* catalogo = &m_catalogo;
    return m_dispatcher.despachar<ResultadoCatalogo>([catalogo, consulta] { return catalogo->consultar(consulta); });
}

namespace {

// Lectura del historial en UNA tarea del dispatcher (T023.5).
struct LecturaHistorial {
    QList<LoteGuiado> lotes;
    QList<SolicitudSinLote> anteriores;
    QHash<SolicitudId, int> conteos;
    // Evidencia por mes con lotes; ausente si su lectura fallo.
    QHash<QString, QList<EvidenciaSolicitudMensual>> evidencia;
};

} // namespace

QFuture<CatalogoMensualCfdiService::ResultadoEntradas> CatalogoMensualCfdiService::historial(const PerfilId& perfil)
{
    if (perfil.esNulo()) {
        return QtFuture::makeReadyValueFuture(ResultadoEntradas::exito({}));
    }
    using ResultadoLectura = Resultado<LecturaHistorial, ErrorPersistencia>;
    LoteDescargaGuiadaRepository* lotes = &m_lotes;
    CatalogoMensualRepository* catalogo = &m_catalogo;
    const int version = m_indexacion.versionParser();
    const QDateTime instante = m_reloj(); // D19: una vez, al iniciar
    return m_dispatcher
        .despachar<ResultadoLectura>([lotes, catalogo, perfil, version]() -> ResultadoLectura {
            LecturaHistorial l;
            auto rl = lotes->listar(perfil, std::nullopt);
            if (!rl) {
                return ResultadoLectura::fallo(std::move(rl).error());
            }
            l.lotes = std::move(rl).valor();
            auto ra = lotes->listarSinLote(perfil);
            if (!ra) {
                return ResultadoLectura::fallo(std::move(ra).error());
            }
            l.anteriores = std::move(ra).valor();
            auto rc = catalogo->conteosPorSolicitud(perfil);
            if (!rc) {
                return ResultadoLectura::fallo(std::move(rc).error());
            }
            l.conteos = std::move(rc).valor();
            for (const LoteGuiado& lote : std::as_const(l.lotes)) {
                const QString clave = lote.mes.texto();
                if (l.evidencia.contains(clave)) {
                    continue;
                }
                auto re = catalogo->leerEvidencia(perfil, lote.mes, version);
                if (re) {
                    l.evidencia.insert(clave, std::move(re).valor());
                }
            }
            return ResultadoLectura::exito(std::move(l));
        })
        .then(this, [this, perfil, instante](ResultadoLectura r) {
            if (!r) {
                return ResultadoEntradas::fallo(std::move(r).error());
            }
            LecturaHistorial l = std::move(r).valor();
            QHash<QString, EvaluacionCoberturaMensual> evaluaciones;
            for (auto it = l.evidencia.begin(); it != l.evidencia.end(); ++it) {
                const auto mes = MesCalendario::desdeTexto(it.key());
                auto e = evaluar(perfil, *mes, instante, ResultadoEvidencia::exito(std::move(it.value())));
                if (e) {
                    evaluaciones.insert(it.key(), std::move(e).valor());
                }
            }
            QList<EntradaHistorial> entradas;
            entradas.reserve(l.lotes.size() + l.anteriores.size());
            for (const LoteGuiado& lote : std::as_const(l.lotes)) {
                const auto it = evaluaciones.constFind(lote.mes.texto());
                entradas.append(entradaDeLote(lote, it != evaluaciones.cend() ? &it.value() : nullptr, l.conteos));
            }
            for (const SolicitudSinLote& s : std::as_const(l.anteriores)) {
                entradas.append(entradaAnterior(perfil, s, l.conteos));
            }
            // Mas reciente primero; desempate estable por id (lote o solicitud).
            auto id = [](const EntradaHistorial& e) {
                return e.tipo == EntradaHistorial::Tipo::Guiada
                           ? e.loteId
                           : (e.diagnosticoEmitidas ? e.diagnosticoEmitidas : e.diagnosticoRecibidas)->texto();
            };
            std::stable_sort(entradas.begin(), entradas.end(), [&](const EntradaHistorial& a, const EntradaHistorial& b) {
                if (a.creadoEn != b.creadoEn) {
                    return a.creadoEn > b.creadoEn;
                }
                return id(a) > id(b);
            });
            return ResultadoEntradas::exito(std::move(entradas));
        });
}

QFuture<CatalogoMensualCfdiService::ResultadoUbicacion> CatalogoMensualCfdiService::ubicar(const SolicitudId& solicitud)
{
    if (solicitud.esNulo()) {
        return QtFuture::makeReadyValueFuture(ResultadoUbicacion::exito(std::nullopt));
    }
    LoteDescargaGuiadaRepository* lotes = &m_lotes;
    return m_dispatcher.despachar<ResultadoUbicacion>([lotes, solicitud] { return lotes->ubicar(solicitud); });
}

QFuture<CatalogoMensualCfdiService::ResultadoLotes>
CatalogoMensualCfdiService::lotes(const PerfilId& perfil, const std::optional<MesCalendario>& mes)
{
    if (perfil.esNulo()) {
        return QtFuture::makeReadyValueFuture(ResultadoLotes::exito({}));
    }
    LoteDescargaGuiadaRepository* repositorio = &m_lotes;
    return m_dispatcher.despachar<ResultadoLotes>([repositorio, perfil, mes] { return repositorio->listar(perfil, mes); });
}

QFuture<CatalogoMensualCfdiService::ResultadoEvidencia> CatalogoMensualCfdiService::leerEvidencia(const PerfilId& perfil,
                                                                                                   MesCalendario mes)
{
    CatalogoMensualRepository* catalogo = &m_catalogo;
    const int version = m_indexacion.versionParser();
    return m_dispatcher.despachar<ResultadoEvidencia>(
        [catalogo, perfil, mes, version] { return catalogo->leerEvidencia(perfil, mes, version); });
}

CatalogoMensualCfdiService::ResultadoDisponibilidad
CatalogoMensualCfdiService::evaluar(const PerfilId& perfil, MesCalendario mes, const QDateTime& instante,
                                    ResultadoEvidencia evidencia) const
{
    if (!evidencia) {
        return ResultadoDisponibilidad::fallo(std::move(evidencia).error());
    }
    QList<EvidenciaSolicitudMensual> lista = std::move(evidencia).valor();
    // Hilo de este objeto: el estado del indexador vive aqui.
    for (EvidenciaSolicitudMensual& e : lista) {
        e.indexacionProgramada = m_indexacion.cubre(e.solicitud.id);
    }
    return ResultadoDisponibilidad::exito(evaluarCoberturaMensual(perfil, mes, instante, lista));
}

QFuture<CatalogoMensualCfdiService::ResultadoDisponibilidad>
CatalogoMensualCfdiService::disponibilidad(const PerfilId& perfil, MesCalendario mes, Revalidacion revalidacion)
{
    // D19: el instante se captura una vez, al iniciar la accion.
    const QDateTime instante = m_reloj();
    if (perfil.esNulo()) {
        return QtFuture::makeReadyValueFuture(
            ResultadoDisponibilidad::exito(evaluarCoberturaMensual(perfil, mes, instante, {})));
    }
    if (revalidacion == Revalidacion::No) {
        return leerEvidencia(perfil, mes).then(this, [this, perfil, mes, instante](ResultadoEvidencia r) {
            return evaluar(perfil, mes, instante, std::move(r));
        });
    }
    return leerEvidencia(perfil, mes)
        .then(this,
              [this, perfil, mes, instante](ResultadoEvidencia r) -> QFuture<ResultadoDisponibilidad> {
                  if (!r) {
                      return QtFuture::makeReadyValueFuture(evaluar(perfil, mes, instante, std::move(r)));
                  }
                  QList<QFuture<void>> revisiones;
                  for (const EvidenciaSolicitudMensual& e : r.valor()) {
                      if (tieneDescargados(e)) {
                          revisiones.append(m_indexacion.revisarSolicitud(e.solicitud.id));
                      }
                  }
                  if (revisiones.isEmpty()) {
                      return QtFuture::makeReadyValueFuture(evaluar(perfil, mes, instante, std::move(r)));
                  }
                  // Terminadas (o canceladas al detener el indexador), se relee
                  // la evidencia con el indice revalidado.
                  return QtFuture::whenAll(revisiones.begin(), revisiones.end())
                      .then(this, [this, perfil, mes](const QList<QFuture<void>>&) { return leerEvidencia(perfil, mes); })
                      .unwrap()
                      .then(this, [this, perfil, mes, instante](ResultadoEvidencia segunda) {
                          return evaluar(perfil, mes, instante, std::move(segunda));
                      });
              })
        .unwrap();
}

std::optional<MesCalendario> CatalogoMensualCfdiService::mesEnCurso() const
{
    return centro::mesEnCurso(m_reloj());
}

std::optional<MesCalendario> CatalogoMensualCfdiService::mesInicialPorOmision() const
{
    return centro::mesInicialPorOmision(m_reloj());
}

} // namespace satcfdi
