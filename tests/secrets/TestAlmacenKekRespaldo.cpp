// T025.4: AlmacenKekRespaldoMacOS con FakeKeychainApi (sin Keychain real):
// ida y vuelta de ambos tipos de KEK, formato del item, servicio propio
// distinto de e.firma, nunca sobrescribe, errores tipados sin secretos y
// datos danados. Datos sinteticos.

#include "FakeKeychainApi.h"

#include "infrastructure/secrets/macos/AlmacenKekRespaldoMacOS.h"
#include "infrastructure/secrets/macos/MacOSSecretStore.h"

#include <QTemporaryDir>
#include <QTest>

#include <cstring>

using namespace satcfdi;
using namespace satcfdi::respaldo;
using satcfdi::secrets::AlmacenKekRespaldoMacOS;
namespace os = satcfdi::secrets::osstatus;
using C = ErrorAlmacenKek::Categoria;

namespace {

const QString kBundle = QStringLiteral("mx.adenium.satcfdi-downloader.test");
const QString kAmbito = QStringLiteral("3f2a1b0c-9d8e-4f7a-b6c5-d4e3f2a1b0c9");
const QString kOtroAmbito = QStringLiteral("7e6d5c4b-3a29-4180-9f8e-7d6c5b4a3928");

QByteArray salSintetica()
{
    QByteArray s(int(kTamanoSal), '\0');
    for (int i = 0; i < s.size(); ++i) {
        s[i] = char(0xA0 + i);
    }
    return s;
}

BufferSecreto claveSintetica(std::uint8_t base)
{
    BufferSecreto b(kTamanoKek);
    for (std::size_t i = 0; i < kTamanoKek; ++i) {
        b.datos()[i] = static_cast<std::uint8_t>(base + i);
    }
    return b;
}

KekRespaldo kekContrasena()
{
    ParametrosKdf kdf = ParametrosKdf::predeterminados(salSintetica());
    kdf.memoriaKib = 128 * 1024;
    kdf.iteraciones = 4;
    kdf.paralelismo = 2;
    return KekRespaldo::desdeBytes(KekRespaldo::Tipo::Contrasena, kdf, claveSintetica(0x10));
}

KekRespaldo kekRecuperacion()
{
    return KekRespaldo::desdeBytes(KekRespaldo::Tipo::Recuperacion, ParametrosKdf{}, claveSintetica(0x40));
}

bool mismaClave(const KekRespaldo& a, const KekRespaldo& b)
{
    return a.clave().igualA(b.clave().datos(), b.clave().tamano());
}

} // namespace

class TestAlmacenKekRespaldo : public QObject {
    Q_OBJECT

private slots:
    void idaYVueltaContrasena()
    {
        auto api = std::make_shared<fakes::FakeKeychainApi>();
        AlmacenKekRespaldoMacOS almacen(kBundle, kAmbito, api);
        const KekRespaldo original = kekContrasena();
        const auto ref = almacen.guardar(original);
        QVERIFY(ref.esExito());
        QVERIFY(!ref.valor().nula());
        QVERIFY(api->contiene(almacen.servicio(), ref.valor().texto()));
        QCOMPARE(api->dato(almacen.servicio(), ref.valor().texto())->tamano(), AlmacenKekRespaldoMacOS::kTamanoDato);

        const auto cargada = almacen.cargar(ref.valor());
        QVERIFY(cargada.esExito());
        QCOMPARE(cargada.valor().tipo(), KekRespaldo::Tipo::Contrasena);
        QCOMPARE(cargada.valor().parametros(), original.parametros());
        QCOMPARE(cargada.valor().parametros().sal, salSintetica());
        QVERIFY(mismaClave(cargada.valor(), original));
    }

    void idaYVueltaRecuperacion()
    {
        auto api = std::make_shared<fakes::FakeKeychainApi>();
        AlmacenKekRespaldoMacOS almacen(kBundle, kAmbito, api);
        const KekRespaldo original = kekRecuperacion();
        const auto ref = almacen.guardar(original);
        QVERIFY(ref.esExito());
        const auto cargada = almacen.cargar(ref.valor());
        QVERIFY(cargada.esExito());
        QCOMPARE(cargada.valor().tipo(), KekRespaldo::Tipo::Recuperacion);
        QVERIFY(mismaClave(cargada.valor(), original));
    }

    void formatoDelItem()
    {
        auto api = std::make_shared<fakes::FakeKeychainApi>();
        AlmacenKekRespaldoMacOS almacen(kBundle, kAmbito, api);
        const auto ref = almacen.guardar(kekContrasena());
        QVERIFY(ref.esExito());
        const BufferSecreto* d = api->dato(almacen.servicio(), ref.valor().texto());
        QVERIFY(d != nullptr);
        const std::uint8_t* p = d->datos();
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(p), 4), QByteArray("AFK1"));
        QCOMPARE(int(p[4]), 1);
        // memoriaKib = 131072 (big endian).
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(p + 8), 4), QByteArray::fromHex("00020000"));
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(p + 20), int(kTamanoSal)), salSintetica());
    }

    void servicioPropioDistintoDeEFirma()
    {
        auto api = std::make_shared<fakes::FakeKeychainApi>();
        AlmacenKekRespaldoMacOS almacen(kBundle, kAmbito, api);
        QTemporaryDir dir;
        MacOSSecretStore efirma(dir.filePath(QStringLiteral("credentials")), kBundle, api);
        QCOMPARE(almacen.servicio(), kBundle + QStringLiteral(".respaldo.v1.kek.") + kAmbito);
        QVERIFY(almacen.servicio() != efirma.servicioContrasena());
        QVERIFY(almacen.servicio() != efirma.servicioClaveEnvoltura());
        // La reconciliacion de e.firma no ve ni borra la KEK.
        const auto ref = almacen.guardar(kekContrasena());
        QVERIFY(ref.esExito());
        QVERIFY(efirma.reconciliar({}).esExito());
        QVERIFY(almacen.cargar(ref.valor()).esExito());
    }

    // Hallazgo T025.4: dos instalaciones (otro --data-dir) sobre el mismo
    // Keychain no se ven ni se borran entre si, tampoco al reconciliar.
    void ambitosSeAislan()
    {
        auto api = std::make_shared<fakes::FakeKeychainApi>();
        AlmacenKekRespaldoMacOS a(kBundle, kAmbito, api);
        AlmacenKekRespaldoMacOS b(kBundle, kOtroAmbito, api);
        QVERIFY(a.servicio() != b.servicio());
        const auto refA = a.guardar(kekContrasena());
        const auto refB = b.guardar(kekRecuperacion());
        QVERIFY(refA.esExito() && refB.esExito());
        QCOMPARE(api->cantidad(), std::size_t(2));

        QCOMPARE(a.listar().valor(), QList<ReferenciaKek>{refA.valor()});
        QCOMPARE(b.listar().valor(), QList<ReferenciaKek>{refB.valor()});
        QCOMPARE(a.cargar(refB.valor()).error().categoria, C::NoEncontrada);
        QCOMPARE(b.cargar(refA.valor()).error().categoria, C::NoEncontrada);

        // Reconciliacion de `a` sin vigentes: borra todo lo suyo y nada de `b`.
        const auto propias = a.listar();
        QVERIFY(propias.esExito());
        for (const ReferenciaKek& r : propias.valor()) {
            QVERIFY(a.borrar(r).esExito());
        }
        QVERIFY(a.borrar(refB.valor()).esExito()); // idempotente y fuera de su ambito
        QVERIFY(a.listar().valor().isEmpty());
        QVERIFY(b.cargar(refB.valor()).esExito());
        QCOMPARE(api->cantidad(), std::size_t(1));
    }

    void ambitoInvalidoNoOpera_data()
    {
        QTest::addColumn<QString>("ambito");
        QTest::newRow("vacio") << QString();
        QTest::newRow("nulo") << QStringLiteral("00000000-0000-0000-0000-000000000000");
        QTest::newRow("mayusculas") << kAmbito.toUpper();
        QTest::newRow("llaves") << (QLatin1Char('{') + kAmbito + QLatin1Char('}'));
        QTest::newRow("texto") << QStringLiteral("instalacion");
    }
    void ambitoInvalidoNoOpera()
    {
        QFETCH(QString, ambito);
        auto api = std::make_shared<fakes::FakeKeychainApi>();
        // Un item de otra instalacion que no debe verse ni borrarse.
        AlmacenKekRespaldoMacOS valido(kBundle, kAmbito, api);
        const auto ref = valido.guardar(kekContrasena());
        QVERIFY(ref.esExito());

        AlmacenKekRespaldoMacOS almacen(kBundle, ambito, api);
        QVERIFY(!almacen.ambitoValido());
        QVERIFY(almacen.servicio().isEmpty());
        const ErrorAlmacenKek esperado = respaldo::errorAmbitoInvalido();
        QCOMPARE(esperado.categoria, C::AlmacenMalConfigurado);
        QCOMPARE(esperado.diagnostico, QStringLiteral("ambito.invalido"));
        QCOMPARE(almacen.guardar(kekContrasena()).error(), esperado);
        QCOMPARE(almacen.cargar(ref.valor()).error(), esperado);
        QCOMPARE(almacen.borrar(ref.valor()).error(), esperado);
        QCOMPARE(almacen.listar().error(), esperado);
        QCOMPARE(api->cantidad(), std::size_t(1));
        QVERIFY(valido.cargar(ref.valor()).esExito());
    }

    void cadaGuardarCreaUnItemNuevo()
    {
        auto api = std::make_shared<fakes::FakeKeychainApi>();
        AlmacenKekRespaldoMacOS almacen(kBundle, kAmbito, api);
        const auto a = almacen.guardar(kekContrasena());
        const auto b = almacen.guardar(kekRecuperacion());
        QVERIFY(a.esExito() && b.esExito());
        QVERIFY(!(a.valor() == b.valor()));
        auto lista = almacen.listar();
        QVERIFY(lista.esExito());
        QCOMPARE(lista.valor().size(), 2);
        QVERIFY(lista.valor().contains(a.valor()) && lista.valor().contains(b.valor()));

        // listar() ignora cuentas que no son referencias canonicas.
        api->poner(almacen.servicio(), QStringLiteral("otra-cosa"), QByteArray("x"));
        QCOMPARE(almacen.listar().valor().size(), 2);

        QVERIFY(almacen.borrar(a.valor()).esExito());
        QVERIFY(almacen.borrar(a.valor()).esExito()); // idempotente
        QCOMPARE(almacen.cargar(a.valor()).error().categoria, C::NoEncontrada);
        QVERIFY(almacen.cargar(b.valor()).esExito());
    }

    void argumentosInvalidos()
    {
        auto api = std::make_shared<fakes::FakeKeychainApi>();
        AlmacenKekRespaldoMacOS almacen(kBundle, kAmbito, api);
        QCOMPARE(almacen.guardar(KekRespaldo{}).error().categoria, C::KekInvalida);
        QCOMPARE(almacen.cargar(ReferenciaKek{}).error().categoria, C::ReferenciaInvalida);
        QCOMPARE(almacen.borrar(ReferenciaKek{}).error().categoria, C::ReferenciaInvalida);
        QCOMPARE(api->cantidad(), std::size_t(0));

        QVERIFY(ReferenciaKek::desdeTexto(QStringLiteral("0f8fad5b-d9cb-469f-a165-70867728950e")).has_value());
        QVERIFY(!ReferenciaKek::desdeTexto(QStringLiteral("0F8FAD5B-D9CB-469F-A165-70867728950E")).has_value());
        QVERIFY(!ReferenciaKek::desdeTexto(QStringLiteral("{0f8fad5b-d9cb-469f-a165-70867728950e}")).has_value());
        QVERIFY(!ReferenciaKek::desdeTexto(QStringLiteral("x")).has_value());
    }

    void erroresDelAlmacen_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<int>("categoria");
        QTest::newRow("bloqueado") << int(os::kInteractionNotAllowed) << int(C::AlmacenBloqueado);
        QTest::newRow("sin-entitlement") << int(os::kMissingEntitlement) << int(C::AlmacenMalConfigurado);
        QTest::newRow("denegado") << int(os::kAuthFailed) << int(C::AccesoDenegado);
        QTest::newRow("no-disponible") << int(os::kNotAvailable) << int(C::AlmacenNoDisponible);
        QTest::newRow("disco") << int(os::kDiskFull) << int(C::FalloEscritura);
        QTest::newRow("desconocido") << -1 << int(C::Interno);
    }
    void erroresDelAlmacen()
    {
        QFETCH(int, status);
        QFETCH(int, categoria);
        auto api = std::make_shared<fakes::FakeKeychainApi>();
        AlmacenKekRespaldoMacOS almacen(kBundle, kAmbito, api);
        const auto ref = almacen.guardar(kekContrasena());
        QVERIFY(ref.esExito());
        using Op = fakes::FakeKeychainApi::Operacion;
        for (Op op : {Op::Agregar, Op::Copiar, Op::Eliminar, Op::Listar}) {
            api->fallos[op] = {status, std::nullopt};
        }
        const auto g = almacen.guardar(kekContrasena());
        QCOMPARE(int(g.error().categoria), categoria);
        QCOMPARE(g.error().codigoNativo, std::optional<int>(status));
        QCOMPARE(g.error().diagnostico, QStringLiteral("keychain.agregar"));
        const auto c = almacen.cargar(ref.valor());
        QCOMPARE(int(c.error().categoria), categoria);
        // El diagnostico nunca lleva la referencia ni el servicio.
        QVERIFY(!c.error().diagnostico.contains(ref.valor().texto()));
        QVERIFY(!c.error().diagnostico.contains(kBundle));
        QCOMPARE(int(almacen.borrar(ref.valor()).error().categoria), categoria);
        QCOMPARE(int(almacen.listar().error().categoria), categoria);
    }

    void itemDanado_data()
    {
        QTest::addColumn<QByteArray>("dato");
        QByteArray valido(68, '\0');
        std::memcpy(valido.data(), "AFK1", 4);
        valido[4] = 2; // recuperacion con parametros en cero
        QTest::newRow("vacio") << QByteArray();
        QTest::newRow("corto") << valido.left(67);
        QTest::newRow("largo") << (valido + QByteArray(1, '\0'));
        QByteArray magia = valido;
        magia[0] = 'X';
        QTest::newRow("magia") << magia;
        QByteArray tipo = valido;
        tipo[4] = 3;
        QTest::newRow("tipo") << tipo;
        QByteArray reservado = valido;
        reservado[6] = 1;
        QTest::newRow("reservado") << reservado;
        QByteArray recuperacionConSal = valido;
        recuperacionConSal[20] = 1;
        QTest::newRow("recuperacion-con-sal") << recuperacionConSal;
        QByteArray contrasenaSinParametros = valido;
        contrasenaSinParametros[4] = 1; // memoria/iteraciones en cero: invalidos
        QTest::newRow("contrasena-parametros") << contrasenaSinParametros;
    }
    void itemDanado()
    {
        QFETCH(QByteArray, dato);
        auto api = std::make_shared<fakes::FakeKeychainApi>();
        AlmacenKekRespaldoMacOS almacen(kBundle, kAmbito, api);
        const ReferenciaKek ref = ReferenciaKek::generar();
        api->poner(almacen.servicio(), ref.texto(), dato);
        QCOMPARE(almacen.cargar(ref).error().categoria, C::Danada);
    }
};

QTEST_GUILESS_MAIN(TestAlmacenKekRespaldo)
#include "TestAlmacenKekRespaldo.moc"
