// satcfdi_manual_capturas (T012 D2-D7): genera las imagenes del manual de
// usuario en docs/manual/img/. Carga el modulo QML real (SatCfdiDownloader)
// con PresentacionViewModels sobre servicios FIJOS (ServiciosFijos.h); los
// elementos nativos (menu bar, notificacion, Finder, selector de archivos) son
// ilustraciones QML (qml/) alimentadas por la definicion compartida del menu
// bar, por ServicioNotificaciones y (T016) por el dibujo real del icono del
// menu bar y las acciones de notificacion. Sin SQLite, worker, SAT ni Keychain.
//
// Uso: satcfdi_manual_capturas --salida <dir> [--solo <archivo.png>]
// Requiere sesion grafica (QT_QPA_PLATFORM=cocoa); no forma parte de ctest.

#include "ServiciosFijos.h"

#include "app_core/IntegracionWorker.h"
#include "application/notificaciones/Notificador.h"
#include "application/notificaciones/ServicioNotificaciones.h"
#include "infrastructure/cfdi/pdf/RenderizadorPdfCfdi.h"
#include "infrastructure/cfdi/xml/ParserCfdi.h"
#include "infrastructure/os/MenuBarDefinicion.h"
#include "infrastructure/os/macos/IconoMenuBar.h"
#include "infrastructure/os/macos/MacOSMapeos.h"
#include "presentation/viewmodels/AppViewModel.h"
#include "presentation/viewmodels/ExploradorCfdiViewModel.h"
#include "presentation/viewmodels/EFirmaFormViewModel.h"
#include "presentation/viewmodels/NuevaSolicitudViewModel.h"
#include "presentation/viewmodels/PerfilesSatViewModel.h"
#include "presentation/viewmodels/PresentacionViewModels.h"
#include "presentation/viewmodels/VisorPdfCfdiViewModel.h"
#include "presentation/estilo/EstiloVisual.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImageWriter>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QStyleHints>
#include <QTemporaryDir>
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

// T016 (T014.3 D2): e.firma Lista que vence dentro del umbral de 30 dias:
// con el reloj fijo (kAhora) faltan 21 dias ("Vence en 21 días").
const QDateTime kVenceEFirma(QDate(2026, 10, 22), QTime(12, 0), QTimeZone::UTC);
constexpr int kDiasParaVencer = 21;
constexpr int kUmbralVencimiento = 30;

ResumenCredencial credencialPorVencer()
{
    ResumenCredencial r = credencialLista();
    r.vigenteDesde = QDateTime(QDate(2022, 10, 22), QTime(12, 0), QTimeZone::UTC);
    r.vigenteHasta = kVenceEFirma;
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
    error.puedeReintentar = true; // como politicas::puedeReintentarManual: Error sin 5008
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

// T015.3: CFDI sinteticos de la solicitud Terminada (Recibidos): RFC
// genericos y nombres inventados; montos tal cual el texto del XML.
CfdiListado cfdiFijo(int posicion, TipoComprobante tipo, const QString& fecha, const QString& serie,
                     const QString& folio, const QString& emisor, const QString& total, const QString& metodo,
                     EstadoParseo estado = EstadoParseo::Completo)
{
    CfdiListado c;
    c.paqueteId = idPaquete(3, 1);
    c.posicion = posicion;
    ResumenCfdi& r = c.resumen;
    r.estado = estado;
    r.version = QStringLiteral("4.0");
    r.tipo = tipo;
    r.fecha = fecha;
    r.serie = serie;
    r.folio = folio;
    r.emisor.rfc = kRfcOtro;
    r.emisor.nombre = emisor;
    r.receptor.rfc = kRfcPerfil;
    r.receptor.nombre = perfilListo().nombre;
    if (!total.isEmpty()) {
        r.total = ImporteCfdi::desdeTexto(total);
    }
    // Pago 2.0: Total 0 y Moneda XXX en el comprobante (montos en el complemento).
    r.moneda = total.isEmpty() ? QString() : (tipo == TipoComprobante::Pago ? QStringLiteral("XXX") : QStringLiteral("MXN"));
    r.metodoPago = metodo;
    r.uuid = QStringLiteral("5F2B%1-4D5E-4F60-8A7B-0C1D2E3F4A5B")
                 .arg(QString::number(0x1A30 + posicion * 0x111, 16).toUpper());
    return c;
}

void llenarCatalogo(CatalogoFijo& catalogo)
{
    catalogo.cfdis = {
        cfdiFijo(0, TipoComprobante::Ingreso, QStringLiteral("2026-09-03T18:42:10"), QStringLiteral("A"),
                 QStringLiteral("1187"), QStringLiteral("Papelería Ejemplo"), QStringLiteral("1160.00"),
                 QStringLiteral("PUE")),
        cfdiFijo(1, TipoComprobante::Pago, QStringLiteral("2026-09-03T16:05:44"), QStringLiteral("P"),
                 QStringLiteral("204"), QStringLiteral("Servicios Demo"), QStringLiteral("0"), QString()),
        cfdiFijo(2, TipoComprobante::Ingreso, QStringLiteral("2026-09-03T13:20:01"), QString(),
                 QStringLiteral("88341"), QStringLiteral("Transportes Ficticios"), QStringLiteral("2436.50"),
                 QStringLiteral("PPD")),
        cfdiFijo(3, TipoComprobante::Egreso, QStringLiteral("2026-09-03T11:47:30"), QStringLiteral("NC"),
                 QStringLiteral("17"), QStringLiteral("Papelería Ejemplo"), QStringLiteral("116.00"),
                 QStringLiteral("PUE")),
        cfdiFijo(4, TipoComprobante::Ingreso, QStringLiteral("2026-09-03T10:02:15"), QStringLiteral("F"),
                 QStringLiteral("5521"), QStringLiteral("Comercializadora Imaginaria"), QStringLiteral("8352.00"),
                 QStringLiteral("PUE"), EstadoParseo::Parcial),
        cfdiFijo(5, TipoComprobante::Ingreso, QStringLiteral("2026-09-03T08:31:59"), QStringLiteral("B"),
                 QStringLiteral("903"), QStringLiteral("Servicios Demo"), QStringLiteral("580.00"),
                 QStringLiteral("PUE")),
        cfdiFijo(6, TipoComprobante::Desconocido, QString(), QString(), QString(), QString(), QString(), QString(),
                 EstadoParseo::Ilegible),
    };
    catalogo.cfdis[6].resumen.emisor = {};
    catalogo.cfdis[6].resumen.receptor = {};
    catalogo.cfdis[6].resumen.uuid.clear();
    ResumenIndiceSolicitud& r = catalogo.resumenIndice;
    r.paquetesDescargados = 1;
    r.paquetesIndexados = 1;
    r.cfdiTotal = int(catalogo.cfdis.size());
    r.cfdiIlegibles = 1;
}

// T015.4: PDF del primer CFDI de la lista (Papeleria Ejemplo, A-1187) con el
// parser y el renderer reales. XML sintetico: RFC genericos, nombres, UUID,
// certificados y sellos inventados.
QByteArray xmlCfdiFijo()
{
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<cfdi:Comprobante xmlns:cfdi=\"http://www.sat.gob.mx/cfd/4\" "
               "xmlns:tfd=\"http://www.sat.gob.mx/TimbreFiscalDigital\" Version=\"4.0\" Serie=\"A\" "
               "Folio=\"1187\" Fecha=\"2026-09-03T18:42:10\" Sello=\"U0VMTE9GSUNUSUNJT0RFTUFOVUFM=Xk9fQ2w=\" "
               "FormaPago=\"03\" NoCertificado=\"00001000000000000001\" SubTotal=\"1000.00\" Moneda=\"MXN\" "
               "Total=\"1160.00\" TipoDeComprobante=\"I\" Exportacion=\"01\" MetodoPago=\"PUE\" "
               "LugarExpedicion=\"64000\">"
               "<cfdi:Emisor Rfc=\"%1\" Nombre=\"PAPELERIA EJEMPLO\" RegimenFiscal=\"601\"/>"
               "<cfdi:Receptor Rfc=\"%2\" Nombre=\"CONTRIBUYENTE DE EJEMPLO\" DomicilioFiscalReceptor=\"64000\" "
               "RegimenFiscalReceptor=\"616\" UsoCFDI=\"G03\"/>"
               "<cfdi:Conceptos>"
               "<cfdi:Concepto ClaveProdServ=\"44121600\" Cantidad=\"10\" ClaveUnidad=\"H87\" Unidad=\"Pieza\" "
               "Descripcion=\"Cuaderno profesional de ejemplo\" ValorUnitario=\"60.00\" Importe=\"600.00\" "
               "ObjetoImp=\"02\"/>"
               "<cfdi:Concepto ClaveProdServ=\"44121700\" Cantidad=\"20\" ClaveUnidad=\"H87\" Unidad=\"Pieza\" "
               "Descripcion=\"Bolígrafo de ejemplo\" ValorUnitario=\"20.00\" Importe=\"400.00\" "
               "ObjetoImp=\"02\"/>"
               "</cfdi:Conceptos>"
               "<cfdi:Impuestos TotalImpuestosTrasladados=\"160.00\"><cfdi:Traslados>"
               "<cfdi:Traslado Base=\"1000.00\" Impuesto=\"002\" TipoFactor=\"Tasa\" TasaOCuota=\"0.160000\" "
               "Importe=\"160.00\"/></cfdi:Traslados></cfdi:Impuestos>"
               "<cfdi:Complemento><tfd:TimbreFiscalDigital Version=\"1.1\" UUID=\"%3\" "
               "FechaTimbrado=\"2026-09-03T18:43:02\" RfcProvCertif=\"%2\" "
               "SelloCFD=\"U0VMTE9GSUNUSUNJT0RFTUFOVUFM=Xk9fQ2w=\" NoCertificadoSAT=\"00001000000000000002\" "
               "SelloSAT=\"U0VMTE9TQVRGSUNUSUNJTw==\"/></cfdi:Complemento>"
               "</cfdi:Comprobante>\n")
        .arg(kRfcOtro, kRfcPerfil, QStringLiteral("5F2B1A30-4D5E-4F60-8A7B-0C1D2E3F4A5B"))
        .toUtf8();
}

// Escribe el PDF en `dir`; "" si no se pudo.
QString pdfFijo(const QString& dir)
{
    RenderizadorPdfCfdi renderer;
    const auto pdf = renderer.renderizar(parsearCfdi(xmlCfdiFijo()));
    if (!pdf.esExito()) {
        return {};
    }
    const QString ruta = QDir(dir).filePath(QStringLiteral("CFDI-A1187-5F2B1A30.pdf"));
    QFile f(ruta);
    if (!f.open(QIODevice::WriteOnly) || f.write(pdf.valor().contenido) != pdf.valor().contenido.size()) {
        return {};
    }
    return ruta;
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

// T015.4: pantalla con mayor devicePixelRatio. Con un monitor externo de
// dpr 1 como principal, la ventana se abria ahi y se reescalaba a 1920x1280:
// QtPdfQuick rasteriza el PDF a renderScale * Screen.devicePixelRatio y, a
// 1.25 px/pt, pdfium pierde el acento de "i" ("Boligrafo"). Todas las
// capturas usan la pantalla de mayor dpr (Retina = 2 = kEscala).
QScreen* pantallaCaptura()
{
    QScreen* mejor = QGuiApplication::primaryScreen();
    for (QScreen* s : QGuiApplication::screens()) {
        if (mejor == nullptr || s->devicePixelRatio() > mejor->devicePixelRatio()) {
            mejor = s;
        }
    }
    return mejor;
}

void colocarEnPantallaCaptura(QWindow* ventana)
{
    if (QScreen* s = pantallaCaptura()) {
        ventana->setScreen(s);
        ventana->setPosition(s->availableGeometry().topLeft() + QPoint(40, 40));
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
        std::fprintf(stderr, "aviso: sin pantalla de dpr %d; la captura se reescala (texto del PDF menos nitido)\n",
                     kEscala);
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
    CatalogoFijo catalogo; // T015.3
    PrevisualizacionFija previsualizacion; // T015.4
    PrimerUsoFijo primerUso; // T016: solo lo conecta la lista vacia
    std::unique_ptr<PresentacionViewModels> vms;
    std::unique_ptr<QQmlApplicationEngine> engine;

    Grafo()
    {
        perfiles.perfiles = {perfilListo(), perfilSinEFirma()};
        credenciales.resumenes.insert(perfilListo().id.texto(), credencialLista());
        llenarCatalogo(catalogo);
    }

    QQuickWindow* cargar()
    {
        // Reloj fijo (T016): el aviso "Vence en N días" se calcula con kAhora.
        vms = std::make_unique<PresentacionViewModels>(&solicitudes, &perfiles, &credenciales, nullptr,
                                                       [] { return kAhora; });
        vms->setAccionesSolicitud(&acciones);
        vms->setAccionesFinder(&acciones);
        vms->setConsultaExistencia(&existencia);
        vms->setConsultaCatalogo(&catalogo);
        vms->setPrevisualizacionCfdi(&previsualizacion);
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
        colocarEnPantallaCaptura(ventana);
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
    colocarEnPantallaCaptura(&ventana);
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

// T016 (T014.4 D2): variantes del icono del menu bar con el dibujo real
// (IconoMenuBar) y el texto real al pasar el cursor (textoEstadoIcono). Los
// PNG se escriben en `dir` (temporal) a 2x: `imagen` (18 pt) y `ampliada`
// (48 pt, para que se distinga la insignia).
QVariantMap varianteIcono(const QString& dir, OSIntegration::EstadoIcono estado, const QString& clave)
{
    QVariantMap v{{QStringLiteral("texto"), macos::textoEstadoIcono(estado)}};
    const QList<QPair<QString, int>> tamanos{{QStringLiteral("imagen"), 18 * kEscala},
                                             {QStringLiteral("ampliada"), 48 * kEscala}};
    for (const auto& [propiedad, lado] : tamanos) {
        const QString ruta = QDir(dir).filePath(QStringLiteral("icono-%1-%2.png").arg(clave).arg(lado));
        if (!macos::dibujarIconoMenuBar(estado, lado).save(ruta, "png")) {
            return {};
        }
        v.insert(propiedad, QUrl::fromLocalFile(ruta));
    }
    return v;
}

// Orden de la ilustracion: Normal, En pausa, Trabajando, Atencion.
QVariantList variantesIcono(const QString& dir)
{
    using E = OSIntegration::EstadoIcono;
    QVariantList r;
    const QList<QPair<E, QString>> estados{{E::Normal, QStringLiteral("normal")},
                                           {E::Pausado, QStringLiteral("pausado")},
                                           {E::Trabajando, QStringLiteral("trabajando")},
                                           {E::Atencion, QStringLiteral("atencion")}};
    for (const auto& [estado, clave] : estados) {
        const QVariantMap v = varianteIcono(dir, estado, clave);
        if (v.isEmpty()) {
            return {};
        }
        r.append(v);
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

// T016 (T014.4 D1): notificacion para la ilustracion, con las acciones que el
// composition root agrega por tipo (notificacionLocalDe) y sus textos reales
// (textoAccionNotificacion).
QVariantMap notificacionIlustrada(const Notificacion& n)
{
    QStringList acciones;
    for (const OSIntegration::AccionNotificacion a : notificacionLocalDe(n).accionesExtra) {
        acciones.append(macos::textoAccionNotificacion(a));
    }
    return {{QStringLiteral("titulo"), n.titulo},
            {QStringLiteral("cuerpo"), n.cuerpo},
            {QStringLiteral("acciones"), acciones}};
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
                 // T016: la e.firma del perfil seleccionado vence en 21 dias.
                 g.credenciales.resumenes.insert(perfilListo().id.texto(), credencialPorVencer());
                 g.vms->app()->mostrarPerfiles();
                 esperar(300);
                 return g.vms->perfiles()->seleccionar(perfilListo().id.texto())
                        && g.vms->perfiles()->seleccionDiasParaVencer() == kDiasParaVencer;
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
         [](const QString& r) {
             // T016 (T014.3 D1): guia de primer uso con el paso 1 hecho y la
             // e.firma como siguiente paso.
             return capturaApp(r, [](Grafo& g, QQuickWindow* v) {
                 g.primerUso.estado = EstadoPrimerUso{true, false, false};
                 g.vms->setConsultaPrimerUso(&g.primerUso);
                 esperar(300);
                 QQuickItem* guia = g.pagina(v, QStringLiteral("guiaPrimerUso"));
                 return g.vms->app()->mostrarGuiaPrimerUso() && guia && guia->isVisible();
             });
         }},
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
             // T016: el icono de la barra es el dibujo real (variante Normal).
             QTemporaryDir dir;
             const QVariantMap normal = dir.isValid()
                                            ? varianteIcono(dir.path(), OSIntegration::EstadoIcono::Normal,
                                                            QStringLiteral("normal"))
                                            : QVariantMap{};
             if (normal.isEmpty()) {
                 std::fprintf(stderr, "11: no se pudo dibujar el icono\n");
                 return false;
             }
             return ilustracion(r, QStringLiteral("IlustracionMenuBar"),
                                {{QStringLiteral("entradas"), entradasMenu()},
                                 {QStringLiteral("icono"), normal.value(QStringLiteral("imagen"))}});
         }},
        {QStringLiteral("12-notificacion.png"), QStringLiteral("Ilustracion"),
         [](const QString& r) {
             // T016 (T014.4 D1): con "Opciones" abierto: "Descarga completa"
             // con Mostrar en Finder y "e.firma por vencer" (T014.3) con Abrir
             // Perfiles SAT.
             const auto n = notificacionDescargaCompleta();
             if (!n) {
                 return false;
             }
             const Notificacion vence = ServicioNotificaciones::componerVencimiento(
                 perfilListo().id, kRfcPerfil, kVenceEFirma, kUmbralVencimiento, kDiasParaVencer);
             return ilustracion(r, QStringLiteral("IlustracionNotificacion"),
                                {{QStringLiteral("notificaciones"),
                                  QVariantList{notificacionIlustrada(*n), notificacionIlustrada(vence)}}});
         }},
        // T015.3: explorador de facturas de la solicitud Terminada.
        {QStringLiteral("13-explorador-facturas.png"), QStringLiteral("Captura"),
         [](const QString& r) {
             return capturaApp(r, [](Grafo& g, QQuickWindow*) {
                 g.solicitudes.detalles = {solicitudTerminada()};
                 if (!g.vms->app()->abrirDetalle(solicitudId(3).texto())) {
                     return false;
                 }
                 esperar(300);
                 if (!g.vms->app()->abrirFacturas(solicitudId(3).texto())) {
                     std::fprintf(stderr, "13: no se pudo abrir el explorador\n");
                     return false;
                 }
                 esperar(500);
                 return g.vms->explorador()->vista() == QStringLiteral("lista");
             });
         }},
        // T015.4: visor del PDF del primer CFDI (renderer real, XML sintetico).
        {QStringLiteral("14-visor-pdf.png"), QStringLiteral("Captura"),
         [](const QString& r) {
             QTemporaryDir dir;
             const QString pdf = dir.isValid() ? pdfFijo(dir.path()) : QString();
             if (pdf.isEmpty()) {
                 std::fprintf(stderr, "14: no se pudo generar el PDF sintetico\n");
                 return false;
             }
             return capturaApp(r, [pdf](Grafo& g, QQuickWindow* v) {
                 g.previsualizacion.rutaPdf = pdf;
                 g.solicitudes.detalles = {solicitudTerminada()};
                 if (!g.vms->app()->abrirDetalle(solicitudId(3).texto())) {
                     return false;
                 }
                 esperar(300);
                 if (!g.vms->app()->abrirFacturas(solicitudId(3).texto())) {
                     return false;
                 }
                 esperar(500);
                 if (!g.vms->explorador()->previsualizar(0)) {
                     std::fprintf(stderr, "14: no se pudo previsualizar\n");
                     return false;
                 }
                 // QtQuick.Pdf carga y renderiza la pagina de forma asincrona.
                 QElapsedTimer t;
                 t.start();
                 QQuickItem* visor = g.pagina(v, QStringLiteral("visorPdfCfdi"));
                 while (t.elapsed() < 8000 && !(visor && visor->property("documentoListo").toBool())) {
                     esperar(100);
                     visor = g.pagina(v, QStringLiteral("visorPdfCfdi"));
                 }
                 esperar(1500);
                 return g.vms->explorador()->visor()->estado() == VisorPdfCfdiViewModel::Estado::Listo && visor
                        && visor->property("documentoListo").toBool();
             });
         }},
        // T016 (T014.4 D2): las cuatro variantes del icono del menu bar.
        {QStringLiteral("15-icono-menu-bar.png"), QStringLiteral("Ilustracion"),
         [](const QString& r) {
             QTemporaryDir dir;
             const QVariantList variantes = dir.isValid() ? variantesIcono(dir.path()) : QVariantList{};
             if (variantes.size() != 4) {
                 std::fprintf(stderr, "15: no se pudieron dibujar las variantes\n");
                 return false;
             }
             return ilustracion(r, QStringLiteral("IlustracionIconoMenuBar"),
                                {{QStringLiteral("variantes"), variantes}});
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
