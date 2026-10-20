#include "TestSqliteCatalogoCfdi.h"

#include "SqlitePruebasComun.h"

#include "domain/cfdi/IndiceCfdi.h"
#include "ports/repositories/CfdiCatalogRepository.h"
#include "ports/repositories/PaqueteSolicitudRepository.h"
#include "ports/repositories/SolicitudMasivaRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <QTest>
#include <QTimeZone>

#include <memory>

using namespace satcfdi;
using namespace satcfdi::pruebas;

namespace {

const QDateTime kT0 = QDateTime(QDate(2026, 10, 11), QTime(12, 0, 0), QTimeZone(QTimeZone::UTC));
const QString kSha1 = QString(64, QLatin1Char('a'));
const QString kSha2 = QString(64, QLatin1Char('b'));

bool consistente(SqliteConnectionProvider& prov)
{
    return escalar(prov, QStringLiteral("PRAGMA integrity_check")).toString() == QStringLiteral("ok")
           && columna(prov, QStringLiteral("PRAGMA foreign_key_check")).isEmpty();
}

SolicitudId sid(const QString& texto)
{
    return *SolicitudId::desdeTexto(texto);
}

ResumenCfdi resumen(const QString& fecha, TipoComprobante tipo, const QString& emisor, const QString& nombre,
                    const QString& uuid, EstadoParseo estado = EstadoParseo::Completo)
{
    ResumenCfdi r;
    r.estado = estado;
    r.version = QStringLiteral("4.0");
    r.tipo = tipo;
    r.fecha = fecha;
    r.serie = QStringLiteral("A");
    r.folio = QStringLiteral("100");
    r.emisor.rfc = emisor;
    r.emisor.nombre = nombre;
    r.receptor.rfc = QStringLiteral("XAXX010101000");
    r.receptor.nombre = QStringLiteral("PUBLICO EN GENERAL");
    r.subtotal = ImporteCfdi::desdeTexto(u"100.50");
    r.total = ImporteCfdi::desdeTexto(u"116.58");
    r.moneda = QStringLiteral("MXN");
    r.uuid = uuid;
    return r;
}

CfdiIndexado cfdi(int posicion, ResumenCfdi r)
{
    CfdiIndexado c;
    c.posicion = posicion;
    c.textoBusqueda = textoBusquedaCfdi(r);
    c.resumen = std::move(r);
    return c;
}

IndicePaquete indexado(const QString& paqueteId, QList<CfdiIndexado> cfdis, const QString& sha = kSha1)
{
    IndicePaquete i;
    i.paqueteId = paqueteId;
    i.estado = EstadoIndicePaquete::Indexado;
    i.huella = HuellaZip{1234, 1700000000000, sha};
    i.versionParser = 1;
    i.conteos.xmlLeidos = int(cfdis.size()) + 1;
    i.conteos.xmlNoCfdi = 1;
    i.conteos.omitidas[MotivoOmision::NoXml] = 2;
    i.cfdis = std::move(cfdis);
    i.actualizadoEn = kT0;
    return i;
}

struct Entorno {
    QTemporaryDir dir;
    std::unique_ptr<SqlitePersistencia> p;
    QString perfil;

    bool preparar()
    {
        if (!dir.isValid() || !inicializarBaseSqlite(rutaBase(dir))) {
            return false;
        }
        p = std::make_unique<SqlitePersistencia>(rutaBase(dir));
        perfil = uuid::generarCanonico();
        return insertarPerfilFixture(p->proveedor(), perfil, kRfcPerfil).isEmpty();
    }
    ~Entorno()
    {
        if (p) {
            p->cerrarConexionDelHiloActual();
        }
    }

    QString solicitud(const QString& semilla)
    {
        const SolicitudFixture s = fixture(perfil, DedupKey::calcularV1(semilla.toUtf8()), QStringLiteral("Enviada"),
                                           QStringLiteral("Terminada"));
        return insertarSolicitudFixture(p->proveedor(), s).isEmpty() ? s.id : QString();
    }

    QString paquete(const QString& solicitudId, const QString& idSat,
                    const QString& estado = QStringLiteral("Descargado"))
    {
        const QString id = uuid::generarCanonico();
        return insertarPaqueteFixture(p->proveedor(), id, solicitudId, idSat, estado).isEmpty() ? id : QString();
    }

    CfdiCatalogRepository& cat() { return p->catalogoCfdi(); }

    Resultado<bool, ErrorPersistencia> reemplazar(const IndicePaquete& i)
    {
        (void)p->unidadDeTrabajo().begin();
        auto r = cat().reemplazarIndicePaquete(i);
        if (r && r.valor()) {
            (void)p->unidadDeTrabajo().commit();
        } else {
            (void)p->unidadDeTrabajo().rollback();
        }
        return r;
    }

    int contar(const QString& sql) { return escalar(p->proveedor(), sql).toInt(); }
};

} // namespace

void TestSqliteCatalogoCfdi::migracion006SobreBase005ConDatos()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ruta = rutaBase(dir);
    auto embebidas = migracionesSqliteEmbebidas();
    QVERIFY(embebidas);
    QVERIFY(embebidas.valor().size() >= 6);
    // Base con el esquema de T014 (001-005) y datos.
    QVERIFY(inicializarBaseSqlite(ruta, embebidas.valor().mid(0, 5)));
    const QString huellaSql = QStringLiteral(
        "SELECT (SELECT group_concat(id || rfc, ',') FROM perfil_sat) || '|' || "
        "(SELECT group_concat(id || estado_local || ifnull(estado_solicitud_sat, '-'), ',') FROM solicitud_masiva) "
        "|| '|' || (SELECT group_concat(id || estado_descarga || ifnull(ruta_local, '-') || "
        "ifnull(eliminado_en, '-'), ',') FROM paquete_solicitud) || '|' || "
        "(SELECT count(*) FROM aviso_vencimiento_efirma)");
    QString huellaAntes;
    QString paqueteDescargado;
    QString solicitudId;
    {
        SqliteConnectionProvider prov(ruta);
        const QString perfil = uuid::generarCanonico();
        QCOMPARE(insertarPerfilFixture(prov, perfil, kRfcPerfil), QString());
        const SolicitudFixture s = fixture(perfil, DedupKey::calcularV1("m006"), QStringLiteral("Enviada"),
                                           QStringLiteral("Terminada"));
        QCOMPARE(insertarSolicitudFixture(prov, s), QString());
        solicitudId = s.id;
        paqueteDescargado = uuid::generarCanonico();
        QCOMPARE(insertarPaqueteFixture(prov, paqueteDescargado, s.id, QStringLiteral("PAQ_1"),
                                        QStringLiteral("Descargado")),
                 QString());
        QCOMPARE(insertarPaqueteFixture(prov, uuid::generarCanonico(), s.id, QStringLiteral("PAQ_2"),
                                        QStringLiteral("Error")),
                 QString());
        QCOMPARE(ejecutarSql(prov, QStringLiteral("INSERT INTO aviso_vencimiento_efirma VALUES (%1, %2, 30, %2)")
                                       .arg(citar(perfil), citar(kAhora))),
                 QString());
        QVERIFY(!ejecutarSql(prov, QStringLiteral("SELECT * FROM cfdi_indexado")).isEmpty()); // aun no existe
        huellaAntes = escalar(prov, huellaSql).toString();
        QVERIFY(!huellaAntes.isEmpty());
        prov.cerrarConexionDelHiloActual();
    }

    // Solo hasta 006: esta prueba fija el efecto de 006 (007 tiene la suya).
    auto migrada = inicializarBaseSqlite(ruta, embebidas.valor().mid(0, 6));
    QVERIFY2(migrada, migrada ? "" : qPrintable(migrada.error().mensaje));
    QCOMPARE(migrada.valor().migracion.versionInicial, 5);
    QCOMPARE(migrada.valor().migracion.aplicadas, QList<int>{6});
    QCOMPARE(migrada.valor().migracion.versionFinal, 6);
    {
        SqliteConnectionProvider prov(ruta);
        // paquete_solicitud y el resto no cambian; el indice nace vacio.
        QCOMPARE(escalar(prov, huellaSql).toString(), huellaAntes);
        QCOMPARE(escalar(prov, QStringLiteral("SELECT count(*) FROM indice_cfdi_paquete")).toInt(), 0);
        QCOMPARE(escalar(prov, QStringLiteral("SELECT count(*) FROM cfdi_indexado")).toInt(), 0);
        QCOMPARE(columna(prov, QStringLiteral("SELECT name FROM sqlite_master WHERE type IN ('index', 'trigger') "
                                              "AND tbl_name IN ('cfdi_indexado', 'paquete_solicitud') "
                                              "AND name LIKE '%cfdi%' AND name NOT LIKE 'sqlite_%' "
                                              "ORDER BY name")),
                 (QStringList{QStringLiteral("ix_cfdi_indexado_lista"),
                              QStringLiteral("tr_paquete_solicitud_eliminado_indice_cfdi")}));
        // CHECKs y FK de 006.
        QVERIFY(ejecutarSql(prov, QStringLiteral("INSERT INTO indice_cfdi_paquete (paquete_solicitud_id, estado, "
                                                 "version_parser, actualizado_en) VALUES (%1, 'Otro', 1, %2)")
                                      .arg(citar(paqueteDescargado), citar(kAhora)))
                    .contains(QStringLiteral("CHECK")));
        QVERIFY(ejecutarSql(prov, QStringLiteral("INSERT INTO indice_cfdi_paquete (paquete_solicitud_id, estado, "
                                                 "version_parser, actualizado_en) VALUES (%1, 'Indexado', 1, %2)")
                                      .arg(citar(paqueteDescargado), citar(kAhora)))
                    .contains(QStringLiteral("CHECK"))); // Indexado sin huella
        QVERIFY(ejecutarSql(prov, QStringLiteral("INSERT INTO indice_cfdi_paquete (paquete_solicitud_id, estado, "
                                                 "version_parser, actualizado_en) VALUES (%1, 'Pendiente', 1, %2)")
                                      .arg(citar(uuid::generarCanonico()), citar(kAhora)))
                    .contains(QStringLiteral("FOREIGN KEY")));
        QVERIFY(consistente(prov));
        prov.cerrarConexionDelHiloActual();
    }

    // Idempotente: una segunda apertura no aplica nada ni cambia objetos.
    auto repetida = inicializarBaseSqlite(ruta, embebidas.valor().mid(0, 6));
    QVERIFY(repetida);
    QVERIFY(repetida.valor().migracion.aplicadas.isEmpty());
    QCOMPARE(repetida.valor().migracion.versionInicial, 6);

    // El repositorio ve el paquete Descargado de la base T014 como pendiente.
    SqlitePersistencia p(ruta);
    auto indexables = p.catalogoCfdi().listarPaquetesIndexables(std::nullopt);
    QVERIFY(indexables);
    QCOMPARE(indexables.valor().size(), 1);
    QCOMPARE(indexables.valor().first().paqueteId, paqueteDescargado);
    QVERIFY(!indexables.valor().first().estado.has_value());
    auto resumenT014 = p.catalogoCfdi().resumirSolicitud(sid(solicitudId), 1);
    QVERIFY(resumenT014);
    QCOMPARE(resumenT014.valor().paquetesDescargados, 1);
    QCOMPARE(resumenT014.valor().paquetesPendientes, 1);
    p.cerrarConexionDelHiloActual();
}

void TestSqliteCatalogoCfdi::listarPaquetesIndexables()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString s1 = e.solicitud(QStringLiteral("s1"));
    const QString s2 = e.solicitud(QStringLiteral("s2"));
    const QString a = e.paquete(s1, QStringLiteral("A"));
    const QString b = e.paquete(s1, QStringLiteral("B"));
    QVERIFY(!e.paquete(s1, QStringLiteral("C"), QStringLiteral("Disponible")).isEmpty());
    const QString d = e.paquete(s2, QStringLiteral("D"));
    QVERIFY(!a.isEmpty() && !b.isEmpty() && !d.isEmpty());
    QVERIFY(e.reemplazar(indexado(a, {})).valor());

    auto todos = e.cat().listarPaquetesIndexables(std::nullopt);
    QVERIFY(todos);
    QCOMPARE(todos.valor().size(), 3); // solo Descargado
    auto deS1 = e.cat().listarPaquetesIndexables(sid(s1));
    QVERIFY(deS1);
    QCOMPARE(deS1.valor().size(), 2);
    QCOMPARE(deS1.valor().at(0).paqueteId, a);
    QCOMPARE(deS1.valor().at(0).estado, std::optional(EstadoIndicePaquete::Indexado));
    QCOMPARE(deS1.valor().at(0).huella, std::optional(HuellaZip{1234, 1700000000000, kSha1}));
    QCOMPARE(deS1.valor().at(0).versionParser, 1);
    QCOMPARE(deS1.valor().at(0).rutaLocal, QStringLiteral("paquetes/p.zip"));
    QCOMPARE(deS1.valor().at(1).paqueteId, b);
    QVERIFY(!deS1.valor().at(1).estado.has_value());
    QCOMPARE(deS1.valor().at(1).solicitudId, sid(s1));
}

void TestSqliteCatalogoCfdi::reemplazoEnUnaTransaccion()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString s = e.solicitud(QStringLiteral("tx"));
    const QString paq = e.paquete(s, QStringLiteral("P1"));
    QVERIFY(!paq.isEmpty());

    // Sin UnitOfWork: Transaccion, sin escribir nada.
    auto sinTx = e.cat().reemplazarIndicePaquete(indexado(paq, {cfdi(0, resumen("2026-01-01T00:00:00",
                                                                                TipoComprobante::Ingreso,
                                                                                "AAA010101AAA", "Uno", "U1"))}));
    QVERIFY(!sinTx && sinTx.error().tipo == ErrorPersistencia::Tipo::Transaccion);
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM indice_cfdi_paquete")), 0);

    // Tres CFDI.
    QList<CfdiIndexado> tres;
    for (int i = 0; i < 3; ++i) {
        tres.append(cfdi(i * 2, resumen(QStringLiteral("2026-01-0%1T00:00:00").arg(i + 1), TipoComprobante::Ingreso,
                                        QStringLiteral("AAA010101AAA"), QStringLiteral("Emisor"),
                                        QStringLiteral("UUID-%1").arg(i))));
    }
    QVERIFY(e.reemplazar(indexado(paq, tres)).valor());
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM cfdi_indexado")), 3);
    QCOMPARE(escalar(e.p->proveedor(), QStringLiteral("SELECT omitidas_json FROM indice_cfdi_paquete")).toString(),
             QStringLiteral("{\"no-xml\":2}"));
    QCOMPARE(e.contar(QStringLiteral("SELECT entradas_omitidas FROM indice_cfdi_paquete")), 2);

    // Rollback a mitad: lo anterior se conserva intacto.
    (void)e.p->unidadDeTrabajo().begin();
    QVERIFY(e.cat().reemplazarIndicePaquete(indexado(paq, {tres.first()}, kSha2)).valor());
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM cfdi_indexado")), 1);
    (void)e.p->unidadDeTrabajo().rollback();
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM cfdi_indexado")), 3);
    QCOMPARE(escalar(e.p->proveedor(), QStringLiteral("SELECT sha256 FROM indice_cfdi_paquete")).toString(), kSha1);

    // Reemplazo confirmado: borra lo derivado e inserta lo nuevo.
    QVERIFY(e.reemplazar(indexado(paq, {tres.first()}, kSha2)).valor());
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM cfdi_indexado")), 1);
    QCOMPARE(escalar(e.p->proveedor(), QStringLiteral("SELECT sha256 FROM indice_cfdi_paquete")).toString(), kSha2);

    // Pasar a OrigenNoDisponible borra los CFDI y la huella.
    IndicePaquete nd;
    nd.paqueteId = paq;
    nd.estado = EstadoIndicePaquete::OrigenNoDisponible;
    nd.causa = QStringLiteral("no-existe");
    nd.actualizadoEn = kT0;
    QVERIFY(e.reemplazar(nd).valor());
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM cfdi_indexado")), 0);
    QVERIFY(escalar(e.p->proveedor(), QStringLiteral("SELECT sha256 FROM indice_cfdi_paquete")).isNull());

    // Ilegible conserva la huella y no tiene CFDI.
    IndicePaquete il = nd;
    il.estado = EstadoIndicePaquete::Ilegible;
    il.causa = QStringLiteral("corrupto");
    il.huella = HuellaZip{10, 20, kSha1};
    QVERIFY(e.reemplazar(il).valor());
    QCOMPARE(escalar(e.p->proveedor(), QStringLiteral("SELECT estado || ':' || causa FROM indice_cfdi_paquete"))
                 .toString(),
             QStringLiteral("Ilegible:corrupto"));
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM indice_cfdi_paquete")), 1);
    QVERIFY(consistente(e.p->proveedor()));

    // Importes y datos se leen exactos.
    QVERIFY(e.reemplazar(indexado(paq, tres)).valor());
    auto lista = e.cat().listarCfdi(sid(s), {});
    QVERIFY(lista);
    QCOMPARE(lista.valor().size(), 3);
    const ResumenCfdi& r = lista.valor().first().resumen;
    QCOMPARE(r.total->texto(), QStringLiteral("116.58"));
    QCOMPARE(r.subtotal->texto(), QStringLiteral("100.50"));
    QVERIFY(!r.descuento.has_value());
    QCOMPARE(r.receptor.nombre, QStringLiteral("PUBLICO EN GENERAL"));
    QCOMPARE(r.tipo, TipoComprobante::Ingreso);
    QCOMPARE(r.estado, EstadoParseo::Completo);
    QCOMPARE(lista.valor().first().posicion, 4);
    QCOMPARE(lista.valor().first().paqueteId, paq);
}

void TestSqliteCatalogoCfdi::reemplazoRevalidaPaqueteYCoherencia()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString s = e.solicitud(QStringLiteral("rv"));
    const QString disponible = e.paquete(s, QStringLiteral("DISP"), QStringLiteral("Disponible"));
    const QString descargado = e.paquete(s, QStringLiteral("DESC"));
    QVERIFY(!disponible.isEmpty() && !descargado.isEmpty());

    // Paquete no Descargado o inexistente: no escribe y devuelve false.
    QCOMPARE(e.reemplazar(indexado(disponible, {})).valor(), false);
    QCOMPARE(e.reemplazar(indexado(uuid::generarCanonico(), {})).valor(), false);
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM indice_cfdi_paquete")), 0);

    // Estado incoherente (causa sin error, CFDI sin Indexado): Integridad.
    IndicePaquete malo = indexado(descargado, {});
    malo.causa = QStringLiteral("x");
    auto r1 = e.reemplazar(malo);
    QVERIFY(!r1 && r1.error().tipo == ErrorPersistencia::Tipo::Integridad);
    IndicePaquete conCfdi = indexado(descargado, {cfdi(0, resumen({}, TipoComprobante::Pago, {}, {}, {}))});
    conCfdi.estado = EstadoIndicePaquete::Pendiente;
    auto r2 = e.reemplazar(conCfdi);
    QVERIFY(!r2 && r2.error().tipo == ErrorPersistencia::Tipo::Integridad);
    // Indexado sin huella: lo rechaza el CHECK de 006 y no queda nada.
    IndicePaquete sinHuella = indexado(descargado, {});
    sinHuella.huella.reset();
    auto r3 = e.reemplazar(sinHuella);
    QVERIFY(!r3 && r3.error().tipo == ErrorPersistencia::Tipo::Integridad);
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM indice_cfdi_paquete")), 0);

    // Paquete de una solicitud eliminada: no escribe.
    QVERIFY(e.reemplazar(indexado(descargado, {})).valor());
    QCOMPARE(ejecutarSql(e.p->proveedor(), QStringLiteral("UPDATE solicitud_masiva SET eliminado_en = %1 WHERE id = %2")
                                               .arg(citar(kAhora), citar(s))),
             QString());
    QCOMPARE(e.reemplazar(indexado(descargado, {}, kSha2)).valor(), false);
}

void TestSqliteCatalogoCfdi::uuidRepetidoEntrePaquetes()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString s1 = e.solicitud(QStringLiteral("u1"));
    const QString s2 = e.solicitud(QStringLiteral("u2"));
    const QString a = e.paquete(s1, QStringLiteral("A"));
    const QString b = e.paquete(s2, QStringLiteral("B"));
    const ResumenCfdi mismo =
        resumen("2026-02-01T00:00:00", TipoComprobante::Egreso, "AAA010101AAA", "X", "DUPLICADO-UUID");
    QVERIFY(e.reemplazar(indexado(a, {cfdi(0, mismo), cfdi(1, mismo)})).valor());
    QVERIFY(e.reemplazar(indexado(b, {cfdi(0, mismo)})).valor());
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM cfdi_indexado WHERE uuid = 'DUPLICADO-UUID'")), 3);
    // Unicidad por (paquete, posicion).
    auto r = e.reemplazar(indexado(a, {cfdi(0, mismo), cfdi(0, mismo)}));
    QVERIFY(!r && r.error().tipo == ErrorPersistencia::Tipo::Unicidad);
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM cfdi_indexado WHERE paquete_solicitud_id = %1")
                          .arg(citar(a))),
             2); // el rollback conserva lo anterior
}

void TestSqliteCatalogoCfdi::actualizarMetadatosHuella()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString s = e.solicitud(QStringLiteral("hu"));
    const QString paq = e.paquete(s, QStringLiteral("H"));
    QVERIFY(e.reemplazar(indexado(paq, {cfdi(0, resumen({}, TipoComprobante::Ingreso, {}, {}, {}))})).valor());
    auto sinTx = e.cat().actualizarMetadatosHuella(paq, HuellaZip{1, 2, kSha1}, kT0);
    QVERIFY(!sinTx && sinTx.error().tipo == ErrorPersistencia::Tipo::Transaccion);
    (void)e.p->unidadDeTrabajo().begin();
    QCOMPARE(e.cat().actualizarMetadatosHuella(paq, HuellaZip{1, 2, kSha2}, kT0).valor(), false); // otro SHA
    QCOMPARE(e.cat().actualizarMetadatosHuella(paq, HuellaZip{999, 888, kSha1}, kT0).valor(), true);
    (void)e.p->unidadDeTrabajo().commit();
    auto lista = e.cat().listarPaquetesIndexables(sid(s));
    QCOMPARE(lista.valor().first().huella, std::optional(HuellaZip{999, 888, kSha1}));
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM cfdi_indexado")), 1); // CFDI intactos
}

void TestSqliteCatalogoCfdi::listaFiltrosYOrden()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString s = e.solicitud(QStringLiteral("lf"));
    const QString otra = e.solicitud(QStringLiteral("lf-otra"));
    const QString p1 = e.paquete(s, QStringLiteral("P1"));
    const QString p2 = e.paquete(s, QStringLiteral("P2"));
    const QString p3 = e.paquete(otra, QStringLiteral("P3"));
    QVERIFY(e.reemplazar(indexado(
                             p1, {cfdi(0, resumen("2026-03-02T10:00:00", TipoComprobante::Ingreso, "PEÑ010101AAA",
                                                   "José Peña Ávila", "UUID-B")),
                                  cfdi(1, resumen("2026-03-01T10:00:00", TipoComprobante::Egreso, "BBB010101BBB",
                                                   "Comercial 100%_Sur", "UUID-A")),
                                  cfdi(2, resumen({}, TipoComprobante::Desconocido, {}, {}, {},
                                                   EstadoParseo::Ilegible))}))
                .valor());
    QVERIFY(e.reemplazar(indexado(p2, {cfdi(0, resumen("2026-03-03T10:00:00", TipoComprobante::Pago,
                                                       "CCC010101CCC", "Pagos SA", "UUID-C"))}))
                .valor());
    QVERIFY(e.reemplazar(indexado(p3, {cfdi(0, resumen("2026-04-01T10:00:00", TipoComprobante::Ingreso,
                                                       "PEÑ010101AAA", "José Peña", "UUID-OTRA"))}))
                .valor());
    const auto uuids = [](const auto& r) {
        QStringList l;
        for (const CfdiListado& c : r.valor()) {
            l.append(c.resumen.uuid.isEmpty() ? QStringLiteral("-") : c.resumen.uuid);
        }
        return l;
    };

    // Fecha descendente; sin fecha al final; solo la solicitud pedida.
    auto todos = e.cat().listarCfdi(sid(s), {});
    QVERIFY(todos);
    QCOMPARE(uuids(todos), (QStringList{"UUID-C", "UUID-B", "UUID-A", "-"}));
    FiltroListaCfdi asc;
    asc.orden = OrdenListaCfdi::FechaAscendente;
    QCOMPARE(uuids(e.cat().listarCfdi(sid(s), asc)), (QStringList{"UUID-A", "UUID-B", "UUID-C", "-"}));

    // Texto sin acentos ni mayusculas: "pena", "PEÑA" y "jose" encuentran a
    // "José Peña Ávila"; RFC, UUID, serie/folio tambien.
    for (const QString& texto : {QStringLiteral("pena"), QStringLiteral("PEÑA"), QStringLiteral("  JOSÉ  "),
                                 QStringLiteral("avila"), QStringLiteral("peñ010101"), QStringLiteral("uuid-b")}) {
        FiltroListaCfdi f;
        f.texto = texto;
        QCOMPARE(uuids(e.cat().listarCfdi(sid(s), f)), QStringList{"UUID-B"});
    }
    // Comodines de LIKE escapados.
    FiltroListaCfdi porcentaje;
    porcentaje.texto = QStringLiteral("100%_s");
    QCOMPARE(uuids(e.cat().listarCfdi(sid(s), porcentaje)), QStringList{"UUID-A"});
    porcentaje.texto = QStringLiteral("%");
    QCOMPARE(uuids(e.cat().listarCfdi(sid(s), porcentaje)), QStringList{"UUID-A"});
    porcentaje.texto = QStringLiteral("_");
    QCOMPARE(uuids(e.cat().listarCfdi(sid(s), porcentaje)), QStringList{"UUID-A"});

    // Tipo, combinado con texto, y limite.
    FiltroListaCfdi tipo;
    tipo.tipo = TipoComprobante::Egreso;
    QCOMPARE(uuids(e.cat().listarCfdi(sid(s), tipo)), QStringList{"UUID-A"});
    tipo.tipo = TipoComprobante::Ingreso;
    tipo.texto = QStringLiteral("pagos");
    QVERIFY(e.cat().listarCfdi(sid(s), tipo).valor().isEmpty());
    FiltroListaCfdi limite;
    limite.limite = 2;
    QCOMPARE(uuids(e.cat().listarCfdi(sid(s), limite)), (QStringList{"UUID-C", "UUID-B"}));
    QVERIFY(e.cat().listarCfdi(SolicitudId::generar(), {}).valor().isEmpty());
}

void TestSqliteCatalogoCfdi::resumenDeSolicitud()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString s = e.solicitud(QStringLiteral("rs"));
    const QString indexadoId = e.paquete(s, QStringLiteral("I"));
    const QString viejo = e.paquete(s, QStringLiteral("V"));
    const QString noDisp = e.paquete(s, QStringLiteral("N"));
    const QString ilegible = e.paquete(s, QStringLiteral("L"));
    QVERIFY(!e.paquete(s, QStringLiteral("SIN")).isEmpty()); // sin fila -> pendiente
    QVERIFY(!e.paquete(s, QStringLiteral("ERR"), QStringLiteral("Error")).isEmpty()); // no cuenta
    QVERIFY(e.reemplazar(indexado(indexadoId,
                                  {cfdi(0, resumen("2026-01-01T00:00:00", TipoComprobante::Ingreso, "A", "A", "1")),
                                   cfdi(1, resumen({}, TipoComprobante::Desconocido, {}, {}, {}, EstadoParseo::Ilegible)),
                                   cfdi(2, resumen({}, TipoComprobante::Ingreso, {}, {}, {},
                                                   EstadoParseo::VersionNoSoportada)),
                                   cfdi(3, resumen({}, TipoComprobante::Ingreso, {}, {}, {}, EstadoParseo::Parcial))}))
                .valor());
    IndicePaquete v = indexado(viejo, {cfdi(0, resumen({}, TipoComprobante::Ingreso, {}, {}, {}))});
    v.versionParser = 1;
    QVERIFY(e.reemplazar(v).valor());
    IndicePaquete nd;
    nd.paqueteId = noDisp;
    nd.estado = EstadoIndicePaquete::OrigenNoDisponible;
    nd.causa = QStringLiteral("no-existe");
    nd.actualizadoEn = kT0;
    QVERIFY(e.reemplazar(nd).valor());
    IndicePaquete il = nd;
    il.paqueteId = ilegible;
    il.estado = EstadoIndicePaquete::Ilegible;
    il.causa = QStringLiteral("truncado");
    il.huella = HuellaZip{1, 1, kSha1};
    QVERIFY(e.reemplazar(il).valor());
    // El paquete "viejo" quedo con version 1; el resumen se pide con version 2
    // salvo para el indexado vigente (se reescribe con version 2).
    IndicePaquete vigente = indexado(indexadoId,
                                     {cfdi(0, resumen("2026-01-01T00:00:00", TipoComprobante::Ingreso, "A", "A", "1")),
                                      cfdi(1, resumen({}, TipoComprobante::Desconocido, {}, {}, {},
                                                      EstadoParseo::Ilegible)),
                                      cfdi(2, resumen({}, TipoComprobante::Ingreso, {}, {}, {},
                                                      EstadoParseo::VersionNoSoportada)),
                                      cfdi(3, resumen({}, TipoComprobante::Ingreso, {}, {}, {}, EstadoParseo::Parcial))});
    vigente.versionParser = 2;
    QVERIFY(e.reemplazar(vigente).valor());
    nd.versionParser = 2;
    QVERIFY(e.reemplazar(nd).valor());
    il.versionParser = 2;
    QVERIFY(e.reemplazar(il).valor());

    auto r = e.cat().resumirSolicitud(sid(s), 2);
    QVERIFY(r);
    ResumenIndiceSolicitud esperado;
    esperado.paquetesDescargados = 5;
    esperado.paquetesIndexados = 1;
    esperado.paquetesPendientes = 2; // sin fila + version distinta
    esperado.paquetesNoDisponibles = 1;
    esperado.paquetesIlegibles = 1;
    esperado.cfdiTotal = 5;          // 4 vigentes + 1 de la version anterior
    esperado.cfdiIlegibles = 1;
    esperado.cfdiNoSoportados = 1;
    esperado.cfdiParciales = 1;
    esperado.xmlNoCfdi = 2;          // 1 por cada paquete Indexado
    esperado.entradasOmitidas = 4;   // 2 por cada paquete Indexado
    QCOMPARE(r.valor(), esperado);
    auto vacio = e.cat().resumirSolicitud(SolicitudId::generar(), 2);
    QVERIFY(vacio);
    QCOMPARE(vacio.valor(), ResumenIndiceSolicitud{});
}

void TestSqliteCatalogoCfdi::eliminarSolicitudBorraIndice()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString s = e.solicitud(QStringLiteral("el"));
    const QString otra = e.solicitud(QStringLiteral("el-otra"));
    const QString a = e.paquete(s, QStringLiteral("A"));
    const QString b = e.paquete(s, QStringLiteral("B"));
    const QString c = e.paquete(otra, QStringLiteral("C"));
    const auto uno = [](int pos) { return cfdi(pos, resumen({}, TipoComprobante::Ingreso, {}, {}, {})); };
    QVERIFY(e.reemplazar(indexado(a, {uno(0), uno(1)})).valor());
    QVERIFY(e.reemplazar(indexado(b, {uno(0)})).valor());
    QVERIFY(e.reemplazar(indexado(c, {uno(0)})).valor());

    // Eliminacion logica (T007 D5) con los repositorios reales, una transaccion.
    (void)e.p->unidadDeTrabajo().begin();
    QVERIFY(e.p->solicitudes().marcarEliminadaVisible(sid(s), kT0).valor());
    QCOMPARE(e.p->paquetes().marcarEliminadosPorSolicitud(sid(s), kT0).valor(), 2);
    QVERIFY(e.p->unidadDeTrabajo().commit());

    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM indice_cfdi_paquete")), 1);
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM cfdi_indexado")), 1);
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM cfdi_indexado WHERE solicitud_masiva_id = %1")
                          .arg(citar(s))),
             0);
    QVERIFY(e.cat().listarCfdi(sid(s), {}).valor().isEmpty());
    // paquete_solicitud conserva sus filas (eliminacion logica).
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM paquete_solicitud WHERE solicitud_masiva_id = %1")
                          .arg(citar(s))),
             2);
    QVERIFY(consistente(e.p->proveedor()));
}

void TestSqliteCatalogoCfdi::borradoFisicoEnCascada()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString s = e.solicitud(QStringLiteral("bf"));
    const QString a = e.paquete(s, QStringLiteral("A"));
    QVERIFY(e.reemplazar(indexado(a, {cfdi(0, resumen({}, TipoComprobante::Ingreso, {}, {}, {}))})).valor());
    QCOMPARE(ejecutarSql(e.p->proveedor(),
                         QStringLiteral("DELETE FROM paquete_solicitud WHERE id = %1").arg(citar(a))),
             QString());
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM indice_cfdi_paquete")), 0);
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM cfdi_indexado")), 0);
    QVERIFY(consistente(e.p->proveedor()));
}
