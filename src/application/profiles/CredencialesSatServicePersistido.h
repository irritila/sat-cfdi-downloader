#pragma once

#include "application/persistence/PuertosPersistencia.h"
#include "application/profiles/AccesoCredencialSat.h"
#include "application/profiles/CredencialesSatService.h"

#include <QSet>
#include <QStringView>

#include <functional>
#include <memory>

QT_BEGIN_NAMESPACE
class QMutex;
QT_END_NAMESPACE

namespace satcfdi {

class PersistenceDispatcher;
class PerfilSatRepository;
class CredencialSatRepository;
class UnitOfWork;
class SecretStore;

// CredencialesSatService sobre PerfilSatRepository, CredencialSatRepository,
// UnitOfWork y SecretStore (T005, DA2).
//
// Ownership: referencias NO propietarias; el composition root crea los
// puertos y los destruye despues de PersistenceDispatcher::cerrar(). El
// servicio vive en el hilo grafico. Las lambdas despachadas capturan solo
// punteros a puertos y copias de datos; la EntradaEFirma viaja en un
// shared_ptr unico (se destruye, y limpia su contrasena, en la tarea o al
// descartarse la tarea).
//
// Exclusion (calidad T005): `m_exclusion` (QMutex compartido por shared_ptr
// para sobrevivir al servicio si una tarea corre tras su destruccion) se
// toma durante TODA la tarea de importar/reemplazar/eliminar/reconciliar y
// durante obtenerMaterialFirma() y estadoEnHiloDeTrabajo(). Asi la lectura de la fila + el descifrado
// de material son atomicos respecto a reemplazo y limpieza, aunque T009 llame
// desde su propio ejecutor. obtenerEstado() no lo toma (solo lectura en el
// dispatcher, serial con las demas tareas). No reentrante.
//
// Diagnostico: ante un error del SecretStore o una limpieza fallida se llama a
// `registro(operacion, categoria)` (por defecto qCWarning en la categoria
// "satcfdi.credenciales"). Solo recibe una etiqueta fija de operacion y la
// categoria: nunca RFC, rutas, `diagnostico` ni OSStatus.
class CredencialesSatServicePersistido final : public CredencialesSatService, public AccesoCredencialSat {
    Q_OBJECT

public:
    using RegistroDiagnostico = std::function<void(QStringView operacion, ErrorSecretStore::Categoria)>;

    CredencialesSatServicePersistido(PersistenceDispatcher& dispatcher,
                                     PerfilSatRepository& perfiles,
                                     CredencialSatRepository& credenciales,
                                     UnitOfWork& unidadDeTrabajo,
                                     SecretStore& secretStore,
                                     RelojUtc reloj = relojSistema(),
                                     RegistroDiagnostico registro = {},
                                     QObject* parent = nullptr);

    QFuture<ResultadoImportacion> importar(const PerfilId& perfilId, EntradaEFirma&& entrada) override;
    QFuture<ResultadoImportacion> reemplazar(const PerfilId& perfilId, EntradaEFirma&& entrada) override;
    QFuture<ResultadoEstado> obtenerEstado(const PerfilId& perfilId) override;
    QFuture<ResultadoResumen> obtenerResumen(const PerfilId& perfilId) override;
    QFuture<ResultadoEliminacion> eliminar(const PerfilId& perfilId) override;
    QFuture<ResultadoReconciliacion> reconciliar() override;
    ResultadoMaterial obtenerMaterialFirma(const PerfilId& perfilId) override;

    // AccesoCredencialSat (T009 D5): hilo de trabajo, bajo `m_exclusion`.
    Resultado<EstadoCredencial, ErrorCredencialSat> estadoEnHiloDeTrabajo(const PerfilId& perfilId) override;
    Resultado<MaterialFirma, ErrorCredencialSat> materialEnHiloDeTrabajo(const PerfilId& perfilId) override;

    // Registro por defecto (qCWarning, solo operacion + categoria).
    static void registroPorDefecto(QStringView operacion, ErrorSecretStore::Categoria categoria);

private:
    bool enHiloNoPermitido() const;
    QFuture<ResultadoImportacion> registrar(const PerfilId& perfilId, EntradaEFirma&& entrada,
                                            bool esReemplazo);

    PersistenceDispatcher& m_dispatcher;
    PerfilSatRepository& m_perfiles;
    CredencialSatRepository& m_credenciales;
    UnitOfWork& m_unidadDeTrabajo;
    SecretStore& m_secretStore;
    RelojUtc m_reloj;
    RegistroDiagnostico m_registro;
    std::shared_ptr<QMutex> m_exclusion;
    // Perfiles con importacion/reemplazo en curso (solo hilo grafico).
    QSet<PerfilId> m_validando;
};

} // namespace satcfdi
