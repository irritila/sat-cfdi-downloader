#pragma once

#include "ports/repositories/ConfiguracionAppRepository.h"
#include "ports/repositories/LogSolicitudRepository.h"
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

private:
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

private:
    SqliteConnectionProvider& m_proveedor;
};

} // namespace satcfdi
