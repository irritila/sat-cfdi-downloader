#include "TestExportacionFacturas.h"

#include "domain/cfdi/ExportacionFacturas.h"

#include <QTest>

using namespace satcfdi;

namespace {

// Datos sinteticos: UUID inventados, RFC generico del SAT, ruta de ejemplo.
const QString kUuidA = QStringLiteral("AAAAAAAA-0000-4000-8000-000000000001");
const QString kUuidB = QStringLiteral("BBBBBBBB-0000-4000-8000-000000000002");
const QString kUuidC = QStringLiteral("CCCCCCCC-0000-4000-8000-000000000003");
const PerfilId kPerfil = *PerfilId::desdeTexto(u"0f000000-0000-4000-8000-000000000001");
const MesCalendario kAgosto = *MesCalendario::crear(2026, 8);
const MesCalendario kSeptiembre = *MesCalendario::crear(2026, 9);

OmisionFacturaExportacion factura(TipoDescarga d, MesCalendario mes, const QString& id, MotivoOmisionFactura m)
{
    return OmisionFacturaExportacion{d, mes, id, m};
}

} // namespace

void TestExportacionFacturas::nombreSugeridoSinRfc()
{
    const QDate hoy(2026, 10, 24);
    QCOMPARE(nombreSugeridoZip(AlcanceMesExportacion{kPerfil, kSeptiembre, std::nullopt}, hoy),
             QStringLiteral("Archivo-Fiscal-2026-09.zip"));
    QCOMPARE(nombreSugeridoZip(AlcanceRangoExportacion{kPerfil, kAgosto, kSeptiembre, TipoDescarga::Emitidos}, hoy),
             QStringLiteral("Archivo-Fiscal-2026-08_2026-09.zip"));
    // Rango de un solo mes: forma de mes.
    QCOMPARE(nombreSugeridoZip(AlcanceRangoExportacion{kPerfil, kAgosto, kAgosto, std::nullopt}, hoy),
             QStringLiteral("Archivo-Fiscal-2026-08.zip"));
    QCOMPARE(nombreSugeridoZip(AlcanceSeleccionExportacion{kPerfil, {}}, hoy),
             QStringLiteral("Archivo-Fiscal-seleccion-2026-10-24.zip"));
    // Las sugerencias ya estan saneadas.
    for (const QString& n : {nombreSugeridoZip(AlcanceMesExportacion{kPerfil, kSeptiembre, std::nullopt}, hoy),
                             nombreSugeridoZip(AlcanceSeleccionExportacion{kPerfil, {}}, hoy)}) {
        QCOMPARE(sanearNombreZip(n), n);
    }
}

void TestExportacionFacturas::sanearNombre_data()
{
    QTest::addColumn<QString>("entrada");
    QTest::addColumn<QString>("esperado");

    QTest::newRow("vacio") << QString() << QStringLiteral("Archivo-Fiscal.zip");
    QTest::newRow("solo espacios") << QStringLiteral("   ") << QStringLiteral("Archivo-Fiscal.zip");
    QTest::newRow("sin extension") << QStringLiteral("facturas") << QStringLiteral("facturas.zip");
    QTest::newRow("con extension") << QStringLiteral("facturas.zip") << QStringLiteral("facturas.zip");
    QTest::newRow("doble extension") << QStringLiteral("facturas.zip.zip") << QStringLiteral("facturas.zip");
    QTest::newRow("extension en mayusculas") << QStringLiteral("facturas.ZIP") << QStringLiteral("facturas.zip");
    QTest::newRow("otra extension") << QStringLiteral("facturas.txt") << QStringLiteral("facturas.txt.zip");
    QTest::newRow("diagonal") << QStringLiteral("2026/09") << QStringLiteral("2026-09.zip");
    QTest::newRow("dos puntos") << QStringLiteral("a:b") << QStringLiteral("a-b.zip");
    QTest::newRow("ruta absoluta") << QStringLiteral("/Users/usuario/x") << QStringLiteral("-Users-usuario-x.zip");
    QTest::newRow("inicia con punto") << QStringLiteral(".oculto") << QStringLiteral("oculto.zip");
    QTest::newRow("puntos y espacios iniciales") << QStringLiteral(". .. oculto.zip") << QStringLiteral("oculto.zip");
    QTest::newRow("solo extension") << QStringLiteral(".zip") << QStringLiteral("Archivo-Fiscal.zip");
    QTest::newRow("punto y extension") << QStringLiteral("..zip.zip") << QStringLiteral("Archivo-Fiscal.zip");
    QTest::newRow("controles y formato") << QStringLiteral("a\nb\tc‮d​e") << QStringLiteral("abcde.zip");
    QTest::newRow("NFC") << QStringLiteral("factura-é") << QStringLiteral("factura-é.zip");
    QTest::newRow("espacios internos") << QStringLiteral(" mis facturas .zip ") << QStringLiteral("mis facturas.zip");
}

void TestExportacionFacturas::sanearNombre()
{
    QFETCH(QString, entrada);
    QFETCH(QString, esperado);
    const QString r = sanearNombreZip(entrada);
    QCOMPARE(r, esperado);
    QCOMPARE(sanearNombreZip(r), r); // idempotente
}

void TestExportacionFacturas::sanearNombreRecortaA255BytesSinPartir()
{
    const auto verificar = [](const QString& entrada, qsizetype bytesEsperados) {
        const QString r = sanearNombreZip(entrada);
        const QByteArray utf8 = r.toUtf8();
        QVERIFY2(utf8.size() <= 255, qPrintable(QString::number(utf8.size())));
        QCOMPARE(utf8.size(), bytesEsperados);
        QVERIFY(r.endsWith(QStringLiteral(".zip")));
        QVERIFY(!r.endsWith(QStringLiteral(".zip.zip")));
        QCOMPARE(QString::fromUtf8(utf8), r); // sin sustitutos aislados
        QCOMPARE(sanearNombreZip(r), r);
    };
    verificar(QString(300, QLatin1Char('a')), 255);
    verificar(QString(255, QLatin1Char('a')), 255);
    verificar(QString(251, QLatin1Char('a')), 255);
    verificar(QString(200, QChar(u'\u00F1')), 4 + 125 * 2); // "ñ": 2 bytes
    verificar(QString(200, QChar(u'\u20AC')), 4 + 83 * 3);  // "€": 3 bytes
    QString emoji;
    for (int i = 0; i < 100; ++i) {
        emoji += QString::fromUtf8("\xF0\x9F\x98\x80"); // U+1F600, par sustituto
    }
    verificar(emoji, 4 + 62 * 4);
    // El recorte deja ".zip" al final del tallo: no queda doble extension.
    verificar(QString(247, QLatin1Char('a')) + QStringLiteral(".zipzzzzzzzz"), 251);
    QCOMPARE(sanearNombreZip(QString(247, QLatin1Char('a')) + QStringLiteral(".zipzzzzzzzz")),
             QString(247, QLatin1Char('a')) + QStringLiteral(".zip"));
}

void TestExportacionFacturas::asignadorUuidCanonicoSinUuidYColisiones()
{
    AsignadorNombresExportacion a;
    const auto e = TipoDescarga::Emitidos;
    const auto r = TipoDescarga::Recibidos;

    const NombreEntradaFactura n1 = a.siguiente(e, kSeptiembre, kUuidA);
    QCOMPARE(n1.carpeta, QStringLiteral("emitidas/2026-09"));
    QCOMPARE(n1.base, kUuidA);
    QCOMPARE(n1.xml(), QStringLiteral("emitidas/2026-09/") + kUuidA + QStringLiteral(".xml"));
    QCOMPARE(n1.pdf(), QStringLiteral("emitidas/2026-09/") + kUuidA + QStringLiteral(".pdf"));

    // Colision: sufijo -<n> desde 2; XML y PDF comparten la base.
    const NombreEntradaFactura n2 = a.siguiente(e, kSeptiembre, kUuidA);
    QCOMPARE(n2.base, kUuidA + QStringLiteral("-2"));
    QCOMPARE(n2.pdf(), QStringLiteral("emitidas/2026-09/") + kUuidA + QStringLiteral("-2.pdf"));
    QCOMPARE(a.siguiente(e, kSeptiembre, kUuidA.toLower()).base, kUuidA + QStringLiteral("-3")); // minusculas: canonico

    // Sin UUID o no canonico: sin-uuid-<n> por carpeta, nunca el valor original.
    QCOMPARE(a.siguiente(e, kSeptiembre, u"").base, QStringLiteral("sin-uuid-1"));
    QCOMPARE(a.siguiente(e, kSeptiembre, u" AAAAAAAA-0000-4000-8000-000000000001").base, QStringLiteral("sin-uuid-2"));
    QCOMPARE(a.siguiente(e, kSeptiembre, u"../../etc/passwd").base, QStringLiteral("sin-uuid-3"));
    QCOMPARE(a.siguiente(e, kSeptiembre, u"XAXX010101000").base, QStringLiteral("sin-uuid-4"));
    QCOMPARE(a.siguiente(e, kSeptiembre, u"/Users/usuario/x.xml").base, QStringLiteral("sin-uuid-5"));

    // Otra carpeta: contadores y colisiones independientes.
    QCOMPARE(a.siguiente(r, kSeptiembre, kUuidA).base, kUuidA);
    QCOMPARE(a.siguiente(e, kAgosto, kUuidA).base, kUuidA);
    QCOMPARE(a.siguiente(r, kSeptiembre, u"").base, QStringLiteral("sin-uuid-1"));
    QCOMPARE(a.siguiente(r, kSeptiembre, u"").xml(), QStringLiteral("recibidas/2026-09/sin-uuid-2.xml"));

    // Ninguna entrada es absoluta, sube de nivel ni lleva otra cosa que ASCII.
    AsignadorNombresExportacion b;
    for (const QString& u : {kUuidA, kUuidB, QString(), QStringLiteral(".."), QStringLiteral("a/../b")}) {
        const NombreEntradaFactura n = b.siguiente(r, kAgosto, u);
        for (const QString& ruta : {n.xml(), n.pdf()}) {
            QVERIFY(!ruta.startsWith(QLatin1Char('/')));
            QVERIFY(!ruta.split(QLatin1Char('/')).contains(QStringLiteral("..")));
            QCOMPARE(ruta.count(QLatin1Char('/')), 2);
            for (const QChar c : ruta) {
                QVERIFY(c.unicode() < 0x80);
            }
        }
    }
}

void TestExportacionFacturas::omitidasByteABytes()
{
    const auto e = TipoDescarga::Emitidos;
    const auto r = TipoDescarga::Recibidos;
    const OmisionExportacion sinPdf = factura(r, kSeptiembre, kUuidB, MotivoOmisionFactura::SinPdf);
    const QList<OmisionExportacion> omisiones = {
        sinPdf,
        OmisionMesExportacion{e, kSeptiembre, MotivoOmisionMes::DescargasPendientes},
        factura(e, kAgosto, kUuidA.toLower(), MotivoOmisionFactura::CambioDesdeIndexacion),
        factura(e, kSeptiembre, QStringLiteral("sin-uuid-2"), MotivoOmisionFactura::Ilegible),
        factura(e, kSeptiembre, kUuidA, MotivoOmisionFactura::NoDisponibleEnEsteEquipo),
        OmisionMesExportacion{r, kAgosto, MotivoOmisionMes::CoberturaIncompleta},
        factura(r, kSeptiembre, kUuidC, MotivoOmisionFactura::YaNoDisponible),
        sinPdf, // repetida: una linea
    };
    const QByteArray esperado =
        "emitidas/2026-08/AAAAAAAA-0000-4000-8000-000000000001: cambi\xC3\xB3 desde que se index\xC3\xB3\n"
        "emitidas/2026-09: descargas pendientes\n"
        "emitidas/2026-09/AAAAAAAA-0000-4000-8000-000000000001: no disponible en este equipo\n"
        "emitidas/2026-09/sin-uuid-2: ilegible\n"
        "recibidas/2026-08: cobertura incompleta\n"
        "recibidas/2026-09/BBBBBBBB-0000-4000-8000-000000000002: sin PDF\n"
        "recibidas/2026-09/CCCCCCCC-0000-4000-8000-000000000003: ya no disponible\n";
    const std::optional<QByteArray> texto = formatearOmitidas(omisiones);
    QVERIFY(texto);
    QCOMPARE(*texto, esperado);
    QVERIFY(!texto->startsWith("\xEF\xBB\xBF")); // sin BOM
    QVERIFY(!texto->contains('\r'));

    // El orden de entrada no cambia la salida.
    QList<OmisionExportacion> invertidas(omisiones.crbegin(), omisiones.crend());
    QCOMPARE(formatearOmitidas(invertidas), texto);

    // Conteos derivables del archivo (criterio T026.1): omitidas = lineas de
    // CFDI con motivo distinto de "sin PDF"; sin PDF = las de ese motivo.
    int omitidasCfdi = 0;
    int sinPdfLineas = 0;
    for (const QByteArray& l : texto->split('\n')) {
        if (l.count('/') == 2) {
            ++(l.endsWith(": sin PDF") ? sinPdfLineas : omitidasCfdi);
        }
    }
    QCOMPARE(omitidasCfdi, 4);
    QCOMPARE(sinPdfLineas, 1);

    // Mes unico y motivos de mes.
    QCOMPARE(formatearOmitidas({OmisionMesExportacion{r, kSeptiembre, MotivoOmisionMes::DescargasPendientes}}),
             std::optional<QByteArray>("recibidas/2026-09: descargas pendientes\n"));
}

void TestExportacionFacturas::omitidasSinLineasNoHayArchivo()
{
    QCOMPARE(formatearOmitidas({}), std::optional<QByteArray>());
}

void TestExportacionFacturas::omitidasNuncaSerializaIdentificadoresNoValidados()
{
    const auto e = TipoDescarga::Emitidos;
    const auto m = MotivoOmisionFactura::Ilegible;
    const QList<OmisionExportacion> omisiones = {
        factura(e, kSeptiembre, QStringLiteral("/Users/usuario/paquete.zip"), m),
        factura(e, kSeptiembre, QStringLiteral("XAXX010101000"), m),
        factura(e, kSeptiembre, QStringLiteral(" ") + kUuidA, m),
        factura(e, kSeptiembre, QStringLiteral("sin-uuid-0"), m),
        factura(e, kSeptiembre, QStringLiteral("sin-uuid-3-1"), m),
        factura(e, kSeptiembre, QString(), m),
        factura(e, kSeptiembre, QStringLiteral("sin-uuid-3-2"), m),
        factura(e, kSeptiembre, kUuidA + QStringLiteral("-12"), m),
    };
    // Cada identificador no validado conserva su linea (hallazgo 7 de
    // seguridad: el numero de lineas de CFDI es el conteo de omisiones).
    QByteArray esperado = "emitidas/2026-09/AAAAAAAA-0000-4000-8000-000000000001-12: ilegible\n";
    for (int i = 0; i < 6; ++i) {
        esperado += "emitidas/2026-09/sin-uuid: ilegible\n";
    }
    esperado += "emitidas/2026-09/sin-uuid-3-2: ilegible\n";
    QCOMPARE(formatearOmitidas(omisiones), std::optional<QByteArray>(esperado));
}

void TestExportacionFacturas::omitidasSinUuidNoSeFusionan()
{
    const auto e = TipoDescarga::Emitidos;
    const auto m = MotivoOmisionFactura::YaNoDisponible;
    // Bases distintas del asignador: dos lineas. La misma factura (mismo
    // identificador validado y motivo) dos veces: una linea.
    QCOMPARE(formatearOmitidas({factura(e, kSeptiembre, QStringLiteral("sin-uuid-1"), m),
                                factura(e, kSeptiembre, QStringLiteral("sin-uuid-2"), m),
                                factura(e, kSeptiembre, QStringLiteral("sin-uuid-2"), m)}),
             std::optional<QByteArray>("emitidas/2026-09/sin-uuid-1: ya no disponible\n"
                                       "emitidas/2026-09/sin-uuid-2: ya no disponible\n"));
    // Dos omisiones sin identificador valido del mismo mes y motivo: dos lineas.
    QCOMPARE(formatearOmitidas({factura(e, kSeptiembre, QString(), m), factura(e, kSeptiembre, QString(), m)}),
             std::optional<QByteArray>("emitidas/2026-09/sin-uuid: ya no disponible\n"
                                       "emitidas/2026-09/sin-uuid: ya no disponible\n"));
    // Lineas de mes repetidas: una.
    const OmisionMesExportacion mes{e, kSeptiembre, MotivoOmisionMes::CoberturaIncompleta};
    QCOMPARE(formatearOmitidas({mes, mes}), std::optional<QByteArray>("emitidas/2026-09: cobertura incompleta\n"));
}

void TestExportacionFacturas::mesesDelAlcance()
{
    QCOMPARE(mesesDe(AlcanceMesExportacion{kPerfil, kSeptiembre, std::nullopt}), QList<MesCalendario>{kSeptiembre});
    const MesCalendario dic = *MesCalendario::crear(2025, 12);
    const MesCalendario ene = *MesCalendario::crear(2026, 1);
    QCOMPARE(mesesDe(AlcanceRangoExportacion{kPerfil, dic, ene, std::nullopt}), (QList<MesCalendario>{dic, ene}));
    QVERIFY(mesesDe(AlcanceRangoExportacion{kPerfil, ene, dic, std::nullopt}).isEmpty()); // invertido
    AlcanceSeleccionExportacion s{kPerfil, {}};
    s.facturas = {FacturaSeleccionada{{QStringLiteral("p1"), 0}, kSeptiembre, TipoDescarga::Emitidos, kUuidA},
                  FacturaSeleccionada{{QStringLiteral("p2"), 0}, kAgosto, TipoDescarga::Recibidos, kUuidB},
                  FacturaSeleccionada{{QStringLiteral("p3"), 1}, kSeptiembre, TipoDescarga::Recibidos, kUuidC}};
    QCOMPARE(mesesDe(s), (QList<MesCalendario>{kAgosto, kSeptiembre}));
    QCOMPARE(perfilDe(s), kPerfil);
}
