#include "infrastructure/persistence/sqlite/SqliteConnectionProvider.h"
#include "infrastructure/persistence/sqlite/SqliteRepositorios.h"
#include "infrastructure/persistence/sqlite/SqliteSoporte.h"

#include "domain/common/UuidCanonico.h"

#include <QSqlQuery>

#include <utility>

namespace satcfdi {

namespace {

const QString kColumnas = QStringLiteral(
    "id, perfil_sat_id, certificado_ref, llave_privada_ref, contrasena_ref, numero_serie, "
    "vigente_desde, vigente_hasta, registrada_en, actualizada_en");

ErrorPersistencia metadataIncompleta()
{
    return ErrorPersistencia::de(ErrorPersistencia::Tipo::Integridad,
                                 QStringLiteral("credencial_sat sin metadata completa"),
                                 QStringLiteral("credencial_sat.metadata"));
}

Resultado<CredencialSat, ErrorPersistencia> leerCredencial(const QSqlQuery& q)
{
    using R = Resultado<CredencialSat, ErrorPersistencia>;
    CredencialSat c;
    c.id = q.value(0).toString();
    if (!uuid::esCanonico(c.id)) {
        return R::fallo(sqlite::filaIlegible(u"credencial_sat", u"id"));
    }
    const auto perfil = PerfilId::desdeTexto(q.value(1).toString());
    if (!perfil) {
        return R::fallo(sqlite::filaIlegible(u"credencial_sat", u"perfil_sat_id"));
    }
    c.perfilSatId = *perfil;
    c.certificadoRef = q.value(2).toString();
    c.llavePrivadaRef = q.value(3).toString();
    c.contrasenaRef = q.value(4).toString();
    c.numeroSerie = sqlite::leerTextoOpcional(q.value(5));
    if (!sqlite::leerInstanteOpcional(q.value(6), c.vigenteDesde)) {
        return R::fallo(sqlite::filaIlegible(u"credencial_sat", u"vigente_desde"));
    }
    if (!sqlite::leerInstanteOpcional(q.value(7), c.vigenteHasta)) {
        return R::fallo(sqlite::filaIlegible(u"credencial_sat", u"vigente_hasta"));
    }
    if (!sqlite::leerInstante(q.value(8), c.registradaEn)) {
        return R::fallo(sqlite::filaIlegible(u"credencial_sat", u"registrada_en"));
    }
    if (!sqlite::leerInstante(q.value(9), c.actualizadaEn)) {
        return R::fallo(sqlite::filaIlegible(u"credencial_sat", u"actualizada_en"));
    }
    return R::exito(std::move(c));
}

void enlazarMetadata(QSqlQuery& q, const CredencialSat& c)
{
    q.bindValue(QStringLiteral(":certificado_ref"), sqlite::texto(c.certificadoRef));
    q.bindValue(QStringLiteral(":llave_privada_ref"), sqlite::texto(c.llavePrivadaRef));
    q.bindValue(QStringLiteral(":contrasena_ref"), sqlite::texto(c.contrasenaRef));
    q.bindValue(QStringLiteral(":numero_serie"), sqlite::textoOpcional(c.numeroSerie));
    q.bindValue(QStringLiteral(":vigente_desde"), sqlite::instanteOpcional(c.vigenteDesde));
    q.bindValue(QStringLiteral(":vigente_hasta"), sqlite::instanteOpcional(c.vigenteHasta));
    q.bindValue(QStringLiteral(":actualizada_en"), sqlite::instante(c.actualizadaEn));
}

} // namespace

SqliteCredencialSatRepository::SqliteCredencialSatRepository(SqliteConnectionProvider& proveedor)
    : m_proveedor(proveedor)
{
}

Resultado<Exito, ErrorPersistencia> SqliteCredencialSatRepository::insertar(const CredencialSat& c)
{
    using R = Resultado<Exito, ErrorPersistencia>;
    constexpr QStringView kContexto = u"credencial_sat.insertar";
    auto conexion = sqlite::conexionEscritura(m_proveedor, kContexto);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    if (!c.metadataCompleta()) {
        return R::fallo(metadataIncompleta());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    if (auto r = sqlite::preparar(
            q,
            QStringLiteral("INSERT INTO credencial_sat (") + kColumnas
                + QStringLiteral(") VALUES (:id, :perfil_sat_id, :certificado_ref, "
                                 ":llave_privada_ref, :contrasena_ref, :numero_serie, "
                                 ":vigente_desde, :vigente_hasta, :registrada_en, "
                                 ":actualizada_en)"),
            kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    q.bindValue(QStringLiteral(":id"), sqlite::texto(c.id));
    q.bindValue(QStringLiteral(":perfil_sat_id"), sqlite::texto(c.perfilSatId.texto()));
    q.bindValue(QStringLiteral(":registrada_en"), sqlite::instante(c.registradaEn));
    enlazarMetadata(q, c);
    if (auto r = sqlite::ejecutar(q, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    return R::exito({});
}

Resultado<Exito, ErrorPersistencia> SqliteCredencialSatRepository::reemplazar(const PerfilId& perfilId,
                                                                           const CredencialSat& nueva)
{
    using R = Resultado<Exito, ErrorPersistencia>;
    constexpr QStringView kContexto = u"credencial_sat.reemplazar";
    auto conexion = sqlite::conexionEscritura(m_proveedor, kContexto);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    if (!nueva.metadataCompleta()) {
        return R::fallo(metadataIncompleta());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    if (auto r = sqlite::preparar(
            q,
            QStringLiteral("UPDATE credencial_sat SET certificado_ref = :certificado_ref, "
                           "llave_privada_ref = :llave_privada_ref, contrasena_ref = :contrasena_ref, "
                           "numero_serie = :numero_serie, vigente_desde = :vigente_desde, "
                           "vigente_hasta = :vigente_hasta, actualizada_en = :actualizada_en "
                           "WHERE perfil_sat_id = :perfil_sat_id"),
            kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    enlazarMetadata(q, nueva);
    q.bindValue(QStringLiteral(":perfil_sat_id"), sqlite::texto(perfilId.texto()));
    if (auto r = sqlite::ejecutar(q, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    if (q.numRowsAffected() != 1) {
        return R::fallo(ErrorPersistencia::de(ErrorPersistencia::Tipo::NoEncontrado,
                                              QStringLiteral("credencial_sat sin fila del perfil")));
    }
    return R::exito({});
}

Resultado<std::optional<CredencialSat>, ErrorPersistencia>
SqliteCredencialSatRepository::obtenerPorPerfil(const PerfilId& perfilId)
{
    using R = Resultado<std::optional<CredencialSat>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"credencial_sat.obtener_por_perfil";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (auto r = sqlite::preparar(q,
                                  QStringLiteral("SELECT ") + kColumnas
                                      + QStringLiteral(" FROM credencial_sat "
                                                       "WHERE perfil_sat_id = :perfil_sat_id"),
                                  kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    q.bindValue(QStringLiteral(":perfil_sat_id"), sqlite::texto(perfilId.texto()));
    if (auto r = sqlite::ejecutar(q, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    if (!q.next()) {
        return R::exito(std::nullopt);
    }
    auto fila = leerCredencial(q);
    if (!fila) {
        return R::fallo(std::move(fila).error());
    }
    return R::exito(std::optional<CredencialSat>(std::move(fila).valor()));
}

Resultado<bool, ErrorPersistencia> SqliteCredencialSatRepository::eliminarPorPerfil(const PerfilId& perfilId)
{
    using R = Resultado<bool, ErrorPersistencia>;
    constexpr QStringView kContexto = u"credencial_sat.eliminar";
    auto conexion = sqlite::conexionEscritura(m_proveedor, kContexto);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    if (auto r = sqlite::preparar(
            q, QStringLiteral("DELETE FROM credencial_sat WHERE perfil_sat_id = :perfil_sat_id"),
            kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    q.bindValue(QStringLiteral(":perfil_sat_id"), sqlite::texto(perfilId.texto()));
    if (auto r = sqlite::ejecutar(q, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    return R::exito(q.numRowsAffected() > 0);
}

Resultado<QList<CredencialRef>, ErrorPersistencia> SqliteCredencialSatRepository::listarReferenciasVigentes()
{
    using R = Resultado<QList<CredencialRef>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"credencial_sat.listar_referencias";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (auto r = sqlite::ejecutarDirecto(
            q,
            QStringLiteral("SELECT certificado_ref, llave_privada_ref, contrasena_ref "
                           "FROM credencial_sat ORDER BY id"),
            kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    QList<CredencialRef> refs;
    while (q.next()) {
        const auto ref = CredencialRef::desdeReferencias(q.value(0).toString(), q.value(1).toString(),
                                                         q.value(2).toString());
        if (!ref) {
            // Fail-safe: sin lista parcial (reconciliar borraria una vigente).
            return R::fallo(sqlite::filaIlegible(u"credencial_sat", u"referencias"));
        }
        refs.append(*ref);
    }
    return R::exito(std::move(refs));
}

} // namespace satcfdi
