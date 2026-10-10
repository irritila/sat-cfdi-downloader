#include "application/vencimiento/AvisoVencimientoEFirma.h"

#include "application/notificaciones/ServicioNotificaciones.h"
#include "application/operaciones/Programador.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "domain/credenciales/VencimientoEFirma.h"
#include "ports/repositories/AvisoVencimientoRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <QList>
#include <QLoggingCategory>
#include <QPointer>

#include <memory>

Q_LOGGING_CATEGORY(lcVencimiento, "satcfdi.vencimiento.efirma")

namespace satcfdi {

namespace {

constexpr qint64 kIntervaloSegundos = 24 * 3600;

struct Candidato {
    PerfilId perfil;
    QString rfc;
    QDateTime vigenteHasta;
    int umbral = 0;
    int dias = 0;
};

} // namespace

AvisoVencimientoEFirma::AvisoVencimientoEFirma(ListarPerfiles listarPerfiles, PersistenceDispatcher& dispatcher,
                                               AvisoVencimientoRepository& avisos, UnitOfWork& unidadDeTrabajo,
                                               ServicioNotificaciones& notificaciones, Programador& programador,
                                               RelojUtc reloj, QObject* parent)
    : QObject(parent)
    , m_listar(std::move(listarPerfiles))
    , m_dispatcher(dispatcher)
    , m_avisos(avisos)
    , m_unidadDeTrabajo(unidadDeTrabajo)
    , m_notificaciones(notificaciones)
    , m_programador(programador)
    , m_reloj(reloj ? std::move(reloj) : relojSistema())
{
}

AvisoVencimientoEFirma::~AvisoVencimientoEFirma()
{
    detener();
}

void AvisoVencimientoEFirma::iniciar()
{
    if (m_activo) {
        return;
    }
    m_activo = true;
    evaluar();
    programarSiguiente();
}

void AvisoVencimientoEFirma::detener()
{
    m_activo = false;
    if (m_programado) {
        m_programador.cancelar(*m_programado);
        m_programado.reset();
    }
}

void AvisoVencimientoEFirma::programarSiguiente()
{
    if (!m_activo) {
        return;
    }
    m_programado = m_programador.programar(m_reloj().addSecs(kIntervaloSegundos), [this]() {
        m_programado.reset();
        evaluar();
        programarSiguiente();
    });
}

void AvisoVencimientoEFirma::alCambiarCredencial()
{
    if (m_activo) {
        evaluar();
    }
}

QFuture<int> AvisoVencimientoEFirma::evaluar()
{
    const QDateTime ahora = m_reloj();
    PersistenceDispatcher* dispatcher = &m_dispatcher;
    AvisoVencimientoRepository* avisos = &m_avisos;
    UnitOfWork* uow = &m_unidadDeTrabajo;
    QPointer<AvisoVencimientoEFirma> yo(this);
    return m_listar()
        .then(this,
              [ahora, dispatcher, avisos, uow](ConsultaPreparacionPerfiles::ResultadoLista r) -> QFuture<QList<Candidato>> {
                  QList<Candidato> candidatos;
                  if (!r.esExito()) {
                      qCWarning(lcVencimiento, "no se pudo listar perfiles para evaluar vencimientos");
                      return QtFuture::makeReadyValueFuture(candidatos);
                  }
                  for (const PerfilConPreparacion& p : r.valor()) {
                      if (p.preparacion != PreparacionPerfil::Lista || !p.vigenteHasta) {
                          continue; // vencida, no vigente aun, sin credencial...: sin aviso (D3)
                      }
                      const auto dias = vencimientoefirma::diasParaVencer(*p.vigenteHasta, ahora);
                      if (!dias) {
                          continue;
                      }
                      candidatos.append(Candidato{p.perfil.id, p.perfil.rfc, *p.vigenteHasta,
                                                  vencimientoefirma::umbralPara(*dias), *dias});
                  }
                  if (candidatos.isEmpty()) {
                      return QtFuture::makeReadyValueFuture(candidatos);
                  }
                  // Dedupe persistido: una transaccion por candidato; solo los
                  // nuevos se notifican.
                  return dispatcher->despachar<QList<Candidato>>([candidatos, ahora, avisos, uow]() {
                      QList<Candidato> nuevos;
                      for (const Candidato& c : candidatos) {
                          if (!uow->begin()) {
                              continue;
                          }
                          auto r = avisos->registrarSiNuevo(c.perfil, c.vigenteHasta, c.umbral, ahora);
                          if (!r || !uow->commit()) {
                              (void)uow->rollback();
                              continue;
                          }
                          if (r.valor()) {
                              nuevos.append(c);
                          }
                      }
                      return nuevos;
                  });
              })
        .unwrap()
        .then(this, [yo](const QList<Candidato>& nuevos) {
            if (!yo) {
                return 0;
            }
            for (const Candidato& c : nuevos) {
                yo->m_notificaciones.notificarVencimiento(c.perfil, c.rfc, c.vigenteHasta, c.umbral, c.dias);
            }
            emit yo->evaluacionTerminada(static_cast<int>(nuevos.size()));
            return static_cast<int>(nuevos.size());
        })
        .onFailed(this, []() { return 0; })
        .onCanceled(this, []() { return 0; });
}

} // namespace satcfdi
