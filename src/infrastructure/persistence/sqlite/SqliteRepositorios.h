#pragma once

#include "ports/repositories/ConfiguracionAppRepository.h"
#include "ports/repositories/CredencialSatRepository.h"
#include "ports/repositories/LogSolicitudRepository.h"
#include "ports/repositories/OperacionesSolicitudRepository.h"
#include "ports/repositories/PaqueteSolicitudRepository.h"
#include "ports/repositories/PerfilSatRepository.h"
#include "ports/repositories/SolicitudMasivaRepository.h"

namespace satcfdi {

class SqliteConnectionProvider;

// Implementaciones SQLite de los puertos de repositorio (T003). Sin tipos
// QSql* en la interfaz: usan la conexion del hilo actual del proveedor en cada
// llamada y no retienen QSqlQuery/QSqlDatabase entre llamadas.
//
// Ownership: no poseen el proveedor; deben destruirse antes que el.
// Hilo: el del llamador (tareas de PersistenceDispatcher en produccion).
// Escrituras: exigen un SqliteUnitOfWork activo en el hilo (error Transaccion
// si no lo hay). Errores SQL se traducen con sqlite::traducirError().

class SqlitePerfilSatRepository final : public PerfilSatRepository {
public:
    explicit SqlitePerfilSatRepository(SqliteConnectionProvider& proveedor);

    Resultado<PerfilSat, ErrorPersistencia> insertar(const NuevoPerfilSat& perfil) override;
    Resultado<std::optional<PerfilSat>, ErrorPersistencia> obtener(const PerfilId& id) override;
    Resultado<std::optional<PerfilSat>, ErrorPersistencia>
    obtenerVigentePorRfc(QStringView rfcNormalizado) override;
    Resultado<QList<PerfilSat>, ErrorPersistencia> listarActivosVisibles() override;
    Resultado<QList<PerfilSat>, ErrorPersistencia> listarVisibles() override;
    Resultado<std::optional<PerfilSat>, ErrorPersistencia>
    actualizarNombreVisible(const PerfilId& id, const QString& nombre, const QDateTime& actualizadoEn) override;

private:
    Resultado<QList<PerfilSat>, ErrorPersistencia> listar(bool soloActivos, QStringView contexto);

    SqliteConnectionProvider& m_proveedor;
};

class SqliteSolicitudMasivaRepository final : public SolicitudMasivaRepository {
public:
    explicit SqliteSolicitudMasivaRepository(SqliteConnectionProvider& proveedor);

    Resultado<QList<SolicitudPersistida>, ErrorPersistencia> listarVisibles() override;
    Resultado<std::optional<SolicitudPersistida>, ErrorPersistencia>
    obtenerVisible(const SolicitudId& id) override;
    Resultado<EvaluacionDuplicado, ErrorPersistencia>
    clasificarDuplicado(const DedupKey& clave) override;
    Resultado<Exito, ErrorPersistencia>
    insertarCreada(const SolicitudNuevaPersistida& solicitud) override;
    Resultado<bool, ErrorPersistencia> marcarEliminadaVisible(const SolicitudId& id,
                                                              const QDateTime& eliminadoEn) override;

private:
    SqliteConnectionProvider& m_proveedor;
};

class SqlitePaqueteSolicitudRepository final : public PaqueteSolicitudRepository {
public:
    explicit SqlitePaqueteSolicitudRepository(SqliteConnectionProvider& proveedor);

    Resultado<QList<PaquetePersistido>, ErrorPersistencia>
    listarVisiblesPorSolicitud(const SolicitudId& solicitudId) override;
    Resultado<QHash<SolicitudId, int>, ErrorPersistencia> contarVisiblesPorSolicitud() override;
    Resultado<QHash<SolicitudId, ConteoPaquetes>, ErrorPersistencia> contarVisiblesPorSolicitudYEstado() override;
    Resultado<int, ErrorPersistencia>
    marcarEliminadosPorSolicitud(const SolicitudId& solicitudId,
                                 const QDateTime& eliminadoEn) override;

private:
    SqliteConnectionProvider& m_proveedor;
};

class SqliteLogSolicitudRepository final : public LogSolicitudRepository {
public:
    explicit SqliteLogSolicitudRepository(SqliteConnectionProvider& proveedor);

    Resultado<QList<LogPersistido>, ErrorPersistencia>
    listarVisiblesPorSolicitud(const SolicitudId& solicitudId) override;
    Resultado<Exito, ErrorPersistencia> agregar(const LogEntradaSaneada& entrada) override;
    Resultado<int, ErrorPersistencia>
    marcarEliminadosPorSolicitud(const SolicitudId& solicitudId,
                                 const QDateTime& eliminadoEn) override;

private:
    SqliteConnectionProvider& m_proveedor;
};

class SqliteConfiguracionAppRepository final : public ConfiguracionAppRepository {
public:
    explicit SqliteConfiguracionAppRepository(SqliteConnectionProvider& proveedor);

    Resultado<ConfiguracionApp, ErrorPersistencia> obtener() override;
    Resultado<ConfiguracionApp, ErrorPersistencia> actualizarInicioAutomatico(bool habilitado,
                                                                              const QDateTime& en) override;
    Resultado<ConfiguracionApp, ErrorPersistencia> actualizarMonitoreoPausado(bool pausado,
                                                                              const QDateTime& en) override;
    Resultado<ConfiguracionApp, ErrorPersistencia> registrarUltimoCierre(const QDateTime& en) override;

private:
    SqliteConnectionProvider& m_proveedor;
};

// credencial_sat (T005). Solo referencias opacas y metadata no secreta.
class SqliteCredencialSatRepository final : public CredencialSatRepository {
public:
    explicit SqliteCredencialSatRepository(SqliteConnectionProvider& proveedor);

    Resultado<Exito, ErrorPersistencia> insertar(const CredencialSat& credencial) override;
    Resultado<Exito, ErrorPersistencia> reemplazar(const PerfilId& perfilId,
                                                   const CredencialSat& nueva) override;
    Resultado<std::optional<CredencialSat>, ErrorPersistencia>
    obtenerPorPerfil(const PerfilId& perfilId) override;
    Resultado<bool, ErrorPersistencia> eliminarPorPerfil(const PerfilId& perfilId) override;
    Resultado<QList<CredencialRef>, ErrorPersistencia> listarReferenciasVigentes() override;

private:
    SqliteConnectionProvider& m_proveedor;
};

// Operaciones del ejecutor serial y del worker (T007). Hilo: el del
// OperacionExecutor, con su propia conexion por hilo del proveedor (distinta de
// la del PersistenceDispatcher). Las escrituras exigen SqliteUnitOfWork activo
// (BEGIN IMMEDIATE) y revalidan en su WHERE eliminado_en IS NULL y el estado de
// origen; si no aplica devuelven exito con false sin modificar nada.
class SqliteOperacionesSolicitudRepository final : public OperacionesSolicitudRepository {
public:
    explicit SqliteOperacionesSolicitudRepository(SqliteConnectionProvider& proveedor);

    Resultado<QList<PerfilId>, ErrorPersistencia> listarPerfilesConTrabajo(const QDateTime& ahoraUtc) override;
    Resultado<QList<PaqueteDescargable>, ErrorPersistencia>
    listarVencimientosEstimados(const QDateTime& ahoraUtc, int limite) override;
    Resultado<QList<IntencionPendiente>, ErrorPersistencia>
    listarIntencionesPendientes(const QList<PerfilId>& perfiles, int limite) override;
    Resultado<QList<SolicitudPersistida>, ErrorPersistencia>
    listarVerificacionesDebidas(const QList<PerfilId>& perfiles, const QDateTime& ahoraUtc, int limite) override;
    Resultado<QList<PaqueteDescargable>, ErrorPersistencia>
    listarDescargasAutomaticas(const QList<PerfilId>& perfiles, int limite) override;
    Resultado<std::optional<PaqueteDescargable>, ErrorPersistencia> obtenerPaquete(const QString& paqueteId) override;
    Resultado<QList<PaqueteDescargable>, ErrorPersistencia>
    listarPaquetesReintentables(const SolicitudId& solicitudId) override;
    Resultado<TrabajoInterrumpido, ErrorPersistencia> listarInterrumpidos() override;
    Resultado<std::optional<RachaVerificacion>, ErrorPersistencia>
    leerRachaVerificacion(const SolicitudId& solicitudId) override;

    Resultado<bool, ErrorPersistencia> marcarEnviando(const SolicitudId& solicitudId,
                                                      const QDateTime& ahoraUtc) override;
    Resultado<bool, ErrorPersistencia> aplicarEnvio(const AplicacionEnvio& aplicacion) override;
    Resultado<ResultadoAplicacionVerificacion, ErrorPersistencia>
    aplicarVerificacion(const AplicacionVerificacion& aplicacion) override;
    Resultado<bool, ErrorPersistencia> aplicarFallaVerificacion(const AplicacionFallaVerificacion& aplicacion) override;
    Resultado<bool, ErrorPersistencia> marcarDescargando(const QString& paqueteId, const QDateTime& ahoraUtc,
                                                         bool permitirError) override;
    Resultado<bool, ErrorPersistencia> aplicarDescarga(const AplicacionDescarga& aplicacion) override;
    Resultado<bool, ErrorPersistencia> vencerPaqueteEstimado(const QString& paqueteId,
                                                             const QDateTime& ahoraUtc) override;
    Resultado<bool, ErrorPersistencia> registrarIntencion(const SolicitudId& solicitudId, TipoIntencion tipo,
                                                          const QDateTime& ahoraUtc) override;
    Resultado<bool, ErrorPersistencia> consumirIntencion(const SolicitudId& solicitudId, TipoIntencion tipo,
                                                         const QDateTime& capturadaEn) override;

private:
    SqliteConnectionProvider& m_proveedor;
};

} // namespace satcfdi
