#include "infrastructure/persistence/sqlite/SqliteConnectionProvider.h"
#include "infrastructure/persistence/sqlite/SqliteRepositorios.h"
#include "infrastructure/persistence/sqlite/SqliteSoporte.h"

#include <QSqlQuery>

#include <utility>

namespace satcfdi {

namespace {

const QString kColumnas = QStringLiteral(
    "id, rfc, nombre, activo, creado_en, actualizado_en, eliminado_en");

Resultado<PerfilSat, ErrorPersistencia> leerPerfil(const QSqlQuery& q)
{
    using R = Resultado<PerfilSat, ErrorPersistencia>;
    PerfilSat p;
    const auto id = PerfilId::desdeTexto(q.value(0).toString());
    if (!id) {
        return R::fallo(sqlite::filaIlegible(u"perfil_sat", u"id"));
    }
    p.id = *id;
    p.rfc = q.value(1).toString();
    p.nombre = q.value(2).toString();
    p.activo = q.value(3).toInt() != 0;
    if (!sqlite::leerInstante(q.value(4), p.creadoEn)) {
        return R::fallo(sqlite::filaIlegible(u"perfil_sat", u"creado_en"));
    }
    if (!sqlite::leerInstante(q.value(5), p.actualizadoEn)) {
        return R::fallo(sqlite::filaIlegible(u"perfil_sat", u"actualizado_en"));
    }
    if (!sqlite::leerInstanteOpcional(q.value(6), p.eliminadoEn)) {
        return R::fallo(sqlite::filaIlegible(u"perfil_sat", u"eliminado_en"));
    }
    return R::exito(std::move(p));
}

// Ejecuta una consulta preparada que devuelve 0..1 perfiles.
Resultado<std::optional<PerfilSat>, ErrorPersistencia> leerUno(QSqlQuery& q, QStringView contexto)
{
    using R = Resultado<std::optional<PerfilSat>, ErrorPersistencia>;
    if (auto r = sqlite::ejecutar(q, contexto); !r) {
        return R::fallo(std::move(r).error());
    }
    if (!q.next()) {
        return R::exito(std::nullopt);
    }
    auto perfil = leerPerfil(q);
    if (!perfil) {
        return R::fallo(std::move(perfil).error());
    }
    return R::exito(std::optional<PerfilSat>(std::move(perfil).valor()));
}

} // namespace

SqlitePerfilSatRepository::SqlitePerfilSatRepository(SqliteConnectionProvider& proveedor)
    : m_proveedor(proveedor)
{
}

Resultado<PerfilSat, ErrorPersistencia> SqlitePerfilSatRepository::insertar(const NuevoPerfilSat& perfil)
{
    using R = Resultado<PerfilSat, ErrorPersistencia>;
    constexpr QStringView kContexto = u"perfil_sat.insertar";
    auto conexion = sqlite::conexionEscritura(m_proveedor, kContexto);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    if (auto r = sqlite::preparar(
            q,
            QStringLiteral("INSERT INTO perfil_sat (id, rfc, nombre, activo, creado_en, "
                           "actualizado_en, eliminado_en) VALUES (:id, :rfc, :nombre, :activo, "
                           ":creado_en, :actualizado_en, NULL)"),
            kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    q.bindValue(QStringLiteral(":id"), sqlite::texto(perfil.id.texto()));
    q.bindValue(QStringLiteral(":rfc"), sqlite::texto(perfil.rfc));
    q.bindValue(QStringLiteral(":nombre"), sqlite::texto(perfil.nombre));
    q.bindValue(QStringLiteral(":activo"), sqlite::booleano(perfil.activo));
    q.bindValue(QStringLiteral(":creado_en"), sqlite::instante(perfil.creadoEn));
    q.bindValue(QStringLiteral(":actualizado_en"), sqlite::instante(perfil.actualizadoEn));
    if (auto r = sqlite::ejecutar(q, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    PerfilSat insertado;
    insertado.id = perfil.id;
    insertado.rfc = perfil.rfc;
    insertado.nombre = perfil.nombre;
    insertado.activo = perfil.activo;
    insertado.creadoEn = perfil.creadoEn;
    insertado.actualizadoEn = perfil.actualizadoEn;
    return R::exito(std::move(insertado));
}

Resultado<std::optional<PerfilSat>, ErrorPersistencia> SqlitePerfilSatRepository::obtener(const PerfilId& id)
{
    using R = Resultado<std::optional<PerfilSat>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"perfil_sat.obtener";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (auto r = sqlite::preparar(q,
                                  QStringLiteral("SELECT ") + kColumnas
                                      + QStringLiteral(" FROM perfil_sat WHERE id = :id "
                                                       "AND eliminado_en IS NULL"),
                                  kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    q.bindValue(QStringLiteral(":id"), sqlite::texto(id.texto()));
    return leerUno(q, kContexto);
}

Resultado<std::optional<PerfilSat>, ErrorPersistencia>
SqlitePerfilSatRepository::obtenerVigentePorRfc(QStringView rfcNormalizado)
{
    using R = Resultado<std::optional<PerfilSat>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"perfil_sat.obtener_por_rfc";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (auto r = sqlite::preparar(q,
                                  QStringLiteral("SELECT ") + kColumnas
                                      + QStringLiteral(" FROM perfil_sat WHERE rfc = :rfc "
                                                       "AND eliminado_en IS NULL"),
                                  kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    q.bindValue(QStringLiteral(":rfc"), sqlite::texto(rfcNormalizado.toString()));
    return leerUno(q, kContexto);
}

Resultado<QList<PerfilSat>, ErrorPersistencia> SqlitePerfilSatRepository::listarActivosVisibles()
{
    using R = Resultado<QList<PerfilSat>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"perfil_sat.listar_activos";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (auto r = sqlite::ejecutarDirecto(
            q,
            QStringLiteral("SELECT ") + kColumnas
                + QStringLiteral(" FROM perfil_sat WHERE eliminado_en IS NULL AND activo = 1 "
                                 "ORDER BY rfc ASC"),
            kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    QList<PerfilSat> perfiles;
    while (q.next()) {
        auto perfil = leerPerfil(q);
        if (!perfil) {
            return R::fallo(std::move(perfil).error());
        }
        perfiles.append(std::move(perfil).valor());
    }
    return R::exito(std::move(perfiles));
}

} // namespace satcfdi
