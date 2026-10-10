// satcfdi_manual_capturas (T012 D2-D7): genera las imagenes del manual de
// usuario en docs/manual/img/. Carga el modulo QML real (SatCfdiDownloader)
// con PresentacionViewModels sobre servicios FIJOS (ServiciosFijos.h); los
// elementos nativos (menu bar, notificacion, Finder, selector de archivos) son
// ilustraciones QML (qml/) alimentadas por la definicion compartida del menu
// bar y por ServicioNotificaciones. Sin SQLite, worker, SAT ni Keychain.
//
// Uso: satcfdi_manual_capturas --salida <dir> [--solo <archivo.png>]
// Requiere sesion grafica (QT_QPA_PLATFORM=cocoa); no forma parte de ctest.

#include "ServiciosFijos.h"

#include "application/notificaciones/Notificador.h"
#include "application/notificaciones/ServicioNotificaciones.h"
#include "infrastructure/os/MenuBarDefinicion.h"
#include "presentation/viewmodels/AppViewModel.h"
#include "presentation/viewmodels/EFirmaFormViewModel.h"
#include "presentation/viewmodels/NuevaSolicitudViewModel.h"
#include "presentation/viewmodels/PerfilesSatViewModel.h"
#include "presentation/viewmodels/PresentacionViewModels.h"
#include "presentation/estilo/EstiloVisual.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImageWriter>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStyleHints>
#include <QTimeZone>
#include <QTimer>

#include <cstdio>
#include <ctime>
#include <functional>
#include <memory>

#ifndef SATCFDI_CAPTURAS_QML_DIR
#error "SATCFDI_CAPTURAS_QML_DIR debe definirse en CMake"
#endif

using namespace satcfdi;
using namespace capturas;

namespace {

constexpr int kAncho = 960;
constexpr int kAlto = 640;
constexpr int kEscala = 2; // D4: 1920x1280
constexpr qint64 kPesoMaximo = 1024 * 1024; // PNG versionable: maximo 1 MB

// T013 D9: --tema oscuro fija Theme.oscuro = true en cada engine de la app
// (las ilustraciones imitan la interfaz de macOS en claro y no cambian).
bool gTemaOscuro = false;

// --- Datos sinteticos (D7) --------------------------------------------------

const QString kRfcPerfil = QStringLiteral("XAXX010101000");
const QString kRfcOtro = QStringLiteral("XEXX010101000");
const QDateTime kAhora(QDate(2026, 10, 1), QTime(10, 0), QTimeZone::UTC);

PerfilId perfilId(char c)
{
    const QString t = QString(8, QLatin1Char(c)) + QStringLiteral("-") + QString(4, QLatin1Char(c)) + QStringLiteral("-4")
                      + QString(3, QLatin1Char(c)) + QStringLiteral("-8") + QString(3, QLatin1Char(c)) + QStringLiteral("-")
                      + QString(12, QLatin1Char(c));
    return PerfilId::desdeTexto(t).value();
}

SolicitudId solicitudId(int n)
{
    return SolicitudId::desdeTexto(QStringLiteral("00000000-0000-4000-8000-0000000000%1").arg(n, 2, 10, QLatin1Char('0')))
        .value();
}

QString idPaquete(int solicitud, int n)
{
    return QStringLiteral("0F1E2D3C-4B5A-4978-8796-A5B4C3D2E1%1_%2")
        .arg(solicitud, 2, 10, QLatin1Char('0'))
        .arg(n, 2, 10, QLatin1Char('0'));
}

PerfilResumen perfilListo() { return {perfilId('1'), kRfcPerfil, QStringLiteral("Contribuyente de ejemplo"), true}; }
PerfilResumen perfilSinEFirma() { return {perfilId('2'), kRfcOtro, QStringLiteral("Proveedor de ejemplo"), true}; }

ResumenCredencial credencialLista()
{
    ResumenCredencial r;
    r.estado = EstadoCredencial::Lista;
    r.vigenteDesde = QDateTime(QDate(2025, 1, 1), QTime(0, 0), QTimeZone::UTC);
    r.vigenteHasta = QDateTime(QDate(2029, 1, 1), QTime(0, 0), QTimeZone::UTC);
    return r;
}

LogResumen log(TipoEventoLog tipo, OrigenLog origen, int minutos)
{
    LogResumen l;
    l.tipoEvento = tipo;
    l.origen = origen;
    l.creadoEn = kAhora.addSecs(qint64(minutos) * 60);
    return l;
}

PaqueteResumen paquete(int solicitud, int n, EstadoDescarga estado)
{
    PaqueteResumen p;
    p.idPaqueteSat = idPaquete(solicitud, n);
    p.estadoDescarga = estado;
    p.disponibleEn = kAhora.addSecs(40 * 60);
    if (estado == EstadoDescarga::Descargado) {
        p.descargadoEn = kAhora.addSecs(45 * 60);
    }
    if (estado == EstadoDescarga::Vencido) {
        p.vencidoEn = kAhora.addSecs(50 * 60);
        p.codigoDescargaSat = QStringLiteral("5007");
    }
    return p;
}

SolicitudDetalle solicitud(int n, EstadoLocal local, std::optional<EstadoSolicitudSat> sat, int dia)
{
    SolicitudDetalle d;
    d.resumen.id = solicitudId(n);
    d.resumen.perfilRfc = kRfcPerfil;
    d.resumen.perfilNombre = perfilListo().nombre;
    d.resumen.tipoDescarga = n % 2 == 0 ? TipoDescarga::Emitidos : TipoDescarga::Recibidos;
    d.resumen.fechaInicial = QDate(2026, 9, dia);
    d.resumen.fechaFinal = QDate(2026, 9, dia);
    d.resumen.estadoLocal = local;
    d.resumen.estadoSat = sat;
    d.resumen.creadaEn = kAhora.addSecs(qint64(n) * 60);
    d.fechaInicialSat = QStringLiteral("2026-09-%1T00:00:00").arg(dia, 2, 10, QLatin1Char('0'));
    d.fechaFinalSat = QStringLiteral("2026-09-%1T23:59:59").arg(dia, 2, 10, QLatin1Char('0'));
    d.logs = {log(TipoEventoLog::SolicitudCreada, OrigenLog::Usuario, n)};
    if (local != EstadoLocal::Creada) {
        d.idSolicitudSat = QStringLiteral("4e80345d-917f-40bb-a98f-4a73939343%1").arg(n, 2, 10, QLatin1Char('0'));
        d.codEstatusSolicitud = QStringLiteral("5000");
        d.mensajeSolicitudSat = QStringLiteral("Solicitud Aceptada");
        d.enviadaEn = kAhora.addSecs(qint64(n + 1) * 60);
        d.logs.append(log(TipoEventoLog::SolicitudEnviada, OrigenLog::Usuario, n + 1));
    }
    return d;
}

// Conteos del resumen coherentes con los paquetes (T014.1 D2, como
// SolicitudesServicePersistido): Descargado; Disponible + Error pendientes.
void contarPaquetes(SolicitudDetalle& d)
{
    d.resumen.totalPaquetes = int(d.paquetes.size());
    d.resumen.paquetesDescargados = 0;
    d.resumen.paquetesPendientesDescarga = 0;
    for (const PaqueteResumen& p : d.paquetes) {
        if (p.estadoDescarga == EstadoDescarga::Descargado) {
            ++d.resumen.paquetesDescargados;
        } else if (p.estadoDescarga == EstadoDescarga::Disponible || p.estadoDescarga == EstadoDescarga::Error) {
            ++d.resumen.paquetesPendientesDescarga;
        }
    }
}

SolicitudDetalle solicitudTerminada()
{
    SolicitudDetalle d = solicitud(3, EstadoLocal::Enviada, EstadoSolicitudSat::Terminada, 3);
    d.numeroCfdi = 12;
    d.codigoEstadoSolicitud = QStringLiteral("5000");
    d.ultimaVerificacionEn = kAhora.addSecs(35 * 60);
    d.paquetes = {paquete(3, 1, EstadoDescarga::Descargado), paquete(3, 2, EstadoDescarga::Disponible),
                  paquete(3, 3, EstadoDescarga::Vencido)};
    contarPaquetes(d);
    d.logs.append(log(TipoEventoLog::VerificacionRealizada, OrigenLog::Worker, 35));
    d.logs.append(log(TipoEventoLog::PaquetesRegistrados, OrigenLog::Worker, 35));
    d.logs.append(log(TipoEventoLog::PaqueteDescargado, OrigenLog::Worker, 45));
    return d;
}

SolicitudDetalle solicitudConIncidencia()
{
    SolicitudDetalle d = solicitud(5, EstadoLocal::Enviada, EstadoSolicitudSat::Terminada, 5);
    d.numeroCfdi = 4;
    d.codigoEstadoSolicitud = QStringLiteral("5000");
    d.ultimaVerificacionEn = kAhora.addSecs(35 * 60);
    PaqueteResumen error = paquete(5, 1, EstadoDescarga::Error);
    error.codigoDescargaSat = QStringLiteral("5000");
    d.paquetes = {error, paquete(5, 2, EstadoDescarga::Descargado)};
    contarPaquetes(d);
    d.logs.append(log(TipoEventoLog::VerificacionRealizada, OrigenLog::Worker, 35));
    d.logs.append(log(TipoEventoLog::DescargaFallida, OrigenLog::Worker, 46));
    return d;
}

// En el orden de los datos; SolicitudesFijas::listar ordena como la app real
// (mas reciente primero).
QList<SolicitudDetalle> solicitudesLista()
{
    return {solicitud(1, EstadoLocal::Creada, std::nullopt, 1), solicitud(2, EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso, 2),
            solicitudTerminada(), solicitud(4, EstadoLocal::Enviada, EstadoSolicitudSat::Error, 4)};
}

// --- Render -----------------------------------------------------------------

void esperar(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

// Frame estable: deja correr bindings y animaciones y espera un frame nuevo.
void esperarFrameEstable(QQuickWindow* ventana)
{
    esperar(600);
    QEventLoop bucle;
    QObject::connect(ventana, &QQuickWindow::frameSwapped, &bucle, &QEventLoop::quit);
    QTimer::singleShot(2000, &bucle, &QEventLoop::quit);
    ventana->update();
    bucle.exec();
    esperar(100);
}

// Captura normalizada a 1920x1280 sin metadatos variables (D4).
bool guardar(QQuickWindow* ventana, const QString& ruta)
{
    QImage imagen = ventana->grabWindow();
    if (imagen.isNull()) {
        return false;
    }
    imagen = imagen.convertToFormat(QImage::Format_RGB32);
    if (imagen.size() != QSize(kAncho * kEscala, kAlto * kEscala)) {
        imagen = imagen.scaled(kAncho * kEscala, kAlto * kEscala, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    imagen.setDevicePixelRatio(1.0);
    imagen.setDotsPerMeterX(5669); // 144 dpi fijo
    imagen.setDotsPerMeterY(5669);
    // PNG sin perdida con compresion zlib maxima: en el handler PNG de Qt,
    // quality 0 equivale a nivel 9 (setCompression no lo controla).
    QImageWriter escritor(ruta, "png");
    escritor.setQuality(0);
    return escritor.write(imagen);
}

// --- Escenarios con la app real (Captura) -----------------------------------

struct Grafo {
    SolicitudesFijas solicitudes;
    PerfilesFijos perfiles;
    CredencialesFijas credenciales;
    AccionesNulas acciones;
    ExistenciaFija existencia;
    std::unique_ptr<PresentacionViewModels> vms;
    std::unique_ptr<QQmlApplicationEngine> engine;

    Grafo()
    {
        perfiles.perfiles = {perfilListo(), perfilSinEFirma()};
        credenciales.resumenes.insert(perfilListo().id.texto(), credencialLista());
    }

    QQuickWindow* cargar()
    {
        vms = std::make_unique<PresentacionViewModels>(&solicitudes, &perfiles, &credenciales);
        vms->setAccionesSolicitud(&acciones);
        vms->setAccionesFinder(&acciones);
        vms->setConsultaExistencia(&existencia);
        engine = std::make_unique<QQmlApplicationEngine>();
        if (gTemaOscuro) {
            QObject* tema = engine->singletonInstance<QObject*>("SatCfdiDownloader", "Theme");
            if (!tema || !tema->setProperty("oscuro", true)) {
                std::fprintf(stderr, "error: no se pudo fijar Theme.oscuro\n");
                return nullptr;
            }
        }
        engine->setInitialProperties(vms->initialProperties());
        engine->loadFromModule("SatCfdiDownloader", "Main");
        if (engine->rootObjects().isEmpty()) {
            return nullptr;
        }
        auto* ventana = qobject_cast<QQuickWindow*>(engine->rootObjects().constFirst());
        ventana->resize(kAncho, kAlto);
        ventana->show();
        return ventana;
    }

    // Busqueda en el arbol VISUAL (las paginas de StackView no siempre son
    // hijos QObject del contentItem).
    static QQuickItem* buscar(QQuickItem* raiz, const QString& nombre)
    {
        if (!raiz) {
            return nullptr;
        }
        if (raiz->objectName() == nombre) {
            return raiz;
        }
        for (QQuickItem* hijo : raiz->childItems()) {
            if (QQuickItem* r = buscar(hijo, nombre)) {
                return r;
            }
        }
        return nullptr;
    }

    QQuickItem* pagina(QQuickWindow* ventana, const QString& nombre) const
    {
        return buscar(ventana->contentItem(), nombre);
    }

    // Detalle (T013): pestanas Paquetes/Datos/Historial en vez de secciones
    // desplazables; la captura muestra la pestana indicada.
    bool mostrarPestana(QQuickWindow* ventana, const QString& clave) const
    {
        QQuickItem* p = pagina(ventana, QStringLiteral("paginaDetalleSolicitud"));
        return p && p->setProperty("pestana", clave) && p->property("pestana").toString() == clave;
    }
};

using Preparar = std::function<bool(Grafo&, QQuickWindow*)>;

bool capturaApp(const QString& ruta, const Preparar& preparar)
{
    Grafo g;
    QQuickWindow* ventana = g.cargar();
    if (!ventana) {
        return false;
    }
    esperar(200);
    if (!preparar(g, ventana)) {
        return false;
    }
    esperarFrameEstable(ventana);
    const bool ok = guardar(ventana, ruta);
    ventana->hide();
    return ok;
}

// --- Escenarios de ilustracion ----------------------------------------------

bool ilustracion(const QString& ruta, const QString& componente, const QVariantMap& propiedades)
{
    QQmlEngine engine;
    QQmlComponent comp(&engine);
    const QByteArray qml = QStringLiteral(
                               "import QtQuick\n"
                               "Rectangle {\n"
                               "  width: %1; height: %2; color: \"#e5e5ea\"\n"
                               "  %3 { objectName: \"ilustracion\"; anchors.centerIn: parent }\n"
                               "}\n")
                               .arg(kAncho)
                               .arg(kAlto)
                               .arg(componente)
                               .toUtf8();
    comp.setData(qml, QUrl::fromLocalFile(QStringLiteral(SATCFDI_CAPTURAS_QML_DIR "/Envoltura.qml")));
    std::unique_ptr<QObject> raizObj(comp.create());
    auto* raiz = qobject_cast<QQuickItem*>(raizObj.get());
    if (!raiz) {
        std::fprintf(stderr, "%s\n", qPrintable(comp.errorString()));
        return false;
    }
    QObject* il = raiz->findChild<QObject*>(QStringLiteral("ilustracion"));
    for (auto it = propiedades.cbegin(); it != propiedades.cend(); ++it) {
        il->setProperty(it.key().toUtf8().constData(), it.value());
    }
    QQuickWindow ventana;
    ventana.setColor(QColor(QStringLiteral("#e5e5ea")));
    ventana.resize(kAncho, kAlto);
    raiz->setParentItem(ventana.contentItem());
    ventana.show();
    esperarFrameEstable(&ventana);
    const bool ok = guardar(&ventana, ruta);
    ventana.hide();
    raiz->setParentItem(nullptr);
    return ok;
}

// D6: entradas visibles del menu bar desde la definicion compartida.
QVariantList entradasMenu()
{
    menubar::EstadoMenu estado;
    estado.monitoreo.fase = OSIntegration::EstadoMonitoreo::Fase::ActivoEnEspera;
    estado.monitoreo.pendientes = 1;
    estado.preferenciaLoginItem = true;
    estado.loginItem = OSIntegration::LoginItemStatus::Enabled;
    estado.notificaciones = OSIntegration::NotificationStatus::Granted;
    QVariantList r;
    for (const menubar::Entrada& e : menubar::entradas(estado)) {
        if (!e.visible) {
            continue;
        }
        r.append(QVariantMap{{QStringLiteral("texto"), e.texto},
                             {QStringLiteral("habilitada"), e.habilitada},
                             {QStringLiteral("separador"), e.separador},
                             {QStringLiteral("marcada"), e.marcable && e.marcada},
                             {QStringLiteral("icono"), e.icono}});
    }
    return r;
}

// D6: texto de notificacion de ServicioNotificaciones con un Notificador fake.
class NotificadorCaptura final : public Notificador {
public:
    QList<Notificacion> recibidas;
    void notificar(const Notificacion& n) override { recibidas.append(n); }
};

std::optional<Notificacion> notificacionDescargaCompleta()
{
    NotificadorCaptura notificador;
    ServicioNotificaciones servicio(notificador);
    TransicionNotificable t;
    t.solicitudId = solicitudId(3);
    t.tipo = TipoTransicionNotificable::DescargaCompleta;
    t.tipoDescarga = TipoDescarga::Recibidos;
    t.rfcSolicitante = kRfcPerfil;
    t.fechaInicialSat = QStringLiteral("2026-09-03T00:00:00");
    t.fechaFinalSat = QStringLiteral("2026-09-03T23:59:59");
    t.paquetes = 1;
    t.descargados = 1;
    servicio.alConfirmarTransicion(t);
    if (notificador.recibidas.isEmpty()) {
        return std::nullopt;
    }
    return notificador.recibidas.constFirst();
}

// --- Tabla de escenarios ----------------------------------------------------

struct Escenario {
    QString archivo;
    QString tipo; // Captura | Ilustracion
    std::function<bool(const QString&)> generar;
};

QList<Escenario> escenarios()
{
    const QString carpetaSolicitud =
        QStringLiteral("/Users/usuario/SAT-CFDI-Downloader/paquetes/%1/2026-09/%2").arg(kRfcPerfil, solicitudId(3).texto());
    return {
        {QStringLiteral("01-perfiles.png"), QStringLiteral("Captura"),
         [](const QString& r) {
             return capturaApp(r, [](Grafo& g, QQuickWindow*) {
                 g.vms->app()->mostrarPerfiles();
                 esperar(300);
                 return g.vms->perfiles()->seleccionar(perfilListo().id.texto());
             });
         }},
        {QStringLiteral("02-efirma.png"), QStringLiteral("Captura"),
         [](const QString& r) {
             return capturaApp(r, [](Grafo& g, QQuickWindow* v) {
                 g.vms->app()->mostrarPerfiles();
                 esperar(300);
                 if (!g.vms->perfiles()->seleccionar(perfilSinEFirma().id.texto())) {
                     std::fprintf(stderr, "02: no se pudo seleccionar el perfil\n");
                     return false;
                 }
                 esperar(300);
                 QQuickItem* pagina = g.pagina(v, QStringLiteral("paginaPerfilesSat"));
                 if (!pagina) {
                     std::fprintf(stderr, "02: pagina de perfiles no encontrada\n");
                     return false;
                 }
                 if (!QMetaObject::invokeMethod(pagina, "gestionarEFirma")) {
                     std::fprintf(stderr, "02: gestionarEFirma no se pudo invocar\n");
                     return false;
                 }
                 g.vms->eFirma()->seleccionarCertificado(QUrl::fromLocalFile(QStringLiteral("/Users/usuario/Documentos/efirma/demo.cer")));
                 g.vms->eFirma()->seleccionarLlave(QUrl::fromLocalFile(QStringLiteral("/Users/usuario/Documentos/efirma/demo.key")));
                 return true;
             });
         }},
        {QStringLiteral("03-selector-archivos.png"), QStringLiteral("Ilustracion"),
         [](const QString& r) {
             return ilustracion(r, QStringLiteral("IlustracionSelectorArchivos"),
                                {{QStringLiteral("carpeta"), QStringLiteral("/Users/usuario/Documentos/efirma")},
                                 {QStringLiteral("archivos"), QStringList{QStringLiteral("demo.cer"), QStringLiteral("demo.key")}},
                                 {QStringLiteral("seleccionado"), QStringLiteral("demo.cer")}});
         }},
        {QStringLiteral("04-nueva-solicitud.png"), QStringLiteral("Captura"),
         [](const QString& r) {
             return capturaApp(r, [](Grafo& g, QQuickWindow*) {
                 g.vms->app()->mostrarNueva();
                 esperar(300);
                 NuevaSolicitudViewModel* f = g.vms->nuevaSolicitud();
                 // Estado inicial real del formulario: Emitidos y tipo de
                 // comprobante "Todos" (sin forzar valores en los ComboBox).
                 f->setPerfilId(perfilListo().id.texto());
                 f->setFechaInicial(QStringLiteral("2026-09-01"));
                 f->setFechaFinal(QStringLiteral("2026-09-30"));
                 return f->canSubmit();
             });
         }},
        {QStringLiteral("05-duplicado.png"), QStringLiteral("Captura"),
         [](const QString& r) {
             return capturaApp(r, [](Grafo& g, QQuickWindow*) {
                 EvaluacionDuplicado e;
                 e.clasificacion = ClasificacionDuplicado::RequiereConfirmacion;
                 e.motivo = MotivoDuplicado::TerminadaSinPaquetes;
                 e.solicitudReferencia = solicitudId(2);
                 g.solicitudes.evaluacion = e;
                 g.vms->app()->mostrarNueva();
                 esperar(300);
                 NuevaSolicitudViewModel* f = g.vms->nuevaSolicitud();
                 // Emitidos (valor inicial): el duplicado se evalua sobre los
                 // mismos filtros que el usuario captura por defecto.
                 f->setPerfilId(perfilListo().id.texto());
                 f->setFechaInicial(QStringLiteral("2026-09-02"));
                 f->setFechaFinal(QStringLiteral("2026-09-02"));
                 f->submit();
                 return true;
             });
         }},
        {QStringLiteral("06-lista.png"), QStringLiteral("Captura"),
         [](const QString& r) {
             return capturaApp(r, [](Grafo& g, QQuickWindow*) {
                 g.solicitudes.detalles = solicitudesLista();
                 emit g.solicitudes.listaCambiada(); // recarga la lista con los datos fijos
                 return true;
             });
         }},
        {QStringLiteral("07-lista-vacia.png"), QStringLiteral("Captura"),
         [](const QString& r) { return capturaApp(r, [](Grafo&, QQuickWindow*) { return true; }); }},
        {QStringLiteral("08-detalle-terminada.png"), QStringLiteral("Captura"),
         [](const QString& r) {
             return capturaApp(r, [](Grafo& g, QQuickWindow* v) {
                 g.solicitudes.detalles = {solicitudTerminada()};
                 if (!g.vms->app()->abrirDetalle(solicitudId(3).texto())) {
                     return false;
                 }
                 esperar(500);
                 return g.mostrarPestana(v, QStringLiteral("paquetes"));
             });
         }},
        {QStringLiteral("09-detalle-incidencia.png"), QStringLiteral("Captura"),
         [](const QString& r) {
             return capturaApp(r, [](Grafo& g, QQuickWindow* v) {
                 g.solicitudes.detalles = {solicitudConIncidencia()};
                 g.existencia.porPaquete.insert(idPaquete(5, 2), ExistenciaPaquete::NoEncontrado);
                 if (!g.vms->app()->abrirDetalle(solicitudId(5).texto())) {
                     return false;
                 }
                 esperar(500);
                 return g.mostrarPestana(v, QStringLiteral("paquetes"));
             });
         }},
        {QStringLiteral("10-finder.png"), QStringLiteral("Ilustracion"),
         [carpetaSolicitud](const QString& r) {
             const QString zip = idPaquete(3, 1) + QStringLiteral(".zip");
             return ilustracion(r, QStringLiteral("IlustracionFinder"),
                                {{QStringLiteral("carpeta"), carpetaSolicitud},
                                 {QStringLiteral("archivos"),
                                  QVariantList{QVariantMap{{QStringLiteral("nombre"), zip},
                                                           {QStringLiteral("tamano"), QStringLiteral("48 KB")},
                                                           {QStringLiteral("fecha"), QStringLiteral("1 oct 2026, 10:45")}}}},
                                 {QStringLiteral("seleccionado"), zip}});
         }},
        {QStringLiteral("11-menu-bar.png"), QStringLiteral("Ilustracion"),
         [](const QString& r) {
             return ilustracion(r, QStringLiteral("IlustracionMenuBar"), {{QStringLiteral("entradas"), entradasMenu()}});
         }},
        {QStringLiteral("12-notificacion.png"), QStringLiteral("Ilustracion"),
         [](const QString& r) {
             const auto n = notificacionDescargaCompleta();
             if (!n) {
                 return false;
             }
             return ilustracion(r, QStringLiteral("IlustracionNotificacion"),
                                {{QStringLiteral("titulo"), n->titulo}, {QStringLiteral("cuerpo"), n->cuerpo}});
         }},
    };
}

} // namespace

int main(int argc, char* argv[])
{
    // D4: zona UTC y entorno fijo antes de crear la app.
    qputenv("TZ", "UTC");
    ::tzset();
    QApplication app(argc, argv);
    satcfdi::presentacion::fijarEstiloBasico(); // T013 D1, igual que la app
    QCoreApplication::setOrganizationName(QStringLiteral("Adenium"));
    QCoreApplication::setApplicationName(QStringLiteral("SAT CFDI Downloader"));
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Mexico));
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);

    QString salida;
    QString solo;
    const QStringList args = QCoreApplication::arguments();
    for (qsizetype i = 1; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--salida") && i + 1 < args.size()) {
            salida = args.at(++i);
        } else if (args.at(i) == QLatin1String("--solo") && i + 1 < args.size()) {
            solo = args.at(++i);
        } else if (args.at(i) == QLatin1String("--tema") && i + 1 < args.size()
                   && (args.at(i + 1) == QLatin1String("claro") || args.at(i + 1) == QLatin1String("oscuro"))) {
            gTemaOscuro = args.at(++i) == QLatin1String("oscuro");
        } else {
            std::fprintf(stderr,
                         "uso: satcfdi_manual_capturas --salida <dir> [--solo <archivo.png>] [--tema claro|oscuro]\n");
            return 2;
        }
    }
    if (salida.isEmpty() || !QDir().mkpath(salida)) {
        std::fprintf(stderr, "error: --salida <dir> es obligatorio\n");
        return 2;
    }

    int fallas = 0;
    int generadas = 0;
    for (const Escenario& e : escenarios()) {
        if (!solo.isEmpty() && e.archivo != solo) {
            continue;
        }
        const QString ruta = QDir(salida).filePath(e.archivo);
        const bool ok = e.generar(ruta);
        const QImage leida(ruta);
        const bool tamano = leida.size() == QSize(kAncho * kEscala, kAlto * kEscala);
        const qint64 bytes = QFileInfo(ruta).size();
        const bool peso = bytes > 0 && bytes <= kPesoMaximo;
        const bool valida = ok && tamano && peso;
        std::printf("%s %s %s %dx%d %lld bytes%s\n", valida ? "OK   " : "FALLA", qPrintable(e.archivo),
                    qPrintable(e.tipo), leida.width(), leida.height(), static_cast<long long>(bytes),
                    bytes == 0 ? " (no generada)" : (peso ? "" : " (excede 1 MB)"));
        std::fflush(stdout);
        fallas += valida ? 0 : 1;
        ++generadas;
    }
    if (generadas == 0) {
        std::fprintf(stderr, "error: --solo no coincide con ningun escenario\n");
        return 2;
    }
    return fallas == 0 ? 0 : 1;
}
