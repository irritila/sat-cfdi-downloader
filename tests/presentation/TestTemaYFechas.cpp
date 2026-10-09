// satcfdi_tema_tests (T013 corte 1): estilo Basic, Theme.qml frente a
// docs/design/ui-ux-v2/tokens.json en claro y oscuro, alias de estados e
// iconos, Icono.qml tenido con color exacto y formato de fechas (D5) en varias
// zonas horarias. Cualquier warning hace fallar la prueba.

#include "FormatoFechas.h"

#include <QColor>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QStyleHints>
#include <QTest>
#include <QTimeZone>

#include <ctime>
#include <memory>

using satcfdi::FormatoFechas;

namespace {

QString camel(const QString& nombre)
{
    const QStringList partes = nombre.split(QLatin1Char('-'));
    QString r = partes.constFirst();
    for (qsizetype i = 1; i < partes.size(); ++i) {
        r += partes.at(i).left(1).toUpper() + partes.at(i).mid(1);
    }
    return r;
}

// Color CSS del JSON (#RRGGBB o #RRGGBBAA) a QColor.
QColor colorCss(const QString& css)
{
    if (css.size() == 9) {
        const auto canal = [&](int i) { return css.mid(1 + 2 * i, 2).toInt(nullptr, 16); };
        return QColor(canal(0), canal(1), canal(2), canal(3));
    }
    return QColor(css);
}

int px(const QJsonValue& v)
{
    return v.isDouble() ? v.toInt() : v.toString().remove(QStringLiteral("px")).toInt();
}

QJsonObject leerTokens()
{
    QFile archivo(QStringLiteral(SATCFDI_TOKENS_JSON));
    if (!archivo.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(archivo.readAll()).object();
}

void fijarZona(const char* tz)
{
    qputenv("TZ", tz);
    ::tzset();
}

} // namespace

class TestTemaYFechas : public QObject {
    Q_OBJECT

private slots:
    void init();
    void initTestCase();
    void cleanupTestCase();

    void estiloBasicoActivo();
    void tokensCoincidenEnAmbosTemas_data();
    void tokensCoincidenEnAmbosTemas();
    void estadosAliasEIconos();
    void oscuroSigueAlSistemaYSeSobrescribe();
    void iconoTenidoConColorExacto();
    void iconoInexistenteAvisa();

    void fechasFormato();
    void fechasSinHoraNoCambianConLaZona_data();
    void fechasSinHoraNoCambianConLaZona();
    void fechaHoraUsaHoraLocal();
    void fechasDesdeQml();

private:
    QObject* tema(QQmlEngine& engine) { return engine.singletonInstance<QObject*>("SatCfdiDownloader", "Theme"); }

    QJsonObject m_tokens;
    QByteArray m_tzOriginal;
};

void TestTemaYFechas::init()
{
    QTest::failOnWarning(QRegularExpression(QStringLiteral("^(?!Populating font family aliases took).*")));
}

void TestTemaYFechas::initTestCase()
{
    m_tzOriginal = qgetenv("TZ");
    m_tokens = leerTokens();
    QVERIFY2(!m_tokens.isEmpty(), SATCFDI_TOKENS_JSON);
}

void TestTemaYFechas::cleanupTestCase()
{
    if (m_tzOriginal.isEmpty()) {
        qunsetenv("TZ");
    } else {
        qputenv("TZ", m_tzOriginal);
    }
    ::tzset();
}

void TestTemaYFechas::estiloBasicoActivo()
{
    QCOMPARE(QQuickStyle::name(), QStringLiteral("Basic"));
}

void TestTemaYFechas::tokensCoincidenEnAmbosTemas_data()
{
    QTest::addColumn<bool>("oscuro");
    QTest::newRow("claro") << false;
    QTest::newRow("oscuro") << true;
}

void TestTemaYFechas::tokensCoincidenEnAmbosTemas()
{
    QFETCH(bool, oscuro);
    const QString clave = oscuro ? QStringLiteral("dark") : QStringLiteral("light");
    QQmlEngine engine;
    QObject* t = tema(engine);
    QVERIFY(t);
    QVERIFY(t->setProperty("oscuro", oscuro));

    // Colores (con referencias {token} resueltas).
    const QJsonArray colores = m_tokens.value(QStringLiteral("color")).toObject().value(QStringLiteral("tokens")).toArray();
    QHash<QString, QString> porNombre;
    for (const QJsonValue& v : colores) {
        const QJsonObject o = v.toObject();
        porNombre.insert(o.value(QStringLiteral("name")).toString(),
                         o.value(QStringLiteral("value")).toObject().value(clave).toString());
    }
    QVERIFY(porNombre.size() > 100);
    const QRegularExpression referencia(QStringLiteral("^\\{([^}]+)\\}$"));
    int comparados = 0;
    for (auto it = porNombre.cbegin(); it != porNombre.cend(); ++it) {
        QString valor = it.value();
        for (int i = 0; i < 4; ++i) {
            const auto m = referencia.match(valor);
            if (!m.hasMatch()) {
                break;
            }
            valor = porNombre.value(m.captured(1));
        }
        const QColor esperado = colorCss(valor);
        QVERIFY2(esperado.isValid(), qPrintable(it.key()));
        QColor actual;
        if (it.key().startsWith(QStringLiteral("estado-"))) {
            const QStringList partes = it.key().split(QLatin1Char('-'));
            QVariant r;
            QVERIFY(QMetaObject::invokeMethod(t, "estado", Q_RETURN_ARG(QVariant, r),
                                              Q_ARG(QVariant, partes.at(1))));
            actual = r.toMap().value(partes.at(2)).value<QColor>();
        } else {
            actual = t->property(camel(it.key()).toUtf8()).value<QColor>();
        }
        QVERIFY2(actual == esperado, qPrintable(QStringLiteral("%1 (%2): %3 != %4")
                                                    .arg(it.key(), clave, actual.name(QColor::HexArgb),
                                                         esperado.name(QColor::HexArgb))));
        ++comparados;
    }
    QCOMPARE(comparados, porNombre.size());

    // Tipografia.
    const QJsonArray grupos = m_tokens.value(QStringLiteral("type")).toObject().value(QStringLiteral("groups")).toArray();
    int estilos = 0;
    for (const QJsonValue& g : grupos) {
        for (const QJsonValue& e : g.toObject().value(QStringLiteral("styles")).toArray()) {
            const QJsonObject o = e.toObject();
            const QVariantMap m = t->property(o.value(QStringLiteral("name")).toString().toUtf8()).toMap();
            QCOMPARE(m.value(QStringLiteral("size")).toInt(), px(o.value(QStringLiteral("fontSize"))));
            QCOMPARE(m.value(QStringLiteral("weight")).toInt(), o.value(QStringLiteral("fontWeight")).toInt());
            QCOMPARE(m.value(QStringLiteral("lineHeight")).toInt(), px(o.value(QStringLiteral("lineHeight"))));
            ++estilos;
        }
    }
    QCOMPARE(estilos, 7);
    QVERIFY(!t->property("familia").toString().isEmpty());
    QVERIFY(!t->property("familiaMono").toString().isEmpty());

    // Espaciado, radios y medidas.
    for (const char* grupo : {"spacing", "radius", "medida"}) {
        const QJsonArray tokens = m_tokens.value(QLatin1String(grupo)).toObject().value(QStringLiteral("tokens")).toArray();
        QVERIFY(!tokens.isEmpty());
        for (const QJsonValue& v : tokens) {
            const QJsonObject o = v.toObject();
            const QString nombre = camel(o.value(QStringLiteral("name")).toString());
            QCOMPARE(t->property(nombre.toUtf8()).toInt(), px(o.value(QStringLiteral("value"))));
        }
    }

    // Sombras (texto CSS del token).
    for (const QJsonValue& v : m_tokens.value(QStringLiteral("shadow")).toObject().value(QStringLiteral("tokens")).toArray()) {
        const QJsonObject o = v.toObject();
        QCOMPARE(t->property(camel(o.value(QStringLiteral("name")).toString()).toUtf8()).toString(),
                 o.value(QStringLiteral("value")).toObject().value(clave).toString());
    }
}

void TestTemaYFechas::estadosAliasEIconos()
{
    QQmlEngine engine;
    QObject* t = tema(engine);
    QVERIFY(t);
    const auto estado = [&](const char* metodo, const QString& clave) {
        QVariant r;
        const bool ok = QMetaObject::invokeMethod(t, metodo, Q_RETURN_ARG(QVariant, r), Q_ARG(QVariant, clave));
        return ok ? r.toMap() : QVariantMap();
    };

    // Cada alias del JSON tiene icono existente en qrc.
    const QVariantMap alias = t->property("alias").toMap();
    QCOMPARE(alias.size(), 24);
    for (auto it = alias.cbegin(); it != alias.cend(); ++it) {
        const QString icono = estado("estado", it.key()).value(QStringLiteral("icono")).toString();
        QVERIFY2(QFile::exists(QStringLiteral(":/qt/qml/SatCfdiDownloader/assets/icons/%1.svg").arg(icono)),
                 qPrintable(it.key() + QStringLiteral(" -> ") + icono));
    }

    // Claves estables de la app.
    QCOMPARE(estado("estado", QStringLiteral("Aceptada")), estado("estado", QStringLiteral("AceptadaSat")));
    QCOMPARE(estado("estado", QStringLiteral("EnProceso")), estado("estado", QStringLiteral("EnProcesoSat")));
    QCOMPARE(estado("estado", QStringLiteral("Terminada")).value(QStringLiteral("tono")).toString(), QStringLiteral("exito"));
    QCOMPARE(estado("estado", QStringLiteral("Error")).value(QStringLiteral("icono")).toString(),
             QStringLiteral("exclamation-triangle")); // paquete
    QCOMPARE(estado("estado", QStringLiteral("ErrorSat")).value(QStringLiteral("tono")).toString(), QStringLiteral("error"));
    // e.firma desde PreparacionPerfil (Vencida de e.firma != Vencida de solicitud).
    QCOMPARE(estado("estadoEFirma", QStringLiteral("Vencida")), estado("estado", QStringLiteral("EFirmaVencida")));
    QCOMPARE(estado("estadoEFirma", QStringLiteral("Lista")).value(QStringLiteral("icono")).toString(),
             QStringLiteral("check-shield"));
    QCOMPARE(estado("estadoEFirma", QStringLiteral("SinCredencial")).value(QStringLiteral("icono")).toString(),
             QStringLiteral("key-slash"));
    QCOMPARE(estado("estadoEFirma", QStringLiteral("MaterialFaltante")).value(QStringLiteral("tono")).toString(),
             QStringLiteral("error"));
    QCOMPARE(estado("estadoEFirma", QStringLiteral("EstadoNoDisponible")).value(QStringLiteral("tono")).toString(),
             QStringLiteral("advertencia"));
    // Desconocida: neutro, nunca indefinido.
    const QVariantMap desconocido = estado("estado", QStringLiteral("NoExiste"));
    QCOMPARE(desconocido.value(QStringLiteral("tono")).toString(), QStringLiteral("neutro"));
    QVERIFY(desconocido.value(QStringLiteral("fondo")).value<QColor>().isValid());
}

void TestTemaYFechas::oscuroSigueAlSistemaYSeSobrescribe()
{
    QStyleHints* hints = QGuiApplication::styleHints();
    const Qt::ColorScheme previo = hints->colorScheme();
    hints->setColorScheme(Qt::ColorScheme::Dark);
    if (hints->colorScheme() != Qt::ColorScheme::Dark) {
        // La plataforma (p. ej. offscreen) no admite cambiar el esquema: se
        // prueba que Theme refleja el del sistema y que se puede sobrescribir.
        QQmlEngine engine;
        QObject* t = tema(engine);
        QVERIFY(t);
        QCOMPARE(t->property("oscuro").toBool(), hints->colorScheme() == Qt::ColorScheme::Dark);
        QVERIFY(t->setProperty("oscuro", true));
        QCOMPARE(t->property("texto").value<QColor>(), QColor(QStringLiteral("#f2f2f5")));
        QVERIFY(t->setProperty("oscuro", false));
        QCOMPARE(t->property("texto").value<QColor>(), QColor(QStringLiteral("#1d1d1f")));
        QSKIP("La plataforma no permite cambiar el esquema de color; se probo la sobrescritura.");
    }
    {
        QQmlEngine engine;
        QObject* t = tema(engine);
        QVERIFY(t);
        QVERIFY(t->property("oscuro").toBool());
        QCOMPARE(t->property("fondo").value<QColor>(), QColor(QStringLiteral("#1c1c1e")));
        hints->setColorScheme(Qt::ColorScheme::Light);
        QTRY_VERIFY(!t->property("oscuro").toBool()); // sigue al sistema en vivo
        QCOMPARE(t->property("fondo").value<QColor>(), QColor(QStringLiteral("#f0f0f2")));
        // Sobrescrito (herramienta, D9): ya no sigue al sistema.
        QVERIFY(t->setProperty("oscuro", true));
        hints->setColorScheme(Qt::ColorScheme::Light);
        QVERIFY(t->property("oscuro").toBool());
        QCOMPARE(t->property("texto").value<QColor>(), QColor(QStringLiteral("#f2f2f5")));
    }
    hints->setColorScheme(previo);
}

void TestTemaYFechas::iconoTenidoConColorExacto()
{
    QQmlEngine engine;
    QQmlComponent componente(&engine);
    componente.setData(R"(
        import QtQuick
        import SatCfdiDownloader
        Window {
            width: 64; height: 64; visible: true; color: "transparent"
            Icono { objectName: "icono"; x: 8; y: 8; nombre: "check-circle"; color: "#ff0000"; tamano: 48 }
        }
    )",
                       QUrl(QStringLiteral("qrc:/prueba/IconoPrueba.qml")));
    std::unique_ptr<QObject> raiz(componente.create());
    QVERIFY2(raiz, qPrintable(componente.errorString()));
    auto* ventana = qobject_cast<QQuickWindow*>(raiz.get());
    QVERIFY(ventana);
    QVERIFY(QTest::qWaitForWindowExposed(ventana));
    auto* icono = ventana->findChild<QQuickItem*>(QStringLiteral("icono"));
    QVERIFY(icono);
    QCOMPARE(icono->width(), 48.0);
    QVERIFY(icono->findChild<QObject*>(QStringLiteral("iconoSvg"))->property("valido").toBool());

    const QImage imagen = ventana->grabWindow().convertToFormat(QImage::Format_ARGB32);
    int tenidos = 0;
    int ajenos = 0;
    for (int y = 0; y < imagen.height(); ++y) {
        for (int x = 0; x < imagen.width(); ++x) {
            const QColor c = imagen.pixelColor(x, y);
            if (c.alpha() > 200) {
                // Trazo opaco: rojo puro (sin la tinta #1D1D1F del SVG).
                (c.red() > 230 && c.green() < 25 && c.blue() < 25) ? ++tenidos : ++ajenos;
            }
        }
    }
    QVERIFY2(tenidos > 50, qPrintable(QString::number(tenidos)));
    QCOMPARE(ajenos, 0);
}

void TestTemaYFechas::iconoInexistenteAvisa()
{
    QQmlEngine engine;
    QQmlComponent componente(&engine);
    componente.setData("import SatCfdiDownloader\nIcono { nombre: \"no-existe\" }",
                       QUrl(QStringLiteral("qrc:/prueba/IconoInexistente.qml")));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("IconoSvg: no se pudo cargar .*no-existe\\.svg")));
    std::unique_ptr<QObject> icono(componente.create());
    QVERIFY2(icono, qPrintable(componente.errorString()));
    QVERIFY(!icono->findChild<QObject*>(QStringLiteral("iconoSvg"))->property("valido").toBool());
}

void TestTemaYFechas::fechasFormato()
{
    QCOMPARE(FormatoFechas::fecha(QDate(2026, 9, 3)), QStringLiteral("3 sep 2026"));
    QCOMPARE(FormatoFechas::fecha(QDate(2026, 1, 31)), QStringLiteral("31 ene 2026"));
    QCOMPARE(FormatoFechas::fecha(QDate(2026, 12, 1)), QStringLiteral("1 dic 2026"));
    QCOMPARE(FormatoFechas::fecha(QDate()), QString());

    const QString guion = QStringLiteral("–");
    QCOMPARE(FormatoFechas::rango(QDate(2026, 9, 1), QDate(2026, 9, 30)), QStringLiteral("1") + guion + QStringLiteral("30 sep 2026"));
    QCOMPARE(FormatoFechas::rango(QDate(2026, 9, 28), QDate(2026, 10, 2)),
             QStringLiteral("28 sep ") + guion + QStringLiteral(" 2 oct 2026"));
    QCOMPARE(FormatoFechas::rango(QDate(2026, 12, 28), QDate(2027, 1, 2)),
             QStringLiteral("28 dic 2026 ") + guion + QStringLiteral(" 2 ene 2027"));
    QCOMPARE(FormatoFechas::rango(QDate(2026, 9, 3), QDate(2026, 9, 3)), QStringLiteral("3 sep 2026"));

    // Invocables con QVariant (texto ISO como lo exponen los view models).
    FormatoFechas f;
    QCOMPARE(f.fecha(QVariant(QStringLiteral("2026-09-03"))), QStringLiteral("3 sep 2026"));
    QCOMPARE(f.rango(QVariant(QStringLiteral("2026-09-01")), QVariant(QStringLiteral("2026-09-30"))),
             QStringLiteral("1") + guion + QStringLiteral("30 sep 2026"));
    QCOMPARE(f.fecha(QVariant()), QString());
    QCOMPARE(f.fechaHora(QVariant::fromValue(nullptr)), QString());
}

void TestTemaYFechas::fechasSinHoraNoCambianConLaZona_data()
{
    QTest::addColumn<QByteArray>("zona");
    QTest::newRow("UTC") << QByteArray("UTC");
    QTest::newRow("Mexico") << QByteArray("America/Mexico_City");
    QTest::newRow("Tijuana") << QByteArray("America/Tijuana");
    QTest::newRow("Kiritimati+14") << QByteArray("Pacific/Kiritimati");
}

void TestTemaYFechas::fechasSinHoraNoCambianConLaZona()
{
    QFETCH(QByteArray, zona);
    fijarZona(zona.constData());
    FormatoFechas f;
    QCOMPARE(f.fecha(QVariant(QStringLiteral("2026-09-01"))), QStringLiteral("1 sep 2026"));
    QCOMPARE(f.fecha(QVariant(QDate(2026, 9, 30))), QStringLiteral("30 sep 2026"));
    // Un QDate que pasa por QML llega como fecha-hora a medianoche local.
    QCOMPARE(f.fecha(QVariant(QDateTime(QDate(2026, 9, 1), QTime(0, 0)))), QStringLiteral("1 sep 2026"));
    // Texto SAT del filtro (hora Centro sin offset): solo cuenta el dia.
    QCOMPARE(f.fecha(QVariant(QStringLiteral("2026-09-30T23:59:59"))), QStringLiteral("30 sep 2026"));
}

void TestTemaYFechas::fechaHoraUsaHoraLocal()
{
    const QDateTime utc(QDate(2026, 10, 1), QTime(16, 3), QTimeZone::UTC);
    fijarZona("UTC");
    QCOMPARE(FormatoFechas::fechaHora(utc), QStringLiteral("1 oct 2026, 16:03"));
    fijarZona("America/Mexico_City"); // UTC-6
    QCOMPARE(FormatoFechas::fechaHora(utc), QStringLiteral("1 oct 2026, 10:03"));
    QCOMPARE(FormatoFechas::fechaHora(QDateTime(QDate(2026, 10, 1), QTime(1, 30), QTimeZone::UTC)),
             QStringLiteral("30 sep 2026, 19:30")); // cruza el dia en hora local
    FormatoFechas f;
    QCOMPARE(f.fechaHora(QVariant(utc)), QStringLiteral("1 oct 2026, 10:03"));
    fijarZona("UTC");
}

void TestTemaYFechas::fechasDesdeQml()
{
    fijarZona("America/Mexico_City");
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("dia"), QDate(2026, 9, 3));
    engine.rootContext()->setContextProperty(QStringLiteral("instante"),
                                             QDateTime(QDate(2026, 10, 1), QTime(16, 3), QTimeZone::UTC));
    QQmlComponent componente(&engine);
    componente.setData(R"(
        import QtQml
        import SatCfdiDownloader
        QtObject {
            property string a: FormatoFechas.fecha(dia)
            property string b: FormatoFechas.fecha("2026-09-03")
            property string c: FormatoFechas.fechaHora(instante)
            property string d: FormatoFechas.rango("2026-09-28", "2026-10-02")
        }
    )",
                       QUrl(QStringLiteral("qrc:/prueba/Fechas.qml")));
    std::unique_ptr<QObject> o(componente.create());
    QVERIFY2(o, qPrintable(componente.errorString()));
    QCOMPARE(o->property("a").toString(), QStringLiteral("3 sep 2026"));
    QCOMPARE(o->property("b").toString(), QStringLiteral("3 sep 2026"));
    QCOMPARE(o->property("c").toString(), QStringLiteral("1 oct 2026, 10:03"));
    QCOMPARE(o->property("d").toString(), QStringLiteral("28 sep – 2 oct 2026"));
    fijarZona("UTC");
}

QTEST_MAIN(TestTemaYFechas)

#include "TestTemaYFechas.moc"
