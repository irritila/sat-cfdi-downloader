#include "AccesoFinder.h"

#include "application/operaciones/TiposOperacion.h"
#include "application/paquetes/AccesoPaquetesService.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "domain/paquetes/PaquetePersistido.h"
#include "ports/repositories/PaqueteSolicitudRepository.h"

#include <QUuid>

namespace satcfdi {

namespace {

ResultadoAccionFinder resultado(ResultadoAccionFinder::Estado estado, QString mensaje = {})
{
    return ResultadoAccionFinder{estado, std::move(mensaje)};
}

} // namespace

AccesoFinder::AccesoFinder(AccesoPaquetesService& acceso, PersistenceDispatcher& dispatcher,
                           PaqueteSolicitudRepository& paquetes, QObject* parent)
    : QObject(parent)
    , m_acceso(acceso)
    , m_dispatcher(dispatcher)
    , m_paquetes(paquetes)
{
}

AccesoFinder::~AccesoFinder()
{
    for (auto& p : m_pendientes) {
        p.promesa->addResult(resultado(ResultadoAccionFinder::Estado::Fallido, mensajeFinderFallido()));
        p.promesa->finish();
    }
}

QString AccesoFinder::mensajeFinderFallido()
{
    return QStringLiteral("No se pudo abrir Finder");
}

void AccesoFinder::setOS(OSIntegration* os)
{
    QObject::disconnect(m_conexion);
    m_os = os;
    if (os) {
        m_conexion = connect(os, &OSIntegration::finderTerminado, this, &AccesoFinder::alTerminarFinder);
    }
}

QFuture<ResultadoAccionFinder> AccesoFinder::mostrarPaquete(const SolicitudId& solicitud, const QString& idPaqueteSat)
{
    // La presentacion conoce el IdPaquete SAT; la fachada, el id local.
    PaqueteSolicitudRepository* paquetes = &m_paquetes;
    QFuture<std::optional<QString>> idLocal =
        m_dispatcher.despachar<std::optional<QString>>([paquetes, solicitud, idPaqueteSat]() -> std::optional<QString> {
            auto lista = paquetes->listarVisiblesPorSolicitud(solicitud);
            if (!lista) {
                return std::nullopt;
            }
            for (const PaquetePersistido& p : lista.valor()) {
                if (p.idPaqueteSat == idPaqueteSat) {
                    return p.id;
                }
            }
            return std::nullopt;
        });
    AccesoPaquetesService* acceso = &m_acceso;
    QFuture<ResolucionRevelable> resolucion =
        idLocal
            .then(this,
                  [acceso](const std::optional<QString>& id) -> QFuture<ResolucionRevelable> {
                      if (!id) {
                          ResolucionRevelable r;
                          r.estado = ResolucionRevelable::Estado::NoEncontrado;
                          r.mensaje = mensajesacceso::archivoNoEncontrado();
                          return QtFuture::makeReadyValueFuture(r);
                      }
                      return acceso->resolverArchivoPaquete(*id);
                  })
            .unwrap();
    return revelar(resolucion, Destino::Archivo);
}

QFuture<ResultadoAccionFinder> AccesoFinder::abrirCarpetaSolicitud(const SolicitudId& solicitud)
{
    return revelar(m_acceso.resolverCarpetaSolicitud(solicitud), Destino::Carpeta);
}

QFuture<ResultadoAccionFinder> AccesoFinder::abrirCarpetaPaquetes()
{
    return revelar(m_acceso.resolverRaizPaquetes(), Destino::Carpeta);
}

QFuture<ResultadoAccionFinder> AccesoFinder::revelar(QFuture<ResolucionRevelable> resolucion, Destino destino)
{
    auto promesa = std::make_shared<QPromise<ResultadoAccionFinder>>();
    promesa->start();
    QFuture<ResultadoAccionFinder> futuro = promesa->future();
    auto terminar = [promesa](ResultadoAccionFinder r) {
        promesa->addResult(std::move(r));
        promesa->finish();
    };
    resolucion
        .then(this,
              [this, destino, promesa, terminar](const ResolucionRevelable& r) {
                  using E = ResolucionRevelable::Estado;
                  switch (r.estado) {
                  case E::NoEncontrado:
                      terminar(resultado(ResultadoAccionFinder::Estado::NoEncontrado, r.mensaje));
                      return;
                  case E::NoAplica:
                      terminar(resultado(ResultadoAccionFinder::Estado::NoAplica,
                                         r.mensaje.isEmpty() ? QStringLiteral("No hay paquetes descargados")
                                                             : r.mensaje));
                      return;
                  case E::Error:
                      terminar(resultado(ResultadoAccionFinder::Estado::Fallido,
                                         r.mensaje.isEmpty() ? mensajeFinderFallido() : r.mensaje));
                      return;
                  case E::Disponible:
                      break;
                  }
                  if (!m_os) {
                      terminar(resultado(ResultadoAccionFinder::Estado::Fallido, mensajeFinderFallido()));
                      return;
                  }
                  const QString peticion = QUuid::createUuid().toString(QUuid::WithoutBraces);
                  m_pendientes.insert(peticion, Pendiente{promesa, destino});
                  if (destino == Destino::Archivo) {
                      m_os->mostrarEnFinder(peticion, r.rutaAbsoluta);
                  } else {
                      m_os->abrirCarpetaEnFinder(peticion, r.rutaAbsoluta);
                  }
              })
        .onCanceled(this, [terminar] {
            terminar(resultado(ResultadoAccionFinder::Estado::Fallido, mensajeFinderFallido()));
        })
        .onFailed(this, [terminar] {
            terminar(resultado(ResultadoAccionFinder::Estado::Fallido, mensajeFinderFallido()));
        });
    return futuro;
}

void AccesoFinder::alTerminarFinder(const QString& peticionId, OSIntegration::ResultadoFinder r)
{
    const auto it = m_pendientes.constFind(peticionId);
    if (it == m_pendientes.cend()) {
        return; // de otro solicitante
    }
    const Pendiente p = *it;
    m_pendientes.erase(it);
    ResultadoAccionFinder salida;
    switch (r) {
    case OSIntegration::ResultadoFinder::Mostrado:
        salida = resultado(ResultadoAccionFinder::Estado::Mostrado);
        break;
    case OSIntegration::ResultadoFinder::NoEncontrado:
        salida = resultado(ResultadoAccionFinder::Estado::NoEncontrado,
                           p.destino == Destino::Archivo ? mensajesacceso::archivoNoEncontrado()
                                                         : mensajesacceso::carpetaNoEncontrada());
        break;
    case OSIntegration::ResultadoFinder::Fallido:
        salida = resultado(ResultadoAccionFinder::Estado::Fallido, mensajeFinderFallido());
        break;
    }
    p.promesa->addResult(salida);
    p.promesa->finish();
}

} // namespace satcfdi
