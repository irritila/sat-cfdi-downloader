// satcfdi_manual_capturas (T012 D2-D7, T024.2): genera las imagenes del manual
// de usuario. Carga el modulo QML real (SatCfdiDownloader) con
// PresentacionViewModels sobre servicios FIJOS: los fakes sinteticos comunes
// de tools/fakes-materiales (Facturas, Historial, Exportacion, Respaldo; T024
// D9) y, para el apendice Diagnostico, ServiciosFijos.h. Los elementos nativos
// (menu bar, notificacion, Finder, selector de archivos) son ilustraciones QML
// (qml/) alimentadas por la definicion compartida del menu bar, por
// ServicioNotificaciones y por el dibujo real del icono. Sin SQLite, worker,
// SAT, red ni Keychain.
//
// T024.2: la lista de capturas y el entorno salen del manifiesto versionado
// (escenarios.json). El entorno es fijo y no depende de la sesion grafica:
// plataforma offscreen (no abre ventanas ni roba el foco), Qt Quick con el
// backend software y render loop basico, escala 2, ventana 960x640 (o la del
// escenario), tema claro, es_MX, UTC, reloj de datos fijo y reloj de
// animaciones VIRTUAL (avanza 16 ms por paso, nunca con el tiempo real). Dos
// ejecuciones limpias dan los mismos bytes (verificar-capturas.cmake).
//
// Uso: satcfdi_manual_capturas --salida <dir> [--solo <archivo.png>]
//                              [--tema claro|oscuro] [--textos <dir>]
//                              [--manifiesto <json>] (solo pruebas)
// --textos escribe, por captura, los textos visibles (<archivo>.txt) para el
// escaneo de datos reales (escanear-capturas.cmake).

#include "ServiciosFijos.h"

#include "DatosSinteticos.h"
#include "ExportacionSintetica.h"
#include "FuentesSinteticas.h"
#include "RespaldoSintetico.h"

#include "app_core/IntegracionWorker.h"
#include "application/notificaciones/Notificador.h"
#include "application/notificaciones/ServicioNotificaciones.h"
#include "infrastructure/cfdi/pdf/RenderizadorPdfCfdi.h"
#include "infrastructure/cfdi/xml/ParserCfdi.h"
#include "infrastructure/os/MenuBarDefinicion.h"
#include "infrastructure/os/macos/IconoMenuBar.h"
#include "infrastructure/os/macos/MacOSMapeos.h"
#include "presentation/viewmodels/AppViewModel.h"
#include "presentation/viewmodels/EFirmaFormViewModel.h"
#include "presentation/viewmodels/ExportacionFacturasViewModel.h"
#include "presentation/viewmodels/FacturasViewModel.h"
#include "presentation/viewmodels/FranjaMesViewModel.h"
#include "presentation/viewmodels/HistorialViewModel.h"
#include "presentation/viewmodels/PerfilesSatViewModel.h"
#include "presentation/viewmodels/PresentacionViewModels.h"
#include "presentation/viewmodels/RespaldoViewModel.h"
#include "presentation/viewmodels/VisorPdfCfdiViewModel.h"
#include "presentation/estilo/EstiloVisual.h"

#include <QAbstractAnimation>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QThread>
#include <QTimeZone>

#include <cstdio>
#include <ctime>
#include <functional>
#include <memory>

#ifndef SATCFDI_CAPTURAS_QML_DIR
#error "SATCFDI_CAPTURAS_QML_DIR debe definirse en CMake"
#endif
#ifndef SATCFDI_CAPTURAS_MANIFIESTO
#error "SATCFDI_CAPTURAS_MANIFIESTO debe definirse en CMake"
#endif

using namespace satcfdi;
using namespace capturas;

namespace {

// --- Entorno fijo (debe coincidir con "entorno" de escenarios.json) ---------

constexpr int kAncho = 960;
constexpr int kAlto = 640;
constexpr int kEscala = 2;
constexpr qint64 kPesoMaximo = 1024 * 1024; // PNG versionable: maximo 1 MB
constexpr int kPasoAnimacionMs = 16;

// T013 D9: --tema oscuro fija Theme.oscuro = true en cada engine de la app
// (las ilustraciones imitan la interfaz de macOS en claro y no cambian).
bool gTemaOscuro = false;

// Reloj de animaciones virtual (T024.2): QAbstractAnimation, animaciones y
// Timer de QML avanzan solo con avanzar(); el estado de un spinner o de una
// transicion al capturar depende del numero de pasos, no del tiempo real.
class RelojAnimaciones final : public QAnimationDriver {
public:
    qint64 elapsed() const override { return m_ms; }
    void avanzar(int ms)
    {
        m_ms += ms;
        advance();
    }

private:
    qint64 m_ms = 0;
};

RelojAnimaciones* gReloj = nullptr;

// --- Datos sinteticos del apendice Diagnostico (D7) -------------------------


const QString kRfcPerfil = QStringLiteral("XAXX010101000");
const QString kRfcOtro = QStringLiteral("XEXX010101000");
// T024.2: un solo reloj para todas las capturas (el de los fakes comunes).
const QDateTime kAhora = materiales::datosFacturasDeEjemplo().ahoraUtc;

PerfilId perfilId(char c) { return materiales::perfilIdFijo(c); }

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
const QDateTime kVenceEFirma = kAhora.addDays(21);
constexpr int kDiasParaVencer = 21;
constexpr int kUmbralVencimiento = 30;

ResumenCredencial credencialPorVencer()
{
    ResumenCredencial r = credencialLista();
    r.vigenteDesde = kVenceEFirma.addYears(-4);
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

// Paso determinista: procesa eventos y avanza el reloj virtual `ms` en pasos
// de 16 ms. Los QTimer de C++ (tiempo real) tambien tienen ocasion de correr.
void esperar(int ms)
{
    for (int t = 0; t < ms; t += kPasoAnimacionMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (gReloj) {
            gReloj->avanzar(kPasoAnimacionMs);
        }
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

// Espera una condicion SIN avanzar el reloj virtual (trabajo en otros hilos,
// p. ej. QtPdf). false si no se cumplio en `ms` de tiempo real.
bool esperarCondicion(const std::function<bool()>& condicion, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!condicion()) {
        if (t.elapsed() > ms) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    return true;
}

// Frame estable: deja correr bindings y animaciones (reloj virtual).
void esperarFrameEstable(QQuickWindow* ventana)
{
    esperar(800);
    ventana->update();
    esperar(64);
}

// Raiz del arbol visual de la ventana (incluye el Overlay de popups y dialogos).
QQuickItem* raizVisual(QQuickWindow* ventana)
{
    QQuickItem* raiz = ventana->contentItem();
    while (raiz && raiz->parentItem()) {
        raiz = raiz->parentItem();
    }
    return raiz;
}

void agregarTexto(const QVariant& v, QStringList& textos)
{
    if (v.metaType().id() == QMetaType::QString && !v.toString().trimmed().isEmpty()) {
        textos.append(v.toString());
    }
}

// Textos visibles del arbol de items, en orden de recorrido: text,
// placeholderText y title, y los textos accesibles (Accessible.name y
// Accessible.description, que VoiceOver lee aunque no se dibujen). Para el
// escaneo de datos reales y las aserciones del manifiesto. El objeto adjunto
// Accessible es hijo QObject del item (QQuickAccessibleAttached); se lee por
// sus propiedades, sin cabeceras privadas.
void recolectarTextos(QQuickItem* item, QStringList& textos)
{
    if (!item || !item->isVisible() || item->opacity() <= 0.0) {
        return;
    }
    for (const char* propiedad : {"text", "placeholderText", "title"}) {
        agregarTexto(item->property(propiedad), textos);
    }
    for (QObject* adjunto : item->children()) {
        if (qstrcmp(adjunto->metaObject()->className(), "QQuickAccessibleAttached") == 0
            && !adjunto->property("ignored").toBool()) {
            agregarTexto(adjunto->property("name"), textos);
            agregarTexto(adjunto->property("description"), textos);
        }
    }
    for (QQuickItem* hijo : item->childItems()) {
        recolectarTextos(hijo, textos);
    }
}

QStringList textosVisibles(QQuickWindow* ventana)
{
    QStringList textos;
    recolectarTextos(raizVisual(ventana), textos);
    return textos;
}

QString gDirTextos;

bool volcarTextos(QQuickWindow* ventana, const QString& rutaPng)
{
    if (gDirTextos.isEmpty()) {
        return true;
    }
    const QStringList textos = textosVisibles(ventana);
    QFile f(QDir(gDirTextos).filePath(QFileInfo(rutaPng).fileName() + QStringLiteral(".txt")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    f.write(textos.join(QLatin1Char('\n')).toUtf8());
    f.write("\n");
    return true;
}

// --- Aserciones semanticas (T024.2) -----------------------------------------
//
// El SHA-256 de dos corridas prueba que el cuadro se repite, no que sea el
// correcto: un estado visual equivocado pero estable pasaria. Cada escenario
// del manifiesto declara "aserciones" que se comprueban sobre el cuadro
// estable, justo antes de capturarlo; si una falla, la captura falla.
// - visibles: objectName con al menos un item efectivamente visible
//   (visible en toda la cadena y opacidad > 0).
// - ocultos: objectName sin ningun item efectivamente visible (p. ej. los
//   indicadores de carga que un Timer de QML muestra tras 300 ms).
// - textos: lineas exactas que deben estar entre los textos visibles o
//   accesibles (los mismos del volcado --textos).
// - propiedades: {objeto, propiedad, valor} sobre el primer item visible con
//   ese objectName (p. ej. Image.status == Image.Ready).
// - dibujados: objectName cuyo rectangulo, en la MISMA imagen que se guarda,
//   no es de un solo color (p. ej. la pagina del PDF: PdfPageImage asincrono
//   puede estar Ready con la pagina aun en blanco mientras re-rasteriza al
//   tamano final).

struct PropiedadEsperada {
    QString objeto;
    QString propiedad;
    QJsonValue valor;
};

struct Aserciones {
    QStringList visibles;
    QStringList ocultos;
    QStringList textos;
    QList<PropiedadEsperada> propiedades;
    QStringList dibujados;
};

const Aserciones* gAserciones = nullptr;

bool efectivamenteVisible(const QQuickItem* item)
{
    for (const QQuickItem* i = item; i; i = i->parentItem()) {
        if (!i->isVisible() || i->opacity() <= 0.0) {
            return false;
        }
    }
    return true;
}

void buscarVisibles(QQuickItem* raiz, const QString& nombre, QList<QQuickItem*>& r)
{
    if (!raiz) {
        return;
    }
    if (raiz->objectName() == nombre && efectivamenteVisible(raiz)) {
        r.append(raiz);
    }
    for (QQuickItem* hijo : raiz->childItems()) {
        buscarVisibles(hijo, nombre, r);
    }
}

QList<QQuickItem*> visiblesCon(QQuickWindow* ventana, const QString& nombre)
{
    QList<QQuickItem*> r;
    buscarVisibles(raizVisual(ventana), nombre, r);
    return r;
}

// El rectangulo del item en `imagen` (captura a escala kEscala) tiene contenido:
// al menos 0.5 % de sus pixeles difiere del de la esquina superior izquierda.
bool regionDibujada(const QImage& imagen, const QQuickItem* item)
{
    const QRectF escena = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
    const QRect r = QRectF(escena.topLeft() * kEscala, escena.size() * kEscala).toAlignedRect().intersected(imagen.rect());
    if (r.width() < 4 || r.height() < 4) {
        return false;
    }
    const QRgb fondo = imagen.pixel(r.topLeft());
    qint64 distintos = 0;
    qint64 total = 0;
    for (int y = r.top(); y <= r.bottom(); y += 2) {
        for (int x = r.left(); x <= r.right(); x += 2) {
            ++total;
            distintos += imagen.pixel(x, y) != fondo ? 1 : 0;
        }
    }
    return distintos * 200 >= total;
}

// Numero (tambien enums de QML, p. ej. Image.Ready = 1), booleano o texto.
bool coincide(const QVariant& v, const QJsonValue& esperado)
{
    if (!v.isValid()) {
        return false;
    }
    bool ok = false;
    if (esperado.isBool()) {
        return v.metaType().id() == QMetaType::Bool && v.toBool() == esperado.toBool();
    }
    if (esperado.isDouble()) {
        const double d = v.toDouble(&ok);
        return ok && d == esperado.toDouble();
    }
    return esperado.isString() && v.toString() == esperado.toString();
}

bool comprobarAserciones(QQuickWindow* ventana, const QString& ruta)
{
    if (!gAserciones) {
        std::fprintf(stderr, "error: %s sin aserciones\n", qPrintable(ruta));
        return false;
    }
    const QString archivo = QFileInfo(ruta).fileName();
    QStringList fallas;
    for (const QString& n : gAserciones->visibles) {
        if (visiblesCon(ventana, n).isEmpty()) {
            fallas.append(QStringLiteral("'%1' no esta visible").arg(n));
        }
    }
    for (const QString& n : gAserciones->ocultos) {
        if (!visiblesCon(ventana, n).isEmpty()) {
            fallas.append(QStringLiteral("'%1' esta visible").arg(n));
        }
    }
    const QStringList textos = textosVisibles(ventana);
    for (const QString& t : gAserciones->textos) {
        if (!textos.contains(t)) {
            fallas.append(QStringLiteral("falta el texto \"%1\"").arg(t));
        }
    }
    for (const PropiedadEsperada& p : gAserciones->propiedades) {
        const QList<QQuickItem*> items = visiblesCon(ventana, p.objeto);
        const QVariant v = items.isEmpty() ? QVariant() : items.constFirst()->property(p.propiedad.toUtf8().constData());
        if (!coincide(v, p.valor)) {
            fallas.append(QStringLiteral("%1.%2 = %3, se esperaba %4")
                              .arg(p.objeto, p.propiedad, v.isValid() ? v.toString() : QStringLiteral("(sin item o propiedad)"),
                                   p.valor.toVariant().toString()));
        }
    }
    for (const QString& f : fallas) {
        std::fprintf(stderr, "%s: asercion fallida: %s\n", qPrintable(archivo), qPrintable(f));
    }
    return fallas.isEmpty();
}

// Captura normalizada a escala 2 sin metadatos variables (D4).
bool guardar(QQuickWindow* ventana, const QString& ruta)
{
    if (!comprobarAserciones(ventana, ruta)) {
        return false;
    }
    QImage imagen = ventana->grabWindow();
    if (imagen.isNull()) {
        return false;
    }
    for (const QString& n : gAserciones->dibujados) {
        const QList<QQuickItem*> items = visiblesCon(ventana, n);
        if (items.isEmpty() || !regionDibujada(imagen, items.constFirst())) {
            std::fprintf(stderr, "%s: asercion fallida: '%s' esta en blanco en el cuadro capturado\n",
                         qPrintable(QFileInfo(ruta).fileName()), qPrintable(n));
            return false;
        }
    }
    imagen = imagen.convertToFormat(QImage::Format_RGB32);
    const QSize esperado = ventana->size() * kEscala;
    if (imagen.size() != esperado) {
        std::fprintf(stderr, "error: captura de %dx%d, se esperaba %dx%d (escala %d)\n", imagen.width(),
                     imagen.height(), esperado.width(), esperado.height(), kEscala);
        return false;
    }
    imagen.setDevicePixelRatio(1.0);
    imagen.setDotsPerMeterX(5669); // 144 dpi fijo
    imagen.setDotsPerMeterY(5669);
    // PNG sin perdida con compresion zlib maxima: en el handler PNG de Qt,
    // quality 0 equivale a nivel 9 (setCompression no lo controla).
    QImageWriter escritor(ruta, "png");
    escritor.setQuality(0);
    return escritor.write(imagen) && volcarTextos(ventana, ruta);
}

// --- Escenarios con la app real (Captura) -----------------------------------

struct Grafo {
    // Apendice Diagnostico (ServiciosFijos.h).
    SolicitudesFijas solicitudes;
    PerfilesFijos perfiles;
    CredencialesFijas credenciales;
    AccionesNulas acciones;
    ExistenciaFija existencia;
    CatalogoFijo catalogo;
    PrevisualizacionFija previsualizacion; // T015.4
    // T024.2 (D9): fakes comunes de Facturas, Historial, Exportacion y Respaldo.
    materiales::FuenteFacturasSintetica facturas;
    materiales::PreferenciasFacturasMemoria preferencias;
    materiales::HistorialSintetico historial;
    materiales::ControlRespaldoSintetico respaldo;
    materiales::ControlRestauracionSintetico restauracion;
    materiales::DatosPreviosSinteticos datosPrevios;
    std::unique_ptr<materiales::ExportacionSintetica> exportacion;
    std::unique_ptr<PresentacionViewModels> vms;
    std::unique_ptr<QQmlApplicationEngine> engine;

    Grafo()
    {
        perfiles.perfiles = {perfilListo(), perfilSinEFirma()};
        credenciales.resumenes.insert(perfilListo().id.texto(), credencialLista());
        historial.entradas.insert(perfilListo().id.texto(), materiales::historialDeEjemplo(perfilListo().id));
        exportacion = std::make_unique<materiales::ExportacionSintetica>(facturas);
    }

    ~Grafo()
    {
        engine.reset();
        if (vms) {
            vms->setExportacionFacturas(nullptr);
        }
        vms.reset();
        exportacion.reset(); // une el hilo del servicio
    }

    QQuickWindow* cargar(QSize tamano)
    {
        // Reloj fijo (T016): el aviso "Vence en N días" se calcula con kAhora.
        vms = std::make_unique<PresentacionViewModels>(&solicitudes, &perfiles, &credenciales, nullptr,
                                                       [] { return kAhora; });
        vms->setAccionesSolicitud(&acciones);
        vms->setAccionesFinder(&acciones);
        vms->setConsultaExistencia(&existencia);
        vms->setConsultaCatalogo(&catalogo);
        vms->setPrevisualizacionCfdi(&previsualizacion);
        // Como AppCompositionRoot: Facturas (pagina inicial), exportacion,
        // Historial y Respaldo antes de cargar().
        vms->setFuenteFacturas(&facturas, &preferencias);
        vms->facturas()->setIntervaloBusqueda(0);
        vms->setExportacionFacturas(exportacion->servicio(), [] { return kAhora; });
        vms->setFuenteHistorial(&historial);
        vms->setControlRespaldo(&respaldo);
        vms->setControlRestauracion(&restauracion);
        vms->setDatosPreviosRestauracion(&datosPrevios);
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
        ventana->resize(tamano);
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

    FacturasViewModel* vmFacturas() const { return vms->facturas(); }
    const PerfilId& perfilPrincipal() const { return facturas.datos().perfiles.constFirst().id; }

    // Facturas en `mes` de 2026 con el estado declarado.
    bool mesConEstado(int mes, const materiales::MesDeclarado& declarado)
    {
        facturas.fijarMes(perfilPrincipal(), 2026, mes, declarado);
        esperar(200);
        if (!vmFacturas()->seleccionarMes(2026, mes)) {
            esperar(200);
        }
        esperar(400);
        return vmFacturas()->anio() == 2026 && vmFacturas()->mes() == mes;
    }
};

using Preparar = std::function<bool(Grafo&, QQuickWindow*)>;

bool capturaApp(const QString& ruta, QSize tamano, const Preparar& preparar)
{
    Grafo g;
    QQuickWindow* ventana = g.cargar(tamano);
    if (!ventana) {
        return false;
    }
    esperar(400);
    if (!preparar(g, ventana)) {
        return false;
    }
    esperarFrameEstable(ventana);
    const bool ok = guardar(ventana, ruta);
    ventana->hide();
    return ok;
}

// --- Escenarios de ilustracion ----------------------------------------------

bool ilustracion(const QString& ruta, QSize tamano, const QString& componente, const QVariantMap& propiedades)
{
    QQmlEngine engine;
    QQmlComponent comp(&engine);
    const QByteArray qml = QStringLiteral(
                               "import QtQuick\n"
                               "Rectangle {\n"
                               "  width: %1; height: %2; color: \"#e5e5ea\"\n"
                               "  %3 { objectName: \"ilustracion\"; anchors.centerIn: parent }\n"
                               "}\n")
                               .arg(tamano.width())
                               .arg(tamano.height())
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
    ventana.resize(tamano);
    raiz->setParentItem(ventana.contentItem());
    ventana.show();
    esperarFrameEstable(&ventana);
    const bool ok = guardar(&ventana, ruta);
    ventana.hide();
    raiz->setParentItem(nullptr);
    return ok;
}

// --- Datos de las ilustraciones -------------------------------------------

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


// --- Escenarios ---------------------------------------------------------------

// Generador por clave de escenario del manifiesto.
using Generador = std::function<bool(const QString& ruta, QSize tamano)>;

materiales::MesDeclarado mesDeclarado(materiales::EstadoMesSintetico estado, int emitidas, int recibidas)
{
    materiales::MesDeclarado m;
    m.estado = estado;
    m.emitidas = emitidas;
    m.recibidas = recibidas;
    return m;
}

// Facturas de septiembre 2026 en el estado indicado; valida el estado de la franja.
Generador franja(materiales::EstadoMesSintetico estado, int emitidas, int recibidas, FranjaMesViewModel::Estado esperado)
{
    return [=](const QString& r, QSize t) {
        return capturaApp(r, t, [=](Grafo& g, QQuickWindow*) {
            return g.mesConEstado(9, mesDeclarado(estado, emitidas, recibidas))
                   && g.vmFacturas()->franja()->estado() == esperado;
        });
    };
}

QHash<QString, Generador> generadores()
{
    using E = materiales::EstadoMesSintetico;
    using F = FranjaMesViewModel::Estado;
    const QString carpetaSolicitud =
        QStringLiteral("/Users/usuario/Archivo-Fiscal/paquetes/%1/2026-09/%2").arg(kRfcPerfil, solicitudId(3).texto());
    QHash<QString, Generador> g;

    // ---- Facturas (T024.3: secciones nuevas) ----
    g.insert(QStringLiteral("facturas-mes-disponibles"), [](const QString& r, QSize t) {
        return capturaApp(r, t, [](Grafo& g, QQuickWindow*) {
            return g.vmFacturas()->seleccionarMes(2026, 9) && (esperar(400), true)
                   && g.vmFacturas()->franja()->estado() == FranjaMesViewModel::Estado::Disponibles
                   && g.vmFacturas()->vista() == QStringLiteral("lista");
        });
    });
    g.insert(QStringLiteral("franja-parcial"), franja(E::DisponiblesParcialmente, 18, 0, F::DisponiblesParcialmente));
    g.insert(QStringLiteral("franja-preparando"), franja(E::Preparando, 0, 0, F::Preparando));
    g.insert(QStringLiteral("franja-atencion"), franja(E::RequiereAtencion, 15, 0, F::RequiereAtencion));
    g.insert(QStringLiteral("franja-sin-descargar"), franja(E::SinDescargar, 0, 0, F::SinDescargar));
    g.insert(QStringLiteral("confirmacion-direccion"), [](const QString& r, QSize t) {
        return capturaApp(r, t, [](Grafo& g, QQuickWindow*) {
            g.facturas.fijarPreplan(materiales::PreplanDeclarado{{TipoDescarga::Recibidos}});
            if (!g.mesConEstado(9, mesDeclarado(E::RequiereAtencion, 15, 0))) {
                return false;
            }
            g.vmFacturas()->ejecutarAccion();
            esperar(400);
            return g.vmFacturas()->confirmacionPendiente();
        });
    });
    g.insert(QStringLiteral("busqueda-activa"), [](const QString& r, QSize t) {
        return capturaApp(r, t, [](Grafo& g, QQuickWindow*) {
            if (!g.vmFacturas()->seleccionarMes(2026, 9)) {
                return false;
            }
            esperar(300);
            g.vmFacturas()->setTexto(QStringLiteral("Papelería"));
            esperar(400);
            return g.vmFacturas()->hayFiltros() && g.vmFacturas()->vista() == QStringLiteral("lista");
        });
    });
    g.insert(QStringLiteral("layout-angosto"), [](const QString& r, QSize t) {
        return capturaApp(r, t, [](Grafo& g, QQuickWindow* v) {
            if (!g.vmFacturas()->seleccionarMes(2026, 9)) {
                return false;
            }
            esperar(300);
            QQuickItem* p = g.pagina(v, QStringLiteral("paginaFacturas"));
            return p && p->property("angosto").toBool();
        });
    });
    g.insert(QStringLiteral("visor-pdf-facturas"), [](const QString& r, QSize t) {
        QTemporaryDir dir;
        const QString pdf = dir.isValid() ? pdfFijo(dir.path()) : QString();
        if (pdf.isEmpty()) {
            std::fprintf(stderr, "visor-pdf-facturas: no se pudo generar el PDF sintetico\n");
            return false;
        }
        return capturaApp(r, t, [pdf](Grafo& g, QQuickWindow* v) {
            g.previsualizacion.rutaPdf = pdf;
            if (!g.vmFacturas()->seleccionarMes(2026, 9)) {
                return false;
            }
            esperar(300);
            if (!g.vmFacturas()->previsualizar(0)) {
                std::fprintf(stderr, "visor-pdf-facturas: no se pudo previsualizar\n");
                return false;
            }
            // QtQuick.Pdf carga y renderiza la pagina en otro hilo: se espera
            // la condicion sin avanzar el reloj virtual.
            auto listo = [&g, v] {
                QQuickItem* visor = g.pagina(v, QStringLiteral("visorPdfCfdi"));
                return visor && visor->property("documentoListo").toBool();
            };
            esperar(200);
            if (!esperarCondicion(listo, 10000)) {
                return false;
            }
            esperar(200);
            // Imagen de la pagina rasterizada (asincrona).
            auto pagina = [&g, v] {
                QQuickItem* img = g.pagina(v, QStringLiteral("imagenPaginaPdf_0"));
                return img && img->property("status").toInt() == 1; // Image.Ready
            };
            if (!esperarCondicion(pagina, 10000)) {
                std::fprintf(stderr, "visor-pdf-facturas: la pagina del PDF no se rasterizo en 10 s\n");
                return false;
            }
            // Ready no basta: PdfPageImage (asincrono, sourceSize segun el
            // ancho) puede re-rasterizar y mostrar la pagina en blanco. Se
            // espera, sin avanzar el reloj, a que el cuadro muestre la pagina
            // dibujada, y de nuevo tras asentar animaciones.
            auto dibujada = [&g, v] {
                QQuickItem* img = g.pagina(v, QStringLiteral("imagenPaginaPdf_0"));
                return img && img->property("status").toInt() == 1 && regionDibujada(v->grabWindow(), img);
            };
            if (!esperarCondicion(dibujada, 10000)) {
                std::fprintf(stderr, "visor-pdf-facturas: la pagina del PDF sigue en blanco tras 10 s\n");
                return false;
            }
            esperarFrameEstable(v);
            if (!esperarCondicion(dibujada, 10000)) {
                std::fprintf(stderr, "visor-pdf-facturas: la pagina del PDF quedo en blanco tras asentar\n");
                return false;
            }
            return g.vmFacturas()->visor()->estado() == VisorPdfCfdiViewModel::Estado::Listo;
        });
    });
    g.insert(QStringLiteral("historial"), [](const QString& r, QSize t) {
        return capturaApp(r, t, [](Grafo& g, QQuickWindow*) {
            g.vms->app()->mostrarHistorial();
            esperar(500);
            return g.vms->historial()->vista() == QStringLiteral("lista");
        });
    });
    g.insert(QStringLiteral("dialogo-exportar"), [](const QString& r, QSize t) {
        return capturaApp(r, t, [](Grafo& g, QQuickWindow*) {
            if (!g.vmFacturas()->seleccionarMes(2026, 9)) {
                return false;
            }
            esperar(300);
            ExportacionFacturasViewModel* e = g.vms->exportacionFacturas();
            e->abrirMesActual();
            esperar(300);
            e->elegirDestino(QUrl::fromLocalFile(QStringLiteral("/Users/usuario/Documentos")));
            esperar(300);
            return e->estado() == ExportacionFacturasViewModel::Estado::Dialogo && e->conteoPrevisto() > 0
                   && e->puedeIniciar();
        });
    });
    g.insert(QStringLiteral("respaldo"), [](const QString& r, QSize t) {
        return capturaApp(r, t, [](Grafo& g, QQuickWindow* v) {
            g.vms->app()->mostrarRespaldo();
            esperar(500);
            return g.pagina(v, QStringLiteral("paginaRespaldo")) != nullptr;
        });
    });

    // ---- Perfiles (rehechas) ----
    g.insert(QStringLiteral("perfiles"), [](const QString& r, QSize t) {
        return capturaApp(r, t, [](Grafo& g, QQuickWindow*) {
            // T016: la e.firma del perfil seleccionado vence en 21 dias.
            g.credenciales.resumenes.insert(perfilListo().id.texto(), credencialPorVencer());
            g.vms->app()->mostrarPerfiles();
            esperar(300);
            return g.vms->perfiles()->seleccionar(perfilListo().id.texto())
                   && g.vms->perfiles()->seleccionDiasParaVencer() == kDiasParaVencer;
        });
    });
    g.insert(QStringLiteral("efirma"), [](const QString& r, QSize t) {
        return capturaApp(r, t, [](Grafo& g, QQuickWindow* v) {
            g.vms->app()->mostrarPerfiles();
            esperar(300);
            if (!g.vms->perfiles()->seleccionar(perfilSinEFirma().id.texto())) {
                std::fprintf(stderr, "efirma: no se pudo seleccionar el perfil\n");
                return false;
            }
            esperar(300);
            QQuickItem* pagina = g.pagina(v, QStringLiteral("paginaPerfilesSat"));
            if (!pagina || !QMetaObject::invokeMethod(pagina, "gestionarEFirma")) {
                std::fprintf(stderr, "efirma: gestionarEFirma no se pudo invocar\n");
                return false;
            }
            g.vms->eFirma()->seleccionarCertificado(
                QUrl::fromLocalFile(QStringLiteral("/Users/usuario/Documentos/efirma/demo.cer")));
            g.vms->eFirma()->seleccionarLlave(
                QUrl::fromLocalFile(QStringLiteral("/Users/usuario/Documentos/efirma/demo.key")));
            return true;
        });
    });
    g.insert(QStringLiteral("selector-archivos"), [](const QString& r, QSize t) {
        return ilustracion(r, t, QStringLiteral("IlustracionSelectorArchivos"),
                           {{QStringLiteral("carpeta"), QStringLiteral("/Users/usuario/Documentos/efirma")},
                            {QStringLiteral("archivos"), QStringList{QStringLiteral("demo.cer"), QStringLiteral("demo.key")}},
                            {QStringLiteral("seleccionado"), QStringLiteral("demo.cer")}});
    });

    // ---- Apendice Diagnostico ----
    g.insert(QStringLiteral("diagnostico-detalle-terminada"), [](const QString& r, QSize t) {
        return capturaApp(r, t, [](Grafo& g, QQuickWindow* v) {
            g.solicitudes.detalles = {solicitudTerminada()};
            if (!g.vms->app()->abrirDetalle(solicitudId(3).texto())) {
                return false;
            }
            esperar(500);
            return g.mostrarPestana(v, QStringLiteral("paquetes"));
        });
    });
    g.insert(QStringLiteral("diagnostico-detalle-incidencia"), [](const QString& r, QSize t) {
        return capturaApp(r, t, [](Grafo& g, QQuickWindow* v) {
            g.solicitudes.detalles = {solicitudConIncidencia()};
            g.existencia.porPaquete.insert(idPaquete(5, 2), ExistenciaPaquete::NoEncontrado);
            if (!g.vms->app()->abrirDetalle(solicitudId(5).texto())) {
                return false;
            }
            esperar(500);
            return g.mostrarPestana(v, QStringLiteral("paquetes"));
        });
    });
    g.insert(QStringLiteral("diagnostico-finder"), [carpetaSolicitud](const QString& r, QSize t) {
        const QString zip = idPaquete(3, 1) + QStringLiteral(".zip");
        return ilustracion(r, t, QStringLiteral("IlustracionFinder"),
                           {{QStringLiteral("carpeta"), carpetaSolicitud},
                            {QStringLiteral("archivos"),
                             QVariantList{QVariantMap{{QStringLiteral("nombre"), zip},
                                                      {QStringLiteral("tamano"), QStringLiteral("48 KB")},
                                                      {QStringLiteral("fecha"), QStringLiteral("15 oct 2026, 12:45")}}}},
                            {QStringLiteral("seleccionado"), zip}});
    });

    // ---- Menu bar y notificaciones ----
    g.insert(QStringLiteral("menu-bar"), [](const QString& r, QSize t) {
        // T016: el icono de la barra es el dibujo real (variante Normal).
        QTemporaryDir dir;
        const QVariantMap normal = dir.isValid() ? varianteIcono(dir.path(), OSIntegration::EstadoIcono::Normal,
                                                                 QStringLiteral("normal"))
                                                 : QVariantMap{};
        if (normal.isEmpty()) {
            std::fprintf(stderr, "menu-bar: no se pudo dibujar el icono\n");
            return false;
        }
        return ilustracion(r, t, QStringLiteral("IlustracionMenuBar"),
                           {{QStringLiteral("entradas"), entradasMenu()},
                            {QStringLiteral("icono"), normal.value(QStringLiteral("imagen"))}});
    });
    g.insert(QStringLiteral("notificacion"), [](const QString& r, QSize t) {
        // T016 (T014.4 D1): con "Opciones" abierto: la notificacion de la
        // descarga y "e.firma por vencer" (T014.3) con Abrir Perfiles SAT.
        const auto n = notificacionDescargaCompleta();
        if (!n) {
            std::fprintf(stderr, "notificacion: ServicioNotificaciones no notifico\n");
            return false;
        }
        const Notificacion vence = ServicioNotificaciones::componerVencimiento(
            perfilListo().id, kRfcPerfil, kVenceEFirma, kUmbralVencimiento, kDiasParaVencer);
        return ilustracion(r, t, QStringLiteral("IlustracionNotificacion"),
                           {{QStringLiteral("notificaciones"),
                             QVariantList{notificacionIlustrada(*n), notificacionIlustrada(vence)}}});
    });
    g.insert(QStringLiteral("icono-menu-bar"), [](const QString& r, QSize t) {
        QTemporaryDir dir;
        const QVariantList variantes = dir.isValid() ? variantesIcono(dir.path()) : QVariantList{};
        if (variantes.size() != 4) {
            std::fprintf(stderr, "icono-menu-bar: no se pudieron dibujar las variantes\n");
            return false;
        }
        return ilustracion(r, t, QStringLiteral("IlustracionIconoMenuBar"), {{QStringLiteral("variantes"), variantes}});
    });
    return g;
}

// --- Manifiesto ---------------------------------------------------------------

struct Escenario {
    QString archivo;
    QString clave;
    QString tipo; // Captura | Ilustracion
    QSize tamano;
    Aserciones aserciones;
};

// "aserciones" de un escenario: obligatorias, con al menos un visible y un
// texto; solo claves conocidas.
std::optional<Aserciones> leerAserciones(const QJsonObject& o)
{
    const QStringList claves{QStringLiteral("visibles"), QStringLiteral("ocultos"), QStringLiteral("textos"),
                             QStringLiteral("propiedades"), QStringLiteral("dibujados")};
    for (const QString& k : o.keys()) {
        if (!claves.contains(k)) {
            return std::nullopt;
        }
    }
    auto lista = [&o](const QString& clave, QStringList& destino) {
        for (const QJsonValue& v : o.value(clave).toArray()) {
            if (!v.isString() || v.toString().isEmpty()) {
                return false;
            }
            destino.append(v.toString());
        }
        return true;
    };
    Aserciones a;
    if (!lista(QStringLiteral("visibles"), a.visibles) || !lista(QStringLiteral("ocultos"), a.ocultos)
        || !lista(QStringLiteral("textos"), a.textos) || !lista(QStringLiteral("dibujados"), a.dibujados)
        || a.visibles.isEmpty() || a.textos.isEmpty()) {
        return std::nullopt;
    }
    for (const QJsonValue& v : o.value(QStringLiteral("propiedades")).toArray()) {
        const QJsonObject p = v.toObject();
        PropiedadEsperada e{p.value(QStringLiteral("objeto")).toString(), p.value(QStringLiteral("propiedad")).toString(),
                            p.value(QStringLiteral("valor"))};
        if (e.objeto.isEmpty() || e.propiedad.isEmpty() || e.valor.isUndefined() || p.size() != 3) {
            return std::nullopt;
        }
        a.propiedades.append(e);
    }
    return a;
}

// Fuentes (T024.2): la app usa la del sistema (Theme.familia =
// Application.font.family) y, para codigos, SF Mono o Menlo (Theme.familiaMono).
// La herramienta no las cambia (seria otra tipografia que la de la app); el
// manifiesto declara la familia efectiva observada y la herramienta falla si
// la de esta maquina no coincide: "familia pedida -> familia resuelta".
QString familiaResuelta(const QFont& f) { return f.family() + QStringLiteral(" -> ") + QFontInfo(f).family(); }

QString familiaFuente() { return familiaResuelta(QGuiApplication::font()); }

QString familiaMono()
{
    const QString pedida =
        QFontDatabase::families().contains(QStringLiteral("SF Mono")) ? QStringLiteral("SF Mono") : QStringLiteral("Menlo");
    return familiaResuelta(QFont(pedida));
}

// Entorno que la herramienta fija o verifica; debe coincidir con "entorno" del manifiesto.
QJsonObject entornoFijo()
{
    return QJsonObject{
        {QStringLiteral("plataforma"), QStringLiteral("offscreen")},
        {QStringLiteral("backendQtQuick"), QStringLiteral("software")},
        {QStringLiteral("renderLoop"), QStringLiteral("basic")},
        {QStringLiteral("escala"), kEscala},
        {QStringLiteral("ancho"), kAncho},
        {QStringLiteral("alto"), kAlto},
        {QStringLiteral("tema"), QStringLiteral("claro")},
        {QStringLiteral("locale"), QStringLiteral("es_MX")},
        {QStringLiteral("zonaHoraria"), QStringLiteral("UTC")},
        {QStringLiteral("relojUtc"), kAhora.toString(Qt::ISODate)},
        {QStringLiteral("pasoAnimacionMs"), kPasoAnimacionMs},
        {QStringLiteral("parpadeoCursorMs"), 0},
        {QStringLiteral("fuente"), familiaFuente()},
        {QStringLiteral("fuenteMono"), familiaMono()},
    };
}

std::optional<QList<Escenario>> leerManifiesto(const QString& ruta, const QHash<QString, Generador>& gen)
{
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "error: no se pudo leer el manifiesto %s\n", qPrintable(ruta));
        return std::nullopt;
    }
    QJsonParseError e;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &e);
    if (!doc.isObject()) {
        std::fprintf(stderr, "error: manifiesto invalido: %s\n", qPrintable(e.errorString()));
        return std::nullopt;
    }
    const QJsonObject raiz = doc.object();
    if (raiz.value(QStringLiteral("entorno")).toObject() != entornoFijo()) {
        std::fprintf(stderr, "error: el entorno del manifiesto no coincide con el de la herramienta:\n%s\n",
                     QJsonDocument(entornoFijo()).toJson().constData());
        return std::nullopt;
    }
    QList<Escenario> r;
    QSet<QString> archivos;
    QSet<QString> claves;
    for (const QJsonValue& v : raiz.value(QStringLiteral("escenarios")).toArray()) {
        const QJsonObject o = v.toObject();
        Escenario s;
        s.archivo = o.value(QStringLiteral("archivo")).toString();
        s.clave = o.value(QStringLiteral("escenario")).toString();
        s.tipo = o.value(QStringLiteral("tipo")).toString();
        s.tamano = QSize(o.value(QStringLiteral("ancho")).toInt(kAncho), o.value(QStringLiteral("alto")).toInt(kAlto));
        const auto aserciones = leerAserciones(o.value(QStringLiteral("aserciones")).toObject());
        if (!aserciones) {
            std::fprintf(stderr, "error: %s: \"aserciones\" falta o es invalida (visibles y textos obligatorios)\n",
                         qPrintable(s.archivo));
            return std::nullopt;
        }
        s.aserciones = *aserciones;
        if (!s.archivo.endsWith(QStringLiteral(".png")) || !gen.contains(s.clave) || archivos.contains(s.archivo)
            || claves.contains(s.clave)
            || (s.tipo != QLatin1String("Captura") && s.tipo != QLatin1String("Ilustracion"))) {
            std::fprintf(stderr, "error: entrada invalida o repetida en el manifiesto: %s -> %s\n",
                         qPrintable(s.archivo), qPrintable(s.clave));
            return std::nullopt;
        }
        archivos.insert(s.archivo);
        claves.insert(s.clave);
        r.append(s);
    }
    for (auto it = gen.cbegin(); it != gen.cend(); ++it) {
        if (!claves.contains(it.key())) {
            std::fprintf(stderr, "error: el escenario '%s' no esta en el manifiesto\n", qPrintable(it.key()));
            return std::nullopt;
        }
    }
    return r;
}

} // namespace

int main(int argc, char* argv[])
{
    // T024.2: entorno fijo antes de crear la app; no depende de la sesion
    // grafica ni del entorno del usuario.
    qputenv("TZ", "UTC");
    ::tzset();
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QSG_RENDER_LOOP", "basic");
    qputenv("QT_SCALE_FACTOR", QByteArray::number(kEscala));
    qunsetenv("QT_SCREEN_SCALE_FACTORS");
    qunsetenv("QT_FONT_DPI");
    qunsetenv("QT_QUICK_CONTROLS_STYLE");
    QApplication app(argc, argv);
    RelojAnimaciones reloj;
    reloj.install();
    gReloj = &reloj;
    satcfdi::presentacion::fijarEstiloBasico(); // T013 D1, igual que la app
    QCoreApplication::setOrganizationName(QStringLiteral("Adenium"));
    // T019: nombre interno propio de la herramienta (no resuelve rutas de datos
    // de la app); lo visible usa el nombre de la app, como satcfdi_app (D2).
    QCoreApplication::setApplicationName(QStringLiteral("Archivo Fiscal Capturas Manual"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Archivo Fiscal"));
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Mexico));
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    QGuiApplication::styleHints()->setCursorFlashTime(0); // cursor sin parpadeo

    QString salida;
    QString solo;
    QString rutaManifiesto = QStringLiteral(SATCFDI_CAPTURAS_MANIFIESTO);
    const QStringList args = QCoreApplication::arguments();
    for (qsizetype i = 1; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--salida") && i + 1 < args.size()) {
            salida = args.at(++i);
        } else if (args.at(i) == QLatin1String("--solo") && i + 1 < args.size()) {
            solo = args.at(++i);
        } else if (args.at(i) == QLatin1String("--manifiesto") && i + 1 < args.size()) {
            rutaManifiesto = args.at(++i); // pruebas de la herramienta (asercion-negativa.cmake)
        } else if (args.at(i) == QLatin1String("--textos") && i + 1 < args.size()) {
            gDirTextos = args.at(++i);
        } else if (args.at(i) == QLatin1String("--tema") && i + 1 < args.size()
                   && (args.at(i + 1) == QLatin1String("claro") || args.at(i + 1) == QLatin1String("oscuro"))) {
            gTemaOscuro = args.at(++i) == QLatin1String("oscuro");
        } else {
            std::fprintf(stderr, "uso: satcfdi_manual_capturas --salida <dir> [--solo <archivo.png>] "
                                 "[--tema claro|oscuro] [--textos <dir>] [--manifiesto <json>]\n");
            return 2;
        }
    }
    if (salida.isEmpty() || !QDir().mkpath(salida)) {
        std::fprintf(stderr, "error: --salida <dir> es obligatorio\n");
        return 2;
    }
    if (!gDirTextos.isEmpty() && !QDir().mkpath(gDirTextos)) {
        std::fprintf(stderr, "error: no se pudo crear --textos\n");
        return 2;
    }

    const QHash<QString, Generador> gen = generadores();
    const auto manifiesto = leerManifiesto(rutaManifiesto, gen);
    if (!manifiesto) {
        return 2;
    }

    int fallas = 0;
    int generadas = 0;
    for (const Escenario& e : *manifiesto) {
        if (!solo.isEmpty() && e.archivo != solo) {
            continue;
        }
        const QString ruta = QDir(salida).filePath(e.archivo);
        QFile::remove(ruta);
        gAserciones = &e.aserciones;
        const bool ok = gen.value(e.clave)(ruta, e.tamano);
        gAserciones = nullptr;
        const QImage leida(ruta);
        const bool tamano = leida.size() == e.tamano * kEscala;
        const qint64 bytes = QFileInfo(ruta).size();
        const bool peso = bytes > 0 && bytes <= kPesoMaximo;
        const bool valida = ok && tamano && peso;
        std::printf("%s %s %s %s %dx%d %lld bytes%s\n", valida ? "OK   " : "FALLA", qPrintable(e.archivo),
                    qPrintable(e.clave), qPrintable(e.tipo), leida.width(), leida.height(),
                    static_cast<long long>(bytes),
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
