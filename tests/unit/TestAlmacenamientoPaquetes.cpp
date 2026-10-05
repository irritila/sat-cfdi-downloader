#include "TestAlmacenamientoPaquetes.h"

#include "fakes/FakePackageStorage.h"

#include "application/operaciones/SenalCancelacion.h"
#include "ports/PackageStorage.h"

#include <QTest>

using namespace satcfdi;
using fakes::FakePackageStorage;
using fakes::FuenteZipEnMemoria;

namespace {

const QString kUuid = QStringLiteral("0f8fad5b-d9cb-469f-a165-70867728950e");

UbicacionPaquete ubicacion(const QString& idPaquete = QStringLiteral("0F8FAD5B-D9CB-469F-A165-70867728950E_01"))
{
    return UbicacionPaquete{QStringLiteral("AAA010101AAA"), QStringLiteral("2026-01-31T23:59:59"), kUuid, idPaquete};
}

QString nombre(const QString& id)
{
    return PackageStorage::derivarRutaRelativa(ubicacion(id)).valor().section(QLatin1Char('/'), 3);
}

} // namespace

void TestAlmacenamientoPaquetes::derivacionDeterministaYForma()
{
    const auto a = PackageStorage::derivarRutaRelativa(ubicacion());
    const auto b = PackageStorage::derivarRutaRelativa(ubicacion());
    QVERIFY(a && b);
    QCOMPARE(a.valor(), b.valor());
    // D1: mes de FechaInicial (aunque el rango cruce meses); D2: UUID local.
    QCOMPARE(a.valor(), QStringLiteral("AAA010101AAA/2026-01/%1/0F8FAD5B-D9CB-469F-A165-70867728950E_01.zip").arg(kUuid));
    QVERIFY(!a.valor().startsWith(QLatin1Char('/')));
    QVERIFY(rutapaquete::esRutaFinalValida(a.valor()));
    // RFC de persona fisica (13) con Ñ y &.
    UbicacionPaquete fisica = ubicacion();
    fisica.rfcSolicitante = QStringLiteral("ÑA&A010101AB1");
    QVERIFY(PackageStorage::derivarRutaRelativa(fisica));
}

void TestAlmacenamientoPaquetes::idValidoSinHash()
{
    QCOMPARE(nombre(QStringLiteral("uuid_01")), QStringLiteral("uuid_01.zip"));
    QCOMPARE(nombre(QStringLiteral("a.b-c_D9")), QStringLiteral("a.b-c_D9.zip"));
    QCOMPARE(nombre(QString(100, QLatin1Char('x'))), QString(100, QLatin1Char('x')) + QStringLiteral(".zip"));
    QVERIFY(!nombre(QStringLiteral("uuid_01")).contains(QStringLiteral("--")));
}

void TestAlmacenamientoPaquetes::saneamientoNoEscapaYDistingue_data()
{
    QTest::addColumn<QString>("id");
    QTest::addColumn<QString>("prefijo");
    QTest::newRow("barra") << QStringLiteral("a/b") << QStringLiteral("a_b");
    QTest::newRow("puntos") << QStringLiteral("..") << QStringLiteral("_.");
    QTest::newRow("escape") << QStringLiteral("../../etc/passwd") << QStringLiteral("_._.._etc_passwd");
    QTest::newRow("espacios") << QStringLiteral("id con espacios") << QStringLiteral("id_con_espacios");
    QTest::newRow("unicode") << QStringLiteral("año_ü_😀") << QStringLiteral("a_o____"); // 7 puntos de codigo (emoji = 1)
    QTest::newRow("punto_inicial") << QStringLiteral(".oculto") << QStringLiteral("_oculto");
    QTest::newRow("largo") << QString(300, QLatin1Char('z')) << QString(100, QLatin1Char('z'));
    QTest::newRow("largo_unicode") << QString(300, QChar(0x00F1)) << QString(100, QLatin1Char('_'));
    QTest::newRow("nul") << QStringLiteral("a") + QChar(0) + QStringLiteral("b") << QStringLiteral("a_b");
}

void TestAlmacenamientoPaquetes::saneamientoNoEscapaYDistingue()
{
    QFETCH(QString, id);
    QFETCH(QString, prefijo);
    const auto r = PackageStorage::derivarRutaRelativa(ubicacion(id));
    QVERIFY(r);
    const QStringList partes = r.valor().split(QLatin1Char('/'));
    QCOMPARE(partes.size(), 4); // ningun componente extra: no sale de la carpeta
    for (const QString& p : partes) {
        QVERIFY(!p.isEmpty() && p != QStringLiteral(".") && p != QStringLiteral(".."));
        QVERIFY(p.toUtf8().size() <= 255);
    }
    QVERIFY(rutapaquete::esRutaFinalValida(r.valor()));
    QVERIFY(!partes.last().startsWith(QLatin1Char('.')));
    // "--" + 16 hex del SHA-256 del id ORIGINAL (UTF-8).
    const QString esperado = prefijo + QStringLiteral("--")
                             + QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256)
                                                       .toHex()
                                                       .left(16))
                             + QStringLiteral(".zip");
    QCOMPARE(partes.last(), esperado);
}

void TestAlmacenamientoPaquetes::entradaInvalida_data()
{
    QTest::addColumn<QString>("rfc");
    QTest::addColumn<QString>("fecha");
    QTest::addColumn<QString>("uuid");
    QTest::addColumn<QString>("id");
    const UbicacionPaquete u = ubicacion();
    QTest::newRow("rfc_minusculas") << QStringLiteral("aaa010101aaa") << u.fechaInicialSat << u.solicitudId << u.idPaqueteSat;
    QTest::newRow("rfc_espacios") << QStringLiteral(" AAA010101AAA") << u.fechaInicialSat << u.solicitudId << u.idPaqueteSat;
    QTest::newRow("rfc_invalido") << QStringLiteral("AAA") << u.fechaInicialSat << u.solicitudId << u.idPaqueteSat;
    QTest::newRow("rfc_traversal") << QStringLiteral("../AAA010101") << u.fechaInicialSat << u.solicitudId << u.idPaqueteSat;
    QTest::newRow("mes_13") << u.rfcSolicitante << QStringLiteral("2026-13-01T00:00:00") << u.solicitudId << u.idPaqueteSat;
    QTest::newRow("dia_invalido") << u.rfcSolicitante << QStringLiteral("2026-02-30T00:00:00") << u.solicitudId << u.idPaqueteSat;
    QTest::newRow("fecha_formato") << u.rfcSolicitante << QStringLiteral("2026-01") << u.solicitudId << u.idPaqueteSat;
    QTest::newRow("uuid_mayusculas") << u.rfcSolicitante << u.fechaInicialSat << kUuid.toUpper() << u.idPaqueteSat;
    QTest::newRow("uuid_llaves") << u.rfcSolicitante << u.fechaInicialSat << QStringLiteral("{%1}").arg(kUuid) << u.idPaqueteSat;
    QTest::newRow("uuid_traversal") << u.rfcSolicitante << u.fechaInicialSat << QStringLiteral("..") << u.idPaqueteSat;
    QTest::newRow("id_vacio") << u.rfcSolicitante << u.fechaInicialSat << u.solicitudId << QString();
}

void TestAlmacenamientoPaquetes::entradaInvalida()
{
    QFETCH(QString, rfc);
    QFETCH(QString, fecha);
    QFETCH(QString, uuid);
    QFETCH(QString, id);
    const UbicacionPaquete u{rfc, fecha, uuid, id};
    const auto r = PackageStorage::derivarRutaRelativa(u);
    QVERIFY(!r);
    QCOMPARE(r.error(), ErrorAlmacenamiento::EntradaInvalida);
    // El fake tampoco crea nada.
    FakePackageStorage fake;
    FuenteZipEnMemoria fuente({QByteArrayLiteral("x")});
    const auto g = fake.guardarAtomico(u, fuente, Cancelacion());
    QVERIFY(!g);
    QCOMPARE(g.error().almacenamiento, ErrorAlmacenamiento::EntradaInvalida);
    QCOMPARE(fuente.entregados(), 0);
    QVERIFY(fake.finales().isEmpty());
}

void TestAlmacenamientoPaquetes::gramaticaFinalesYTemporales()
{
    using namespace rutapaquete;
    const QString final = nombre(QStringLiteral("a/b"));
    QVERIFY(esNombreFinal(final));
    QVERIFY(esNombreFinal(QStringLiteral("uuid_01.zip")));
    QVERIFY(!esNombreFinal(QStringLiteral(".uuid_01.zip")));
    QVERIFY(!esNombreFinal(QStringLiteral("uuid_01.ZIP")));
    QVERIFY(!esNombreFinal(QStringLiteral("notas.txt")));
    QCOMPARE(finalDeTemporal(QStringLiteral(".uuid_01.zip.0123456789abcdef.part")),
             std::optional<QString>(QStringLiteral("uuid_01.zip")));
    QVERIFY(!finalDeTemporal(QStringLiteral(".uuid_01.zip.0123456789ABCDEF.part"))); // hex en minusculas
    QVERIFY(!finalDeTemporal(QStringLiteral(".uuid_01.zip.0123.part")));
    QVERIFY(!finalDeTemporal(QStringLiteral("uuid_01.zip.0123456789abcdef.part")));
    QVERIFY(!finalDeTemporal(QStringLiteral(".notas.txt.0123456789abcdef.part")));
    // Rutas relativas validas solo con la forma exacta (D8).
    QVERIFY(!esRutaFinalValida(QStringLiteral("/AAA010101AAA/2026-01/%1/a.zip").arg(kUuid)));
    QVERIFY(!esRutaFinalValida(QStringLiteral("AAA010101AAA/2026-01/../a.zip")));
    QVERIFY(!esRutaFinalValida(QStringLiteral("AAA010101AAA/2026-01/%1/../a.zip").arg(kUuid)));
    QVERIFY(!esRutaFinalValida(QStringLiteral("AAA010101AAA//%1/a.zip").arg(kUuid)));
    QVERIFY(esRutaFinalValida(QStringLiteral("AAA010101AAA/2026-01/%1/a.zip").arg(kUuid)));
}

void TestAlmacenamientoPaquetes::cancelacionDesdeSenal()
{
    QVERIFY(!Cancelacion().solicitada());
    SenalCancelacion senal;
    const Cancelacion c([senal] { return senal.solicitada(); });
    QVERIFY(!c.solicitada());
    senal.solicitar();
    QVERIFY(c.solicitada());
}

void TestAlmacenamientoPaquetes::fakeGuardaColisionaYFalla()
{
    FakePackageStorage fake;
    const QString rel = PackageStorage::derivarRutaRelativa(ubicacion()).valor();
    FuenteZipEnMemoria fuente({QByteArrayLiteral("PK"), QByteArrayLiteral("\x03\x04"), QByteArrayLiteral("fin")});
    const auto ok = fake.guardarAtomico(ubicacion(), fuente, Cancelacion());
    QVERIFY(ok);
    QCOMPARE(ok.valor().rutaRelativa, rel);
    QVERIFY(!ok.valor().advertenciaDurabilidad);
    QCOMPARE(fake.bytesFinal(rel), std::optional<QByteArray>(QByteArrayLiteral("PK\x03\x04" "fin")));
    QCOMPARE(fake.existeArchivoFinal(rel).valor(), true);

    // Colision: nunca sobrescribe.
    FuenteZipEnMemoria otra({QByteArrayLiteral("otro")});
    const auto colision = fake.guardarAtomico(ubicacion(), otra, Cancelacion());
    QVERIFY(!colision);
    QCOMPARE(colision.error().almacenamiento, ErrorAlmacenamiento::ColisionDestino);
    QCOMPARE(fake.bytesFinal(rel), std::optional<QByteArray>(QByteArrayLiteral("PK\x03\x04" "fin")));

    // Falla programada, fuente fallida y cancelacion en el chunk K: sin final.
    const UbicacionPaquete u2 = ubicacion(QStringLiteral("p2"));
    fake.programarFalla(ErrorAlmacenamiento::SinEspacio);
    FuenteZipEnMemoria f2({QByteArrayLiteral("a")});
    QCOMPARE(fake.guardarAtomico(u2, f2, Cancelacion()).error().almacenamiento, ErrorAlmacenamiento::SinEspacio);
    FuenteZipEnMemoria f3({QByteArrayLiteral("a"), QByteArrayLiteral("b")});
    f3.fallarEnChunk = 2;
    QCOMPARE(fake.guardarAtomico(u2, f3, Cancelacion()).error().origen, ErrorGuardarZip::Origen::Fuente);
    bool cancelar = false;
    FuenteZipEnMemoria f4({QByteArrayLiteral("a"), QByteArrayLiteral("b"), QByteArrayLiteral("c")});
    f4.alEntregar = [&](int k) { cancelar = k == 2; };
    QCOMPARE(fake.guardarAtomico(u2, f4, Cancelacion([&] { return cancelar; })).error().almacenamiento,
             ErrorAlmacenamiento::Cancelada);
    QCOMPARE(f4.entregados(), 2);
    const QString rel2 = PackageStorage::derivarRutaRelativa(u2).valor();
    QCOMPARE(fake.existeArchivoFinal(rel2).valor(), false);
    fake.programarAdvertenciaDurabilidad();
    FuenteZipEnMemoria f5({QByteArrayLiteral("a")});
    QVERIFY(fake.guardarAtomico(u2, f5, Cancelacion()).valor().advertenciaDurabilidad);

    // Lectura: ruta invalida y error de lectura.
    QCOMPARE(fake.existeArchivoFinal(QStringLiteral("../x.zip")).error(), ErrorAlmacenamiento::EntradaInvalida);
    fake.programarFallaLectura(ErrorAlmacenamiento::LecturaRaiz);
    QCOMPARE(fake.existeArchivoFinal(rel).error(), ErrorAlmacenamiento::LecturaRaiz);
}

void TestAlmacenamientoPaquetes::fakeEscaneoYEliminarSoloTemporales()
{
    FakePackageStorage fake;
    const QString dir = QStringLiteral("AAA010101AAA/2026-01/%1").arg(kUuid);
    fake.agregarFinal(dir + QStringLiteral("/p1.zip"));
    fake.agregarTemporal(dir + QStringLiteral("/.p2.zip.0123456789abcdef.part"));
    fake.agregarTemporal(dir + QStringLiteral("/ajeno.part"));
    fake.agregarFinal(QStringLiteral("fuera/p3.zip"));
    const auto h = fake.escanearRecuperacion();
    QVERIFY(h);
    QCOMPARE(h.valor().size(), 2);
    const HallazgoFilesystem temporal = h.valor().at(0);
    const HallazgoFilesystem final = h.valor().at(1);
    QCOMPARE(temporal.tipo, TipoHallazgo::Temporal);
    QCOMPARE(temporal.archivoFinal, QStringLiteral("p2.zip"));
    QCOMPARE(temporal.solicitudId.texto(), kUuid);
    QCOMPARE(final.tipo, TipoHallazgo::Final);
    QCOMPARE(final.archivoFinal, QStringLiteral("p1.zip"));

    const auto rechazado = fake.eliminarTemporal(final);
    QVERIFY(!rechazado);
    QCOMPARE(rechazado.error(), ErrorAlmacenamiento::EntradaInvalida);
    QVERIFY(fake.bytesFinal(final.rutaRelativa));
    QVERIFY(fake.eliminarTemporal(temporal));
    QCOMPARE(fake.temporales(), QStringList{dir + QStringLiteral("/ajeno.part")});
}
