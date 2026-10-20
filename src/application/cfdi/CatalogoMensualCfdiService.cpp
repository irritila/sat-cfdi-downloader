#include "application/cfdi/CatalogoMensualCfdiService.h"

#include "application/cfdi/ProgresoIndexacionCfdi.h"
#include "application/persistence/PersistenceDispatcher.h"

#include <QFuture>

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
