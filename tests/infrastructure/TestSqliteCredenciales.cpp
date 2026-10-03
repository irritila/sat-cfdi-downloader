#include "TestSqliteCredenciales.h"

#include "SqlitePruebasComun.h"
#include "fakes/FakeSecretStore.h"

#include "application/persistence/PersistenceDispatcher.h"
#include "application/profiles/CredencialesSatServicePersistido.h"
#include "domain/perfiles/PerfilSat.h"
#include "ports/repositories/CredencialSatRepository.h"
#include "ports/repositories/PerfilSatRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <QTest>
#include <QTimeZone>

#include <memory>

using namespace satcfdi;
using namespace satcfdi::pruebas;

namespace {

const QDateTime kInstante = QDateTime(QDate(2026, 10, 3), QTime(12, 0, 0, 125), QTimeZone(QTimeZone::UTC));

struct Entorno {
    QTemporaryDir dir;
    std::unique_ptr<SqlitePersistencia> p;

    bool preparar()
    {
        if (!dir.isValid() || !inicializarBaseSqlite(rutaBase(dir))) {
            return false;
        }
        p = std::make_unique<SqlitePersistencia>(rutaBase(dir));
        return true;
    }
    ~Entorno()
    {
        if (p) {
            p->cerrarConexionDelHiloActual();
        }
    }
    SqliteConnectionProvider& proveedor() { return p->proveedor(); }

    PerfilId crearPerfil(const QString& rfc = kRfcPerfil)
    {
        NuevoPerfilSat n;
        n.id = PerfilId::generar();
        n.rfc = rfc;
        n.nombre = QStringLiteral("Contribuyente");
        n.creadoEn = kInstante;
        n.actualizadoEn = kInstante;
        if (!p->unidadDeTrabajo().begin() || !p->perfiles().insertar(n) || !p->unidadDeTrabajo().commit()) {
            return {};
        }
        return n.id;
    }
};

CredencialSat credencial(const PerfilId& perfil, const CredencialRef& ref = CredencialRef::generar())
{
    CredencialSat c;
    c.id = uuid::generarCanonico();
    c.perfilSatId = perfil;
    c.certificadoRef = ref.referenciaCertificado();
    c.llavePrivadaRef = ref.referenciaContenedor();
    c.contrasenaRef = ref.referenciaContrasena();
    c.numeroSerie = QStringLiteral("30001000000500003416");
    c.vigenteDesde = QDateTime(QDate(2025, 1, 1), QTime(0, 0), QTimeZone(QTimeZone::UTC));
    c.vigenteHasta = QDateTime(QDate(2029, 1, 1), QTime(0, 0), QTimeZone(QTimeZone::UTC));
    c.registradaEn = kInstante;
    c.actualizadaEn = kInstante;
    return c;
}

template <typename F>
auto enTx(SqlitePersistencia& p, F f)
{
    (void)p.unidadDeTrabajo().begin();
    auto r = f();
    if (r) {
        (void)p.unidadDeTrabajo().commit();
    } else {
        (void)p.unidadDeTrabajo().rollback();
    }
    return r;
}

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, 5000) || f.isCanceled()) {
        return std::nullopt;
    }
    return f.result();
}

} // namespace

void TestSqliteCredenciales::migracion002SobreBase001YRepetida()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto embebidas = migracionesSqliteEmbebidas();
    QVERIFY(embebidas);
    QCOMPARE(embebidas.valor().size(), 2);

    // Base en version 1 con una fila de credencial previa a 002.
    QVERIFY(inicializarBaseSqlite(rutaBase(dir), {embebidas.valor().first()}));
    const QString perfil = uuid::generarCanonico();
    const CredencialRef ref = CredencialRef::generar();
    {
        SqliteConnectionProvider prov(rutaBase(dir));
        QCOMPARE(insertarPerfilFixture(prov, perfil, kRfcPerfil), QString());
        QCOMPARE(ejecutarSql(prov, QStringLiteral(
                                       "INSERT INTO credencial_sat VALUES (%1, %2, %3, %4, %5, %6, %6)")
                                       .arg(citar(uuid::generarCanonico()), citar(perfil),
                                            citar(ref.referenciaCertificado()),
                                            citar(ref.referenciaContenedor()),
                                            citar(ref.referenciaContrasena()), citar(kAhora))),
                 QString());
        prov.cerrarConexionDelHiloActual();
    }

    auto migrada = inicializarBaseSqlite(rutaBase(dir));
    QVERIFY2(migrada, migrada ? "" : qPrintable(migrada.error().mensaje));
    QCOMPARE(migrada.valor().migracion.versionInicial, 1);
    QCOMPARE(migrada.valor().migracion.versionFinal, 2);
    QCOMPARE(migrada.valor().migracion.aplicadas, QList<int>{2});

    auto repetida = inicializarBaseSqlite(rutaBase(dir));
    QVERIFY(repetida);
    QVERIFY(repetida.valor().migracion.aplicadas.isEmpty());
    QCOMPARE(repetida.valor().migracion.versionFinal, 2);

    SqlitePersistencia p(rutaBase(dir));
    QStringList columnas = columna(p.proveedor(), QStringLiteral("SELECT name FROM pragma_table_info('credencial_sat')"));
    QVERIFY(columnas.contains(QStringLiteral("numero_serie")));
    QVERIFY(columnas.contains(QStringLiteral("vigente_desde")));
    QVERIFY(columnas.contains(QStringLiteral("vigente_hasta")));
    QCOMPARE(columna(p.proveedor(), QStringLiteral("SELECT \"notnull\" FROM pragma_table_info('credencial_sat') "
                                                   "WHERE name IN ('numero_serie','vigente_desde','vigente_hasta')")),
             (QStringList{QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0")}));

    // La fila previa se lee con metadata nula y referencias validas.
    auto fila = p.credenciales().obtenerPorPerfil(*PerfilId::desdeTexto(perfil));
    QVERIFY(fila && fila.valor());
    QVERIFY(!fila.valor()->numeroSerie && !fila.valor()->vigenteDesde && !fila.valor()->vigenteHasta);
    QVERIFY(!fila.valor()->metadataCompleta());
    auto refs = p.credenciales().listarReferenciasVigentes();
    QVERIFY(refs);
    QCOMPARE(refs.valor(), QList<CredencialRef>{ref});
    QCOMPARE(escalar(p.proveedor(), QStringLiteral("PRAGMA integrity_check")).toString(), QStringLiteral("ok"));
    QCOMPARE(columna(p.proveedor(), QStringLiteral("PRAGMA foreign_key_check")), QStringList());
    p.cerrarConexionDelHiloActual();
}

void TestSqliteCredenciales::checksDeMetadata()
{
    Entorno e;
    QVERIFY(e.preparar());
    const PerfilId perfil = e.crearPerfil();
    QVERIFY(!perfil.esNulo());
    const QString base = QStringLiteral(
        "INSERT INTO credencial_sat (id, perfil_sat_id, certificado_ref, llave_privada_ref, "
        "contrasena_ref, numero_serie, vigente_desde, vigente_hasta, registrada_en, actualizada_en) "
        "VALUES (%1, %2, 'a', 'b', 'c', %3, %4, %5, %6, %6)");
    auto insertar = [&](const QString& serie, const QString& desde, const QString& hasta) {
        return ejecutarSql(e.proveedor(), base.arg(citar(uuid::generarCanonico()), citar(perfil.texto()),
                                                   serie, desde, hasta, citar(kAhora)));
    };
    const QString d = citar(QStringLiteral("2025-01-01T00:00:00.000Z"));
    const QString h = citar(QStringLiteral("2029-01-01T00:00:00.000Z"));
    QVERIFY(insertar(QStringLiteral("''"), d, h).contains(QStringLiteral("CHECK")));
    QVERIFY(insertar(QStringLiteral("'1'"), d, QStringLiteral("NULL")).contains(QStringLiteral("CHECK")));
    QVERIFY(insertar(QStringLiteral("'1'"), QStringLiteral("NULL"), h).contains(QStringLiteral("CHECK")));
    QVERIFY(insertar(QStringLiteral("'1'"), h, d).contains(QStringLiteral("CHECK")));
    QVERIFY(insertar(QStringLiteral("'1'"), citar(QStringLiteral("2025")), h).contains(QStringLiteral("CHECK")));
    // 24 caracteres ordenados pero sin formato UTC con sufijo Z.
    QVERIFY(insertar(QStringLiteral("'1'"), citar(QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaa")),
                     citar(QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbb")))
                .contains(QStringLiteral("CHECK")));
    QVERIFY(insertar(QStringLiteral("'1'"), citar(QStringLiteral("2025-01-01T00:00:00.000+")),
                     citar(QStringLiteral("2029-01-01T00:00:00.000+")))
                .contains(QStringLiteral("CHECK")));
    QVERIFY(insertar(QStringLiteral("'1'"), d, citar(QStringLiteral("2029-01-01 00:00:00.000Z")))
                .contains(QStringLiteral("CHECK")));
    QCOMPARE(insertar(QStringLiteral("'1'"), d, h), QString());
}

void TestSqliteCredenciales::insertarObtenerReemplazarEliminar()
{
    Entorno e;
    QVERIFY(e.preparar());
    const PerfilId perfil = e.crearPerfil();
    CredencialSatRepository& repo = e.p->credenciales();

    auto vacio = repo.obtenerPorPerfil(perfil);
    QVERIFY(vacio && !vacio.valor());

    const CredencialSat original = credencial(perfil);
    QVERIFY(enTx(*e.p, [&] { return repo.insertar(original); }));
    auto leida = repo.obtenerPorPerfil(perfil);
    QVERIFY(leida && leida.valor());
    QCOMPARE(*leida.valor(), original);

    // Reemplazo: misma fila, nuevas referencias/metadata, conserva registrada_en.
    CredencialSat nueva = credencial(perfil);
    nueva.id = uuid::generarCanonico(); // se ignora: la fila conserva su id
    nueva.registradaEn = kInstante.addDays(5); // se ignora
    nueva.actualizadaEn = kInstante.addDays(1);
    nueva.numeroSerie = QStringLiteral("30001000000500009999");
    QVERIFY(enTx(*e.p, [&] { return repo.reemplazar(perfil, nueva); }));
    auto reemplazada = repo.obtenerPorPerfil(perfil);
    QVERIFY(reemplazada && reemplazada.valor());
    QCOMPARE(reemplazada.valor()->id, original.id);
    QCOMPARE(reemplazada.valor()->registradaEn, original.registradaEn);
    QCOMPARE(reemplazada.valor()->actualizadaEn, kInstante.addDays(1));
    QCOMPARE(reemplazada.valor()->certificadoRef, nueva.certificadoRef);
    QCOMPARE(reemplazada.valor()->numeroSerie, nueva.numeroSerie);
    QCOMPARE(escalar(e.proveedor(), QStringLiteral("SELECT count(*) FROM credencial_sat")).toInt(), 1);

    // Reemplazar sin fila -> NoEncontrado.
    const PerfilId otro = e.crearPerfil(kRfcOtro);
    auto sinFila = enTx(*e.p, [&] { return repo.reemplazar(otro, credencial(otro)); });
    QVERIFY(!sinFila);
    QCOMPARE(sinFila.error().tipo, ErrorPersistencia::Tipo::NoEncontrado);

    // Eliminar: true y luego false (idempotente).
    auto borrada = enTx(*e.p, [&] { return repo.eliminarPorPerfil(perfil); });
    QVERIFY(borrada && borrada.valor());
    auto otraVez = enTx(*e.p, [&] { return repo.eliminarPorPerfil(perfil); });
    QVERIFY(otraVez && !otraVez.valor());
    QVERIFY(!repo.obtenerPorPerfil(perfil).valor());
}

void TestSqliteCredenciales::escriturasExigenTransaccionMetadataYPerfil()
{
    Entorno e;
    QVERIFY(e.preparar());
    const PerfilId perfil = e.crearPerfil();
    CredencialSatRepository& repo = e.p->credenciales();

    auto sinTx = repo.insertar(credencial(perfil));
    QVERIFY(!sinTx);
    QCOMPARE(sinTx.error().tipo, ErrorPersistencia::Tipo::Transaccion);
    QCOMPARE(repo.eliminarPorPerfil(perfil).error().tipo, ErrorPersistencia::Tipo::Transaccion);

    CredencialSat incompleta = credencial(perfil);
    incompleta.vigenteHasta.reset();
    auto sinMeta = enTx(*e.p, [&] { return repo.insertar(incompleta); });
    QVERIFY(!sinMeta);
    QCOMPARE(sinMeta.error().tipo, ErrorPersistencia::Tipo::Integridad);
    QCOMPARE(sinMeta.error().restriccion, QStringLiteral("credencial_sat.metadata"));

    auto sinPerfil = enTx(*e.p, [&] { return repo.insertar(credencial(PerfilId::generar())); });
    QVERIFY(!sinPerfil);
    QCOMPARE(sinPerfil.error().tipo, ErrorPersistencia::Tipo::Integridad);

    QVERIFY(enTx(*e.p, [&] { return repo.insertar(credencial(perfil)); }));
    auto duplicada = enTx(*e.p, [&] { return repo.insertar(credencial(perfil)); });
    QVERIFY(!duplicada);
    QCOMPARE(duplicada.error().tipo, ErrorPersistencia::Tipo::Unicidad);
    QCOMPARE(duplicada.error().restriccion, QStringLiteral("ux_credencial_sat_perfil"));

    CredencialSat sinSerie = credencial(perfil);
    sinSerie.numeroSerie = QStringLiteral("  ");
    auto reemplazoIncompleto = enTx(*e.p, [&] { return repo.reemplazar(perfil, sinSerie); });
    QVERIFY(!reemplazoIncompleto);
    QCOMPARE(reemplazoIncompleto.error().tipo, ErrorPersistencia::Tipo::Integridad);
}

void TestSqliteCredenciales::listarReferenciasVigentesFailSafe()
{
    Entorno e;
    QVERIFY(e.preparar());
    CredencialSatRepository& repo = e.p->credenciales();
    QVERIFY(repo.listarReferenciasVigentes().valor().isEmpty());

    const CredencialRef a = CredencialRef::generar();
    const CredencialRef b = CredencialRef::generar();
    const PerfilId pa = e.crearPerfil();
    const PerfilId pb = e.crearPerfil(kRfcOtro);
    QVERIFY(enTx(*e.p, [&] { return repo.insertar(credencial(pa, a)); }));
    QVERIFY(enTx(*e.p, [&] { return repo.insertar(credencial(pb, b)); }));
    auto refs = repo.listarReferenciasVigentes();
    QVERIFY(refs);
    QCOMPARE(QSet<CredencialRef>(refs.valor().cbegin(), refs.valor().cend()), (QSet<CredencialRef>{a, b}));

    // Una referencia ilegible invalida toda la lista (no lista parcial).
    QCOMPARE(ejecutarSql(e.proveedor(), QStringLiteral("UPDATE credencial_sat SET contrasena_ref = "
                                                       "'scs1:%1:password' WHERE perfil_sat_id = %2")
                                            .arg(CredencialRef::generar().uuid(), citar(pb.texto()))),
             QString());
    auto ilegible = repo.listarReferenciasVigentes();
    QVERIFY(!ilegible);
    QCOMPARE(ilegible.error().tipo, ErrorPersistencia::Tipo::Interno);
}

void TestSqliteCredenciales::referenciasPersistidasSinRutasNiSecretos()
{
    Entorno e;
    QVERIFY(e.preparar());
    const PerfilId perfil = e.crearPerfil();
    QVERIFY(enTx(*e.p, [&] { return e.p->credenciales().insertar(credencial(perfil)); }));
    const QStringList refs = columna(
        e.proveedor(), QStringLiteral("SELECT certificado_ref FROM credencial_sat UNION ALL "
                                      "SELECT llave_privada_ref FROM credencial_sat UNION ALL "
                                      "SELECT contrasena_ref FROM credencial_sat"));
    QCOMPARE(refs.size(), 3);
    for (const QString& r : refs) {
        QVERIFY(r.startsWith(QStringLiteral("scs1:")));
        QVERIFY(!r.contains(u'/'));
        QVERIFY(!r.contains(u'\\'));
        QVERIFY(!r.contains(kRfcPerfil));
    }
}

void TestSqliteCredenciales::servicioConSqliteRealImportaYReemplaza()
{
    Entorno e;
    QVERIFY(e.preparar());
    const PerfilId perfil = e.crearPerfil();
    e.p->cerrarConexionDelHiloActual(); // el resto ocurre en el hilo del dispatcher

    fakes::FakeSecretStore store;
    store.rfcCertificado = kRfcPerfil;
    QStringList registro;
    {
        PersistenceDispatcher dispatcher;
        CredencialesSatServicePersistido servicio(
            dispatcher, e.p->perfiles(), e.p->credenciales(), e.p->unidadDeTrabajo(), store,
            [] { return kInstante; }, [&registro](QStringView op, ErrorSecretStore::Categoria c) {
                registro.append(op.toString() + u':' + claveEstable(c));
            });
        auto entrada = [] {
            EntradaEFirma x;
            x.rutaCertificado = QStringLiteral("/tmp/efirma.cer");
            x.rutaLlavePrivada = QStringLiteral("/tmp/efirma.key");
            x.contrasena = BufferSecreto::desdeTexto(u"clave-de-prueba");
            return x;
        };
        auto importada = esperar(servicio.importar(perfil, entrada()));
        QVERIFY(importada && importada->esExito());
        auto reemplazada = esperar(servicio.reemplazar(perfil, entrada()));
        QVERIFY(reemplazada && reemplazada->esExito());
        QCOMPARE(store.generacionesVivas(), 1);

        // Un fallo de preparacion conserva la fila vigente.
        store.fallos.insert(fakes::FakeSecretStore::Operacion::Preparar,
                            ErrorSecretStore::Categoria::AlmacenBloqueado);
        auto fallida = esperar(servicio.reemplazar(perfil, entrada()));
        QVERIFY(fallida && !fallida->esExito());
        QCOMPARE(store.generacionesVivas(), 1);

        auto estado = esperar(servicio.obtenerEstado(perfil));
        QVERIFY(estado && estado->esExito());
        QCOMPARE(estado->valor(), EstadoCredencial::Lista);

        auto rec = esperar(servicio.reconciliar());
        QVERIFY(rec && rec->esExito());
        QCOMPARE(rec->valor().generacionesConservadas, 1);
        QCOMPARE(rec->valor().generacionesEliminadas, 0);

        SqlitePersistencia* p = e.p.get();
        esperar(dispatcher.despachar<bool>([p] {
            p->cerrarConexionDelHiloActual();
            return true;
        }));
        dispatcher.cerrar();
    }
    QCOMPARE(registro, QStringList{QStringLiteral("credencial.preparar:AlmacenBloqueado")});
    auto refs = e.p->credenciales().listarReferenciasVigentes();
    QVERIFY(refs && refs.valor().size() == 1);
    QVERIFY(store.existe(refs.valor().first()));
    QVERIFY(!leerArchivo(rutaBase(e.dir)).contains("clave-de-prueba"));
}
