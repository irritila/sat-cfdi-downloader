// satcfdi_prototipo_t023 (T023.1, D5): prototipo navegable del recorrido
// Facturas por perfil y mes. Carga el modulo QML PrototipoT023, que reutiliza
// Theme y los componentes reales del modulo SatCfdiDownloader con view models
// falsos en QML/JS y datos sinteticos (XAXX010101000 / XEXX010101000). No hay
// SQLite, SAT, Keychain, worker ni rutas reales.
//
// Uso:
//   satcfdi_prototipo_t023 [--tema claro|oscuro] [--escenario <clave>]
//   satcfdi_prototipo_t023 --capturas <dir> [--solo <clave>]
// El modo capturas genera un PNG por escenario, en claro y oscuro, a 960x640 y
// a 720x640 (lista de meses como combo). Requiere sesion grafica (cocoa).

#include "presentation/estilo/EstiloVisual.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QImageWriter>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QStringList>
#include <QTimer>
#include <QVariant>

#include <cstdio>

namespace {

constexpr int kAlto = 640;
constexpr int kAnchos[] = {960, 720};

void esperar(int ms)
{
    QElapsedTimer reloj;
    reloj.start();
    while (reloj.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

void esperarFrame(QQuickWindow* ventana)
{
    esperar(350);
    QEventLoop bucle;
    QObject::connect(ventana, &QQuickWindow::frameSwapped, &bucle, &QEventLoop::quit);
    QTimer::singleShot(2000, &bucle, &QEventLoop::quit);
    ventana->update();
    bucle.exec();
    esperar(80);
}

bool guardar(QQuickWindow* ventana, const QString& ruta)
{
    QImage imagen = ventana->grabWindow();
    if (imagen.isNull()) {
        return false;
    }
    imagen = imagen.convertToFormat(QImage::Format_RGB32);
    imagen.setDevicePixelRatio(1.0);
    QImageWriter escritor(ruta, "png");
    escritor.setQuality(0);
    return escritor.write(imagen);
}

int capturar(QQmlApplicationEngine& engine, QQuickWindow* ventana, const QString& salida, const QString& solo)
{
    if (!QDir().mkpath(salida)) {
        std::fprintf(stderr, "error: no se pudo crear %s\n", qPrintable(salida));
        return 1;
    }
    QObject* tema = engine.singletonInstance<QObject*>("SatCfdiDownloader", "Theme");
    if (tema == nullptr) {
        std::fprintf(stderr, "error: no se encontro el singleton Theme\n");
        return 1;
    }
    QVariant resultado;
    QMetaObject::invokeMethod(ventana, "clavesEscenarios", Q_RETURN_ARG(QVariant, resultado));
    const QStringList claves = resultado.toStringList();
    if (claves.isEmpty()) {
        std::fprintf(stderr, "error: el prototipo no publica escenarios\n");
        return 1;
    }
    ventana->setPosition(40, 40);
    int escritas = 0;
    for (const bool oscuro : {false, true}) {
        tema->setProperty("oscuro", oscuro);
        for (const int ancho : kAnchos) {
            ventana->resize(ancho, kAlto);
            for (qsizetype i = 0; i < claves.size(); ++i) {
                const QString& clave = claves.at(i);
                if (!solo.isEmpty() && clave != solo) {
                    continue;
                }
                QMetaObject::invokeMethod(ventana, "aplicarEscenario", Q_ARG(QVariant, QVariant(clave)));
                esperarFrame(ventana);
                const QString nombre = QStringLiteral("%1-%2-%3-%4.png")
                                           .arg(i + 1, 2, 10, QLatin1Char('0'))
                                           .arg(clave)
                                           .arg(ancho)
                                           .arg(oscuro ? QStringLiteral("oscuro") : QStringLiteral("claro"));
                const QString ruta = QDir(salida).filePath(nombre);
                if (!guardar(ventana, ruta)) {
                    std::fprintf(stderr, "error: no se pudo escribir %s\n", qPrintable(ruta));
                    return 1;
                }
                ++escritas;
                std::printf("%s\n", qPrintable(ruta));
            }
        }
    }
    std::printf("%d capturas en %s\n", escritas, qPrintable(salida));
    return escritas > 0 ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Archivo Fiscal (prototipo T023)"));
    satcfdi::presentacion::fijarEstiloBasico(); // T013 D1, igual que la app

    QString capturas;
    QString solo;
    QString escenario;
    QString temaInicial;
    const QStringList args = QCoreApplication::arguments();
    for (qsizetype i = 1; i < args.size(); ++i) {
        const QString& a = args.at(i);
        const bool hayValor = i + 1 < args.size();
        if (a == QLatin1String("--capturas") && hayValor) {
            capturas = args.at(++i);
        } else if (a == QLatin1String("--solo") && hayValor) {
            solo = args.at(++i);
        } else if (a == QLatin1String("--escenario") && hayValor) {
            escenario = args.at(++i);
        } else if (a == QLatin1String("--tema") && hayValor) {
            temaInicial = args.at(++i);
        } else {
            std::fprintf(stderr,
                         "uso: satcfdi_prototipo_t023 [--tema claro|oscuro] [--escenario <clave>]\n"
                         "     satcfdi_prototipo_t023 --capturas <dir> [--solo <clave>]\n");
            return 2;
        }
    }

    QQmlApplicationEngine engine;
    if (!temaInicial.isEmpty()) {
        QObject* tema = engine.singletonInstance<QObject*>("SatCfdiDownloader", "Theme");
        if (tema != nullptr) {
            tema->setProperty("oscuro", temaInicial == QLatin1String("oscuro"));
        }
    }
    engine.loadFromModule("PrototipoT023", "Main");
    if (engine.rootObjects().isEmpty()) {
        std::fprintf(stderr, "error: no se pudo cargar PrototipoT023/Main\n");
        return 1;
    }
    auto* ventana = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    if (ventana == nullptr) {
        return 1;
    }
    if (!capturas.isEmpty()) {
        return capturar(engine, ventana, capturas, solo);
    }
    if (!escenario.isEmpty()) {
        QMetaObject::invokeMethod(ventana, "aplicarEscenario", Q_ARG(QVariant, QVariant(escenario)));
    }
    return QGuiApplication::exec();
}
