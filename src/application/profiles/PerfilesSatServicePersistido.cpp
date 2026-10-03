#include "application/profiles/PerfilesSatServicePersistido.h"

#include "application/persistence/PersistenceDispatcher.h"
#include "domain/common/Rfc.h"
#include "ports/repositories/PerfilSatRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <utility>

namespace satcfdi {

// Las lambdas despachadas capturan solo referencias a puertos y copias de
// datos; nunca `this`.

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

QFuture<PerfilesSatService::ResultadoLista> PerfilesSatServicePersistido::listarActivos()
{
    PerfilSatRepository* repo = &m_perfiles;
    return m_dispatcher.despachar<ResultadoLista>([repo]() {
        auto filas = repo->listarActivosVisibles();
        if (!filas.esExito()) {
            return ResultadoLista::fallo(std::move(filas).error());
        }
        QList<PerfilResumen> lista;
        lista.reserve(filas.valor().size());
        for (const PerfilSat& p : filas.valor()) {
            lista.append(PerfilResumen{p.id, p.rfc, p.nombre, p.activo});
        }
        return ResultadoLista::exito(std::move(lista));
    });
}

QFuture<PerfilesSatService::ResultadoCrearPerfil>
PerfilesSatServicePersistido::crearPerfilSimulado(const NuevoPerfilSimuladoRequest& request)
{
    // 1. Validacion pura, fuera de transaccion.
    ErrorCrearPerfil invalido;
    const std::optional<QString> rfc = rfc::normalizarYValidar(request.rfc);
    const QString nombre = request.razonSocial.trimmed();
    if (!rfc) {
        invalido.validaciones.append(ErrorCrearPerfil::CodigoValidacion::RfcInvalido);
    }
    if (nombre.isEmpty()) {
        invalido.validaciones.append(ErrorCrearPerfil::CodigoValidacion::RazonSocialRequerida);
    }
    if (!invalido.validaciones.isEmpty()) {
        invalido.tipo = ErrorCrearPerfil::Tipo::Validacion;
        invalido.mensaje = QStringLiteral("El perfil simulado no es valido.");
        return QtFuture::makeReadyValueFuture(ResultadoCrearPerfil::fallo(std::move(invalido)));
    }

    const QDateTime ahora = m_reloj();
    const NuevoPerfilSat nuevo{PerfilId::generar(), *rfc, nombre, request.activo, ahora, ahora};
    PerfilSatRepository* repo = &m_perfiles;
    UnitOfWork* uow = &m_unidadDeTrabajo;

    auto tarea = [repo, uow, nuevo]() -> ResultadoCrearPerfil {
        using R = ResultadoCrearPerfil;
        auto error = [](ErrorPersistencia e) {
            ErrorCrearPerfil r;
            r.tipo = (e.tipo == ErrorPersistencia::Tipo::Unicidad
                      || e.tipo == ErrorPersistencia::Tipo::Integridad)
                         ? ErrorCrearPerfil::Tipo::Integridad
                         : ErrorCrearPerfil::Tipo::Persistencia;
            r.mensaje = e.mensaje;
            r.causa = std::move(e);
            return R::fallo(std::move(r));
        };

        // 2-4. Transaccion: insertar y confirmar.
        auto begin = uow->begin();
        if (!begin.esExito()) {
            return error(std::move(begin).error());
        }
        auto insertado = repo->insertar(nuevo);
        if (!insertado.esExito()) {
            (void)uow->rollback();
            return error(std::move(insertado).error());
        }
        auto commit = uow->commit();
        if (!commit.esExito()) {
            (void)uow->rollback();
            return error(std::move(commit).error());
        }
        return R::exito(insertado.valor().id);
    };

    return m_dispatcher.despachar<ResultadoCrearPerfil>(std::move(tarea))
        .then(this, [this](ResultadoCrearPerfil r) {
            if (r.esExito()) {
                emit perfilesCambiaron();
            }
            return r;
        });
}

} // namespace satcfdi
