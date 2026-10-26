#include "Grabador.h"

#include "Agarre.h"
#include "Barreras.h"
#include "CuadroApp.h"
#include "DiagnosticoClic.h"
#include "EntornoQml.h"
#include "Ilustraciones.h"
#include "SalidaFfmpeg.h"
#include "Salidas.h"

#include "presentation/viewmodels/AppViewModel.h"
#include "presentation/viewmodels/EFirmaFormViewModel.h"
#include "presentation/viewmodels/NuevaSolicitudViewModel.h"
#include "presentation/viewmodels/PerfilesSatViewModel.h"
#include "presentation/viewmodels/PresentacionViewModels.h"

#include <QDir>
#include <QElapsedTimer>
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

constexpr int kAncho = 960; // D4/D8: ventana logica 960x540 a 2x
constexpr int kAlto = 540;
constexpr int kEscala = 2;
constexpr int kAnchoPx = kAncho * kEscala;
constexpr int kAltoPx = kAlto * kEscala;
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
    bool aplicarInstantanea(const Capitulo& cap, const Escena& esc, const Accion& a);
    bool aplicarCuadro(const Capitulo& cap, const Escena& esc, const Accion& a, int local);
    void moverCursor(QPointF p, bool hover);
    void retirarHover();
    QPointF puntoDestino(QQuickItem* item, const Accion& a) const;
    void enviarRaton(QEvent::Type tipo, Qt::MouseButton boton, Qt::MouseButtons botones);
    void enviarTecla(int tecla, const QString& texto);
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
    QPointF m_cursor{kAncho / 2.0, kAlto / 2.0};
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
    m_reloj.install();
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
    // D8/D16: "hoy" de los atajos de periodo y de la validacion de fechas del
    // formulario es el `ahora` del guion, no el reloj del equipo.
    m_vms->nuevaSolicitud()->setReloj([hoy = m_demo.ahora().date()] { return hoy; });

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
    m_ventana->setMinimumSize(QSize(kAncho, kAlto));
    m_ventana->setMaximumSize(QSize(kAncho, kAlto));
    m_ventana->resize(kAncho, kAlto);

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
    if (!m_demo.perfiles.violacion.isEmpty()) {
        return fallar(m_demo.perfiles.violacion);
    }
    return true;
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
        m_cuadroApp->setImagen(normalizar(app, kAnchoPx, kAltoPx, m_avisoEscala, "la ventana"));
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

void Sesion::enviarTecla(int tecla, const QString& texto)
{
    asegurarActiva();
    QKeyEvent abajo(QEvent::KeyPress, tecla, Qt::NoModifier, texto);
    QCoreApplication::sendEvent(m_ventana, &abajo);
    QKeyEvent arriba(QEvent::KeyRelease, tecla, Qt::NoModifier, texto);
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
    case TipoAccion::CambiarEFirma:
        e = m_demo.cambiarEFirma(a.perfil, a.efirma, a.vigenteHasta);
        break;
    case TipoAccion::CompletarEFirma:
        e = m_demo.completarEFirma(a.perfil, a.vigenteHasta);
        break;
    case TipoAccion::MostrarSolicitudes:
        e = m_demo.mostrarSolicitudes(a.claves);
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
    case TipoAccion::Tecla:
        if (local == 0) {
            static const QHash<QString, int> kTeclas{{QStringLiteral("Enter"), Qt::Key_Return},
                                                    {QStringLiteral("Escape"), Qt::Key_Escape},
                                                    {QStringLiteral("Tab"), Qt::Key_Tab},
                                                    {QStringLiteral("Abajo"), Qt::Key_Down},
                                                    {QStringLiteral("Arriba"), Qt::Key_Up}};
            enviarTecla(kTeclas.value(a.tecla), a.tecla == QLatin1String("Enter") ? QStringLiteral("\r") : QString());
        }
        return m_error.isEmpty();
    default:
        return true; // esperar y esperarLinea: solo pasan cuadros
    }
}

bool Sesion::ejecutarEscena(const Capitulo& cap, const Escena& esc)
{
    m_grabando = m_op.escenas.isEmpty() || m_op.escenas.contains(idEscena(cap, esc));
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
    for (qsizetype c = 0; ok && c < m_guion.capitulos.size(); ++c) {
        // El titulo de capitulo vive en el lienzo (margen superior, D21).
        const Capitulo& cap = m_guion.capitulos.at(c);
        for (qsizetype e = 0; ok && e < cap.escenas.size(); ++e) {
            ok = ejecutarEscena(cap, cap.escenas.at(e));
        }
    }
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
                                {m_op.nombre + QStringLiteral(".linea-tiempo.json"), {},
                                 lineaDeTiempoJson(m_salida, m_op.rapido)}});
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
