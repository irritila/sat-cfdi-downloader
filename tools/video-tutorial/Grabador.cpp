#include "Grabador.h"

#include "Agarre.h"
#include "BarreraExportacion.h"
#include "Barreras.h"
#include "CuadroApp.h"
#include "DiagnosticoClic.h"
#include "EntornoQml.h"
#include "Ilustraciones.h"
#include "SalidaFfmpeg.h"
#include "Salidas.h"

#include "presentation/viewmodels/AppViewModel.h"
#include "presentation/viewmodels/EFirmaFormViewModel.h"
#include "presentation/viewmodels/ExportacionFacturasViewModel.h"
#include "presentation/viewmodels/PerfilesSatViewModel.h"
#include "presentation/viewmodels/PresentacionViewModels.h"
#include "presentation/viewmodels/RespaldoViewModel.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QPointingDevice>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <qpa/qwindowsysteminterface.h>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QThreadPool>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#ifndef SATCFDI_VIDEO_QML_DIR
#error "SATCFDI_VIDEO_QML_DIR debe definirse en CMake"
#endif

namespace video {

namespace {

// D4/D8: ventana logica a 2x; T024.4: tamano del guion ("ventana",
// 960x540 por omision).
constexpr int kEscala = 2;
// D8/D21: lienzo 1280x720 logicos = 2560x1440 px; la ventana va a escala 1.
constexpr int kLienzoAncho = 1280;
constexpr int kLienzoAlto = 720;
constexpr int kLienzoAnchoPx = kLienzoAncho * kEscala;
constexpr int kLienzoAltoPx = kLienzoAlto * kEscala;

struct CueSalida {
    int inicio = 0;
    int fin = 0;
    QString texto;
};

void imprimir(const QString& s)
{
    std::fprintf(stderr, "%s\n", qPrintable(s));
    std::fflush(stderr);
}

// T024.4 (D10): durante la grabacion nada puede abrir un dialogo nativo,
// Finder, Vista Previa ni tocar el portapapeles. QDesktopServices (y
// Qt.openUrlExternally) llegan aqui en vez de al sistema.
class VigilanteNativo final : public QObject {
    Q_OBJECT
public:
    QString violacion;
    void registrar(const QString& v)
    {
        if (violacion.isEmpty()) {
            violacion = v;
        }
    }
public slots:
    void abrirUrl(const QUrl& url)
    {
        registrar(QStringLiteral("la UI intento abrir %1 con el sistema (Finder, Vista Previa o navegador)")
                      .arg(url.scheme() == QLatin1String("file") ? QStringLiteral("un archivo local") : url.scheme()));
    }
};

// Dialogos de QtQuick.Dialogs (FileDialog, FolderDialog, ColorDialog,
// MessageDialog...) visibles: en macOS serian paneles nativos que la
// grabacion no captura. Los cierra y devuelve su clase y objectName.
QString dialogoNativoVisible(QObject* raiz)
{
    for (QObject* o : raiz->findChildren<QObject*>()) {
        if (o->inherits("QQuickAbstractDialog") && o->property("visible").toBool()) {
            const QString nombre = QStringLiteral("%1 '%2'")
                                       .arg(QString::fromLatin1(o->metaObject()->className()), o->objectName());
            QMetaObject::invokeMethod(o, "close");
            return nombre;
        }
    }
    return {};
}

// Simbolos del keycap (03-motion 3): modificadores y la tecla.
QStringList simbolosKeycap(const Accion& a)
{
    static const QHash<QString, QString> kModificadores{{QStringLiteral("Cmd"), QStringLiteral("⌘")},
                                                        {QStringLiteral("Shift"), QStringLiteral("⇧")},
                                                        {QStringLiteral("Opcion"), QStringLiteral("⌥")},
                                                        {QStringLiteral("Ctrl"), QStringLiteral("⌃")}};
    static const QHash<QString, QString> kTeclas{
        {QStringLiteral("Enter"), QStringLiteral("↩")},   {QStringLiteral("Escape"), QStringLiteral("esc")},
        {QStringLiteral("Tab"), QStringLiteral("⇥")},     {QStringLiteral("Abajo"), QStringLiteral("↓")},
        {QStringLiteral("Arriba"), QStringLiteral("↑")},  {QStringLiteral("Izquierda"), QStringLiteral("←")},
        {QStringLiteral("Derecha"), QStringLiteral("→")}, {QStringLiteral("Espacio"), QStringLiteral("espacio")},
        {QStringLiteral("Retroceso"), QStringLiteral("⌫")}, {QStringLiteral("Coma"), QStringLiteral(",")},
        {QStringLiteral("Punto"), QStringLiteral(".")}};
    QStringList r;
    for (const QString& m : a.modificadores) {
        r.append(kModificadores.value(m));
    }
    r.append(kTeclas.value(a.tecla, a.tecla));
    return r;
}

int codigoTecla(const QString& tecla)
{
    static const QHash<QString, int> kTeclas{
        {QStringLiteral("Enter"), Qt::Key_Return},       {QStringLiteral("Escape"), Qt::Key_Escape},
        {QStringLiteral("Tab"), Qt::Key_Tab},            {QStringLiteral("Abajo"), Qt::Key_Down},
        {QStringLiteral("Arriba"), Qt::Key_Up},          {QStringLiteral("Izquierda"), Qt::Key_Left},
        {QStringLiteral("Derecha"), Qt::Key_Right},      {QStringLiteral("Espacio"), Qt::Key_Space},
        {QStringLiteral("Retroceso"), Qt::Key_Backspace}, {QStringLiteral("Coma"), Qt::Key_Comma},
        {QStringLiteral("Punto"), Qt::Key_Period}};
    const auto it = kTeclas.constFind(tecla);
    if (it != kTeclas.constEnd()) {
        return *it;
    }
    return int(tecla.at(0).toUpper().unicode()); // A-Z y 0-9 coinciden con Qt::Key_*
}

// Cmd = Qt::ControlModifier en macOS (Qt intercambia Ctrl y Cmd).
Qt::KeyboardModifiers modificadoresQt(const QStringList& m)
{
    Qt::KeyboardModifiers r;
    if (m.contains(QStringLiteral("Cmd"))) {
        r |= Qt::ControlModifier;
    }
    if (m.contains(QStringLiteral("Shift"))) {
        r |= Qt::ShiftModifier;
    }
    if (m.contains(QStringLiteral("Opcion"))) {
        r |= Qt::AltModifier;
    }
    if (m.contains(QStringLiteral("Ctrl"))) {
        r |= Qt::MetaModifier;
    }
    return r;
}

// Flickable que mueve `desplazar`: el control o su primer Flickable interno
// (p. ej. el contentItem de un ScrollView).
QQuickItem* flickableDe(QQuickItem* item)
{
    if (item == nullptr || item->inherits("QQuickFlickable")) {
        return item;
    }
    for (QQuickItem* hijo : item->findChildren<QQuickItem*>()) {
        if (hijo->inherits("QQuickFlickable")) {
            return hijo;
        }
    }
    return nullptr;
}

// objectName (o clase) del primer Flickable ancestro en cuya vista NO cabe
// el centro de `item`; vacio si esta a la vista o no hay Flickable.
QString fueraDeVista(QQuickItem* item)
{
    for (QQuickItem* p = item->parentItem(); p != nullptr; p = p->parentItem()) {
        if (!p->inherits("QQuickFlickable")) {
            continue;
        }
        const QPointF centro = item->mapToItem(p, QPointF(item->width() / 2, item->height() / 2));
        if (centro.y() < 0 || centro.y() > p->height() || centro.x() < 0 || centro.x() > p->width()) {
            return p->objectName().isEmpty() ? QString::fromLatin1(p->metaObject()->className()) : p->objectName();
        }
        return {};
    }
    return {};
}

// Interprete del guion: aplica acciones por cuadro sobre la ventana real y
// envia cada cuadro a ffmpeg. Fixture: DemoVideo; barreras: Barreras.h;
// salida: SalidaFfmpeg y publicar().
class Sesion {
public:
    Sesion(Guion& guion, const Guion& salida, DemoVideo& demo, const OpcionesGrabacion& opciones)
        : m_guion(guion)
        , m_salida(salida)
        , m_demo(demo)
        , m_op(opciones)
    {
    }

    int ejecutar();

private:
    bool cargar();
    bool cuadro();
    bool bombear(bool renderizar);
    bool estabilizar();
    QQuickItem* esperarControl(const QString& objetivo);
    QString describirPunto(QPointF punto) const;
    QString diagnosticoControl();
    bool ejecutarEscena(const Capitulo& cap, const Escena& esc);
    bool ejecutarMarca(const QString& nombre, int cuadros);
    bool aplicarInstantanea(const Capitulo& cap, const Escena& esc, const Accion& a);
    bool aplicarCuadro(const Capitulo& cap, const Escena& esc, const Accion& a, int local);
    void moverCursor(QPointF p, bool hover);
    void retirarHover();
    QPointF puntoDestino(QQuickItem* item, const Accion& a) const;
    void enviarRaton(QEvent::Type tipo, Qt::MouseButton boton, Qt::MouseButtons botones);
    void enviarTecla(int tecla, const QString& texto, Qt::KeyboardModifiers modificadores = Qt::NoModifier);
    QString vigilar();
    QByteArray lineaDeTiempo() const;
    void asegurarActiva();
    bool fallar(const QString& m)
    {
        if (m_error.isEmpty()) {
            m_error = m;
        }
        return false;
    }
    bool fallarEscena(const Capitulo& cap, const Escena& esc, const QString& m)
    {
        return fallar(nombreEscena(cap, esc) + QStringLiteral(": ") + m);
    }

    Guion& m_guion;
    const Guion& m_salida; // linea de tiempo de los archivos (completa o recortada)
    bool m_grabando = true; // la escena en curso sale en el video (--escenas)
    int m_escritos = 0;     // cuadros enviados a ffmpeg
    DemoVideo& m_demo;
    OpcionesGrabacion m_op;
    QString m_error;

    RelojCuadros m_reloj;
    InterceptorOffline m_interceptor;
    FiltroEntradaReal m_filtro;
    SalidaFfmpeg m_ffmpeg;
    std::unique_ptr<PresentacionViewModels> m_vms;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow* m_ventana = nullptr;
    QQuickItem* m_overlay = nullptr;
    int m_cuadro = 0;
    std::unique_ptr<QQuickWindow> m_duenoLienzo;
    QQuickWindow* m_lienzo = nullptr;
    CuadroApp* m_cuadroApp = nullptr;
    QList<CueSalida> m_cues; // linea de tiempo de la salida (= .srt)
    int m_cuesVerificados = 0;
    int m_ancho = kVentanaAnchoPorOmision; // T024.4: ventana del guion
    int m_alto = kVentanaAltoPorOmision;
    QPointF m_cursor{kVentanaAnchoPorOmision / 2.0, kVentanaAltoPorOmision / 2.0};
    QString m_escena;               // "capitulo/escena" en curso (mensajes D10)
    QJsonArray m_sinEfecto;         // clics sinEfecto (linea de tiempo)
    VigilanteNativo m_vigilante;
    QPointer<QQuickItem> m_flickable; // desplazar
    double m_desdeY = 0;
    double m_haciaY = 0;
    QString m_ultimoClic; // diagnostico: punto y control bajo el ultimo clic
    QPointF m_desde;
    QPointF m_hacia;
    QPointer<QQuickItem> m_objetivo;
    int m_cuadrosMovimiento = 1;
    bool m_hoverActivo = false;
    bool m_avisoEscala = false;
    bool m_avisoEscalaLienzo = false;
    int m_clicsConEfecto = 0; // diagnostico: desactivarEnClic
    std::unique_ptr<DiagnosticoClic> m_diag; // T022: siempre existe; inactivo sin la variable
};

bool Sesion::cargar()
{
    m_ancho = m_guion.ventanaAncho;
    m_alto = m_guion.ventanaAlto;
    m_cursor = QPointF(m_ancho / 2.0, m_alto / 2.0);
    m_reloj.install();
    // D10: abrir URLs con el sistema y el portapapeles quedan vigilados.
    for (const QString& esquema : {QStringLiteral("file"), QStringLiteral("http"), QStringLiteral("https"),
                                   QStringLiteral("mailto")}) {
        QDesktopServices::setUrlHandler(esquema, &m_vigilante, "abrirUrl");
    }
    QObject::connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, &m_vigilante, [this] {
        m_vigilante.registrar(QStringLiteral("la UI escribio en el portapapeles (D10: usa un clic con \"sinEfecto\")"));
    });
    m_vms = std::make_unique<PresentacionViewModels>(&m_demo.solicitudes, &m_demo.perfiles, &m_demo.credenciales,
                                                     nullptr, [ahora = m_demo.ahora()] { return ahora; });
    m_vms->setAccionesSolicitud(&m_demo.acciones);
    m_vms->setAccionesFinder(&m_demo.acciones);
    m_vms->setConsultaExistencia(&m_demo.existencia);
    m_vms->setConsultaCatalogo(&m_demo.catalogo);
    m_vms->setPrevisualizacionCfdi(&m_demo.previsualizacion);
    // T014.3 D1: guia "Empieza en tres pasos" derivada en vivo del guion.
    m_vms->setConsultaPrimerUso(&m_demo.primerUso);
    // T022 D6: Facturas (pagina por omision) e Historial, como la app real.
    m_vms->setFuenteFacturas(&m_demo.facturas, &m_demo.preferenciasFacturas);
    m_vms->setFuenteHistorial(&m_demo.historial);
    // T024.2 (D9): Exportacion y Respaldo sinteticos, como los cablea la app
    // (las escenas que los usan son de T024.4).
    m_vms->setExportacionFacturas(m_demo.exportacion.servicio(), [ahora = m_demo.ahora()] { return ahora; });
    m_vms->setControlRespaldo(&m_demo.respaldo);
    m_vms->setControlRestauracion(&m_demo.restauracion);
    m_vms->setDatosPreviosRestauracion(&m_demo.datosPrevios);
    // T024.4 (6.2): "Copiar" e "Imprimir…" de la clave de recuperacion se ven
    // como en la app, pero portapapeles e impresion son nativos (D10): usarlos
    // con efecto hace fallar la grabacion.
    m_vms->setImpresionTexto([this](const QString&, const QString&) {
        m_vigilante.registrar(QStringLiteral("la UI pidio imprimir (D10: usa un clic con \"sinEfecto\")"));
        return false;
    });
    m_vms->setCopiaTextoSecreto([this](const QString&) {
        m_vigilante.registrar(QStringLiteral("la UI escribio en el portapapeles (D10: usa un clic con \"sinEfecto\")"));
        return false;
    });

    m_engine = std::make_unique<QQmlApplicationEngine>();
    m_engine->addUrlInterceptor(&m_interceptor);
    // D8: tema claro forzado (no sigue al sistema).
    QObject* tema = m_engine->singletonInstance<QObject*>("SatCfdiDownloader", "Theme");
    if (tema == nullptr || !tema->setProperty("oscuro", false)) {
        return fallar(QStringLiteral("no se pudo fijar el tema claro (Theme.oscuro)"));
    }
    m_engine->setInitialProperties(m_vms->initialProperties());
    m_engine->loadFromModule("SatCfdiDownloader", "Main");
    if (m_engine->rootObjects().isEmpty()) {
        return fallar(QStringLiteral("no se pudo cargar Main.qml"));
    }
    m_ventana = qobject_cast<QQuickWindow*>(m_engine->rootObjects().constFirst());
    if (m_ventana == nullptr) {
        return fallar(QStringLiteral("Main.qml no es una ventana"));
    }
    m_filtro.descartarDesactivacion = !m_op.permitirDesactivacion;
    m_ventana->installEventFilter(&m_filtro);
    if (QScreen* s = pantallaGrabacion()) {
        m_ventana->setScreen(s);
        m_ventana->setPosition(s->availableGeometry().topLeft() + QPoint(40, 40));
    }
    m_ventana->setMinimumSize(QSize(m_ancho, m_alto));
    m_ventana->setMaximumSize(QSize(m_ancho, m_alto));
    m_ventana->resize(m_ancho, m_alto);

    QQmlComponent componente(m_engine.get(),
                             QUrl::fromLocalFile(QStringLiteral(SATCFDI_VIDEO_QML_DIR "/Superposicion.qml")));
    QObject* objeto = componente.create();
    m_overlay = qobject_cast<QQuickItem*>(objeto);
    if (m_overlay == nullptr) {
        delete objeto;
        return fallar(QStringLiteral("no se pudo crear la superposicion: %1").arg(componente.errorString()));
    }
    m_overlay->setParent(m_ventana);
    m_overlay->setParentItem(m_ventana->contentItem());
    m_overlay->setProperty("cursor", m_cursor);
    m_overlay->setProperty("rotuloHerramienta", m_guion.rotuloHerramienta);
    m_overlay->setProperty("iconoApp", QUrl::fromLocalFile(QStringLiteral(SATCFDI_VIDEO_QML_DIR
                                                                          "/../../../resources/branding/icono/"
                                                                          "icono-compuesto.svg")));
    // T022 D2: diagnostico de clics (solo observa; inactivo con ruta vacia).
    m_diag = std::make_unique<DiagnosticoClic>(m_op.rutaDiagnosticoClic, m_ventana, m_overlay);
    m_diag->setSemantico({{QStringLiteral("pagina"), m_vms->app(), "pagina", {}},
                          {QStringLiteral("perfiles.modo"), m_vms->perfiles(), "modo", {}},
                          {QStringLiteral("hayFormulario"), nullptr, "hayFormulario", QStringLiteral("paginaPerfilesSat")},
                          {QStringLiteral("dialogoAbierto"), nullptr, "dialogoAbierto", QStringLiteral("paginaPerfilesSat")}});

    // Lienzo (D21): ventana aparte de 1280x720 logicos que compone la captura
    // de la app con el fondo, el titulo y la transcripcion. No toma el foco:
    // el teclado sigue llegando a la app.
    static const int kRegistro = qmlRegisterType<CuadroApp>("SatCfdiVideo", 1, 0, "CuadroApp");
    Q_UNUSED(kRegistro);
    QQmlComponent compLienzo(m_engine.get(), QUrl::fromLocalFile(QStringLiteral(SATCFDI_VIDEO_QML_DIR "/Lienzo.qml")));
    QObject* objetoLienzo = compLienzo.create();
    m_lienzo = qobject_cast<QQuickWindow*>(objetoLienzo);
    if (m_lienzo == nullptr) {
        delete objetoLienzo;
        return fallar(QStringLiteral("no se pudo crear el lienzo: %1").arg(compLienzo.errorString()));
    }
    m_duenoLienzo.reset(m_lienzo);
    m_cuadroApp = m_lienzo->findChild<CuadroApp*>(QStringLiteral("cuadroApp"));
    if (m_cuadroApp == nullptr) {
        return fallar(QStringLiteral("el lienzo no tiene cuadroApp"));
    }
    QVariantList cues;
    QVariantList capitulos;
    for (const Capitulo& c : m_salida.capitulos) {
        capitulos.append(QVariantMap{{QStringLiteral("i"), c.inicio},
                                     {QStringLiteral("f"), c.fin},
                                     {QStringLiteral("titulo"), c.titulo}});
        for (const Escena& e : c.escenas) {
            for (const Linea& l : e.lineas) {
                const CueSalida cue{e.inicio + l.inicio, e.inicio + l.inicio + l.duracion, l.texto};
                m_cues.append(cue);
                cues.append(QVariantMap{{QStringLiteral("i"), cue.inicio},
                                        {QStringLiteral("f"), cue.fin},
                                        {QStringLiteral("e"), e.inicio + e.duracion}, // fin de su escena
                                        {QStringLiteral("t"), cue.texto}});
            }
        }
    }
    const Composicion& comp = m_guion.composicion;
    m_lienzo->setProperty("cues", cues);
    m_lienzo->setProperty("capitulos", capitulos);
    m_lienzo->setProperty("cuadrosTitulo", m_op.rapido ? std::max(1, m_guion.cuadrosTitulo / 10) : m_guion.cuadrosTitulo);
    m_lienzo->setProperty("lineasTranscripcion", comp.lineasTranscripcion);
    m_lienzo->setProperty("semilla", comp.semilla);
    m_lienzo->setProperty("velocidad", comp.velocidad);
    m_lienzo->setProperty("paleta", comp.paleta);
    m_lienzo->setProperty("ventanaAncho", m_ancho);
    // T024.4: portada y cierre en cuadros de la salida.
    m_lienzo->setProperty("cuadrosPortada", m_salida.cuadrosPortada);
    m_lienzo->setProperty("cuadrosCierre", m_salida.cuadrosCierre);
    m_lienzo->setProperty("inicioCierre",
                          m_salida.cuadrosCierre > 0 ? m_salida.totalCuadros - m_salida.cuadrosCierre : -1);
    m_lienzo->setProperty("ventanaAlto", m_alto);
    m_lienzo->setFlag(Qt::WindowDoesNotAcceptFocus, true);
    m_lienzo->installEventFilter(&m_filtro);
    if (QScreen* s = pantallaGrabacion()) {
        m_lienzo->setScreen(s);
        m_lienzo->setPosition(s->availableGeometry().topLeft());
    }
    // Visible: fuera de pantalla grabWindow no dibuja ShaderEffect ni
    // MultiEffect. No toma el foco (WindowDoesNotAcceptFocus).
    m_lienzo->show();

    m_ventana->show();
    m_ventana->requestActivate();
    // Antes del cuadro 0 se espera en tiempo real a que la ventana este
    // expuesta y activa (no se graba nada mientras tanto).
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 5000 && !(m_ventana->isExposed() && m_ventana->isActive() && m_lienzo->isExposed())) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    if (!m_ventana->isExposed()) {
        return fallar(QStringLiteral("la ventana no se expuso (se requiere sesion grafica)"));
    }
    if (!m_ventana->isActive()) {
        imprimir(QStringLiteral("aviso: la ventana no quedo activa; el foco de teclado puede no llegar"));
    }
    return bombear(true);
}

// Procesa eventos pendientes SIN avanzar el reloj (bindings, futures listos,
// Qt.callLater). Con `renderizar`, ademas pule y dibuja (grabWindow) sin
// guardar: necesario para que ListView cree delegados con el reloj congelado.
bool Sesion::bombear(bool renderizar)
{
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
    if (renderizar) {
        (void)m_ventana->grabWindow();
    }
    if (!m_interceptor.bloqueadas.isEmpty()) {
        const QString m =
            QStringLiteral("intento de red bloqueado (%1)").arg(m_interceptor.bloqueadas.join(QStringLiteral(", ")));
        if (m_op.offline) {
            return fallar(QStringLiteral("--offline: ") + m);
        }
        imprimir(QStringLiteral("aviso: ") + m);
        m_interceptor.bloqueadas.clear();
    }
    if (const QString v = vigilar(); !v.isEmpty()) {
        return fallar(m_escena.isEmpty() ? v : QStringLiteral("escena '%1': %2").arg(m_escena, v));
    }
    return true;
}

// D10 (T024.4): perfil no declarado, Finder, Vista Previa, URL al sistema,
// portapapeles o dialogo nativo visible. Vacio si nada.
QString Sesion::vigilar()
{
    if (const QString v = m_demo.violacion(); !v.isEmpty()) {
        return v;
    }
    if (!m_vigilante.violacion.isEmpty()) {
        return m_vigilante.violacion;
    }
    if (m_ventana != nullptr) {
        if (const QString d = dialogoNativoVisible(m_ventana); !d.isEmpty()) {
            m_vigilante.registrar(QStringLiteral("se abrio el dialogo nativo %1 (D10: usa un clic con \"sinEfecto\", "
                                                 "la ilustracion selectorArchivos/selectorCarpeta y elegirCarpeta)")
                                      .arg(d));
            return m_vigilante.violacion;
        }
    }
    return {};
}

// Un cuadro: avanza el reloj 1/30 s, procesa eventos, captura y envia.
QImage normalizar(QImage imagen, int ancho, int alto, bool& aviso, const char* que)
{
    if (imagen.size() != QSize(ancho, alto)) {
        if (!aviso) {
            imprimir(QStringLiteral("aviso: %1 mide %2x%3 px (sin pantalla Retina); se reescala a %4x%5")
                         .arg(QLatin1String(que))
                         .arg(imagen.width())
                         .arg(imagen.height())
                         .arg(ancho)
                         .arg(alto));
            aviso = true;
        }
        imagen = imagen.scaled(ancho, alto, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return imagen;
}

// Un cuadro: avanza el reloj 1/30 s, procesa eventos, captura la app, la
// compone en el lienzo (si la escena sale en el video) y envia el lienzo.
bool Sesion::cuadro()
{
    asegurarActiva(); // la apariencia activa/inactiva tampoco depende del sistema
    if (!m_error.isEmpty()) {
        return false;
    }
    m_reloj.avanzarCuadro();
    // Efectos de la superposicion indexados por cuadro (deterministas).
    m_overlay->setProperty("cuadro", m_cuadro);
    if (!bombear(false)) {
        return false;
    }
    QImage app = m_ventana->grabWindow();
    if (app.isNull()) {
        return fallar(QStringLiteral("grabWindow devolvio una imagen vacia en el cuadro %1").arg(m_cuadro));
    }
    if (m_grabando) {
        m_cuadroApp->setImagen(normalizar(app, m_ancho * kEscala, m_alto * kEscala, m_avisoEscala, "la ventana"));
        m_lienzo->setProperty("cuadro", m_escritos);
        // D21: el texto quemado corresponde al cue vigente de la linea de
        // tiempo del .srt.
        QString esperado;
        for (const CueSalida& c : m_cues) {
            if (c.inicio <= m_escritos && m_escritos < c.fin) {
                esperado = c.texto;
            }
        }
        const QString vigente = m_lienzo->property("textoVigente").toString();
        const QString mostrado = m_lienzo->property("textoActual").toString();
        if (vigente != esperado || (!esperado.isEmpty() && mostrado != esperado)) {
            return fallar(QStringLiteral("transcripcion: en el cuadro %1 se muestra \"%2\" y el cue vigente es \"%3\"")
                              .arg(m_escritos)
                              .arg(mostrado, esperado));
        }
        m_cuesVerificados += esperado.isEmpty() ? 0 : 1;
        QImage imagen = m_lienzo->grabWindow();
        if (imagen.isNull()) {
            return fallar(QStringLiteral("el lienzo devolvio una imagen vacia en el cuadro %1").arg(m_cuadro));
        }
        imagen = normalizar(imagen, kLienzoAnchoPx, kLienzoAltoPx, m_avisoEscalaLienzo, "el lienzo")
                     .convertToFormat(QImage::Format_RGB888);
        if (!m_op.dirDepuracion.isEmpty() && m_cuadro >= m_op.depurarDesde && m_cuadro <= m_op.depurarHasta) {
            imagen.save(QDir(m_op.dirDepuracion).filePath(QStringLiteral("c%1.png").arg(m_cuadro, 5, 10, QLatin1Char('0'))));
        }
        if (const QString e = m_ffmpeg.escribir(imagen); !e.isEmpty()) {
            return fallar(QStringLiteral("cuadro %1: %2").arg(m_cuadro).arg(e));
        }
        ++m_escritos;
    }
    ++m_cuadro;
    if (m_cuadro % (kFps * 10) == 0) {
        std::printf("cuadro %d/%d\n", m_cuadro, m_guion.totalCuadros);
        std::fflush(stdout);
    }
    return true;
}

void Sesion::moverCursor(QPointF p, bool hover)
{
    m_cursor = p;
    m_overlay->setProperty("cursor", p);
    if (hover) {
        enviarRaton(QEvent::MouseMove, Qt::NoButton, Qt::NoButton);
        m_hoverActivo = true;
    } else if (m_hoverActivo) {
        retirarHover();
    }
}

// M4: el raton sintetico sale de la ventana: sin `hovered` no hay tooltips
// con temporizador de tiempo real (ToolTip.delay) ni estados que dependan
// del momento real.
void Sesion::retirarHover()
{
    const QPointF fuera(-100, -100);
    QMouseEvent e(QEvent::MouseMove, fuera, fuera, m_ventana->mapToGlobal(fuera), Qt::NoButton, Qt::NoButton,
                  Qt::NoModifier, QPointingDevice::primaryPointingDevice());
    QCoreApplication::sendEvent(m_ventana, &e);
    m_hoverActivo = false;
}

// M11: punto al que apunta el cursor. auto: a la derecha del texto de un
// Text/Label si el mover no termina en clic; centro en otro caso.
QPointF Sesion::puntoDestino(QQuickItem* item, const Accion& a) const
{
    const qreal w = item->width();
    const qreal h = item->height();
    QPointF local(w / 2, h / 2);
    const bool esTexto = item->inherits("QQuickText");
    const bool derecha = a.ancla == QLatin1String("derecha") || (a.ancla == QLatin1String("auto") && esTexto && !a.seguidoDeClic);
    if (derecha) {
        qreal fin = w;
        if (esTexto) {
            const qreal ancho = std::min(w, item->property("contentWidth").toReal());
            const int alineacion = item->property("horizontalAlignment").toInt();
            const qreal inicio = (alineacion & Qt::AlignRight) ? w - ancho : (alineacion & Qt::AlignHCenter) ? (w - ancho) / 2 : 0;
            fin = inicio + ancho;
        }
        local = QPointF(fin + 6, h / 2);
    } else if (a.ancla == QLatin1String("abajo")) {
        local = QPointF(w / 2, h + 6);
    }
    return item->mapToScene(local) + a.desplazamiento;
}

void Sesion::enviarRaton(QEvent::Type tipo, Qt::MouseButton boton, Qt::MouseButtons botones)
{
    const QPointF global = m_ventana->mapToGlobal(m_cursor);
    QMouseEvent e(tipo, m_cursor, m_cursor, global, boton, botones, Qt::NoModifier,
                  QPointingDevice::primaryPointingDevice());
    QCoreApplication::sendEvent(m_ventana, &e);
}

// El foco de Qt Quick (teclado, campo activo, paleta activa) depende de que
// la ventana sea la ventana con foco de Qt. Si otra aplicacion toma el foco
// del sistema durante la grabacion (macOS ya no deja que una app lo
// recupere sola), se le devuelve a la ventana de la app solo dentro de Qt:
// el sistema no cambia, el video no depende de el y la grabacion sigue
// siendo determinista.
void Sesion::asegurarActiva()
{
    if (m_ventana->isActive()) {
        return;
    }
    QWindowSystemInterface::handleFocusWindowChanged(m_ventana, Qt::ActiveWindowFocusReason);
    QWindowSystemInterface::flushWindowSystemEvents();
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    if (!m_ventana->isActive()) {
        fallar(QStringLiteral("la ventana de la app no recupero el foco de Qt en el cuadro %1").arg(m_cuadro));
    }
}

void Sesion::enviarTecla(int tecla, const QString& texto, Qt::KeyboardModifiers modificadores)
{
    asegurarActiva();
    // T024.4: con modificadores primero el mapa de atajos (los Shortcut de
    // QML no ven un sendEvent directo); si nadie lo toma, el evento normal.
    if (modificadores != Qt::NoModifier
        && QWindowSystemInterface::handleShortcutEvent(m_ventana, 0, tecla, modificadores, 0, 0, 0, texto)) {
        return;
    }
    QKeyEvent abajo(QEvent::KeyPress, tecla, modificadores, texto);
    QCoreApplication::sendEvent(m_ventana, &abajo);
    QKeyEvent arriba(QEvent::KeyRelease, tecla, modificadores, texto);
    QCoreApplication::sendEvent(m_ventana, &arriba);
}

bool Sesion::aplicarInstantanea(const Capitulo& cap, const Escena& esc, const Accion& a)
{
    const Bombear bombearDibujando = [this] { return bombear(true); };
    QString e;
    switch (a.tipo) {
    case TipoAccion::AvanzarSolicitud:
        e = m_demo.avanzar(a.solicitud, a.estado, a.verificadaEn);
        break;
    case TipoAccion::MostrarIlustracion: {
        QUrl archivo;
        QVariantMap propiedades;
        e = prepararIlustracion(a, m_demo, m_op.dirTemporal, archivo, propiedades);
        if (e.isEmpty()) {
            m_overlay->setProperty("parametrosIlustracion", propiedades);
            m_overlay->setProperty("ilustracion", archivo);
        }
        break;
    }
    case TipoAccion::OcultarIlustracion:
        m_overlay->setProperty("ilustracion", QUrl());
        break;
    case TipoAccion::EsperarDocumento:
        e = esperarDocumento(m_ventana->contentItem(), a.timeoutMs, bombearDibujando);
        break;
    case TipoAccion::EsperarConteo:
        e = esperarConteo(m_ventana->contentItem(), a.objetivo, a.conteo, a.timeoutMs, bombearDibujando);
        break;
    case TipoAccion::EsperarExportacion:
        e = esperarExportacion(m_vms->exportacionFacturas(), a.resultadoExportacion, a.timeoutMs, bombearDibujando);
        break;
    case TipoAccion::CambiarEFirma:
        e = m_demo.cambiarEFirma(a.perfil, a.efirma, a.vigenteHasta);
        break;
    case TipoAccion::CompletarEFirma:
        e = m_demo.completarEFirma(a.perfil, a.vigenteHasta);
        break;
    case TipoAccion::MostrarSolicitudes:
        e = m_demo.mostrarSolicitudes(a.claves);
        break;
    case TipoAccion::AvanzarMes:
        e = m_demo.avanzarMes(a.perfil, a.mes, a.direccion, a.estado, a.causa);
        break;
    case TipoAccion::ElegirCarpeta: {
        // Lo que el FolderDialog entregaria (D10): solo la URL de la carpeta.
        const QUrl url = QUrl::fromLocalFile(a.ruta);
        if (a.destino == QLatin1String("exportacion")) {
            m_vms->exportacionFacturas()->elegirDestino(url);
        } else {
            m_vms->respaldo()->elegirCarpeta(url);
        }
        break;
    }
    case TipoAccion::ProgresoRespaldo:
        e = m_demo.progresoRespaldo(a.fase, a.bytes, a.total);
        break;
    case TipoAccion::ResultadoRespaldo:
        e = m_demo.resultadoRespaldo(a.resultado, a.fecha, a.bytes);
        break;
    case TipoAccion::EsperarRespaldo:
        e = esperarRespaldo(m_vms->respaldo(), a.resultado, a.timeoutMs, bombearDibujando);
        break;
    case TipoAccion::MostrarBanner: {
        const QVariantList ns = m_demo.notificaciones(a.parametros, e);
        if (e.isEmpty()) {
            m_overlay->setProperty("banners", ns);
            m_overlay->setProperty("inicioBanner", m_cuadro);
            m_overlay->setProperty("finBanner", -1000);
        }
        break;
    }
    case TipoAccion::OcultarBanner:
        m_overlay->setProperty("finBanner", m_cuadro);
        break;
    case TipoAccion::SeleccionarArchivoEFirma: {
        // Lo que el dialogo nativo entregaria: solo una URL (el archivo
        // sintetico nunca se lee).
        const QUrl url = QUrl::fromLocalFile(a.ruta);
        if (a.archivo == QLatin1String("certificado")) {
            m_vms->eFirma()->seleccionarCertificado(url);
        } else {
            m_vms->eFirma()->seleccionarLlave(url);
        }
        break;
    }
    default:
        return true; // esperarLinea ya alineada a 0 cuadros
    }
    if (!m_error.isEmpty()) {
        return false; // la barrera fallo por otra causa (ya registrada)
    }
    if (!e.isEmpty()) {
        return fallarEscena(cap, esc, e);
    }
    return estabilizar();
}

// Tras una mutacion del guion (reloj congelado): deja terminar TODO el
// trabajo que dispara (continuaciones de QFuture encadenadas, whenAll del pool
// de hilos, layouts) antes del siguiente cuadro. Sin esto, la posicion de un
// control recien mostrado podia variar entre corridas (cursor de M3).
bool Sesion::estabilizar()
{
    for (int i = 0; i < 8; ++i) {
        QThreadPool::globalInstance()->waitForDone(20);
        if (!bombear(true)) {
            return false;
        }
    }
    return true;
}

// Control por objectName, existente y visible. Si no esta en la primera
// busqueda (estado de la UI aun asentandose tras un clic o un future), bombea
// eventos y renderiza CON EL RELOJ CONGELADO hasta que aparezca o venzan
// kEsperaControlMs de tiempo real (independiente de --rapido). No consume
// cuadros de la linea de tiempo: el video no cambia por esperar.
constexpr int kEsperaControlMs = 2000;

QQuickItem* Sesion::esperarControl(const QString& objetivo)
{
    QQuickItem* item = buscarRuta(m_ventana->contentItem(), objetivo);
    QElapsedTimer t;
    t.start();
    while (item == nullptr && t.elapsed() < kEsperaControlMs) {
        QThreadPool::globalInstance()->waitForDone(10);
        if (!bombear(true)) {
            return nullptr;
        }
        item = buscarRuta(m_ventana->contentItem(), objetivo);
    }
    return item;
}

// Diagnostico: el control mas profundo bajo `punto` con objectName (o su clase).
QString Sesion::describirPunto(QPointF punto) const
{
    QString nombre;
    QString clase;
    // Item mas alto bajo el punto respetando z (T022: antes no ordenaba por
    // z y podia nombrar el fondo de la pagina en vez del control).
    for (QQuickItem* item : DiagnosticoClic::cadenaBajo(m_ventana->contentItem(), punto, m_overlay)) {
        if (!item->objectName().isEmpty()) {
            nombre = item->objectName();
        }
        clase = QString::fromLatin1(item->metaObject()->className());
    }
    return QStringLiteral("(%1, %2) sobre '%3' (%4)")
        .arg(punto.x(), 0, 'f', 0)
        .arg(punto.y(), 0, 'f', 0)
        .arg(nombre, clase);
}

// Diagnostico de un control no encontrado: ultimo clic y la ventana de la app
// en <salida>/<nombre>.fallo.png.
QString Sesion::diagnosticoControl()
{
    const QString png = QDir(m_op.dirSalida).filePath(m_op.nombre + QStringLiteral(".fallo.png"));
    const bool guardada = !m_op.dirSalida.isEmpty() && m_ventana->grabWindow().save(png);
    return QStringLiteral("; ultimo clic en %1; cuadro %2%3%4")
        .arg(m_ultimoClic.isEmpty() ? QStringLiteral("(ninguno)") : m_ultimoClic)
        .arg(m_cuadro)
        .arg(guardada ? QStringLiteral("; ventana en ") + png : QString(),
             m_diag && m_diag->activo() ? QStringLiteral("; diagnostico en ") + m_diag->ruta() : QString());
}

bool Sesion::aplicarCuadro(const Capitulo& cap, const Escena& esc, const Accion& a, int local)
{
    switch (a.tipo) {
    case TipoAccion::Mover: {
        if (local == 0) {
            QElapsedTimer espera;
            espera.start();
            m_objetivo = esperarControl(a.objetivo);
            if (!m_error.isEmpty()) {
                return false; // bombear fallo por otra causa (ya registrada)
            }
            m_diag->setContexto(nombreEscena(cap, esc), a.objetivo, m_objetivo);
            m_diag->nota(m_cuadro, QStringLiteral("mover: encontrado=%1 espera=%2 ms")
                                       .arg(m_objetivo.isNull() ? 0 : 1)
                                       .arg(espera.elapsed()));
            if (m_objetivo.isNull()) {
                return fallarEscena(cap, esc,
                                    QStringLiteral("no se encontro un control visible con objectName '%1' "
                                                   "(espera de %2 ms)")
                                            .arg(a.objetivo)
                                            .arg(kEsperaControlMs)
                                        + diagnosticoControl());
            }
            // T024.4: un control fuera de la vista de su Flickable (p. ej. el
            // contenido desplazable de los dialogos de Respaldo/Exportar) no se
            // alcanza: el guion debe `desplazar` ese Flickable antes.
            if (const QString f = fueraDeVista(m_objetivo); !f.isEmpty()) {
                return fallarEscena(cap, esc,
                                    QStringLiteral("'%1' queda fuera de la vista de '%2': usa desplazar sobre '%2' "
                                                   "antes del mover")
                                        .arg(a.objetivo, f));
            }
            m_desde = m_cursor;
            m_hacia = puntoDestino(m_objetivo, a);
            // M6: sin `cuadros`, la duracion depende de la distancia; el plan
            // reserva el maximo y el cursor reposa en el destino el resto.
            m_cuadrosMovimiento = a.cuadros;
            if (a.cuadrosAuto) {
                const QPointF d = m_hacia - m_desde;
                const double distancia = std::sqrt(d.x() * d.x() + d.y() * d.y());
                const int porDistancia =
                    std::clamp(int(std::lround(10 + distancia / 22)), kMoverAutoMin, kMoverAutoMax);
                m_cuadrosMovimiento = std::min(a.cuadros, porDistancia);
            }
        } else if (!m_objetivo.isNull() && m_objetivo->isVisible()) {
            // M3: el destino se recalcula cada cuadro (layouts que se pulen tarde).
            m_hacia = puntoDestino(m_objetivo, a);
        }
        double k = std::min(1.0, double(local + 1) / double(m_cuadrosMovimiento));
        k = k * k * (3 - 2 * k); // suavizado (smoothstep)
        moverCursor(m_desde + (m_hacia - m_desde) * k, a.hover);
        if (local == 0 || local + 1 == m_cuadrosMovimiento) {
            m_diag->instantanea(m_cuadro, local == 0 ? QStringLiteral("mover-inicio") : QStringLiteral("mover-fin"),
                                m_cursor);
        }
        return true;
    }
    case TipoAccion::Clic:
        // M7: `pausa` cuadros quietos; luego onda + press y release 3 cuadros
        // despues. M4: el hover solo existe del press al release.
        if (local == a.pausa) {
            QMetaObject::invokeMethod(m_overlay, "clic");
            if (a.sinEfecto) {
                // D10: queda registrado (stdout y linea de tiempo) que este
                // clic no llega a la app (abriria un dialogo nativo o Finder).
                const QString sobre = describirPunto(m_cursor);
                std::printf("clic sinEfecto: escena %s, objetivo '%s', cuadro %d, %s\n", qPrintable(m_escena),
                            qPrintable(m_objetivo.isNull() ? QString() : m_objetivo->objectName()), m_cuadro,
                            qPrintable(sobre));
                std::fflush(stdout);
                m_sinEfecto.append(QJsonObject{
                    {QStringLiteral("escena"), m_escena},
                    {QStringLiteral("objetivo"), m_objetivo.isNull() ? QString() : m_objetivo->objectName()},
                    {QStringLiteral("cuadro"), m_cuadro}});
            }
            if (!a.sinEfecto) {
                asegurarActiva();
                m_ultimoClic = describirPunto(m_cursor);
                m_diag->instantanea(m_cuadro, QStringLiteral("antes-move"), m_cursor);
                m_diag->vigilar();
                enviarRaton(QEvent::MouseMove, Qt::NoButton, Qt::NoButton);
                m_diag->instantanea(m_cuadro, QStringLiteral("antes-press"), m_cursor);
                enviarRaton(QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
                m_diag->instantanea(m_cuadro, QStringLiteral("tras-press"), m_cursor);
                if (++m_clicsConEfecto == m_op.desactivarEnClic) {
                    QWindowSystemInterface::handleFocusWindowChanged(nullptr, Qt::ActiveWindowFocusReason);
                    QWindowSystemInterface::flushWindowSystemEvents();
                    m_diag->instantanea(m_cuadro, QStringLiteral("tras-desactivar"), m_cursor);
                }
            }
        } else if (local == a.pausa + 3 && !a.sinEfecto) {
            m_diag->instantanea(m_cuadro, QStringLiteral("antes-release"), m_cursor);
            // T022 D7: el release solo cuenta si el press sigue agarrado
            // (Agarre.h); si no, el clic no tendria efecto. Falla con la
            // traza y el PNG, sin reintento.
            if (!conservaAgarre(m_objetivo.data(), m_ventana->mouseGrabberItem())) {
                for (const QString& linea : m_diag->estado(QStringLiteral("antes-release"), m_cursor)) {
                    imprimir(QStringLiteral("traza: ") + linea);
                }
                return fallarEscena(cap, esc,
                                    QStringLiteral("el clic sobre '%1' perdio el agarre del raton antes del release "
                                                   "(sin reintento)")
                                            .arg(m_diag->nombreObjetivo())
                                        + diagnosticoControl());
            }
            enviarRaton(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
            m_diag->instantanea(m_cuadro, QStringLiteral("tras-release"), m_cursor);
            retirarHover();
        } else if (local > a.pausa && !a.sinEfecto) {
            m_diag->instantanea(m_cuadro, QStringLiteral("clic+%1").arg(local - a.pausa), m_cursor);
        }
        return m_error.isEmpty();
    case TipoAccion::Escribir:
    case TipoAccion::EscribirContrasena: {
        const int n = int(a.texto.size());
        for (int i = 0; i < n; ++i) {
            if (i * a.cuadros / n == local) {
                const QChar c = a.texto.at(i);
                enviarTecla(c == QLatin1Char(' ') ? int(Qt::Key_Space) : int(c.toUpper().unicode()), QString(c));
            }
        }
        return m_error.isEmpty();
    }
    case TipoAccion::Tecla: {
        // T024.4 (03-motion 3): con keycap, el dibujo entra en 0-5 y el
        // evento sale en el cuadro 6; sin keycap, en el 0.
        if (local == 0 && a.keycap) {
            m_overlay->setProperty("teclasKeycap", simbolosKeycap(a));
            m_overlay->setProperty("inicioKeycap", m_cuadro);
        }
        if (local == (a.keycap ? kCuadroEventoKeycap : 0)) {
            QString texto;
            if (a.tecla == QLatin1String("Enter")) {
                texto = QStringLiteral("\r");
            } else if (a.modificadores.isEmpty() && a.tecla.size() == 1) {
                texto = a.tecla.toLower();
            }
            enviarTecla(codigoTecla(a.tecla), texto, modificadoresQt(a.modificadores));
        }
        return m_error.isEmpty();
    }
    case TipoAccion::Desplazar: {
        // T024.4 (03-motion 5): contentY por cuadro con InOutQuad.
        if (local == 0) {
            QQuickItem* item = esperarControl(a.objetivo);
            if (!m_error.isEmpty()) {
                return false;
            }
            m_flickable = flickableDe(item);
            if (m_flickable.isNull()) {
                return fallarEscena(cap, esc,
                                    QStringLiteral("desplazar: '%1' no es (ni contiene) un Flickable visible").arg(a.objetivo)
                                        + diagnosticoControl());
            }
            m_desdeY = m_flickable->property("contentY").toDouble();
            const double maximo = std::max(0.0, m_flickable->property("contentHeight").toDouble() - m_flickable->height()
                                                    + m_flickable->property("originY").toDouble());
            m_haciaY = std::clamp(a.hasta ? *a.hasta : m_desdeY + a.delta,
                                  m_flickable->property("originY").toDouble(), maximo);
        }
        if (!m_flickable.isNull()) {
            const double t = double(local + 1) / double(a.cuadros);
            const double k = t < 0.5 ? 2 * t * t : 1 - std::pow(-2 * t + 2, 2) / 2;
            m_flickable->setProperty("contentY", m_desdeY + (m_haciaY - m_desdeY) * k);
        }
        return m_error.isEmpty();
    }
    default:
        return true; // esperar y esperarLinea: solo pasan cuadros
    }
}

bool Sesion::ejecutarEscena(const Capitulo& cap, const Escena& esc)
{
    m_grabando = m_op.escenas.isEmpty() || m_op.escenas.contains(idEscena(cap, esc));
    m_escena = idEscena(cap, esc);
    qsizetype siguiente = 0; // primera accion aun no iniciada
    const Accion* activa = nullptr;
    for (int local = 0; local < esc.duracion; ++local) {
        if (activa != nullptr && local >= activa->inicio + activa->cuadros) {
            activa = nullptr;
        }
        while (activa == nullptr && siguiente < esc.acciones.size() && esc.acciones.at(siguiente).inicio == local) {
            const Accion& a = esc.acciones.at(siguiente++);
            if (a.cuadros == 0) {
                if (!aplicarInstantanea(cap, esc, a)) {
                    return false;
                }
            } else {
                activa = &a;
            }
        }
        if (activa != nullptr && !aplicarCuadro(cap, esc, *activa, local - activa->inicio)) {
            return false;
        }
        if (!cuadro()) {
            return false;
        }
    }
    // Instantaneas al final exacto de la escena (p. ej. tras el colchon).
    while (siguiente < esc.acciones.size()) {
        const Accion& a = esc.acciones.at(siguiente++);
        if (a.cuadros == 0 && !aplicarInstantanea(cap, esc, a)) {
            return false;
        }
    }
    return true;
}

// Linea de tiempo con los clics sinEfecto de esta grabacion (D10).
QByteArray Sesion::lineaDeTiempo() const
{
    QJsonObject raiz = QJsonDocument::fromJson(lineaDeTiempoJson(m_salida, m_op.rapido)).object();
    raiz.insert(QStringLiteral("sinEfecto"), m_sinEfecto);
    return QJsonDocument(raiz).toJson(QJsonDocument::Indented);
}

// T024.4 (03-motion 4): portada o cierre. La app sigue viva detras (la
// ventana entra o sale en el lienzo); el cursor simulado se oculta. Solo sale
// en el video si se graba completo (sin --escenas).
bool Sesion::ejecutarMarca(const QString& nombre, int cuadros)
{
    if (cuadros <= 0) {
        return true;
    }
    m_grabando = m_op.escenas.isEmpty();
    m_escena = nombre;
    m_overlay->setProperty("ocultarCursor", true);
    for (int i = 0; i < cuadros; ++i) {
        if (!cuadro()) {
            return false;
        }
    }
    m_overlay->setProperty("ocultarCursor", false);
    return true;
}

int Sesion::ejecutar()
{
    const QDir tmp(m_op.dirTemporal);
    const QString metadatos = tmp.filePath(QStringLiteral("capitulos.ffmeta"));
    const QString mp4Temporal = tmp.filePath(QStringLiteral("video.mp4"));
    {
        QFile f(metadatos);
        if (!f.open(QIODevice::WriteOnly) || f.write(metadatosCapitulos(m_salida)) < 0) {
            imprimir(QStringLiteral("error: no se pudieron escribir los capitulos"));
            return 1;
        }
    }
    bool ok = cargar();
    if (ok) {
        const QString e = m_ffmpeg.iniciar(m_op.ffmpeg, QSize(kLienzoAnchoPx, kLienzoAltoPx), metadatos, mp4Temporal,
                                           tmp.filePath(QStringLiteral("ffmpeg.log")), m_op.rapido, m_op.pistaVoz);
        ok = e.isEmpty() || fallar(e);
    }
    ok = ok && ejecutarMarca(QStringLiteral("portada"), m_guion.cuadrosPortada);
    for (qsizetype c = 0; ok && c < m_guion.capitulos.size(); ++c) {
        // El titulo de capitulo vive en el lienzo (margen superior, D21).
        const Capitulo& cap = m_guion.capitulos.at(c);
        for (qsizetype e = 0; ok && e < cap.escenas.size(); ++e) {
            ok = ejecutarEscena(cap, cap.escenas.at(e));
        }
    }
    ok = ok && ejecutarMarca(QStringLiteral("cierre"), m_guion.cuadrosCierre);
    if (ok && (m_cuadro != m_guion.totalCuadros || m_escritos != m_salida.totalCuadros)) {
        ok = fallar(QStringLiteral("se grabaron %1 cuadros y el plan tenia %2").arg(m_escritos).arg(m_salida.totalCuadros));
    }
    const QString finFfmpeg = m_ffmpeg.terminar(!ok);
    if (ok && !finFfmpeg.isEmpty()) {
        ok = fallar(finFfmpeg);
    }
    if (m_ventana != nullptr) {
        m_ventana->hide();
    }
    if (m_lienzo != nullptr) {
        m_lienzo->hide();
    }
    if (!ok) {
        imprimir(QStringLiteral("error: ") + m_error);
        return 1;
    }

    // D25: con audio, el AAC decodificado no debe pasar de -1.8 dBTP; si
    // pasa, no se publica nada.
    if (!m_op.pistaVoz.isEmpty()) {
        double lufs = 0;
        double pico = 0;
        if (const QString e = medirAudioDecodificado(m_op.ffmpeg, mp4Temporal, lufs, pico); !e.isEmpty()) {
            imprimir(QStringLiteral("error: ") + e);
            return 1;
        }
        std::printf("audio AAC decodificado: %.2f LUFS, %.2f dBTP (maximo %.1f)\n", lufs, pico, kPicoVerdaderoMaximoAac);
        if (pico > kPicoVerdaderoMaximoAac) {
            imprimir(QStringLiteral("error: el audio AAC decodificado tiene un pico verdadero de %1 dBTP, por encima "
                                    "de %2 dBTP (D25); no se publico el video")
                         .arg(pico, 0, 'f', 2)
                         .arg(kPicoVerdaderoMaximoAac, 0, 'f', 1));
            return 1;
        }
    }

    // Salidas: solo en el directorio de salida, con renombre atomico.
    const QString e = publicar(m_op.dirSalida,
                               {{m_op.nombre + QStringLiteral(".mp4"), mp4Temporal, {}},
                                {m_op.nombre + QStringLiteral(".srt"), {}, subtitulosSrt(m_salida)},
                                {m_op.nombre + QStringLiteral(".linea-tiempo.json"), {}, lineaDeTiempo()}});
    if (!e.isEmpty()) {
        imprimir(QStringLiteral("error: ") + e);
        return 1;
    }
    std::printf("transcripcion verificada: %d cuadros con cue\n", m_cuesVerificados);
    std::printf("OK %s (%d cuadros, %.2f s)\n", qPrintable(QDir(m_op.dirSalida).filePath(m_op.nombre + QStringLiteral(".mp4"))),
                m_escritos, double(m_escritos) / kFps);
    return 0;
}

} // namespace

int grabar(Guion& guion, const Guion& salida, DemoVideo& demo, const OpcionesGrabacion& opciones)
{
    Sesion sesion(guion, salida, demo, opciones);
    return sesion.ejecutar();
}

} // namespace video

#include "Grabador.moc"
