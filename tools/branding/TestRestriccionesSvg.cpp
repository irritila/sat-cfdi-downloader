// T020: restricciones de los SVG de resources/branding (label branding).
//
// - Solo elementos de dibujo planos (svg, g, path, rect, circle, ellipse,
//   line, polyline, polygon): sin text, filter, image, gradientes, mascaras,
//   use, style ni font.
// - Sin href (externos o no), url(...), DTD/entidades, atributos style/class
//   ni referencias a fuentes. Colores planos #RGB/#RRGGBB o none.
// - Menu bar: solo negro (con alfa) por atributos y por render QSvgRenderer.
// - viewBox: icono 0 0 1024 1024; video 176 196 648 624 (D9); menu 18/36.
// - Wordmark y lockup a trazos (D8, propuesta c): el claro es marfil con un
//   unico acento dorado, la pestana de la i de Fiscal (rect 2.75:1); el mono es
//   un solo color. Todo dorado del wordmark/lockup es una pestana 2.75:1. El
//   lockup contiene la geometria literal de marca-video.svg y comparte su
//   sistema de coordenadas.
// - menu/ (propiedad de qt-interface-engineer desde C2): cualquier
//   menu-18*.svg o menu-36*.svg recibe las reglas de su tamano.

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QSvgRenderer>
#include <QTest>
#include <QXmlStreamReader>

namespace {

enum class Tipo { Icono, Video, Menu18, Menu36, Wordmark, WordmarkMono, Lockup };

const QString kRaiz = QStringLiteral(SATCFDI_BRANDING_DIR);

const QSet<QString> kElementos = {
    QStringLiteral("svg"),  QStringLiteral("g"),        QStringLiteral("path"),
    QStringLiteral("rect"), QStringLiteral("circle"),   QStringLiteral("ellipse"),
    QStringLiteral("line"), QStringLiteral("polyline"), QStringLiteral("polygon"),
};

const QSet<QString> kAtributosColor = {
    QStringLiteral("fill"), QStringLiteral("stroke"), QStringLiteral("color"),
    QStringLiteral("stop-color"), QStringLiteral("flood-color"), QStringLiteral("lighting-color"),
};

// Inventario esperado (ruta relativa -> tipo). Un SVG fuera de la lista falla.
QMap<QString, Tipo> inventario()
{
    QMap<QString, Tipo> m;
    for (const char* n : {"fondo", "carpeta", "pestana", "hoja-posterior", "hoja-anterior", "frente",
                          "compuesto", "oscuro-fondo"}) {
        m.insert(QStringLiteral("icono/icono-%1.svg").arg(QLatin1String(n)), Tipo::Icono);
    }
    m.insert(QStringLiteral("video/marca-video.svg"), Tipo::Video);
    for (const char* n : {"carpeta", "pestana", "hoja-posterior", "hoja-anterior", "frente"}) {
        m.insert(QStringLiteral("video/video-%1.svg").arg(QLatin1String(n)), Tipo::Video);
    }
    for (const char* e : {"", "-trabajando", "-pausa", "-atencion"}) {
        m.insert(QStringLiteral("menu/menu-18%1.svg").arg(QLatin1String(e)), Tipo::Menu18);
        m.insert(QStringLiteral("menu/menu-36%1.svg").arg(QLatin1String(e)), Tipo::Menu36);
    }
    m.insert(QStringLiteral("marca/wordmark.svg"), Tipo::Wordmark);
    m.insert(QStringLiteral("marca/wordmark-mono.svg"), Tipo::WordmarkMono);
    m.insert(QStringLiteral("marca/lockup-video.svg"), Tipo::Lockup);
    return m;
}

// Todos los SVG presentes con su tipo: los del inventario y, en menu/, los
// menu-18*.svg / menu-36*.svg. Lo que no tiene regla va a `sinRegla`.
QMap<QString, Tipo> presentes(QStringList* sinRegla = nullptr)
{
    static const QRegularExpression menu(QStringLiteral("^menu/menu-(18|36)(-[a-z0-9-]+)?\\.svg$"));
    const QMap<QString, Tipo> esperado = inventario();
    QMap<QString, Tipo> m;
    QDirIterator it(kRaiz, {QStringLiteral("*.svg")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString r = QDir(kRaiz).relativeFilePath(it.next());
        const QRegularExpressionMatch mm = menu.match(r);
        if (esperado.contains(r)) {
            m.insert(r, esperado.value(r));
        } else if (mm.hasMatch()) {
            m.insert(r, mm.captured(1) == QLatin1String("18") ? Tipo::Menu18 : Tipo::Menu36);
        } else if (sinRegla) {
            *sinRegla << r;
        }
    }
    return m;
}

QList<qreal> numerosViewBox(const QString& vb)
{
    QList<qreal> n;
    for (const QString& p : vb.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts)) {
        bool ok = false;
        n << p.toDouble(&ok);
        if (!ok) {
            return {};
        }
    }
    return n.size() == 4 ? n : QList<qreal>{};
}

bool colorPlano(const QString& v)
{
    static const QRegularExpression hex(QStringLiteral("^#([0-9A-Fa-f]{3}|[0-9A-Fa-f]{6})$"));
    return v == QLatin1String("none") || hex.match(v).hasMatch();
}

bool negro(const QString& v)
{
    return v == QLatin1String("none") || v.compare(QLatin1String("#000"), Qt::CaseInsensitive) == 0
           || v.compare(QLatin1String("#000000"), Qt::CaseInsensitive) == 0;
}

QStringList infracciones(const QByteArray& svg, Tipo tipo)
{
    QStringList fallas;
    const QString bruto = QString::fromUtf8(svg).toLower();
    for (const char* prohibido : {"<!doctype", "<!entity", "href", "url(", "font", "style", "@import"}) {
        if (bruto.contains(QLatin1String(prohibido))) {
            fallas << QStringLiteral("contiene '%1'").arg(QLatin1String(prohibido));
        }
    }

    QXmlStreamReader xml(svg);
    QString viewBox;
    QSet<QString> rellenos;
    QStringList dorados; // infracciones de elementos dorados que no son pestana
    int doradosTotal = 0;
    qreal areaDorada = 0;
    int dibujos = 0;
    bool raiz = true;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isDTD() || xml.isEntityReference()) {
            fallas << QStringLiteral("DTD o entidad");
        }
        if (!xml.isStartElement()) {
            continue;
        }
        const QString nombre = xml.name().toString();
        if (raiz) {
            raiz = false;
            if (nombre != QLatin1String("svg")
                || xml.namespaceUri() != QLatin1String("http://www.w3.org/2000/svg")) {
                fallas << QStringLiteral("raiz no es <svg> SVG");
            }
            viewBox = xml.attributes().value(QLatin1String("viewBox")).toString();
        }
        if (!kElementos.contains(nombre)
            || xml.namespaceUri() != QLatin1String("http://www.w3.org/2000/svg")) {
            fallas << QStringLiteral("elemento no permitido <%1>").arg(nombre);
        } else if (nombre != QLatin1String("svg") && nombre != QLatin1String("g")) {
            ++dibujos;
        }
        for (const QXmlStreamAttribute& a : xml.attributes()) {
            const QString an = a.qualifiedName().toString();
            const QString av = a.value().toString().trimmed();
            for (const char* p : {"font", "filter", "mask", "clip", "href", "style", "class", "xml:base"}) {
                if (an.startsWith(QLatin1String(p)) || an.contains(QLatin1String(":href"))) {
                    fallas << QStringLiteral("atributo no permitido %1").arg(an);
                    break;
                }
            }
            if (kAtributosColor.contains(an)) {
                if (!colorPlano(av)) {
                    fallas << QStringLiteral("color no plano %1=\"%2\"").arg(an, av);
                }
                if ((tipo == Tipo::Menu18 || tipo == Tipo::Menu36) && !negro(av)) {
                    fallas << QStringLiteral("menu no negro %1=\"%2\"").arg(an, av);
                }
                if (av != QLatin1String("none")) {
                    rellenos.insert(av.toUpper());
                }
                if (av.compare(QLatin1String("#C9A227"), Qt::CaseInsensitive) == 0) {
                    ++doradosTotal;
                    const qreal w = xml.attributes().value(QLatin1String("width")).toDouble();
                    const qreal h = xml.attributes().value(QLatin1String("height")).toDouble();
                    if (nombre != QLatin1String("rect") || an != QLatin1String("fill") || h <= 0
                        || qAbs(w / h - 2.75) > 0.02) {
                        dorados << QStringLiteral("dorado fuera de una pestana 2.75:1 en <%1>").arg(nombre);
                    } else {
                        areaDorada += w * h;
                    }
                }
            }
        }
    }
    if (xml.hasError()) {
        fallas << QStringLiteral("XML invalido: %1").arg(xml.errorString());
    }
    if (dibujos == 0) {
        fallas << QStringLiteral("sin elementos de dibujo");
    }

    const QList<qreal> vb = numerosViewBox(viewBox);
    auto exige = [&](const QString& esperado) {
        if (vb != numerosViewBox(esperado)) {
            fallas << QStringLiteral("viewBox \"%1\" en vez de \"%2\"").arg(viewBox, esperado);
        }
    };
    switch (tipo) {
    case Tipo::Icono:
        exige(QStringLiteral("0 0 1024 1024"));
        break;
    case Tipo::Video:
        exige(QStringLiteral("176 196 648 624"));
        break;
    case Tipo::Menu18:
        exige(QStringLiteral("0 0 18 18"));
        break;
    case Tipo::Menu36:
        exige(QStringLiteral("0 0 36 36"));
        break;
    case Tipo::Wordmark:
        // Marfil y un unico acento: la pestana de Fiscal, pequena (<= 1 %
        // del viewBox; D2 limita el dorado a <= 5 %).
        if (rellenos != QSet<QString>{QStringLiteral("#F4F1E8"), QStringLiteral("#C9A227")}) {
            fallas << QStringLiteral("el wordmark claro debe ser marfil con acento dorado");
        }
        if (doradosTotal != 1) {
            fallas << QStringLiteral("el wordmark claro debe tener exactamente una pestana dorada");
        }
        fallas << dorados;
        if (vb.isEmpty() || areaDorada > 0.01 * vb.at(2) * vb.at(3)) {
            fallas << QStringLiteral("viewBox invalido o acento dorado excesivo");
        }
        break;
    case Tipo::WordmarkMono:
        if (rellenos != QSet<QString>{QStringLiteral("#0E1F18")}) {
            fallas << QStringLiteral("el wordmark mono debe ser solo #0E1F18");
        }
        if (vb.isEmpty()) {
            fallas << QStringLiteral("viewBox invalido");
        }
        break;
    case Tipo::Lockup:
        // Sistema de coordenadas de D9: arriba en 196, centrado en x = 500 y
        // mas alto que la marca sola.
        if (vb.isEmpty() || !qFuzzyCompare(vb.at(1), 196.0)
            || !qFuzzyCompare(vb.at(0) + vb.at(2) / 2.0, 500.0) || vb.at(2) < 648.0 || vb.at(3) <= 624.0) {
            fallas << QStringLiteral("viewBox del lockup incoherente con D9: \"%1\"").arg(viewBox);
        }
        // Dorado: la pestana de la carpeta y la de Fiscal, nada mas.
        if (doradosTotal != 2) {
            fallas << QStringLiteral("el lockup debe tener dos pestanas doradas");
        }
        fallas << dorados;
        break;
    }
    return fallas;
}

QByteArray leer(const QString& ruta)
{
    QFile f(ruta);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QString interiorNormalizado(const QByteArray& svg)
{
    QString s = QString::fromUtf8(svg);
    const qsizetype a = s.indexOf(QLatin1Char('>'), s.indexOf(QLatin1String("<svg")));
    const qsizetype b = s.lastIndexOf(QLatin1String("</svg>"));
    s = s.mid(a + 1, b - a - 1);
    return s.simplified();
}

} // namespace

class TestRestriccionesSvg : public QObject {
    Q_OBJECT

private slots:
    void inventarioCompleto()
    {
        const QMap<QString, Tipo> esperado = inventario();
        QStringList sinRegla;
        const QMap<QString, Tipo> encontrados = presentes(&sinRegla);
        QVERIFY2(sinRegla.isEmpty(), qPrintable(QStringLiteral("SVG sin regla: %1").arg(sinRegla.join(u' '))));
        for (auto i = esperado.cbegin(); i != esperado.cend(); ++i) {
            QVERIFY2(encontrados.contains(i.key()), qPrintable(QStringLiteral("falta %1").arg(i.key())));
        }
    }

    void restricciones_data()
    {
        QTest::addColumn<QString>("ruta");
        QTest::addColumn<int>("tipo");
        const QMap<QString, Tipo> m = presentes();
        for (auto i = m.cbegin(); i != m.cend(); ++i) {
            QTest::newRow(qPrintable(i.key())) << i.key() << static_cast<int>(i.value());
        }
    }

    void restricciones()
    {
        QFETCH(QString, ruta);
        QFETCH(int, tipo);
        const QByteArray svg = leer(QDir(kRaiz).filePath(ruta));
        QVERIFY(!svg.isEmpty());
        const QStringList fallas = infracciones(svg, static_cast<Tipo>(tipo));
        QVERIFY2(fallas.isEmpty(), qPrintable(fallas.join(QStringLiteral("; "))));
        QSvgRenderer r(svg);
        QVERIFY(r.isValid());
    }

    void menuSoloNegroConAlfa_data()
    {
        QTest::addColumn<QString>("ruta");
        QTest::addColumn<int>("lado");
        const QMap<QString, Tipo> m = presentes();
        for (auto i = m.cbegin(); i != m.cend(); ++i) {
            if (i.value() == Tipo::Menu18 || i.value() == Tipo::Menu36) {
                QTest::newRow(qPrintable(i.key())) << i.key() << (i.value() == Tipo::Menu18 ? 18 : 36);
            }
        }
    }

    void menuSoloNegroConAlfa()
    {
        QFETCH(QString, ruta);
        QFETCH(int, lado);
        QSvgRenderer r(QDir(kRaiz).filePath(ruta));
        QVERIFY(r.isValid());
        QImage img(lado, lado, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            r.render(&p, QRectF(0, 0, lado, lado));
        }
        int opacos = 0;
        for (int y = 0; y < lado; ++y) {
            for (int x = 0; x < lado; ++x) {
                const QRgb px = img.pixel(x, y);
                QVERIFY2(qRed(px) == 0 && qGreen(px) == 0 && qBlue(px) == 0,
                         qPrintable(QStringLiteral("pixel no negro en %1,%2").arg(x).arg(y)));
                opacos += qAlpha(px) == 255 ? 1 : 0;
            }
        }
        QVERIFY(opacos > 0);
    }

    void lockupCoherenteConMarca()
    {
        const QByteArray marca = leer(QDir(kRaiz).filePath(QStringLiteral("video/marca-video.svg")));
        const QByteArray lockup = leer(QDir(kRaiz).filePath(QStringLiteral("marca/lockup-video.svg")));
        const QByteArray wordmark = leer(QDir(kRaiz).filePath(QStringLiteral("marca/wordmark.svg")));
        QVERIFY(!marca.isEmpty() && !lockup.isEmpty() && !wordmark.isEmpty());
        const QString interiorLockup = interiorNormalizado(lockup);
        // Geometria literal de la marca, con las mismas coordenadas.
        QVERIFY(interiorLockup.startsWith(interiorNormalizado(marca)));
        // El texto del lockup es el del wordmark, en el mismo sistema.
        QVERIFY(interiorLockup.endsWith(interiorNormalizado(wordmark)));
        // 13 glifos y las dos pestanas de las i (propuesta c).
        QCOMPARE(interiorNormalizado(wordmark).count(QLatin1String("<path")), 13);
        QCOMPARE(interiorNormalizado(wordmark).count(QLatin1String("<rect")), 2);

        const QRectF vbLockup = QSvgRenderer(lockup).viewBoxF();
        const QRectF vbWordmark = QSvgRenderer(wordmark).viewBoxF();
        QVERIFY(vbLockup.contains(vbWordmark));
        QVERIFY(vbWordmark.top() > 756.0); // debajo de la carpeta
        QVERIFY(vbLockup.contains(QRectF(176, 196, 648, 624)));
    }

    // El verificador rechaza cada caso prohibido (y acepta uno minimo).
    void verificadorNegativos_data()
    {
        QTest::addColumn<QByteArray>("svg");
        QTest::addColumn<int>("tipo");
        QTest::addColumn<bool>("valido");
        const auto envolver = [](const char* cuerpo, const char* vb = "0 0 1024 1024") {
            return QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"") + vb + "\">" + cuerpo
                   + "</svg>";
        };
        const int icono = static_cast<int>(Tipo::Icono);
        const int menu = static_cast<int>(Tipo::Menu18);
        const int video = static_cast<int>(Tipo::Video);
        QTest::newRow("valido") << envolver("<rect width=\"10\" height=\"10\" fill=\"#052E1C\"/>") << icono << true;
        QTest::newRow("text") << envolver("<rect width=\"1\" height=\"1\"/><text>A</text>") << icono << false;
        QTest::newRow("filter") << envolver("<filter id=\"f\"/><rect width=\"1\" height=\"1\"/>") << icono << false;
        QTest::newRow("image") << envolver("<image width=\"1\" height=\"1\"/>") << icono << false;
        QTest::newRow("linearGradient")
            << envolver("<linearGradient id=\"g\"/><rect width=\"1\" height=\"1\" fill=\"#000\"/>") << icono << false;
        QTest::newRow("radialGradient")
            << envolver("<radialGradient id=\"g\"/><rect width=\"1\" height=\"1\"/>") << icono << false;
        QTest::newRow("fill-url") << envolver("<rect width=\"1\" height=\"1\" fill=\"url(#g)\"/>") << icono << false;
        QTest::newRow("mask") << envolver("<mask id=\"m\"/><rect width=\"1\" height=\"1\"/>") << icono << false;
        QTest::newRow("use-href-externo")
            << envolver("<use href=\"https://example.com/a.svg#x\"/><rect width=\"1\" height=\"1\"/>") << icono << false;
        QTest::newRow("xlink-href")
            << QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
                          "viewBox=\"0 0 1024 1024\"><rect width=\"1\" height=\"1\" xlink:href=\"a.svg\"/></svg>")
            << icono << false;
        QTest::newRow("font-family")
            << envolver("<rect width=\"1\" height=\"1\" font-family=\"Inter\"/>") << icono << false;
        QTest::newRow("style-font-face")
            << envolver("<style>@font-face{src:local(Inter)}</style><rect width=\"1\" height=\"1\"/>") << icono << false;
        QTest::newRow("entidad")
            << QByteArray("<!DOCTYPE svg [<!ENTITY e \"x\">]><svg xmlns=\"http://www.w3.org/2000/svg\" "
                          "viewBox=\"0 0 1024 1024\"><rect width=\"1\" height=\"1\"/></svg>")
            << icono << false;
        QTest::newRow("menu-gris")
            << envolver("<rect width=\"1\" height=\"1\" fill=\"#333\"/>", "0 0 18 18") << menu << false;
        QTest::newRow("menu-negro")
            << envolver("<rect width=\"1\" height=\"1\" fill=\"#000\" opacity=\"0.5\"/>", "0 0 18 18") << menu << true;
        QTest::newRow("video-viewbox")
            << envolver("<rect width=\"1\" height=\"1\"/>", "0 0 648 624") << video << false;
        const int wm = static_cast<int>(Tipo::Wordmark);
        const int wmMono = static_cast<int>(Tipo::WordmarkMono);
        QTest::newRow("wordmark-ok")
            << envolver("<path d=\"M0 0H9V9Z\" fill=\"#F4F1E8\"/>"
                        "<rect width=\"8.8\" height=\"3.2\" fill=\"#C9A227\"/>", "0 0 720 84") << wm << true;
        QTest::newRow("wordmark-sin-acento")
            << envolver("<path d=\"M0 0H9V9Z\" fill=\"#F4F1E8\"/>", "0 0 720 84") << wm << false;
        QTest::newRow("wordmark-dorado-en-glifo")
            << envolver("<path d=\"M0 0H9V9Z\" fill=\"#C9A227\"/><rect width=\"8.8\" height=\"3.2\" "
                        "fill=\"#F4F1E8\"/>", "0 0 720 84") << wm << false;
        QTest::newRow("wordmark-dos-acentos")
            << envolver("<path d=\"M0 0H9V9Z\" fill=\"#F4F1E8\"/><rect width=\"8.8\" height=\"3.2\" "
                        "fill=\"#C9A227\"/><rect x=\"20\" width=\"8.8\" height=\"3.2\" fill=\"#C9A227\"/>",
                        "0 0 720 84") << wm << false;
        QTest::newRow("wordmark-acento-grande")
            << envolver("<path d=\"M0 0H9V9Z\" fill=\"#F4F1E8\"/><rect width=\"88\" height=\"32\" "
                        "fill=\"#C9A227\"/>", "0 0 720 84") << wm << false;
        QTest::newRow("mono-con-acento")
            << envolver("<path d=\"M0 0H9V9Z\" fill=\"#0E1F18\"/><rect width=\"8.8\" height=\"3.2\" "
                        "fill=\"#C9A227\"/>", "0 0 720 84") << wmMono << false;
        QTest::newRow("video-ok")
            << envolver("<rect width=\"1\" height=\"1\"/>", "176 196 648 624") << video << true;
    }

    void verificadorNegativos()
    {
        QFETCH(QByteArray, svg);
        QFETCH(int, tipo);
        QFETCH(bool, valido);
        const QStringList fallas = infracciones(svg, static_cast<Tipo>(tipo));
        QCOMPARE(fallas.isEmpty(), valido);
    }
};

QTEST_MAIN(TestRestriccionesSvg)
#include "TestRestriccionesSvg.moc"
