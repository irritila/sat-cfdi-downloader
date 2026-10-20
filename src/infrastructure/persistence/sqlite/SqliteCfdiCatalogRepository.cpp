#include "infrastructure/persistence/sqlite/SqliteCfdiFilas.h"
#include "infrastructure/persistence/sqlite/SqliteConnectionProvider.h"
#include "infrastructure/persistence/sqlite/SqliteRepositorios.h"
#include "infrastructure/persistence/sqlite/SqliteSoporte.h"

#include "domain/common/TimestampUtc.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>

#include <utility>

// T015.2: catalogo de CFDI sobre la migracion 006 (indice_cfdi_paquete y
// cfdi_indexado). Ningun valor de fila sale en errores ni logs (D11).

namespace satcfdi {

using sqlite::kColumnasCfdi;
using sqlite::leerCfdi;
using sqlite::leerTexto;
using sqlite::patronLike;

namespace {

constexpr QStringView kTablaIndice = u"indice_cfdi_paquete";

QLatin1StringView claveMotivo(MotivoOmision motivo)
{
    switch (motivo) {
    case MotivoOmision::RutaInvalida:
        return QLatin1StringView("ruta-invalida");
    case MotivoOmision::Enlace:
        return QLatin1StringView("enlace");
    case MotivoOmision::Cifrada:
        return QLatin1StringView("cifrada");
    case MotivoOmision::NoXml:
        return QLatin1StringView("no-xml");
    case MotivoOmision::ExcedeTamano:
        return QLatin1StringView("excede-tamano");
    case MotivoOmision::RazonCompresion:
        return QLatin1StringView("razon-compresion");
    case MotivoOmision::ErrorLectura:
        return QLatin1StringView("error-lectura");
    }
    return QLatin1StringView("error-lectura");
}

QString omitidasJson(const QMap<MotivoOmision, int>& omitidas)
{
    QJsonObject objeto;
    for (auto it = omitidas.cbegin(); it != omitidas.cend(); ++it) {
        if (it.value() > 0) {
            objeto.insert(claveMotivo(it.key()), it.value());
        }
    }
    return QString::fromUtf8(QJsonDocument(objeto).toJson(QJsonDocument::Compact));
}

// Texto opcional: vacio -> NULL.
QVariant textoONulo(const QString& valor)
{
    return valor.isEmpty() ? QVariant(QMetaType::fromType<QString>()) : QVariant(valor);
}

QVariant importeONulo(const std::optional<ImporteCfdi>& importe)
{
    return importe ? QVariant(importe->texto()) : QVariant(QMetaType::fromType<QString>());
}

QVariant complementosJson(const QList<ComplementoNoRepresentado>& complementos)
{
    if (complementos.isEmpty()) {
        return QVariant(QMetaType::fromType<QString>());
    }
    QJsonArray arreglo;
    for (const ComplementoNoRepresentado& c : complementos) {
        arreglo.append(QJsonObject{{QStringLiteral("ns"), c.espacioNombres}, {QStringLiteral("nombre"), c.nombreLocal}});
    }
    return QString::fromUtf8(QJsonDocument(arreglo).toJson(QJsonDocument::Compact));
}

QVariant camposJson(const QStringList& campos)
{
    if (campos.isEmpty()) {
        return QVariant(QMetaType::fromType<QString>());
    }
    return QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(campos)).toJson(QJsonDocument::Compact));
}

// Coherencia previa a escribir (los CHECK de 006 la garantizan igual).
bool indiceCoherente(const IndicePaquete& i)
{
    const bool conCausa = i.estado == EstadoIndicePaquete::OrigenNoDisponible
                          || i.estado == EstadoIndicePaquete::Ilegible;
    if (conCausa == i.causa.isEmpty()) {
        return false;
    }
    if (i.estado != EstadoIndicePaquete::Indexado && !i.cfdis.isEmpty()) {
        return false;
    }
    return i.versionParser > 0 && !i.paqueteId.isEmpty();
}

} // namespace

SqliteCfdiCatalogRepository::SqliteCfdiCatalogRepository(SqliteConnectionProvider& proveedor)
    : m_proveedor(proveedor)
{
}

Resultado<QList<PaqueteIndexable>, ErrorPersistencia>
SqliteCfdiCatalogRepository::listarPaquetesIndexables(const std::optional<SolicitudId>& solicitud)
{
    using R = Resultado<QList<PaqueteIndexable>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"indice_cfdi_paquete.listar_indexables";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    q.setForwardOnly(true);
    QString sql = QStringLiteral(
        "SELECT p.id, p.solicitud_masiva_id, p.ruta_local, i.estado, i.tamano_bytes, i.modificado_en_ms, "
        "i.sha256, i.version_parser "
        "FROM paquete_solicitud p "
        "JOIN solicitud_masiva s ON s.id = p.solicitud_masiva_id AND s.eliminado_en IS NULL "
        "LEFT JOIN indice_cfdi_paquete i ON i.paquete_solicitud_id = p.id "
        "WHERE p.eliminado_en IS NULL AND p.estado_descarga = 'Descargado'");
    if (solicitud) {
        sql += QStringLiteral(" AND p.solicitud_masiva_id = :solicitud");
    }
    sql += QStringLiteral(" ORDER BY p.solicitud_masiva_id, p.disponible_en, p.id_paquete_sat");
    if (auto r = sqlite::preparar(q, sql, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    if (solicitud) {
        q.bindValue(QStringLiteral(":solicitud"), sqlite::texto(solicitud->texto()));
    }
    if (auto r = sqlite::ejecutar(q, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    QList<PaqueteIndexable> lista;
    while (q.next()) {
        PaqueteIndexable p;
        p.paqueteId = q.value(0).toString();
        const auto sid = SolicitudId::desdeTexto(q.value(1).toString());
        if (!sid) {
            return R::fallo(sqlite::filaIlegible(u"paquete_solicitud", u"solicitud_masiva_id"));
        }
        p.solicitudId = *sid;
        p.rutaLocal = leerTexto(q.value(2));
        if (!q.value(3).isNull()) {
            const auto estado = estadoIndiceDesdeClave(q.value(3).toString());
            if (!estado) {
                return R::fallo(sqlite::filaIlegible(kTablaIndice, u"estado"));
            }
            p.estado = estado;
            p.versionParser = q.value(7).toInt();
            if (!q.value(6).isNull()) {
                p.huella = HuellaZip{q.value(4).toLongLong(), q.value(5).toLongLong(), q.value(6).toString()};
            }
        }
        lista.append(std::move(p));
    }
    return R::exito(std::move(lista));
}

Resultado<bool, ErrorPersistencia> SqliteCfdiCatalogRepository::reemplazarIndicePaquete(const IndicePaquete& indice)
{
    using R = Resultado<bool, ErrorPersistencia>;
    constexpr QStringView kContexto = u"indice_cfdi_paquete.reemplazar";
    auto conexion = sqlite::conexionEscritura(m_proveedor, kContexto);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    if (!indiceCoherente(indice)) {
        return R::fallo(ErrorPersistencia::de(ErrorPersistencia::Tipo::Integridad,
                                              QStringLiteral("indice_cfdi_paquete: estado incoherente")));
    }
    QSqlDatabase db = conexion.valor();

    // 1. El paquete sigue visible y Descargado (revalidado bajo BEGIN IMMEDIATE).
    QString solicitudId;
    {
        QSqlQuery q(db);
        q.setForwardOnly(true);
        if (auto r = sqlite::preparar(q,
                                      QStringLiteral("SELECT p.solicitud_masiva_id FROM paquete_solicitud p "
                                                     "JOIN solicitud_masiva s ON s.id = p.solicitud_masiva_id "
                                                     "WHERE p.id = :id AND p.eliminado_en IS NULL "
                                                     "AND s.eliminado_en IS NULL AND p.estado_descarga = 'Descargado'"),
                                      kContexto);
            !r) {
            return R::fallo(std::move(r).error());
        }
        q.bindValue(QStringLiteral(":id"), sqlite::texto(indice.paqueteId));
        if (auto r = sqlite::ejecutar(q, kContexto); !r) {
            return R::fallo(std::move(r).error());
        }
        if (!q.next()) {
            return R::exito(false);
        }
        solicitudId = q.value(0).toString();
    }

    // 2. Borra lo derivado del paquete (la fila de indice se conserva y se
    //    actualiza: nunca REPLACE, que dispararia la cascada a destiempo).
    {
        QSqlQuery q(db);
        if (auto r = sqlite::preparar(q, QStringLiteral("DELETE FROM cfdi_indexado WHERE paquete_solicitud_id = :id"),
                                      kContexto);
            !r) {
            return R::fallo(std::move(r).error());
        }
        q.bindValue(QStringLiteral(":id"), sqlite::texto(indice.paqueteId));
        if (auto r = sqlite::ejecutar(q, kContexto); !r) {
            return R::fallo(std::move(r).error());
        }
    }

    // 3. Estado, huella y conteos.
    {
        QSqlQuery q(db);
        if (auto r = sqlite::preparar(
                q,
                QStringLiteral(
                    "INSERT INTO indice_cfdi_paquete (paquete_solicitud_id, estado, tamano_bytes, modificado_en_ms, "
                    "sha256, version_parser, xml_leidos, cfdi_ilegibles, cfdi_no_soportados, xml_no_cfdi, "
                    "entradas_omitidas, omitidas_json, causa, actualizado_en) VALUES (:id, :estado, :tamano, "
                    ":mtime, :sha, :version, :xml, :ilegibles, :nosoportados, :nocfdi, :omitidas, :omitidasjson, "
                    ":causa, :en) "
                    "ON CONFLICT (paquete_solicitud_id) DO UPDATE SET estado = excluded.estado, "
                    "tamano_bytes = excluded.tamano_bytes, modificado_en_ms = excluded.modificado_en_ms, "
                    "sha256 = excluded.sha256, version_parser = excluded.version_parser, "
                    "xml_leidos = excluded.xml_leidos, cfdi_ilegibles = excluded.cfdi_ilegibles, "
                    "cfdi_no_soportados = excluded.cfdi_no_soportados, xml_no_cfdi = excluded.xml_no_cfdi, "
                    "entradas_omitidas = excluded.entradas_omitidas, omitidas_json = excluded.omitidas_json, "
                    "causa = excluded.causa, actualizado_en = excluded.actualizado_en"),
                kContexto);
            !r) {
            return R::fallo(std::move(r).error());
        }
        const QVariant nulo(QMetaType::fromType<qlonglong>());
        const QVariant textoNulo(QMetaType::fromType<QString>());
        q.bindValue(QStringLiteral(":id"), sqlite::texto(indice.paqueteId));
        q.bindValue(QStringLiteral(":estado"), sqlite::texto(QString(claveEstadoIndice(indice.estado))));
        q.bindValue(QStringLiteral(":tamano"), indice.huella ? sqlite::entero(indice.huella->tamano) : nulo);
        q.bindValue(QStringLiteral(":mtime"), indice.huella ? sqlite::entero(indice.huella->modificadoMs) : nulo);
        q.bindValue(QStringLiteral(":sha"), indice.huella ? sqlite::texto(indice.huella->sha256) : textoNulo);
        q.bindValue(QStringLiteral(":version"), sqlite::entero(indice.versionParser));
        q.bindValue(QStringLiteral(":xml"), sqlite::entero(indice.conteos.xmlLeidos));
        q.bindValue(QStringLiteral(":ilegibles"), sqlite::entero(indice.conteos.cfdiIlegibles));
        q.bindValue(QStringLiteral(":nosoportados"), sqlite::entero(indice.conteos.cfdiNoSoportados));
        q.bindValue(QStringLiteral(":nocfdi"), sqlite::entero(indice.conteos.xmlNoCfdi));
        q.bindValue(QStringLiteral(":omitidas"), sqlite::entero(indice.conteos.totalOmitidas()));
        q.bindValue(QStringLiteral(":omitidasjson"), sqlite::texto(omitidasJson(indice.conteos.omitidas)));
        q.bindValue(QStringLiteral(":causa"), textoONulo(indice.causa));
        q.bindValue(QStringLiteral(":en"), sqlite::instante(indice.actualizadoEn));
        if (auto r = sqlite::ejecutar(q, kContexto); !r) {
            return R::fallo(std::move(r).error());
        }
    }

    // 4. CFDI del paquete (una sentencia preparada, una ejecucion por fila).
    if (!indice.cfdis.isEmpty()) {
        QSqlQuery q(db);
        if (auto r = sqlite::preparar(
                q,
                QStringLiteral("INSERT INTO cfdi_indexado (") + kColumnasCfdi
                    + QStringLiteral(", solicitud_masiva_id, texto_busqueda) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
                                     "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"),
                kContexto);
            !r) {
            return R::fallo(std::move(r).error());
        }
        const QVariant tipoNulo(QMetaType::fromType<QString>());
        for (const CfdiIndexado& c : indice.cfdis) {
            const ResumenCfdi& r = c.resumen;
            const QString tipo = QString(codigoTipoComprobante(r.tipo));
            int i = 0;
            q.bindValue(i++, sqlite::texto(indice.paqueteId));
            q.bindValue(i++, sqlite::entero(c.posicion));
            q.bindValue(i++, sqlite::texto(QString(claveEstadoParseo(r.estado))));
            q.bindValue(i++, textoONulo(r.version));
            q.bindValue(i++, tipo.isEmpty() ? tipoNulo : QVariant(tipo));
            q.bindValue(i++, textoONulo(r.fecha));
            q.bindValue(i++, textoONulo(r.serie));
            q.bindValue(i++, textoONulo(r.folio));
            q.bindValue(i++, textoONulo(r.emisor.rfc));
            q.bindValue(i++, textoONulo(r.emisor.nombre));
            q.bindValue(i++, textoONulo(r.emisor.regimenFiscal));
            q.bindValue(i++, textoONulo(r.receptor.rfc));
            q.bindValue(i++, textoONulo(r.receptor.nombre));
            q.bindValue(i++, textoONulo(r.receptor.regimenFiscal));
            q.bindValue(i++, textoONulo(r.receptor.usoCfdi));
            q.bindValue(i++, textoONulo(r.receptor.domicilioFiscal));
            q.bindValue(i++, textoONulo(r.lugarExpedicion));
            q.bindValue(i++, importeONulo(r.subtotal));
            q.bindValue(i++, importeONulo(r.descuento));
            q.bindValue(i++, importeONulo(r.total));
            q.bindValue(i++, textoONulo(r.moneda));
            q.bindValue(i++, importeONulo(r.tipoCambio));
            q.bindValue(i++, textoONulo(r.metodoPago));
            q.bindValue(i++, textoONulo(r.formaPago));
            q.bindValue(i++, textoONulo(r.uuid));
            q.bindValue(i++, complementosJson(r.complementosNoRepresentados));
            q.bindValue(i++, camposJson(r.camposInvalidos));
            q.bindValue(i++, sqlite::texto(solicitudId));
            q.bindValue(i++, sqlite::texto(c.textoBusqueda));
            if (auto e = sqlite::ejecutar(q, kContexto); !e) {
                return R::fallo(std::move(e).error());
            }
        }
    }
    return R::exito(true);
}

Resultado<bool, ErrorPersistencia> SqliteCfdiCatalogRepository::actualizarMetadatosHuella(const QString& paqueteId,
                                                                                         const HuellaZip& huella,
                                                                                         const QDateTime& actualizadoEn)
{
    using R = Resultado<bool, ErrorPersistencia>;
    constexpr QStringView kContexto = u"indice_cfdi_paquete.actualizar_huella";
    auto conexion = sqlite::conexionEscritura(m_proveedor, kContexto);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    if (auto r = sqlite::preparar(q,
                                  QStringLiteral("UPDATE indice_cfdi_paquete SET tamano_bytes = :tamano, "
                                                 "modificado_en_ms = :mtime, actualizado_en = :en "
                                                 "WHERE paquete_solicitud_id = :id AND sha256 = :sha"),
                                  kContexto);
        !r) {
        return R::fallo(std::move(r).error());
    }
    q.bindValue(QStringLiteral(":tamano"), sqlite::entero(huella.tamano));
    q.bindValue(QStringLiteral(":mtime"), sqlite::entero(huella.modificadoMs));
    q.bindValue(QStringLiteral(":en"), sqlite::instante(actualizadoEn));
    q.bindValue(QStringLiteral(":id"), sqlite::texto(paqueteId));
    q.bindValue(QStringLiteral(":sha"), sqlite::texto(huella.sha256));
    if (auto r = sqlite::ejecutar(q, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    return R::exito(q.numRowsAffected() > 0);
}

Resultado<QList<CfdiListado>, ErrorPersistencia> SqliteCfdiCatalogRepository::listarCfdi(const SolicitudId& solicitud,
                                                                                        const FiltroListaCfdi& filtro)
{
    using R = Resultado<QList<CfdiListado>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"cfdi_indexado.listar";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    QSqlQuery q(db);
    q.setForwardOnly(true);
    const QString texto = normalizarTextoBusqueda(filtro.texto);
    const QString tipo = filtro.tipo ? QString(codigoTipoComprobante(*filtro.tipo)) : QString();
    QString sql = QStringLiteral("SELECT ") + kColumnasCfdi
                  + QStringLiteral(" FROM cfdi_indexado WHERE solicitud_masiva_id = :solicitud");
    if (filtro.tipo) {
        sql += QStringLiteral(" AND tipo_comprobante = :tipo");
    }
    if (!texto.isEmpty()) {
        sql += QStringLiteral(" AND texto_busqueda LIKE :texto ESCAPE '\\'");
    }
    sql += filtro.orden == OrdenListaCfdi::FechaDescendente
               ? QStringLiteral(" ORDER BY fecha DESC, paquete_solicitud_id, posicion")
               : QStringLiteral(" ORDER BY fecha IS NULL, fecha ASC, paquete_solicitud_id, posicion");
    if (filtro.limite > 0) {
        sql += QStringLiteral(" LIMIT :limite");
    }
    if (auto r = sqlite::preparar(q, sql, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    q.bindValue(QStringLiteral(":solicitud"), sqlite::texto(solicitud.texto()));
    if (filtro.tipo) {
        // Desconocido no tiene clave: no coincide con ninguna fila.
        q.bindValue(QStringLiteral(":tipo"), sqlite::texto(tipo));
    }
    if (!texto.isEmpty()) {
        q.bindValue(QStringLiteral(":texto"), sqlite::texto(patronLike(texto)));
    }
    if (filtro.limite > 0) {
        q.bindValue(QStringLiteral(":limite"), sqlite::entero(filtro.limite));
    }
    if (auto r = sqlite::ejecutar(q, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    QList<CfdiListado> lista;
    while (q.next()) {
        auto fila = leerCfdi(q);
        if (!fila) {
            return R::fallo(std::move(fila).error());
        }
        lista.append(std::move(fila).valor());
    }
    return R::exito(std::move(lista));
}

Resultado<ResumenIndiceSolicitud, ErrorPersistencia>
SqliteCfdiCatalogRepository::resumirSolicitud(const SolicitudId& solicitud, int versionParser)
{
    using R = Resultado<ResumenIndiceSolicitud, ErrorPersistencia>;
    constexpr QStringView kContexto = u"indice_cfdi_paquete.resumir";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    ResumenIndiceSolicitud resumen;
    {
        QSqlQuery q(db);
        q.setForwardOnly(true);
        if (auto r = sqlite::preparar(
                q,
                QStringLiteral(
                    "SELECT count(*), "
                    "coalesce(sum(i.estado = 'Indexado' AND i.version_parser = :v1), 0), "
                    "coalesce(sum(i.estado IS NULL OR i.estado = 'Pendiente' OR i.version_parser <> :v2), 0), "
                    "coalesce(sum(i.estado = 'OrigenNoDisponible' AND i.version_parser = :v3), 0), "
                    "coalesce(sum(i.estado = 'Ilegible' AND i.version_parser = :v4), 0), "
                    "coalesce(sum(CASE WHEN i.estado = 'Indexado' THEN i.xml_no_cfdi ELSE 0 END), 0), "
                    "coalesce(sum(CASE WHEN i.estado = 'Indexado' THEN i.entradas_omitidas ELSE 0 END), 0) "
                    "FROM paquete_solicitud p "
                    "JOIN solicitud_masiva s ON s.id = p.solicitud_masiva_id AND s.eliminado_en IS NULL "
                    "LEFT JOIN indice_cfdi_paquete i ON i.paquete_solicitud_id = p.id "
                    "WHERE p.solicitud_masiva_id = :s AND p.eliminado_en IS NULL "
                    "AND p.estado_descarga = 'Descargado'"),
                kContexto);
            !r) {
            return R::fallo(std::move(r).error());
        }
        // Un marcador por aparicion (sin depender de marcadores repetidos).
        for (const QString& marcador : {QStringLiteral(":v1"), QStringLiteral(":v2"), QStringLiteral(":v3"),
                                        QStringLiteral(":v4")}) {
            q.bindValue(marcador, sqlite::entero(versionParser));
        }
        q.bindValue(QStringLiteral(":s"), sqlite::texto(solicitud.texto()));
        if (auto r = sqlite::ejecutar(q, kContexto); !r) {
            return R::fallo(std::move(r).error());
        }
        if (q.next()) {
            resumen.paquetesDescargados = q.value(0).toInt();
            resumen.paquetesIndexados = q.value(1).toInt();
            resumen.paquetesPendientes = q.value(2).toInt();
            resumen.paquetesNoDisponibles = q.value(3).toInt();
            resumen.paquetesIlegibles = q.value(4).toInt();
            resumen.xmlNoCfdi = q.value(5).toInt();
            resumen.entradasOmitidas = q.value(6).toInt();
        }
    }
    {
        QSqlQuery q(db);
        q.setForwardOnly(true);
        if (auto r = sqlite::preparar(q,
                                      QStringLiteral("SELECT count(*), "
                                                     "coalesce(sum(estado_parseo = 'Ilegible'), 0), "
                                                     "coalesce(sum(estado_parseo = 'VersionNoSoportada'), 0), "
                                                     "coalesce(sum(estado_parseo = 'Parcial'), 0) "
                                                     "FROM cfdi_indexado WHERE solicitud_masiva_id = :s"),
                                      kContexto);
            !r) {
            return R::fallo(std::move(r).error());
        }
        q.bindValue(QStringLiteral(":s"), sqlite::texto(solicitud.texto()));
        if (auto r = sqlite::ejecutar(q, kContexto); !r) {
            return R::fallo(std::move(r).error());
        }
        if (q.next()) {
            resumen.cfdiTotal = q.value(0).toInt();
            resumen.cfdiIlegibles = q.value(1).toInt();
            resumen.cfdiNoSoportados = q.value(2).toInt();
            resumen.cfdiParciales = q.value(3).toInt();
        }
    }
    return R::exito(resumen);
}

} // namespace satcfdi
