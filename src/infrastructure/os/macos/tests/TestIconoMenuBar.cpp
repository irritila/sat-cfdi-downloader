// Pruebas del icono del menu bar (T020 D6, propuesta A). Renderiza los SVG
// embebidos con QSvgRenderer (offscreen, sin AppKit ni sesion grafica) y
// comprueba:
// - plantilla: solo negro con alfa en los cuatro estados a 18 y 36 px;
// - goldens de mascara alfa versionados (tests/goldens/*.alfa, hex por fila);
// - alfa parcial solo en curvas (esquinas, pestana, insignias curvas) y en la
//   diagonal del frente: los bordes rectos caen en pixel entero;
// - la zona del frente (y >= 7 en la rejilla de 18) es identica en todos los
//   estados y conserva el frente oblicuo; corredor transparente de 1 px
//   alrededor de la insignia.
// Variables: SATCFDI_REGENERAR_GOLDENS_MENU=1 reescribe los goldens;
// SATCFDI_CAPTURAS_MENU=<dir> escribe capturas en claro y oscuro (evidencia,
// no versionada).

#include "infrastructure/os/macos/IconoMenuBar.h"
#include "infrastructure/os/macos/MacOSMapeos.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QFileInfo>
#include <QTest>
#include <QtMath>

using namespace satcfdi::macos;
using E = satcfdi::OSIntegration::EstadoIcono;

namespace {

const QList<E> kEstados{E::Normal, E::Trabajando, E::Pausado, E::Atencion};
const QList<int> kLados{18, 36};

QString nombreEstado(E e)
{
    switch (e) {
    case E::Normal:
        return QStringLiteral("normal");
    case E::Trabajando:
        return QStringLiteral("trabajando");
    case E::Pausado:
        return QStringLiteral("pausa");
    case E::Atencion:
        return QStringLiteral("atencion");
    }
    return {};
}

QString rutaGolden(E e, int lado)
{
    const QString sufijo = e == E::Normal ? QString() : QStringLiteral("-") + nombreEstado(e);
    return QDir(QStringLiteral(SATCFDI_MENU_GOLDENS_DIR)).filePath(QStringLiteral("menu-%1%2.alfa").arg(lado).arg(sufijo));
}

// Mascara alfa como texto: una fila por linea, dos digitos hex por pixel.
QByteArray serializarAlfa(const QImage& img)
{
    QByteArray salida;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            salida += QByteArray::number(qAlpha(img.pixel(x, y)), 16).rightJustified(2, '0');
        }
        salida += '\n';
    }
    return salida;
}

QList<QList<int>> leerAlfa(const QString& ruta)
{
    QFile f(ruta);
    QList<QList<int>> filas;
    if (!f.open(QIODevice::ReadOnly)) {
        return filas;
    }
    for (const QByteArray& linea : f.readAll().split('\n')) {
        if (linea.isEmpty()) {
            continue;
        }
        QList<int> fila;
        for (int i = 0; i + 1 < linea.size(); i += 2) {
            fila.append(linea.mid(i, 2).toInt(nullptr, 16));
        }
        filas.append(fila);
    }
    return filas;
}

// Zonas (rejilla logica de 18) donde se admite alfa parcial: curvas y
// diagonales del trazado. Fuera de ellas todo borde es recto y entero.
QList<QRectF> zonasAlfaParcial(E e)
{
    QList<QRectF> zonas{
        QRectF(QPointF(1, 3), QPointF(3, 5)),     // esquina superior izquierda
        QRectF(QPointF(1, 14), QPointF(3, 16)),   // esquina inferior izquierda
        QRectF(QPointF(15, 14), QPointF(17, 16)), // esquina inferior derecha
        QRectF(QPointF(6, 3), QPointF(9, 7)),     // curva y diagonal de la pestana
        QRectF(QPointF(2, 8), QPointF(16, 13)),   // frente oblicuo
    };
    if (e == E::Normal) {
        zonas.append(QRectF(QPointF(15, 5), QPointF(17, 7))); // esquina superior derecha
    }
    if (e == E::Trabajando || e == E::Atencion) {
        zonas.append(QRectF(QPointF(12, 1), QPointF(17, 6))); // arco / punto
    }
    return zonas;
}

bool enZona(const QList<QRectF>& zonas, int x, int y, qreal escala)
{
    const QRectF pixel(x / escala, y / escala, 1 / escala, 1 / escala);
    for (const QRectF& z : zonas) {
        if (z.intersects(pixel)) {
            return true;
        }
    }
    return false;
}

QImage tenir(const QImage& mascara, const QColor& color)
{
    QImage salida(mascara.size(), QImage::Format_ARGB32);
    for (int y = 0; y < mascara.height(); ++y) {
        for (int x = 0; x < mascara.width(); ++x) {
            QColor c = color;
            c.setAlpha(qAlpha(mascara.pixel(x, y)));
            salida.setPixelColor(x, y, c);
        }
    }
    return salida;
}

} // namespace

class TestIconoMenuBar : public QObject {
    Q_OBJECT

private slots:
    void rutasPorEstado()
    {
        QCOMPARE(rutaSvgIconoMenuBar(E::Normal, 18), QStringLiteral(":/menubar/menu-18.svg"));
        QCOMPARE(rutaSvgIconoMenuBar(E::Trabajando, 36), QStringLiteral(":/menubar/menu-36-trabajando.svg"));
        QCOMPARE(rutaSvgIconoMenuBar(E::Pausado, 18), QStringLiteral(":/menubar/menu-18-pausa.svg"));
        QCOMPARE(rutaSvgIconoMenuBar(E::Atencion, 36), QStringLiteral(":/menubar/menu-36-atencion.svg"));
        for (E e : kEstados) {
            for (int lado : kLados) {
                QVERIFY2(QFile::exists(rutaSvgIconoMenuBar(e, lado)), qPrintable(rutaSvgIconoMenuBar(e, lado)));
            }
        }
    }

    void textoEstadoSinCambios()
    {
        // Tooltip y nombre accesible salen de textoEstadoIcono (T014.4 D2).
        QCOMPARE(textoEstadoIcono(E::Normal), QStringLiteral("Archivo Fiscal"));
        QCOMPARE(textoEstadoIcono(E::Trabajando), QStringLiteral("Archivo Fiscal: trabajando"));
        QCOMPARE(textoEstadoIcono(E::Pausado), QStringLiteral("Archivo Fiscal: en pausa"));
        QCOMPARE(textoEstadoIcono(E::Atencion), QStringLiteral("Archivo Fiscal: requiere atención"));
    }

    void render_data()
    {
        QTest::addColumn<int>("estado");
        QTest::addColumn<int>("lado");
        for (E e : kEstados) {
            for (int lado : kLados) {
                QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(nombreEstado(e)).arg(lado)))
                    << static_cast<int>(e) << lado;
            }
        }
    }

    void render()
    {
        QFETCH(int, estado);
        QFETCH(int, lado);
        const E e = static_cast<E>(estado);
        const QImage img = dibujarIconoMenuBar(e, lado);
        QCOMPARE(img.size(), QSize(lado, lado));
        QVERIFY(img.hasAlphaChannel());
        const qreal escala = lado / 18.0;
        const QList<QRectF> zonas = zonasAlfaParcial(e);
        int opacos = 0;
        for (int y = 0; y < lado; ++y) {
            for (int x = 0; x < lado; ++x) {
                const QRgb px = img.pixel(x, y);
                const int a = qAlpha(px);
                // Plantilla: RGB siempre negro (el color lo pone macOS).
                QVERIFY2(qRed(px) == 0 && qGreen(px) == 0 && qBlue(px) == 0,
                         qPrintable(QStringLiteral("pixel no negro en %1,%2").arg(x).arg(y)));
                opacos += a == 255 ? 1 : 0;
                if (a > 0 && a < 255) {
                    QVERIFY2(enZona(zonas, x, y, escala),
                             qPrintable(QStringLiteral("alfa parcial %1 fuera de curvas/diagonal en %2,%3")
                                            .arg(a).arg(x).arg(y)));
                }
            }
        }
        QVERIFY(opacos > 0);
    }

    void goldens_data() { render_data(); }

    void goldens()
    {
        QFETCH(int, estado);
        QFETCH(int, lado);
        const E e = static_cast<E>(estado);
        const QImage img = dibujarIconoMenuBar(e, lado);
        const QString ruta = rutaGolden(e, lado);
        if (qEnvironmentVariableIntValue("SATCFDI_REGENERAR_GOLDENS_MENU") == 1) {
            QDir().mkpath(QFileInfo(ruta).absolutePath());
            QFile f(ruta);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(serializarAlfa(img));
            QSKIP("golden regenerado");
        }
        const QList<QList<int>> golden = leerAlfa(ruta);
        QVERIFY2(golden.size() == lado, qPrintable(QStringLiteral("golden ausente o invalido: %1").arg(ruta)));
        // Tolerancia minima por diferencias de antialias entre versiones de Qt.
        constexpr int kTolerancia = 2;
        for (int y = 0; y < lado; ++y) {
            QCOMPARE(golden.at(y).size(), lado);
            for (int x = 0; x < lado; ++x) {
                const int a = qAlpha(img.pixel(x, y));
                QVERIFY2(qAbs(a - golden.at(y).at(x)) <= kTolerancia,
                         qPrintable(QStringLiteral("%1,%2: alfa %3, golden %4").arg(x).arg(y).arg(a).arg(golden.at(y).at(x))));
            }
        }
    }

    void frenteReservadoIgualEnTodosLosEstados()
    {
        for (int lado : kLados) {
            const qreal escala = lado / 18.0;
            const int desde = qRound(7 * escala);
            const QImage normal = dibujarIconoMenuBar(E::Normal, lado);
            // El frente oblicuo esta presente: la linea (2,11.5)-(16,9.5)
            // deja tinta opaca en el centro de la carpeta (x=9, y~10.5).
            QCOMPARE(qAlpha(normal.pixel(qFloor(9 * escala), qFloor(10.5 * escala))), 255);
            for (E e : kEstados) {
                const QImage img = dibujarIconoMenuBar(e, lado);
                for (int y = desde; y < lado; ++y) {
                    for (int x = 0; x < lado; ++x) {
                        QVERIFY2(img.pixel(x, y) == normal.pixel(x, y),
                                 qPrintable(QStringLiteral("%1 a %2 px: frente distinto en %3,%4")
                                                .arg(nombreEstado(e)).arg(lado).arg(x).arg(y)));
                    }
                }
            }
        }
    }

    void corredorTransparenteDeLaInsignia()
    {
        for (int lado : kLados) {
            const qreal escala = lado / 18.0;
            for (E e : {E::Trabajando, E::Pausado, E::Atencion}) {
                const QImage img = dibujarIconoMenuBar(e, lado);
                // Columna x=11..12 (y<7) y fila y=6..7 (x>=11): transparentes.
                for (int y = 0; y < qRound(7 * escala); ++y) {
                    for (int x = qRound(11 * escala); x < qRound(12 * escala); ++x) {
                        QCOMPARE(qAlpha(img.pixel(x, y)), 0);
                    }
                }
                for (int y = qRound(6 * escala); y < qRound(7 * escala); ++y) {
                    for (int x = qRound(11 * escala); x < lado; ++x) {
                        QCOMPARE(qAlpha(img.pixel(x, y)), 0);
                    }
                }
            }
        }
    }

    void variantesDistinguibles()
    {
        for (int lado : kLados) {
            QList<QImage> imagenes;
            for (E e : kEstados) {
                imagenes.append(dibujarIconoMenuBar(e, lado));
            }
            for (int i = 0; i < imagenes.size(); ++i) {
                for (int j = i + 1; j < imagenes.size(); ++j) {
                    QVERIFY2(imagenes.at(i) != imagenes.at(j), qPrintable(QStringLiteral("%1 vs %2").arg(i).arg(j)));
                }
            }
        }
    }

    // Evidencia visual: plantilla tenida como lo haria macOS (negro sobre
    // barra clara, blanco sobre barra oscura), nativa y ampliada x8.
    void capturas()
    {
        const QString dir = qEnvironmentVariable("SATCFDI_CAPTURAS_MENU");
        if (dir.isEmpty()) {
            QSKIP("SATCFDI_CAPTURAS_MENU no definida");
        }
        QVERIFY(QDir().mkpath(dir));
        constexpr int kZoom = 8;
        constexpr int kCelda = 36 * kZoom + 24;
        for (const bool oscuro : {false, true}) {
            const QColor fondo = oscuro ? QColor(0x1E, 0x1E, 0x1E) : QColor(0xEC, 0xEC, 0xEC);
            const QColor tinta = oscuro ? Qt::white : Qt::black;
            QImage hoja(kCelda * static_cast<int>(kEstados.size()), 24 + 18 + 24 + 36 + 24 + 18 * kZoom + 24 + 36 * kZoom + 24,
                        QImage::Format_ARGB32);
            hoja.fill(fondo);
            QPainter p(&hoja);
            for (qsizetype i = 0; i < kEstados.size(); ++i) {
                const int x0 = static_cast<int>(i) * kCelda + 12;
                int y = 24;
                for (int lado : kLados) {
                    p.drawImage(x0, y, tenir(dibujarIconoMenuBar(kEstados.at(i), lado), tinta));
                    y += lado + 24;
                }
                for (int lado : kLados) {
                    const QImage t = tenir(dibujarIconoMenuBar(kEstados.at(i), lado), tinta);
                    p.drawImage(x0, y, t.scaled(lado * kZoom, lado * kZoom, Qt::IgnoreAspectRatio, Qt::FastTransformation));
                    y += lado * kZoom + 24;
                }
            }
            p.end();
            const QString ruta = QDir(dir).filePath(oscuro ? QStringLiteral("menubar-oscuro.png")
                                                           : QStringLiteral("menubar-claro.png"));
            QVERIFY(hoja.save(ruta));
        }
    }
};

QTEST_MAIN(TestIconoMenuBar)
#include "TestIconoMenuBar.moc"
