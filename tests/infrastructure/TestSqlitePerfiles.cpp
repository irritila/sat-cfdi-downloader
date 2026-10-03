#include "TestSqlitePerfiles.h"

#include "SqlitePruebasComun.h"

#include "application/persistence/PersistenceDispatcher.h"
#include "application/profiles/PerfilesSatServicePersistido.h"
#include "domain/perfiles/PerfilSat.h"
#include "ports/repositories/PerfilSatRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <QTest>
#include <QTimeZone>

#include <memory>

using namespace satcfdi;
using namespace satcfdi::pruebas;

namespace {

const QDateTime kCreado = QDateTime(QDate(2026, 10, 3), QTime(12, 0), QTimeZone(QTimeZone::UTC));

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

    PerfilId crear(const QString& rfc, const QString& nombre, bool activo)
    {
        const NuevoPerfilSat n{PerfilId::generar(), rfc, nombre, activo, kCreado, kCreado};
        if (!p->unidadDeTrabajo().begin() || !p->perfiles().insertar(n) || !p->unidadDeTrabajo().commit()) {
            return {};
        }
        return n.id;
    }
};

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, 5000) || f.isCanceled()) {
        return std::nullopt;
    }
    return f.result();
}

} // namespace

void TestSqlitePerfiles::listarVisiblesIncluyeInactivosYExcluyeEliminados()
{
    Entorno e;
    QVERIFY(e.preparar());
    const PerfilId c = e.crear(QStringLiteral("CCC030303CCC"), QStringLiteral("C activo"), true);
    const PerfilId a = e.crear(QStringLiteral("AAA010101AAA"), QStringLiteral("A inactivo"), false);
    const PerfilId b = e.crear(QStringLiteral("BBB020202BBB"), QStringLiteral("B eliminado"), true);
    QCOMPARE(ejecutarSql(e.p->proveedor(), QStringLiteral("UPDATE perfil_sat SET eliminado_en = %1 WHERE id = %2")
                                                .arg(citar(kAhora), citar(b.texto()))),
             QString());

    auto visibles = e.p->perfiles().listarVisibles();
    QVERIFY(visibles);
    QCOMPARE(visibles.valor().size(), 2);
    QCOMPARE(visibles.valor().at(0).id, a); // orden por RFC
    QVERIFY(!visibles.valor().at(0).activo);
    QCOMPARE(visibles.valor().at(1).id, c);

    auto activos = e.p->perfiles().listarActivosVisibles();
    QVERIFY(activos);
    QCOMPARE(activos.valor().size(), 1);
    QCOMPARE(activos.valor().first().id, c);
}

void TestSqlitePerfiles::actualizarNombreNoTocaRfc()
{
    Entorno e;
    QVERIFY(e.preparar());
    const PerfilId id = e.crear(QStringLiteral("AAA010101AAA"), QStringLiteral("Original"), false);
    const QDateTime despues = kCreado.addSecs(90);

    QVERIFY(e.p->unidadDeTrabajo().begin());
    auto r = e.p->perfiles().actualizarNombreVisible(id, QStringLiteral("Renombrado"), despues);
    QVERIFY(e.p->unidadDeTrabajo().commit());
    QVERIFY(r && r.valor());
    QCOMPARE(r.valor()->nombre, QStringLiteral("Renombrado"));
    QCOMPARE(r.valor()->rfc, QStringLiteral("AAA010101AAA"));
    QVERIFY(!r.valor()->activo); // inactivo tambien se edita
    QCOMPARE(r.valor()->actualizadoEn, despues);
    QCOMPARE(r.valor()->creadoEn, kCreado);

    const QStringList fila = columna(e.p->proveedor(), QStringLiteral("SELECT rfc || '|' || nombre || '|' || activo "
                                                                      "FROM perfil_sat"));
    QCOMPARE(fila, QStringList{QStringLiteral("AAA010101AAA|Renombrado|0")});

    // Eliminado o inexistente -> nullopt sin cambios.
    QCOMPARE(ejecutarSql(e.p->proveedor(), QStringLiteral("UPDATE perfil_sat SET eliminado_en = %1")
                                                .arg(citar(kAhora))),
             QString());
    QVERIFY(e.p->unidadDeTrabajo().begin());
    auto eliminado = e.p->perfiles().actualizarNombreVisible(id, QStringLiteral("X"), despues);
    auto inexistente = e.p->perfiles().actualizarNombreVisible(PerfilId::generar(), QStringLiteral("X"), despues);
    QVERIFY(e.p->unidadDeTrabajo().commit());
    QVERIFY(eliminado && !eliminado.valor());
    QVERIFY(inexistente && !inexistente.valor());
    QCOMPARE(escalar(e.p->proveedor(), QStringLiteral("SELECT nombre FROM perfil_sat")).toString(),
             QStringLiteral("Renombrado"));
}

void TestSqlitePerfiles::actualizarNombreExigeTransaccionYRespetaCheck()
{
    Entorno e;
    QVERIFY(e.preparar());
    const PerfilId id = e.crear(QStringLiteral("AAA010101AAA"), QStringLiteral("Original"), true);
    auto sinTx = e.p->perfiles().actualizarNombreVisible(id, QStringLiteral("X"), kCreado);
    QVERIFY(!sinTx);
    QCOMPARE(sinTx.error().tipo, ErrorPersistencia::Tipo::Transaccion);

    QVERIFY(e.p->unidadDeTrabajo().begin());
    auto vacio = e.p->perfiles().actualizarNombreVisible(id, QStringLiteral("   "), kCreado);
    QVERIFY(e.p->unidadDeTrabajo().rollback());
    QVERIFY(!vacio);
    QCOMPARE(vacio.error().tipo, ErrorPersistencia::Tipo::Integridad);
}

void TestSqlitePerfiles::servicioCreaDuplicadoYActualiza()
{
    Entorno e;
    QVERIFY(e.preparar());
    const PerfilId inactivo = e.crear(QStringLiteral("AAA010101AAA"), QStringLiteral("Inactivo"), false);
    e.p->cerrarConexionDelHiloActual();
    {
        PersistenceDispatcher dispatcher;
        PerfilesSatServicePersistido servicio(dispatcher, e.p->perfiles(), e.p->unidadDeTrabajo(),
                                              [] { return kCreado; });
        // RFC de un perfil INACTIVO sigue reservado -> RfcDuplicado.
        const auto dup = esperar(servicio.crear(QStringLiteral(" aaa010101aaa "), QStringLiteral("Dup")));
        QVERIFY(dup && !dup->esExito());
        QCOMPARE(dup->error().tipo, ErrorCrearPerfil::Tipo::RfcDuplicado);

        const auto creado = esperar(servicio.crear(QStringLiteral("bbb020202bbb"), QStringLiteral(" Nuevo ")));
        QVERIFY(creado && creado->esExito());
        QCOMPARE(creado->valor().rfc, QStringLiteral("BBB020202BBB"));
        QCOMPARE(creado->valor().nombre, QStringLiteral("Nuevo"));

        const auto renombrado = esperar(servicio.actualizarNombre(inactivo, QStringLiteral("Renombrado")));
        QVERIFY(renombrado && renombrado->esExito());
        QCOMPARE(renombrado->valor().rfc, QStringLiteral("AAA010101AAA"));

        const auto lista = esperar(servicio.listarNoEliminados());
        QVERIFY(lista && lista->esExito());
        QCOMPARE(lista->valor().size(), 2);
        QCOMPARE(lista->valor().at(0).nombre, QStringLiteral("Renombrado"));
        QVERIFY(!lista->valor().at(0).activo);

        SqlitePersistencia* p = e.p.get();
        esperar(dispatcher.despachar<bool>([p] {
            p->cerrarConexionDelHiloActual();
            return true;
        }));
        dispatcher.cerrar();
    }
}
