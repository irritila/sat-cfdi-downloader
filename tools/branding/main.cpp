// T020: herramienta de branding (fuera de la app y de ctest).
//
//   branding_render wordmark --fuente <ttf> --marca <marca-video.svg> --salida <dir>
//       Genera wordmark.svg, wordmark-mono.svg y lockup-video.svg a trazos (D8).
//   branding_render hoja --raiz <resources/branding> --salida <png>
//       Hoja de contactos de control (icono, marca/lockup, menu bar).
//   branding_render render <svg> <alto> <png> [--fondo #RRGGBB]
//       PNG de control de un SVG con QSvgRenderer.

#include "Trazos.h"

#include <QColor>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>

namespace {

QTextStream& err()
{
    static QTextStream s(stderr);
    return s;
}

// ---------------------------------------------------------------------------
// Wordmark y lockup: propuesta c "pestana tipografica" (T020 D8 tras la
// bitacora C2; docs/meetings/T020-desarrollo/svg-nombre/PROPUESTAS.md).
//
// Unidades: las de marca-video.svg (D9). La carpeta ocupa y 260-756 (496
// unidades) = 180 logicos; k = 496/180 convierte las medidas logicas de c:
// - Inter SemiBold 40 en ambas palabras; tracking Archivo -0.015 em y Fiscal
//   -0.020 em; 14 logicos entre cajas de tinta; linea base comun.
// - El punto de cada "i" se sustituye por una pestana 8.8 x 3.2 (radio 0.8,
//   relacion 2.75:1 de la pestana de la carpeta): se recorta el punto original
//   (caja de la i desde 1 logico arriba, 7 de alto) y la pestana se coloca
//   centrada en la i, 1 logico bajo su tope. La de Archivo es marfil y la de
//   Fiscal dorada (#C9A227); en mono todo es #0E1F18.
// - Caja del nombre (antes del recorte) 28 logicos bajo la carpeta, centrada
//   en x = 500.
constexpr qreal k = 496.0 / 180.0;
constexpr qreal kCentroX = 500.0;
constexpr qreal kPieCarpeta = 756.0;
constexpr qreal kMargen = 64.0;
constexpr int kMarcaMinX = 176;
constexpr int kMarcaMinY = 196;
constexpr int kMarcaAncho = 648;
const QString kCrema = QStringLiteral("#F4F1E8");
const QString kDorado = QStringLiteral("#C9A227");
const QString kMono = QStringLiteral("#0E1F18");

bool escribir(const QString& ruta, const QString& contenido)
{
    QFile f(ruta);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        err() << "no se pudo escribir " << ruta << "\n";
        return false;
    }
    f.write(contenido.toUtf8());
    return true;
}

QString viewBox(int x, int y, int w, int h)
{
    return QStringLiteral("%1 %2 %3 %4").arg(x).arg(y).arg(w).arg(h);
}

QString num(qreal v)
{
    return branding::numeroSvg(v);
}

struct Pestana {
    QRectF caja;
    bool acento = false;
};

struct Nombre {
    QList<QPainterPath> glifos;
    QList<Pestana> pestanas;
    QRectF tinta;
};

QString rectPestana(const QRectF& r, const QString& color)
{
    return QStringLiteral("<rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" rx=\"%5\" fill=\"%6\" />")
        .arg(num(r.x()), num(r.y()), num(r.width()), num(r.height()), num(0.8 * k), color);
}

QString grupoNombre(const Nombre& n, bool mono)
{
    const QString base = mono ? kMono : kCrema;
    QString s = QStringLiteral("  <g fill=\"%1\">\n").arg(base);
    for (const QPainterPath& g : n.glifos) {
        const QString regla = g.fillRule() == Qt::OddEvenFill ? QStringLiteral(" fill-rule=\"evenodd\"")
                                                               : QString();
        s += QStringLiteral("    <path%1 d=\"%2\" />\n").arg(regla, branding::datosRuta(g));
    }
    for (const Pestana& p : n.pestanas) {
        if (mono || !p.acento) {
            s += QStringLiteral("    ") + rectPestana(p.caja, base) + QLatin1Char('\n');
        }
    }
    s += QStringLiteral("  </g>\n");
    if (!mono) {
        for (const Pestana& p : n.pestanas) {
            if (p.acento) {
                s += QStringLiteral("  ") + rectPestana(p.caja, kDorado) + QLatin1Char('\n');
            }
        }
    }
    return s;
}

bool palabra(const QString& fuente, const QString& texto, qreal tracking,
             branding::TextoATrazos& salida)
{
    QString error;
    if (!branding::trazarTexto(fuente, texto, 40.0 * k, tracking, salida, error)) {
        err() << error << "\n";
        return false;
    }
    if (salida.glifos.size() != texto.size()) {
        err() << "glifos inesperados en " << texto << "\n";
        return false;
    }
    return true;
}

// Sustituye el punto de la i por una pestana.
Pestana pestanaEnI(QPainterPath& glifo, bool acento)
{
    const QRectF gb = glifo.boundingRect();
    QPainterPath corte;
    corte.addRect(QRectF(gb.left() - 1.0 * k, gb.top() - 1.0 * k, gb.width() + 2.0 * k, 7.0 * k));
    glifo = glifo.subtracted(corte);
    return {QRectF(gb.center().x() - 4.4 * k, gb.top() + 1.0 * k, 8.8 * k, 3.2 * k), acento};
}

bool construirNombre(const QString& fuente, Nombre& n)
{
    branding::TextoATrazos archivo;
    branding::TextoATrazos fiscal;
    if (!palabra(fuente, QStringLiteral("Archivo"), -0.015, archivo)
        || !palabra(fuente, QStringLiteral("Fiscal"), -0.020, fiscal)) {
        return false;
    }
    const qreal dxFiscal = archivo.caja.right() - archivo.caja.left() + 14.0 * k - fiscal.caja.left();
    QList<QPainterPath> glifos;
    for (QPainterPath g : std::as_const(archivo.glifos)) {
        g.translate(-archivo.caja.left(), 0);
        glifos << g;
    }
    for (QPainterPath g : std::as_const(fiscal.glifos)) {
        g.translate(dxFiscal, 0);
        glifos << g;
    }
    QRectF caja;
    for (const QPainterPath& g : std::as_const(glifos)) {
        caja = caja.isNull() ? g.boundingRect() : caja.united(g.boundingRect());
    }
    // Caja del nombre 28 logicos bajo la carpeta y centrada en x = 500.
    const qreal dx = kCentroX - caja.center().x();
    const qreal dy = kPieCarpeta + 28.0 * k - caja.top();
    for (QPainterPath& g : glifos) {
        g.translate(dx, dy);
    }
    n.glifos = glifos;
    n.pestanas = {pestanaEnI(n.glifos[4], false), pestanaEnI(n.glifos[7 + 1], true)};
    n.tinta = QRectF();
    for (const QPainterPath& g : std::as_const(n.glifos)) {
        n.tinta = n.tinta.isNull() ? g.boundingRect() : n.tinta.united(g.boundingRect());
    }
    for (const Pestana& p : std::as_const(n.pestanas)) {
        n.tinta = n.tinta.united(p.caja);
    }
    return true;
}

int generarWordmark(const QString& fuente, const QString& marca, const QString& dirSalida)
{
    QFile archivoMarca(marca);
    if (!archivoMarca.open(QIODevice::ReadOnly | QIODevice::Text)) {
        err() << "no se pudo leer " << marca << "\n";
        return 1;
    }
    const QString svgMarca = QString::fromUtf8(archivoMarca.readAll());
    const qsizetype inicio = svgMarca.indexOf(QLatin1String("<svg"));
    const qsizetype finApertura = svgMarca.indexOf(QLatin1Char('>'), inicio);
    const qsizetype cierre = svgMarca.lastIndexOf(QLatin1String("</svg>"));
    if (inicio < 0 || finApertura < 0 || cierre < finApertura) {
        err() << "marca-video.svg no tiene la forma esperada\n";
        return 1;
    }
    if (!svgMarca.mid(inicio, finApertura - inicio).contains(QStringLiteral("viewBox=\"%1\"")
                                                              .arg(viewBox(kMarcaMinX, kMarcaMinY,
                                                                           kMarcaAncho, 624)))) {
        err() << "marca-video.svg no usa el viewBox de D9\n";
        return 1;
    }
    // Geometria de la marca, literal (sin reinterpretarla).
    QString interior = svgMarca.mid(finApertura + 1, cierre - finApertura - 1);
    while (interior.startsWith(QLatin1Char('\n'))) {
        interior.remove(0, 1);
    }

    Nombre nombre;
    if (!construirNombre(fuente, nombre)) {
        return 1;
    }
    const QRectF& caja = nombre.tinta;

    // Wordmark: caja de tinta en coordenadas del lockup (el viewBox dice donde va).
    const int wx = static_cast<int>(std::floor(caja.left()));
    const int wy = static_cast<int>(std::floor(caja.top()));
    const int ww = static_cast<int>(std::ceil(caja.right())) - wx;
    const int wh = static_cast<int>(std::ceil(caja.bottom())) - wy;
    const QString cabeceraWordmark =
        QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"%1\">\n")
            .arg(viewBox(wx, wy, ww, wh));

    // Lockup: mismo sistema que D9; arriba y a los lados conserva el margen de
    // 64 de la marca, centrado en x = 500, y baja hasta la tinta del texto + 64.
    const int mitad = std::max(kMarcaAncho / 2,
                               static_cast<int>(std::ceil(caja.width() / 2.0 + kMargen)));
    const int lx = static_cast<int>(kCentroX) - mitad;
    const int lh = static_cast<int>(std::ceil(caja.bottom() + kMargen)) - kMarcaMinY;
    const QString cabeceraLockup =
        QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"%1\">\n")
            .arg(viewBox(lx, kMarcaMinY, 2 * mitad, lh));

    const QDir dir(dirSalida);
    if (!dir.mkpath(QStringLiteral("."))) {
        err() << "no se pudo crear " << dirSalida << "\n";
        return 1;
    }
    const QString fin = QStringLiteral("</svg>\n");
    if (!escribir(dir.filePath(QStringLiteral("wordmark.svg")),
                  cabeceraWordmark + grupoNombre(nombre, false) + fin)
        || !escribir(dir.filePath(QStringLiteral("wordmark-mono.svg")),
                     cabeceraWordmark + grupoNombre(nombre, true) + fin)
        || !escribir(dir.filePath(QStringLiteral("lockup-video.svg")),
                     cabeceraLockup + interior + grupoNombre(nombre, false) + fin)) {
        return 1;
    }
    QTextStream out(stdout);
    out << "wordmark viewBox " << viewBox(wx, wy, ww, wh) << "\n"
        << "lockup viewBox " << viewBox(lx, kMarcaMinY, 2 * mitad, lh) << "\n"
        << "caja de tinta " << caja.width() / k << " x " << caja.height() / k << " logicos\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Render y hoja de contactos.

// Renderiza las capas apiladas (mismo viewBox) a `alto` px preservando la
// proporcion del viewBox de la primera. Fondo transparente.
QImage renderizar(const QStringList& capas, int alto)
{
    QSvgRenderer primera(capas.value(0));
    const QRectF vb = primera.viewBoxF();
    const int ancho = static_cast<int>(std::lround(alto * vb.width() / vb.height()));
    QImage img(ancho, alto, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    for (const QString& capa : capas) {
        QSvgRenderer r(capa);
        if (!r.isValid()) {
            err() << "SVG invalido: " << capa << "\n";
        }
        r.render(&p, QRectF(0, 0, ancho, alto));
    }
    return img;
}

QImage sobreFondo(const QImage& img, const QColor& fondo)
{
    QImage out(img.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(fondo);
    QPainter p(&out);
    p.drawImage(0, 0, img);
    return out;
}

// Simula una plantilla de menu bar: conserva el alfa y pinta en `color`.
QImage plantilla(const QImage& img, const QColor& color)
{
    QImage out = img;
    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(out.rect(), color);
    return out;
}

class Hoja {
public:
    explicit Hoja(int ancho) : ancho_(ancho) {}

    void titulo(const QString& texto)
    {
        nuevaFila();
        y_ += 24;
        textos_ << Texto{QPoint(kBorde, y_ + 22), texto, true};
        y_ += 34;
    }

    void poner(const QImage& img, const QString& etiqueta)
    {
        if (x_ + img.width() > ancho_ - kBorde && x_ > kBorde) {
            nuevaFila();
        }
        piezas_ << Pieza{QPoint(x_, y_), img};
        textos_ << Texto{QPoint(x_, y_ + img.height() + 16), etiqueta, false};
        x_ += img.width() + 24;
        altoFila_ = std::max(altoFila_, img.height() + 26);
    }

    void nuevaFila()
    {
        y_ += altoFila_ + 12;
        x_ = kBorde;
        altoFila_ = 0;
    }

    QImage pintar()
    {
        nuevaFila();
        QImage out(ancho_, y_ + kBorde, QImage::Format_ARGB32_Premultiplied);
        out.fill(QColor(0xDD, 0xDD, 0xDA));
        QPainter p(&out);
        for (const Pieza& pz : std::as_const(piezas_)) {
            p.drawImage(pz.pos, pz.img);
        }
        p.setPen(QColor(0x20, 0x20, 0x20));
        QFont f(QStringLiteral("Helvetica Neue"));
        for (const Texto& t : std::as_const(textos_)) {
            f.setPixelSize(t.titulo ? 22 : 13);
            f.setBold(t.titulo);
            p.setFont(f);
            p.drawText(t.pos, t.texto);
        }
        return out;
    }

private:
    static constexpr int kBorde = 32;
    struct Pieza {
        QPoint pos;
        QImage img;
    };
    struct Texto {
        QPoint pos;
        QString texto;
        bool titulo;
    };
    int ancho_;
    int x_ = kBorde;
    int y_ = 0;
    int altoFila_ = 0;
    QList<Pieza> piezas_;
    QList<Texto> textos_;
};

QImage ampliar(const QImage& img, int factor)
{
    return img.scaled(img.size() * factor, Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

int generarHoja(const QString& raiz, const QString& salida)
{
    const QDir r(raiz);
    auto ruta = [&r](const QString& rel) { return r.filePath(rel); };
    const QStringList capas = {
        QStringLiteral("icono/icono-carpeta.svg"),      QStringLiteral("icono/icono-pestana.svg"),
        QStringLiteral("icono/icono-hoja-posterior.svg"), QStringLiteral("icono/icono-hoja-anterior.svg"),
        QStringLiteral("icono/icono-frente.svg"),
    };
    QStringList compuesto = {ruta(QStringLiteral("icono/icono-compuesto.svg"))};
    QStringList oscuro = {ruta(QStringLiteral("icono/icono-oscuro-fondo.svg"))};
    for (const QString& c : capas) {
        oscuro << ruta(c);
    }
    const QList<int> tamanos = {1024, 512, 128, 32, 16};

    Hoja hoja(1880);
    const QColor marcaFondo(0x05, 0x2E, 0x1C);
    const QColor claro(0xF2, 0xF2, 0xF2);
    const QColor oscuroMenu(0x2B, 0x2B, 0x2B);

    struct Variante {
        QString nombre;
        QStringList capas;
    };
    for (const Variante& v : {Variante{QStringLiteral("compuesto"), compuesto},
                              Variante{QStringLiteral("oscuro"), oscuro}}) {
        hoja.titulo(QStringLiteral("Icono %1 (fondo a sangre; macOS aplica el squircle)").arg(v.nombre));
        for (int t : tamanos) {
            hoja.poner(renderizar(v.capas, t), QStringLiteral("%1 px").arg(t));
        }
        hoja.poner(ampliar(renderizar(v.capas, 32), 8), QStringLiteral("32 px x8"));
        hoja.poner(ampliar(renderizar(v.capas, 16), 8), QStringLiteral("16 px x8"));
    }

    // Carpeta de 360 px de alto (1440p): 496 unidades -> 360 px.
    const qreal escala = 360.0 / 496.0;
    hoja.titulo(QStringLiteral("Marca y lockup de video sobre #052E1C (carpeta = 360 px)"));
    for (const QString& rel : {QStringLiteral("video/marca-video.svg"),
                               QStringLiteral("marca/lockup-video.svg"),
                               QStringLiteral("marca/wordmark.svg")}) {
        QSvgRenderer rr(ruta(rel));
        const int alto = static_cast<int>(std::lround(rr.viewBoxF().height() * escala));
        hoja.poner(sobreFondo(renderizar({ruta(rel)}, alto), marcaFondo), rel);
    }
    {
        const QString rel = QStringLiteral("marca/wordmark-mono.svg");
        QSvgRenderer rr(ruta(rel));
        const int alto = static_cast<int>(std::lround(rr.viewBoxF().height() * escala));
        hoja.poner(sobreFondo(renderizar({ruta(rel)}, alto), QColor(0xF4, 0xF1, 0xE8)), rel);
    }

    const QStringList estados = {QString(), QStringLiteral("-trabajando"), QStringLiteral("-pausa"),
                                 QStringLiteral("-atencion")};
    for (const auto& [nombreFondo, fondo, tinta] :
         {std::tuple{QStringLiteral("claro"), claro, QColor(Qt::black)},
          std::tuple{QStringLiteral("oscuro"), oscuroMenu, QColor(Qt::white)}}) {
        hoja.titulo(QStringLiteral("Menu bar en %1: 18 px (1x) y 36 px (2x), nativo y x8")
                        .arg(nombreFondo));
        for (const QString& e : estados) {
            const QString nombre = e.isEmpty() ? QStringLiteral("normal") : e.mid(1);
            const QImage m18 = renderizar({ruta(QStringLiteral("menu/menu-18%1.svg").arg(e))}, 18);
            const QImage m36 = renderizar({ruta(QStringLiteral("menu/menu-36%1.svg").arg(e))}, 36);
            hoja.poner(sobreFondo(plantilla(m18, tinta), fondo), QStringLiteral("%1 1x").arg(nombre));
            hoja.poner(sobreFondo(plantilla(m36, tinta), fondo), QStringLiteral("2x"));
            hoja.poner(ampliar(sobreFondo(plantilla(m18, tinta), fondo), 8), QStringLiteral("1x x8"));
            hoja.poner(ampliar(sobreFondo(plantilla(m36, tinta), fondo), 4), QStringLiteral("2x x4"));
        }
    }

    const QImage out = hoja.pintar();
    QDir().mkpath(QFileInfo(salida).absolutePath());
    if (!out.save(salida)) {
        err() << "no se pudo guardar " << salida << "\n";
        return 1;
    }
    QTextStream(stdout) << "hoja " << salida << " (" << out.width() << "x" << out.height() << ")\n";
    return 0;
}

int renderUno(const QString& svg, int alto, const QString& png, const QString& fondo)
{
    QImage img = renderizar({svg}, alto);
    if (!fondo.isEmpty()) {
        img = sobreFondo(img, QColor::fromString(fondo));
    }
    return img.save(png) ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("T020: wordmark a trazos, PNG de control y hoja de contactos"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("comando"), QStringLiteral("wordmark | hoja | render"));
    const QCommandLineOption fuente(QStringLiteral("fuente"), QStringLiteral("TTF/OTF de Inter SemiBold"), QStringLiteral("ruta"));
    const QCommandLineOption marca(QStringLiteral("marca"), QStringLiteral("marca-video.svg"), QStringLiteral("ruta"));
    const QCommandLineOption salida(QStringLiteral("salida"), QStringLiteral("directorio o PNG de salida"), QStringLiteral("ruta"));
    const QCommandLineOption raiz(QStringLiteral("raiz"), QStringLiteral("resources/branding"), QStringLiteral("ruta"));
    const QCommandLineOption fondo(QStringLiteral("fondo"), QStringLiteral("color de fondo #RRGGBB"), QStringLiteral("color"));
    parser.addOptions({fuente, marca, salida, raiz, fondo});
    parser.process(app);

    const QStringList pos = parser.positionalArguments();
    const QString comando = pos.value(0);
    if (comando == QLatin1String("wordmark") && parser.isSet(fuente) && parser.isSet(marca)
        && parser.isSet(salida)) {
        return generarWordmark(parser.value(fuente), parser.value(marca), parser.value(salida));
    }
    if (comando == QLatin1String("hoja") && parser.isSet(raiz) && parser.isSet(salida)) {
        return generarHoja(parser.value(raiz), parser.value(salida));
    }
    if (comando == QLatin1String("render") && pos.size() == 4) {
        bool ok = false;
        const int alto = pos.at(2).toInt(&ok);
        if (ok && alto > 0) {
            return renderUno(pos.at(1), alto, pos.at(3), parser.value(fondo));
        }
    }
    parser.showHelp(2);
}
