#include "infrastructure/persistence/sqlite/SqlitePersistencia.h"

#include "infrastructure/persistence/sqlite/SqliteConnectionProvider.h"
#include "infrastructure/persistence/sqlite/SqliteMigrationRunner.h"
#include "infrastructure/persistence/sqlite/SqliteRepositorios.h"
#include "infrastructure/persistence/sqlite/SqliteUnitOfWork.h"

#include <utility>

namespace satcfdi {

namespace {

using ResultadoInicializacion = Resultado<InformeInicializacionSqlite, ErrorPersistencia>;

ResultadoInicializacion inicializarConProveedor(SqliteConnectionProvider& proveedor,
                                                const QList<MigracionSql>& migraciones)
{
    // Abre y configura (DM4 + PRAGMAs) antes de leer o escribir nada.
    if (auto conexion = proveedor.conexion(); !conexion) {
        return ResultadoInicializacion::fallo(std::move(conexion).error());
    }
    InformeInicializacionSqlite informe;
    auto version = proveedor.versionSqlite();
    if (!version) {
        return ResultadoInicializacion::fallo(std::move(version).error());
    }
    informe.versionSqlite = version.valor();

    // Migrar ANTES de cambiar journal_mode: un rechazo (version futura, base
    // ajena) no debe modificar ni un byte del archivo.
    SqliteMigrationRunner runner(proveedor, migraciones);
    auto migracion = runner.migrar();
    if (!migracion) {
        return ResultadoInicializacion::fallo(std::move(migracion).error());
    }
    informe.migracion = migracion.valor();

    auto modo = proveedor.habilitarWal();
    if (!modo) {
        return ResultadoInicializacion::fallo(std::move(modo).error());
    }
    informe.journalMode = modo.valor();
    return ResultadoInicializacion::exito(std::move(informe));
}

} // namespace

Resultado<InformeInicializacionSqlite, ErrorPersistencia>
inicializarBaseSqlite(const QString& rutaBase, const QList<MigracionSql>& migraciones,
                      OpcionesConexionSqlite opciones)
{
    opciones.exigirWal = false; // la conexion de bootstrap es la que habilita WAL
    SqliteConnectionProvider proveedor(rutaBase, opciones);
    auto resultado = inicializarConProveedor(proveedor, migraciones);
    proveedor.cerrarConexionDelHiloActual();
    return resultado;
}

Resultado<InformeInicializacionSqlite, ErrorPersistencia> inicializarBaseSqlite(const QString& rutaBase)
{
    auto migraciones = migracionesSqliteEmbebidas();
    if (!migraciones) {
        return ResultadoInicializacion::fallo(std::move(migraciones).error());
    }
    return inicializarBaseSqlite(rutaBase, migraciones.valor());
}

Resultado<QList<MigracionSql>, ErrorPersistencia> migracionesSqliteEmbebidas()
{
    return SqliteMigrationRunner::migracionesEmbebidas();
}

struct SqlitePersistencia::Impl {
    Impl(QString rutaBase, OpcionesConexionSqlite opciones)
        : proveedor(std::move(rutaBase), opciones)
        , perfiles(proveedor)
        , solicitudes(proveedor)
        , paquetes(proveedor)
        , logs(proveedor)
        , configuracion(proveedor)
        , credenciales(proveedor)
        , avisosVencimiento(proveedor)
        , operaciones(proveedor)
        , unidadDeTrabajo(proveedor)
    {
    }

    // Orden de declaracion = orden inverso de destruccion: el proveedor se
    // destruye al final.
    SqliteConnectionProvider proveedor;
    SqlitePerfilSatRepository perfiles;
    SqliteSolicitudMasivaRepository solicitudes;
    SqlitePaqueteSolicitudRepository paquetes;
    SqliteLogSolicitudRepository logs;
    SqliteConfiguracionAppRepository configuracion;
    SqliteCredencialSatRepository credenciales;
    SqliteAvisoVencimientoRepository avisosVencimiento;
    SqliteOperacionesSolicitudRepository operaciones;
    SqliteUnitOfWork unidadDeTrabajo;
};

SqlitePersistencia::SqlitePersistencia(QString rutaBase, OpcionesConexionSqlite opciones)
    : m_impl(std::make_unique<Impl>(std::move(rutaBase), opciones))
{
}

SqlitePersistencia::~SqlitePersistencia() = default;

PerfilSatRepository& SqlitePersistencia::perfiles() noexcept { return m_impl->perfiles; }
SolicitudMasivaRepository& SqlitePersistencia::solicitudes() noexcept { return m_impl->solicitudes; }
PaqueteSolicitudRepository& SqlitePersistencia::paquetes() noexcept { return m_impl->paquetes; }
LogSolicitudRepository& SqlitePersistencia::logs() noexcept { return m_impl->logs; }
ConfiguracionAppRepository& SqlitePersistencia::configuracion() noexcept { return m_impl->configuracion; }
CredencialSatRepository& SqlitePersistencia::credenciales() noexcept { return m_impl->credenciales; }
AvisoVencimientoRepository& SqlitePersistencia::avisosVencimiento() noexcept { return m_impl->avisosVencimiento; }
OperacionesSolicitudRepository& SqlitePersistencia::operaciones() noexcept { return m_impl->operaciones; }
UnitOfWork& SqlitePersistencia::unidadDeTrabajo() noexcept { return m_impl->unidadDeTrabajo; }

void SqlitePersistencia::cerrarConexionDelHiloActual()
{
    m_impl->proveedor.cerrarConexionDelHiloActual();
}

int SqlitePersistencia::conexionesAbiertas() const
{
    return m_impl->proveedor.conexionesAbiertas();
}

SqliteConnectionProvider& SqlitePersistencia::proveedor() noexcept
{
    return m_impl->proveedor;
}

} // namespace satcfdi
