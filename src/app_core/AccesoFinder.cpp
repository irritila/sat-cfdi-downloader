#include "AccesoFinder.h"

#include "application/operaciones/TiposOperacion.h"
#include "application/paquetes/AccesoPaquetesService.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "domain/paquetes/PaquetePersistido.h"
#include "ports/repositories/PaqueteSolicitudRepository.h"

#include <QDir>
#include <QFileInfo>
#include <QUuid>

#include <algorithm>

namespace satcfdi {

namespace {

ResultadoAccionFinder resultado(ResultadoAccionFinder::Estado estado, QString mensaje = {})
{
    return ResultadoAccionFinder{estado, std::move(mensaje)};
}

// true si `ruta` esta ESTRICTAMENTE bajo el directorio `dir` (ambos absolutos
// y limpios). Comparacion textual por componentes: "/a/bc" no esta bajo "/a/b".
bool dentroDe(const QString& ruta, const QString& dir)
{
    if (ruta.isEmpty() || dir.isEmpty()) {
        return false;
    }
    const QString prefijo = dir.endsWith(QLatin1Char('/')) ? dir : dir + QLatin1Char('/');
    return ruta.size() > prefijo.size() && ruta.startsWith(prefijo);
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
        p.promesa->addResult(resultado(ResultadoAccionFinder::Estado::Fallido,
                                       p.destino == Destino::VistaPrevia ? mensajeVistaPreviaFallida()
                                                                         : mensajeFinderFallido()));
        p.promesa->finish();
    }
}

QString AccesoFinder::mensajeFinderFallido()
{
    return QStringLiteral("No se pudo abrir Finder");
}

QString AccesoFinder::mensajeVistaPreviaFallida()
{
    return QStringLiteral("No se pudo abrir Vista Previa");
}

QString AccesoFinder::mensajeVistaPreviaNoEncontrada()
{
    return QStringLiteral("PDF no encontrado");
}

void AccesoFinder::setDirectoriosVistaPrevia(const QStringList& directoriosAbsolutos)
{
    m_directoriosVistaPrevia.clear();
    for (const QString& d : directoriosAbsolutos) {
        if (!d.isEmpty() && QDir::isAbsolutePath(d)) {
            m_directoriosVistaPrevia.append(QDir::cleanPath(d));
        }
    }
}

QFuture<ResultadoAccionFinder> AccesoFinder::abrirEnVistaPrevia(const QString& rutaAbsoluta)
{
    const auto fallido = [] {
        return QtFuture::makeReadyValueFuture(
            resultado(ResultadoAccionFinder::Estado::Fallido, mensajeVistaPreviaFallida()));
    };
    // 1. Forma: absoluta y ya limpia (sin `.`, `..`, `//` ni barra final).
    if (rutaAbsoluta.isEmpty() || !QDir::isAbsolutePath(rutaAbsoluta)
        || QDir::cleanPath(rutaAbsoluta) != rutaAbsoluta) {
        return fallido();
    }
    // 2. Textualmente bajo un directorio permitido (tal cual o canonico).
    QStringList canonicos;
    bool textualmenteDentro = false;
    for (const QString& d : std::as_const(m_directoriosVistaPrevia)) {
        const QString canonico = QFileInfo(d).canonicalFilePath();
        if (!canonico.isEmpty()) {
            canonicos.append(canonico);
        }
        textualmenteDentro = textualmenteDentro || dentroDe(rutaAbsoluta, d) || dentroDe(rutaAbsoluta, canonico);
    }
    if (!textualmenteDentro) {
        return fallido();
    }
    // 3. Existencia (un enlace roto cuenta como inexistente). Sin llamar al SO.
    const QFileInfo info(rutaAbsoluta);
    if (!info.exists()) {
        return QtFuture::makeReadyValueFuture(
            resultado(ResultadoAccionFinder::Estado::NoEncontrado, mensajeVistaPreviaNoEncontrada()));
    }
    // 4. Ruta canonica (enlaces resueltos) estrictamente dentro de un
    //    directorio permitido canonico y archivo regular: un enlace que escapa
    //    se rechaza aqui.
    const QString canonica = info.canonicalFilePath();
    const bool canonicaDentro =
        std::any_of(canonicos.cbegin(), canonicos.cend(), [&](const QString& c) { return dentroDe(canonica, c); });
    if (!canonicaDentro || !QFileInfo(canonica).isFile()) {
        return fallido();
    }
    if (!m_os) {
        return fallido();
    }
    auto promesa = std::make_shared<QPromise<ResultadoAccionFinder>>();
    promesa->start();
    QFuture<ResultadoAccionFinder> futuro = promesa->future();
    const QString peticion = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_pendientes.insert(peticion, Pendiente{promesa, Destino::VistaPrevia});
    // El adaptador revalida con lstat (archivo regular, no enlace).
    m_os->abrirEnVistaPrevia(peticion, canonica);
    return futuro;
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
                           p.destino == Destino::Archivo       ? mensajesacceso::archivoNoEncontrado()
                           : p.destino == Destino::VistaPrevia ? mensajeVistaPreviaNoEncontrada()
                                                               : mensajesacceso::carpetaNoEncontrada());
        break;
    case OSIntegration::ResultadoFinder::Fallido:
        salida = resultado(ResultadoAccionFinder::Estado::Fallido, p.destino == Destino::VistaPrevia
                                                                       ? mensajeVistaPreviaFallida()
                                                                       : mensajeFinderFallido());
        break;
    }
    p.promesa->addResult(salida);
    p.promesa->finish();
}

} // namespace satcfdi
