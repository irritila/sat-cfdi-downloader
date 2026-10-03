#include "application/profiles/PerfilesSatServicePersistido.h"

#include "application/persistence/PersistenceDispatcher.h"
#include "domain/common/Rfc.h"
#include "ports/repositories/PerfilSatRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <utility>

namespace satcfdi {

// Las lambdas despachadas capturan solo punteros a puertos y copias de datos;
// nunca `this`.

namespace {

PerfilResumen resumen(const PerfilSat& p)
{
    return PerfilResumen{p.id, p.rfc, p.nombre, p.activo};
}

PerfilesSatService::ResultadoLista aLista(Resultado<QList<PerfilSat>, ErrorPersistencia> filas)
{
    using R = PerfilesSatService::ResultadoLista;
    if (!filas.esExito()) {
        return R::fallo(std::move(filas).error());
    }
    QList<PerfilResumen> lista;
    lista.reserve(filas.valor().size());
    for (const PerfilSat& p : filas.valor()) {
        lista.append(resumen(p));
    }
    return R::exito(std::move(lista));
}

} // namespace

PerfilesSatServicePersistido::PerfilesSatServicePersistido(PersistenceDispatcher& dispatcher,
                                                           PerfilSatRepository& perfiles,
                                                           UnitOfWork& unidadDeTrabajo,
                                                           RelojUtc reloj,
                                                           QObject* parent)
    : PerfilesSatService(parent)
    , m_dispatcher(dispatcher)
    , m_perfiles(perfiles)
    , m_unidadDeTrabajo(unidadDeTrabajo)
    , m_reloj(std::move(reloj))
{
}

QFuture<PerfilesSatService::ResultadoLista> PerfilesSatServicePersistido::listarNoEliminados()
{
    PerfilSatRepository* repo = &m_perfiles;
    return m_dispatcher.despachar<ResultadoLista>([repo]() { return aLista(repo->listarVisibles()); });
}

QFuture<PerfilesSatService::ResultadoPerfil> PerfilesSatServicePersistido::obtener(const PerfilId& id)
{
    if (id.esNulo()) {
        return QtFuture::makeReadyValueFuture(ResultadoPerfil::exito(std::nullopt));
    }
    PerfilSatRepository* repo = &m_perfiles;
    return m_dispatcher.despachar<ResultadoPerfil>([repo, id]() {
        auto fila = repo->obtener(id);
        if (!fila.esExito()) {
            return ResultadoPerfil::fallo(std::move(fila).error());
        }
        if (!fila.valor()) {
            return ResultadoPerfil::exito(std::nullopt);
        }
        return ResultadoPerfil::exito(resumen(*fila.valor()));
    });
}

QFuture<PerfilesSatService::ResultadoCrear>
PerfilesSatServicePersistido::crear(const QString& rfcCapturado, const QString& nombreCapturado)
{
    // 1. Validacion pura por campo, fuera de transaccion.
    const std::optional<QString> rfc = rfc::normalizarYValidar(rfcCapturado);
    const QString nombre = nombreCapturado.trimmed();
    QList<CodigoValidacionPerfil> invalidos;
    if (!rfc) {
        invalidos.append(CodigoValidacionPerfil::RfcInvalido);
    }
    if (nombre.isEmpty()) {
        invalidos.append(CodigoValidacionPerfil::NombreRequerido);
    }
    if (!invalidos.isEmpty()) {
        return QtFuture::makeReadyValueFuture(
            ResultadoCrear::fallo(ErrorCrearPerfil::validacion(std::move(invalidos))));
    }

    const QDateTime ahora = m_reloj();
    const NuevoPerfilSat nuevo{PerfilId::generar(), *rfc, nombre, true, ahora, ahora};
    PerfilSatRepository* repo = &m_perfiles;
    UnitOfWork* uow = &m_unidadDeTrabajo;

    auto tarea = [repo, uow, nuevo]() -> ResultadoCrear {
        using R = ResultadoCrear;
        auto begin = uow->begin();
        if (!begin.esExito()) {
            return R::fallo(ErrorCrearPerfil::desdePersistencia(std::move(begin).error()));
        }
        auto insertado = repo->insertar(nuevo);
        if (!insertado.esExito()) {
            (void)uow->rollback();
            return R::fallo(ErrorCrearPerfil::desdePersistencia(std::move(insertado).error()));
        }
        auto commit = uow->commit();
        if (!commit.esExito()) {
            (void)uow->rollback();
            return R::fallo(ErrorCrearPerfil::desdePersistencia(std::move(commit).error()));
        }
        return R::exito(resumen(insertado.valor()));
    };

    return m_dispatcher.despachar<ResultadoCrear>(std::move(tarea)).then(this, [this](ResultadoCrear r) {
        if (r.esExito()) {
            emit perfilesCambiaron();
        }
        return r;
    });
}

QFuture<PerfilesSatService::ResultadoActualizar>
PerfilesSatServicePersistido::actualizarNombre(const PerfilId& id, const QString& nombreCapturado)
{
    const QString nombre = nombreCapturado.trimmed();
    if (nombre.isEmpty()) {
        return QtFuture::makeReadyValueFuture(ResultadoActualizar::fallo(ErrorActualizarPerfil::nombreRequerido()));
    }
    if (id.esNulo()) {
        return QtFuture::makeReadyValueFuture(ResultadoActualizar::fallo(ErrorActualizarPerfil::inexistente()));
    }
    const QDateTime ahora = m_reloj();
    PerfilSatRepository* repo = &m_perfiles;
    UnitOfWork* uow = &m_unidadDeTrabajo;

    auto tarea = [repo, uow, id, nombre, ahora]() -> ResultadoActualizar {
        using R = ResultadoActualizar;
        auto begin = uow->begin();
        if (!begin.esExito()) {
            return R::fallo(ErrorActualizarPerfil::persistencia(std::move(begin).error()));
        }
        auto fila = repo->actualizarNombreVisible(id, nombre, ahora);
        if (!fila.esExito()) {
            (void)uow->rollback();
            return R::fallo(ErrorActualizarPerfil::persistencia(std::move(fila).error()));
        }
        if (!fila.valor()) {
            (void)uow->rollback();
            return R::fallo(ErrorActualizarPerfil::inexistente());
        }
        auto commit = uow->commit();
        if (!commit.esExito()) {
            (void)uow->rollback();
            return R::fallo(ErrorActualizarPerfil::persistencia(std::move(commit).error()));
        }
        return R::exito(resumen(*fila.valor()));
    };

    return m_dispatcher.despachar<ResultadoActualizar>(std::move(tarea))
        .then(this, [this](ResultadoActualizar r) {
            if (r.esExito()) {
                emit perfilesCambiaron();
            }
            return r;
        });
}

} // namespace satcfdi
