#include "application/profiles/CredencialesSatServicePersistido.h"

#include "application/persistence/PersistenceDispatcher.h"
#include "domain/common/UuidCanonico.h"
#include "ports/SecretStore.h"
#include "ports/repositories/CredencialSatRepository.h"
#include "ports/repositories/PerfilSatRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <memory>
#include <utility>

Q_LOGGING_CATEGORY(lcCredenciales, "satcfdi.credenciales")

namespace satcfdi {

// --- ErrorCredencialSat ------------------------------------------------------

QString mensajeVisible(ErrorSecretStore::Categoria categoria)
{
    using C = ErrorSecretStore::Categoria;
    switch (categoria) {
    case C::ArchivoIlegible: return QStringLiteral("No se pudo leer el certificado o la llave.");
    case C::FormatoInvalido: return QStringLiteral("El certificado o la llave no tienen un formato valido.");
    case C::ContrasenaIncorrecta: return QStringLiteral("La contrasena de la llave privada es incorrecta.");
    case C::ParejaIncompatible: return QStringLiteral("El certificado y la llave privada no corresponden.");
    case C::RfcNoCoincide: return QStringLiteral("El RFC del certificado no coincide con el del perfil.");
    case C::NoEsEFirma: return QStringLiteral("El certificado no es una e.firma admisible.");
    case C::Vencida: return QStringLiteral("La e.firma esta vencida.");
    case C::NoVigenteAun: return QStringLiteral("La e.firma aun no es vigente.");
    case C::CredencialNoEncontrada: return QStringLiteral("El perfil no tiene una e.firma registrada.");
    case C::CredencialDanada: return QStringLiteral("La e.firma guardada esta danada.");
    case C::AlmacenBloqueado: return QStringLiteral("El llavero esta bloqueado; intenta de nuevo tras desbloquear la sesion.");
    case C::AccesoDenegado: return QStringLiteral("Se denego el acceso al llavero.");
    case C::CanceladoPorUsuario: return QStringLiteral("La operacion se cancelo.");
    case C::AlmacenMalConfigurado: return QStringLiteral("El almacen seguro no esta configurado correctamente.");
    case C::AlmacenNoDisponible: return QStringLiteral("El almacen seguro no esta disponible.");
    case C::FalloEscritura: return QStringLiteral("No se pudo guardar la e.firma.");
    case C::Interno: return QStringLiteral("Ocurrio un error interno con la e.firma.");
    }
    return {};
}

ErrorCredencialSat ErrorCredencialSat::perfil(CodigoPerfil codigo)
{
    ErrorCredencialSat e;
    e.tipo = Tipo::PerfilInvalido;
    e.codigoPerfil = codigo;
    e.mensaje = codigo == CodigoPerfil::PerfilInactivo
                    ? QStringLiteral("El perfil esta inactivo.")
                    : QStringLiteral("El perfil no existe.");
    return e;
}

ErrorCredencialSat ErrorCredencialSat::existente()
{
    ErrorCredencialSat e;
    e.tipo = Tipo::CredencialExistente;
    e.mensaje = QStringLiteral("El perfil ya tiene una e.firma; usa reemplazar.");
    return e;
}

ErrorCredencialSat ErrorCredencialSat::almacen(ErrorSecretStore::Categoria categoria)
{
    ErrorCredencialSat e;
    e.tipo = Tipo::Almacen;
    e.categoria = categoria;
    e.mensaje = mensajeVisible(categoria);
    return e;
}

ErrorCredencialSat ErrorCredencialSat::persistencia(ErrorPersistencia causa)
{
    ErrorCredencialSat e;
    e.tipo = Tipo::Persistencia;
    e.mensaje = QStringLiteral("No se pudo guardar el registro de la e.firma.");
    e.causa = std::move(causa);
    return e;
}

ErrorCredencialSat ErrorCredencialSat::hiloNoPermitido()
{
    ErrorCredencialSat e;
    e.tipo = Tipo::HiloNoPermitido;
    e.mensaje = QStringLiteral("Operacion de e.firma no permitida en este hilo.");
    return e;
}

// --- Auxiliares de tarea -------------------------------------------------------

namespace {

using Registro = CredencialesSatServicePersistido::RegistroDiagnostico;

// Perfil visible; con `exigirActivo`, ademas activo. Devuelve su RFC.
Resultado<QString, ErrorCredencialSat> validarPerfil(PerfilSatRepository& perfiles, const PerfilId& id,
                                                     bool exigirActivo)
{
    using R = Resultado<QString, ErrorCredencialSat>;
    if (id.esNulo()) {
        return R::fallo(ErrorCredencialSat::perfil(ErrorCredencialSat::CodigoPerfil::PerfilInexistente));
    }
    auto perfil = perfiles.obtener(id);
    if (!perfil) {
        return R::fallo(ErrorCredencialSat::persistencia(std::move(perfil).error()));
    }
    if (!perfil.valor()) {
        return R::fallo(ErrorCredencialSat::perfil(ErrorCredencialSat::CodigoPerfil::PerfilInexistente));
    }
    if (exigirActivo && !perfil.valor()->activo) {
        return R::fallo(ErrorCredencialSat::perfil(ErrorCredencialSat::CodigoPerfil::PerfilInactivo));
    }
    return R::exito(perfil.valor()->rfc);
}

ErrorCredencialSat errorAlmacen(const Registro& registro, QStringView operacion,
                                const ErrorSecretStore& error)
{
    registro(operacion, error.categoria);
    return ErrorCredencialSat::almacen(error.categoria);
}

} // namespace

// --- Servicio ------------------------------------------------------------------

CredencialesSatServicePersistido::CredencialesSatServicePersistido(PersistenceDispatcher& dispatcher,
                                                                   PerfilSatRepository& perfiles,
                                                                   CredencialSatRepository& credenciales,
                                                                   UnitOfWork& unidadDeTrabajo,
                                                                   SecretStore& secretStore,
                                                                   RelojUtc reloj,
                                                                   RegistroDiagnostico registro,
                                                                   QObject* parent)
    : CredencialesSatService(parent)
    , m_dispatcher(dispatcher)
    , m_perfiles(perfiles)
    , m_credenciales(credenciales)
    , m_unidadDeTrabajo(unidadDeTrabajo)
    , m_secretStore(secretStore)
    , m_reloj(std::move(reloj))
    , m_registro(registro ? std::move(registro) : RegistroDiagnostico(&registroPorDefecto))
    , m_exclusion(std::make_shared<QMutex>())
{
}

void CredencialesSatServicePersistido::registroPorDefecto(QStringView operacion,
                                                          ErrorSecretStore::Categoria categoria)
{
    qCWarning(lcCredenciales, "%s: %s", qPrintable(operacion.toString()),
              qPrintable(claveEstable(categoria)));
}

QFuture<CredencialesSatService::ResultadoImportacion>
CredencialesSatServicePersistido::importar(const PerfilId& perfilId, EntradaEFirma&& entrada)
{
    return registrar(perfilId, std::move(entrada), false);
}

QFuture<CredencialesSatService::ResultadoImportacion>
CredencialesSatServicePersistido::reemplazar(const PerfilId& perfilId, EntradaEFirma&& entrada)
{
    return registrar(perfilId, std::move(entrada), true);
}

QFuture<CredencialesSatService::ResultadoImportacion>
CredencialesSatServicePersistido::registrar(const PerfilId& perfilId, EntradaEFirma&& entradaMovida,
                                            bool esReemplazo)
{
    // Unica propietaria de la entrada; std::function exige una tarea copiable.
    auto entrada = std::make_shared<EntradaEFirma>(std::move(entradaMovida));
    const QDateTime ahora = m_reloj();
    PerfilSatRepository* perfiles = &m_perfiles;
    CredencialSatRepository* credenciales = &m_credenciales;
    UnitOfWork* uow = &m_unidadDeTrabajo;
    SecretStore* store = &m_secretStore;
    const Registro registro = m_registro;
    const std::shared_ptr<QMutex> exclusion = m_exclusion;

    auto tarea = [=]() -> ResultadoImportacion {
        using R = ResultadoImportacion;
        const QMutexLocker bloqueo(exclusion.get());
        // Garantiza que la contrasena se libere al terminar la tarea aunque
        // prepararEFirma no llegue a consumirla.
        auto entradaLocal = std::move(*entrada);

        auto rfc = validarPerfil(*perfiles, perfilId, true);
        if (!rfc) {
            return R::fallo(std::move(rfc).error());
        }
        auto previo = credenciales->obtenerPorPerfil(perfilId);
        if (!previo) {
            return R::fallo(ErrorCredencialSat::persistencia(std::move(previo).error()));
        }
        if (!esReemplazo && previo.valor()) {
            return R::fallo(ErrorCredencialSat::existente());
        }
        if (esReemplazo && !previo.valor()) {
            return R::fallo(ErrorCredencialSat::almacen(ErrorSecretStore::Categoria::CredencialNoEncontrada));
        }

        // 1. Candidata fuera de transaccion (E/S, Keychain, cripto).
        auto preparada = store->prepararEFirma(std::move(entradaLocal), rfc.valor(), ahora);
        if (!preparada) {
            return R::fallo(errorAlmacen(registro, u"credencial.preparar", preparada.error()));
        }
        CredencialPreparada candidata = std::move(preparada).valor();
        const CredencialRef& ref = candidata.referencia();

        CredencialSat fila;
        fila.perfilSatId = perfilId;
        fila.certificadoRef = ref.referenciaCertificado();
        fila.llavePrivadaRef = ref.referenciaContenedor();
        fila.contrasenaRef = ref.referenciaContrasena();
        fila.numeroSerie = candidata.metadata().numeroSerie;
        fila.vigenteDesde = candidata.metadata().vigenteDesde;
        fila.vigenteHasta = candidata.metadata().vigenteHasta;
        fila.actualizadaEn = ahora;

        // 2. Transaccion. Cualquier return antes de confirmar() descarta la
        // candidata (RAII) y deja la credencial anterior intacta.
        auto falloTx = [uow](ErrorPersistencia e) {
            (void)uow->rollback();
            if (e.tipo == ErrorPersistencia::Tipo::Unicidad) {
                return R::fallo(ErrorCredencialSat::existente());
            }
            return R::fallo(ErrorCredencialSat::persistencia(std::move(e)));
        };
        if (auto begin = uow->begin(); !begin) {
            return R::fallo(ErrorCredencialSat::persistencia(std::move(begin).error()));
        }
        std::optional<CredencialSat> anterior;
        if (esReemplazo) {
            auto actual = credenciales->obtenerPorPerfil(perfilId);
            if (!actual) {
                return falloTx(std::move(actual).error());
            }
            if (!actual.valor()) {
                (void)uow->rollback();
                return R::fallo(
                    ErrorCredencialSat::almacen(ErrorSecretStore::Categoria::CredencialNoEncontrada));
            }
            anterior = std::move(actual).valor();
            fila.id = anterior->id;
            fila.registradaEn = anterior->registradaEn;
            if (auto r = credenciales->reemplazar(perfilId, fila); !r) {
                return falloTx(std::move(r).error());
            }
        } else {
            fila.id = uuid::generarCanonico();
            fila.registradaEn = ahora;
            if (auto r = credenciales->insertar(fila); !r) {
                return falloTx(std::move(r).error());
            }
        }
        if (auto commit = uow->commit(); !commit) {
            return falloTx(std::move(commit).error());
        }

        // 3. Activa: desde aqui nada revierte la nueva credencial.
        candidata.confirmar();
        CredencialImportada importada;
        importada.estado = EstadoCredencial::Lista;
        importada.metadata = candidata.metadata();

        // 4. Limpieza de la generacion anterior (best effort).
        if (anterior) {
            const auto refAnterior = CredencialRef::desdeReferencias(
                anterior->certificadoRef, anterior->llavePrivadaRef, anterior->contrasenaRef);
            if (!refAnterior) {
                registro(u"credencial.limpiar_anterior", ErrorSecretStore::Categoria::Interno);
                importada.limpiezaPendiente = true;
            } else if (auto r = store->eliminar(*refAnterior); !r) {
                registro(u"credencial.limpiar_anterior", r.error().categoria);
                importada.limpiezaPendiente = true;
            }
        }
        return R::exito(std::move(importada));
    };

    m_validando.insert(perfilId);
    return m_dispatcher.despachar<ResultadoImportacion>(std::move(tarea))
        .then(this, [this, perfilId](ResultadoImportacion r) {
            m_validando.remove(perfilId);
            if (r.esExito()) {
                emit credencialCambio(perfilId.texto());
            }
            return r;
        });
}

QFuture<CredencialesSatService::ResultadoEstado>
CredencialesSatServicePersistido::obtenerEstado(const PerfilId& perfilId)
{
    if (m_validando.contains(perfilId)) {
        return QtFuture::makeReadyValueFuture(ResultadoEstado::exito(EstadoCredencial::Validando));
    }
    const QDateTime ahora = m_reloj();
    PerfilSatRepository* perfiles = &m_perfiles;
    CredencialSatRepository* credenciales = &m_credenciales;
    SecretStore* store = &m_secretStore;
    const Registro registro = m_registro;
    return m_dispatcher.despachar<ResultadoEstado>([=]() -> ResultadoEstado {
        using R = ResultadoEstado;
        if (auto perfil = validarPerfil(*perfiles, perfilId, false); !perfil) {
            return R::fallo(std::move(perfil).error());
        }
        auto fila = credenciales->obtenerPorPerfil(perfilId);
        if (!fila) {
            return R::fallo(ErrorCredencialSat::persistencia(std::move(fila).error()));
        }
        if (!fila.valor()) {
            return R::exito(EstadoCredencial::SinCredencial);
        }
        const CredencialSat& c = *fila.valor();
        const auto ref =
            CredencialRef::desdeReferencias(c.certificadoRef, c.llavePrivadaRef, c.contrasenaRef);
        if (!ref) {
            return R::exito(EstadoCredencial::MaterialDanado);
        }
        auto estado = store->obtenerEstado(*ref, ahora);
        if (!estado) {
            return R::fallo(errorAlmacen(registro, u"credencial.estado", estado.error()));
        }
        return R::exito(estado.valor());
    });
}

QFuture<CredencialesSatService::ResultadoEliminacion>
CredencialesSatServicePersistido::eliminar(const PerfilId& perfilId)
{
    PerfilSatRepository* perfiles = &m_perfiles;
    CredencialSatRepository* credenciales = &m_credenciales;
    UnitOfWork* uow = &m_unidadDeTrabajo;
    SecretStore* store = &m_secretStore;
    const Registro registro = m_registro;
    const std::shared_ptr<QMutex> exclusion = m_exclusion;
    auto tarea = [=]() -> ResultadoEliminacion {
        using R = ResultadoEliminacion;
        const QMutexLocker bloqueo(exclusion.get());
        if (auto perfil = validarPerfil(*perfiles, perfilId, false); !perfil) {
            return R::fallo(std::move(perfil).error());
        }
        auto falloTx = [uow](ErrorPersistencia e) {
            (void)uow->rollback();
            return R::fallo(ErrorCredencialSat::persistencia(std::move(e)));
        };
        if (auto begin = uow->begin(); !begin) {
            return R::fallo(ErrorCredencialSat::persistencia(std::move(begin).error()));
        }
        auto fila = credenciales->obtenerPorPerfil(perfilId);
        if (!fila) {
            return falloTx(std::move(fila).error());
        }
        if (!fila.valor()) {
            (void)uow->rollback();
            return R::exito(CredencialEliminada{});
        }
        const CredencialSat anterior = *fila.valor();
        if (auto borrada = credenciales->eliminarPorPerfil(perfilId); !borrada) {
            return falloTx(std::move(borrada).error());
        }
        if (auto commit = uow->commit(); !commit) {
            return falloTx(std::move(commit).error());
        }
        CredencialEliminada resultado;
        resultado.habiaCredencial = true;
        const auto ref = CredencialRef::desdeReferencias(anterior.certificadoRef,
                                                         anterior.llavePrivadaRef,
                                                         anterior.contrasenaRef);
        if (!ref) {
            registro(u"credencial.eliminar", ErrorSecretStore::Categoria::Interno);
            resultado.limpiezaPendiente = true;
        } else if (auto r = store->eliminar(*ref); !r) {
            registro(u"credencial.eliminar", r.error().categoria);
            resultado.limpiezaPendiente = true;
        }
        return R::exito(resultado);
    };
    return m_dispatcher.despachar<ResultadoEliminacion>(std::move(tarea))
        .then(this, [this, perfilId](ResultadoEliminacion r) {
            if (r.esExito() && r.valor().habiaCredencial) {
                emit credencialCambio(perfilId.texto());
            }
            return r;
        });
}

QFuture<CredencialesSatService::ResultadoReconciliacion> CredencialesSatServicePersistido::reconciliar()
{
    CredencialSatRepository* credenciales = &m_credenciales;
    SecretStore* store = &m_secretStore;
    const Registro registro = m_registro;
    const std::shared_ptr<QMutex> exclusion = m_exclusion;
    return m_dispatcher.despachar<ResultadoReconciliacion>([=]() -> ResultadoReconciliacion {
        using R = ResultadoReconciliacion;
        const QMutexLocker bloqueo(exclusion.get());
        auto vigentes = credenciales->listarReferenciasVigentes();
        if (!vigentes) {
            // Sin lista confiable no se borra nada.
            return R::fallo(ErrorCredencialSat::persistencia(std::move(vigentes).error()));
        }
        auto resumen = store->reconciliar(vigentes.valor());
        if (!resumen) {
            return R::fallo(errorAlmacen(registro, u"credencial.reconciliar", resumen.error()));
        }
        if (resumen.valor().fallosLimpieza > 0) {
            registro(u"credencial.reconciliar", ErrorSecretStore::Categoria::FalloEscritura);
        }
        return R::exito(resumen.valor());
    });
}

CredencialesSatService::ResultadoMaterial
CredencialesSatServicePersistido::obtenerMaterialFirma(const PerfilId& perfilId)
{
    using R = ResultadoMaterial;
    // Nunca desde el hilo grafico: verificacion en runtime (no Q_ASSERT).
    QThread* const actual = QThread::currentThread();
    const QCoreApplication* app = QCoreApplication::instance();
    if (actual == thread() || (app != nullptr && actual == app->thread())) {
        return R::fallo(ErrorCredencialSat::hiloNoPermitido());
    }
    const QMutexLocker bloqueo(m_exclusion.get());
    if (auto perfil = validarPerfil(m_perfiles, perfilId, false); !perfil) {
        return R::fallo(std::move(perfil).error());
    }
    auto fila = m_credenciales.obtenerPorPerfil(perfilId);
    if (!fila) {
        return R::fallo(ErrorCredencialSat::persistencia(std::move(fila).error()));
    }
    if (!fila.valor()) {
        return R::fallo(ErrorCredencialSat::almacen(ErrorSecretStore::Categoria::CredencialNoEncontrada));
    }
    const CredencialSat& c = *fila.valor();
    const auto ref = CredencialRef::desdeReferencias(c.certificadoRef, c.llavePrivadaRef, c.contrasenaRef);
    if (!ref) {
        m_registro(u"credencial.material", ErrorSecretStore::Categoria::CredencialDanada);
        return R::fallo(ErrorCredencialSat::almacen(ErrorSecretStore::Categoria::CredencialDanada));
    }
    auto material = m_secretStore.obtenerMaterialFirma(*ref);
    if (!material) {
        return R::fallo(errorAlmacen(m_registro, u"credencial.material", material.error()));
    }
    return R::exito(std::move(material).valor());
}

} // namespace satcfdi
