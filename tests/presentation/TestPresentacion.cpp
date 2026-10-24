// satcfdi_presentation_tests: carga el modulo QML real (SatCfdiDownloader/Main)
// con view models inyectados por setInitialProperties y verifica roles,
// estados, navegacion, teclado y accesibilidad. Cualquier warning (incluidos
// los de carga QML) hace fallar la prueba.
//
// Dos grafos de servicios:
// - Escenario: DemoSolicitudesService (via SolicitudesServiceEspia) y
//   DemoPerfilesSatService, con futures ya completados, y
//   CredencialesSatServiceListo (toda e.firma Lista).
// - EscenarioAsincrono: fakes con QPromise controlados por la prueba, para
//   estados de carga, errores y respuestas tardias o desordenadas.

#include "FuenteFacturasFake.h"
#include "presentation/viewmodels/FacturasViewModel.h"
#include "presentation/viewmodels/FranjaMesViewModel.h"
#include "CredencialesSatServiceListo.h"
#include "ServiciosAsincronosFake.h"
#include "SolicitudesServiceEspia.h"
#include "fakes/FakeCredencialesSatService.h"
#include "fakes/FakePerfilesSatService.h"

#include "application/primeruso/ConsultaPrimerUso.h"
#include "domain/credenciales/VencimientoEFirma.h"
#include "application/profiles/DemoPerfilesSatService.h"
#include "AccionesFinder.h"
#include "AccionesSolicitud.h"
#include "ConsultaExistenciaPaquetes.h"
#include "AppViewModel.h"
#include "AvisoRaizPaquetesViewModel.h"
#include "FormatoFechas.h"
#include "NuevaSolicitudViewModel.h"
#include "PerfilesSatListModel.h"
#include "PerfilesSatViewModel.h"
#include "PresentacionViewModels.h"
#include "SolicitudDetailViewModel.h"
#include "SolicitudesFiltroModel.h"
#include "SolicitudesListModel.h"

#include <QAccessible>
#include <QClipboard>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QPromise>
#include <QSet>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace satcfdi;
using Pagina = AppViewModel::Pagina;
using Rol = SolicitudesListModel::Rol;
using EstadoLista = SolicitudesListModel::Estado;
using EstadoDetalle = SolicitudDetailViewModel::Estado;

namespace {

QQuickItem* buscar(QQuickItem* raiz, const QString& nombre)
{
    if (!raiz) {
        return nullptr;
    }
    if (raiz->objectName() == nombre) {
        return raiz;
    }
    const auto hijos = raiz->childItems();
    for (QQuickItem* hijo : hijos) {
        if (QQuickItem* encontrado = buscar(hijo, nombre)) {
            return encontrado;
        }
    }
    return nullptr;
}

QString nombreAccesible(QObject* objeto)
{
    return QQmlProperty(objeto, QStringLiteral("Accessible.name"), qmlContext(objeto))
        .read()
        .toString();
}

// objectName del elemento con foco activo.
QString objectNameConFoco(QQuickWindow* ventana)
{
    return ventana->activeFocusItem() ? ventana->activeFocusItem()->objectName() : QString();
}

// Verdadero si el foco activo esta en `nombre` o en un descendiente suyo.
bool focoDentroDe(QQuickWindow* ventana, const QString& nombre)
{
    for (QQuickItem* item = ventana->activeFocusItem(); item; item = item->parentItem()) {
        if (item->objectName() == nombre) {
            return true;
        }
    }
    return false;
}

// Entrega las continuaciones then(this, ...) pendientes. Se usa antes de
// verificar que algo NO ocurrio (respuestas tardias descartadas).
void procesarEventos()
{
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
    }
}

// View models + engine sobre servicios abstractos (como el composition root).
// Orden de miembros: view models -> engine (el engine se destruye primero).
struct Vista {
    Vista(SolicitudesService* solicitudes, PerfilesSatService* perfiles,
          CredencialesSatService* credenciales, RelojUtc reloj = relojSistema())
        : vms(solicitudes, perfiles, credenciales, nullptr, std::move(reloj))
    {
    }

    // Main.qml arranca oculta (T004); por defecto el fixture la muestra como
    // lo haria AppLifecycleController en un arranque manual.
    bool cargar(bool mostrar = true)
    {
        engine.setInitialProperties(vms.initialProperties());
        engine.loadFromModule("SatCfdiDownloader", "Main");
        if (engine.rootObjects().size() != 1) {
            return false;
        }
        ventana = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
        if (ventana && mostrar) {
            ventana->show();
        }
        return ventana != nullptr;
    }

    bool activar()
    {
        ventana->requestActivate();
        return QTest::qWaitForWindowActive(ventana);
    }

    QQuickItem* pagina() const
    {
        QQuickItem* loader = buscar(ventana->contentItem(), QStringLiteral("paginaActual"));
        return loader ? qvariant_cast<QQuickItem*>(loader->property("item")) : nullptr;
    }

    QQuickItem* item(const QString& nombre) const { return buscar(pagina(), nombre); }

    // Dialogo (Popup, no Item) declarado en la pagina actual.
    QObject* dialogo(const QString& nombre) const
    {
        return pagina() ? pagina()->findChild<QObject*>(nombre) : nullptr;
    }

    QQuickItem* itemDeDialogo(const QString& dialogoNombre, const QString& nombre) const
    {
        QObject* d = dialogo(dialogoNombre);
        return d ? buscar(qvariant_cast<QQuickItem*>(d->property("contentItem")), nombre) : nullptr;
    }

    PresentacionViewModels vms;
    QQmlApplicationEngine engine;
    QQuickWindow* ventana = nullptr;
};

struct ServiciosDemo {
    explicit ServiciosDemo(DemoSolicitudesService::Datos datos)
        : perfiles(DemoPerfilesSatService::perfilesDemo())
        , solicitudes(DemoPerfilesSatService::perfilesDemo(), datos)
    {
        QObject::connect(&perfiles, &PerfilesSatService::perfilesCambiaron, &solicitudes,
                         [this] { solicitudes.setPerfiles(perfiles.perfiles()); });
    }

    DemoPerfilesSatService perfiles;
    CredencialesSatServiceListo credenciales;
    SolicitudesServiceEspia solicitudes;
};

struct ServiciosAsincronos {
    fakes::FakePerfilesSatService perfiles;
    fakes::FakeCredencialesSatService credenciales;
    SolicitudesServiceAsincrono solicitudes;
};

// Base-from-member: los servicios se construyen antes y se destruyen despues
// que la vista.
struct Escenario : ServiciosDemo, Vista {
    explicit Escenario(DemoSolicitudesService::Datos datos)
        : ServiciosDemo(datos)
        , Vista(&solicitudes, &perfiles, &credenciales)
    {
    }
};

struct EscenarioAsincrono : ServiciosAsincronos, Vista {
    EscenarioAsincrono()
        : Vista(&solicitudes, &perfiles, &credenciales)
    {
    }
};

PerfilResumen perfilDePrueba()
{
    return {PerfilId::desdeTexto(u"6f1c2b3a-4d5e-4f60-8a1b-2c3d4e5f6071").value(),
            QStringLiteral("EKU9003173C9"), QStringLiteral("Perfil de prueba"), true};
}

SolicitudResumen resumenDePrueba(const SolicitudId& id)
{
    SolicitudResumen r;
    r.id = id;
    r.perfilRfc = QStringLiteral("EKU9003173C9");
    r.tipoDescarga = TipoDescarga::Emitidos;
    r.fechaInicial = QDate(2026, 7, 1);
    r.fechaFinal = QDate(2026, 7, 31);
    r.creadaEn = QDateTime(QDate(2026, 8, 1), QTime(12, 0), QTimeZone::UTC);
    return r;
}

// Detalle con filtros, codigos SAT, un paquete y un log (fixture: T003 no
// produce estados SAT).
SolicitudDetalle detalleDePrueba(const SolicitudId& id)
{
    SolicitudDetalle d;
    d.resumen = resumenDePrueba(id);
    d.resumen.estadoLocal = EstadoLocal::Enviada;
    d.resumen.estadoSat = EstadoSolicitudSat::Terminada;
    d.resumen.totalPaquetes = 1;
    d.resumen.rfcContraparte = QStringLiteral("XAXX010101000");
    d.fechaInicialSat = QStringLiteral("2026-07-01T00:00:00");
    d.fechaFinalSat = QStringLiteral("2026-07-31T23:59:59");
    d.rfcContrapartes = {QStringLiteral("XAXX010101000")};
    d.tipoComprobante = QStringLiteral("I");
    d.complemento = QStringLiteral("nomina12");
    d.idSolicitudSat = QStringLiteral("4e3b2a1c-0000-4000-8000-000000000001");
    d.codEstatusSolicitud = QStringLiteral("5000");
    d.mensajeSolicitudSat = QStringLiteral("Solicitud Aceptada");
    d.numeroCfdi = 42;
    PaqueteResumen p;
    p.idPaqueteSat = QStringLiteral("PAQ_01");
    p.estadoDescarga = EstadoDescarga::Disponible;
    p.disponibleEn = d.resumen.creadaEn;
    d.paquetes = {p};
    LogResumen l;
    l.tipoEvento = TipoEventoLog::SolicitudCreada;
    l.origen = OrigenLog::Usuario;
    l.creadoEn = d.resumen.creadaEn;
    d.logs = {l};
    return d;
}

using ResultadoListaSol = SolicitudesService::ResultadoLista;
using ResultadoListaPerfiles = PerfilesSatService::ResultadoLista;

// Resuelve la carga `indice` de perfiles (listarNoEliminados) con `perfiles`
// y despues cada consulta de resumen de credencial que provoque con `estado`.
bool resolverPerfiles(EscenarioAsincrono& e, int indice, const QList<PerfilResumen>& perfiles,
                      EstadoCredencial estado = EstadoCredencial::Lista)
{
    const int base = e.credenciales.resumenes.size();
    e.perfiles.listas.resolver(indice, ResultadoListaPerfiles::exito(perfiles));
    if (!QTest::qWaitFor([&] { return e.credenciales.resumenes.size() == base + perfiles.size(); })) {
        return false;
    }
    for (int i = 0; i < perfiles.size(); ++i) {
        e.credenciales.resolverResumen(base + i, estado);
    }
    return true;
}

// T007: registra las acciones que la presentacion pide (sin worker real).
struct AccionesEspia final : AccionesSolicitud {
    QStringList llamadas;
    void enviar(const SolicitudId& id) override { llamadas.append(QStringLiteral("enviar:") + id.texto()); }
    void verificarAhora(const SolicitudId& id) override
    {
        llamadas.append(QStringLiteral("verificar:") + id.texto());
    }
    void reintentarDescarga(const SolicitudId& id) override
    {
        llamadas.append(QStringLiteral("descargar:") + id.texto());
    }
    void reintentarDescargaPaquete(const SolicitudId& id, const QString& idPaqueteSat) override
    {
        llamadas.append(QStringLiteral("descargarPaquete:") + id.texto() + QLatin1Char('/') + idPaqueteSat);
    }
};

// T008: consulta de existencia con promesas que resuelve la prueba.
struct ExistenciaEspia final : ConsultaExistenciaPaquetes {
    QStringList consultados;
    QList<std::shared_ptr<QPromise<ExistenciaPaquete>>> promesas;
    QFuture<ExistenciaPaquete> consultar(const SolicitudId&, const QString& idPaqueteSat) override
    {
        consultados.append(idPaqueteSat);
        auto p = std::make_shared<QPromise<ExistenciaPaquete>>();
        p->start();
        promesas.append(p);
        return p->future();
    }
    void resolver(int i, ExistenciaPaquete e)
    {
        promesas.at(i)->addResult(e);
        promesas.at(i)->finish();
    }
};

// T009.1: acciones de Finder con promesas que resuelve la prueba.
struct FinderEspia final : AccionesFinder {
    QStringList llamadas;
    QList<std::shared_ptr<QPromise<ResultadoAccionFinder>>> promesas;
    QFuture<ResultadoAccionFinder> nueva(const QString& llamada)
    {
        llamadas.append(llamada);
        auto p = std::make_shared<QPromise<ResultadoAccionFinder>>();
        p->start();
        promesas.append(p);
        return p->future();
    }
    QFuture<ResultadoAccionFinder> mostrarPaquete(const SolicitudId&, const QString& id) override
    {
        return nueva(QStringLiteral("paquete:") + id);
    }
    QFuture<ResultadoAccionFinder> abrirCarpetaSolicitud(const SolicitudId&) override
    {
        return nueva(QStringLiteral("solicitud"));
    }
    QFuture<ResultadoAccionFinder> abrirCarpetaPaquetes() override { return nueva(QStringLiteral("raiz")); }
    QFuture<ResultadoAccionFinder> abrirEnVistaPrevia(const QString&) override // T015.4
    {
        return nueva(QStringLiteral("vistaPrevia"));
    }
    void resolver(int i, ResultadoAccionFinder::Estado e, const QString& mensaje = {})
    {
        promesas.at(i)->addResult(ResultadoAccionFinder{e, mensaje});
        promesas.at(i)->finish();
    }
};

} // namespace

class TestPresentacion : public QObject {
    Q_OBJECT

private slots:
    void init();

    // T002 (adaptadas al contrato T003)
    void rolesDelModelo();
    void cargaQmlSinWarnings();
    void ventanaArrancaOculta();
    void avisoRaizPaquetesArribaDeLasPaginas();
    void estadoVacioConDatosVacios();
    void navegacionListaADetallePorId();
    void recorridoConTeclado();
    void paginasExponenTextoAccesible();
    void qmlSinImportsProhibidos();

    // T003: lista
    void listaEstadosCargandoErrorVaciaConDatos();
    void listaDescartaRespuestaTardia();

    // T003: perfiles y nueva solicitud
    void perfilesDescartaRespuestaTardia();

    // T003: detalle
    void detalleEstadosErrorConDatosYNoEncontrada();
    void detalleReaccionaASolicitudEliminada();
    void detalleDescartaRespuestaTardia();
    void eliminarConConfirmacion();

    // T007: acciones manuales, envio tras crear y textos de eventos nuevos
    void detalleAccionesManualesYEstadosAccesibles();
    void detalleSinAccionesNoMuestraBotones();

    // T008 D11
    void detalleMuestraExistenciaSinCambiarEstado();

    // T009
    void enviarSoloConCredencialLista();
    void mensajesDelCatalogoYSin5008();
    void sinFacturasEnDiagnostico(); // T023.6 (D22)
    void avisoNotificacionesDeshabilitadas();

    // T009.1
    void finderEnDetalleSegunExistencia();
    void finderCarpetaDePaquetesEnLista();

    // T014.1: mejoras de pantalla
    void filtrosLocalesDeLaLista();
    void filtroPorMesIncluyePeriodosQueCruzan();
    void conteoDePaquetesEnLaLista();
    void atajosDeTeclado();
    void atajosDeFacturas();
    void copiarIdentificadores();

    // T014.2: reintento por paquete
    void reintentoPorPaquete();

    // T014.3: primer uso y vencimiento de e.firma
    void guiaPrimerUsoPorPasos();
    void guiaPrimerUsoCargandoOConErrorNoSeMuestra();
    void badgeVencimientoEnPerfiles();
    void guiaPrimerUsoRefrescoFallidoLaOculta();
    void badgeConElMismoRelojQueElAviso();

    // T014.4: destinos de acciones de notificacion
    void notificacionAbreDetalleExistente();
    void notificacionSolicitudInexistenteAbreLista();
    void notificacionIdNoCanonicoAbreLista();
    void notificacionPerfilSeleccionadoTrasCarga();
    void notificacionPerfilInexistenteSinSeleccion();
    void notificacionMostrarCarpetaSolicitudEnFinder();
};

void TestPresentacion::init()
{
    // Falla ante cualquier warning: errores de carga QML, bindings, etc.
    // Unica excepcion: aviso de rendimiento de la base de fuentes de la
    // plataforma offscreen, ajeno a QML y a la app.
    QTest::failOnWarning(QRegularExpression(
        QStringLiteral("^(?!Populating font family aliases took).*")));
}

void TestPresentacion::rolesDelModelo()
{
    Escenario e(DemoSolicitudesService::Datos::Representativos);
    SolicitudesListModel* modelo = e.vms.solicitudes();

    const QList<QByteArray> esperados = {
        "id", "perfilRfc", "rfcContraparte", "tipoDescarga", "fechaInicial", "fechaFinal",
        "estadoLocal", "estadoSat", "estadoResumen", "creadaEn", "totalPaquetes", "perfilNombre",
        "paquetesDescargados", "paquetesPendientesDescarga", // T014.1 D2
    };
    const QList<QByteArray> nombres = modelo->roleNames().values();
    QCOMPARE(nombres.size(), esperados.size());
    for (const QByteArray& rol : esperados) {
        QVERIFY2(nombres.contains(rol), rol.constData());
    }

    QTRY_COMPARE(modelo->rowCount(), 11);
    QVERIFY(!modelo->vacio());
    QCOMPARE(modelo->estado(), EstadoLista::ConDatos);

    QSet<QString> resumenes;
    bool hayNulo = false;
    bool hayConEstadoSat = false;
    for (int fila = 0; fila < modelo->rowCount(); ++fila) {
        const QModelIndex i = modelo->index(fila);
        QVERIFY(SolicitudId::desdeTexto(i.data(Rol::IdRole).toString()).has_value());
        QCOMPARE(modelo->filaDe(i.data(Rol::IdRole).toString()), fila);
        QVERIFY(QDate::fromString(i.data(Rol::FechaInicialRole).toString(), Qt::ISODate).isValid());
        QVERIFY(i.data(Rol::CreadaEnRole).toDateTime().isValid());
        resumenes.insert(i.data(Rol::EstadoResumenRole).toString());

        const QVariant sat = i.data(Rol::EstadoSatRole);
        if (sat.metaType() == QMetaType::fromType<std::nullptr_t>()) {
            hayNulo = true;
            // Sin estado SAT, el resumen es el estado local.
            QCOMPARE(i.data(Rol::EstadoResumenRole), i.data(Rol::EstadoLocalRole));
        } else {
            hayConEstadoSat = true;
            QCOMPARE(sat.metaType(), QMetaType::fromType<QString>());
        }
    }
    QVERIFY(hayNulo);
    QVERIFY(hayConEstadoSat);
    const QSet<QString> claves = {
        QStringLiteral("Creada"), QStringLiteral("Enviando"), QStringLiteral("Enviada"),
        QStringLiteral("EnvioFallido"), QStringLiteral("EnvioIncierto"), QStringLiteral("Aceptada"),
        QStringLiteral("EnProceso"), QStringLiteral("Terminada"), QStringLiteral("ErrorSat"),
        QStringLiteral("Rechazada"), QStringLiteral("Vencida"),
    };
    QCOMPARE(resumenes, claves);
}

void TestPresentacion::cargaQmlSinWarnings()
{
    Escenario e(DemoSolicitudesService::Datos::Representativos);
    QVERIFY(e.cargar());
    QVERIFY(QTest::qWaitForWindowExposed(e.ventana));
    QCOMPARE(e.vms.app()->pagina(), Pagina::Diagnostico);
    QTRY_VERIFY(e.pagina() != nullptr);
    QCOMPARE(e.pagina()->objectName(), QStringLiteral("paginaSolicitudes"));

    auto* lista = e.item(QStringLiteral("listaSolicitudes"));
    QVERIFY(lista);
    QTRY_COMPARE(lista->property("count").toInt(), 11);
    QVERIFY(lista->isVisible());
    QVERIFY(!e.item(QStringLiteral("estadoVacio"))->isVisible());
    QVERIFY(!e.item(QStringLiteral("estadoCargando"))->isVisible());
    QVERIFY(!e.item(QStringLiteral("estadoError"))->isVisible());
    QVERIFY(e.ventana->minimumWidth() > 0 && e.ventana->minimumHeight() > 0);
}

void TestPresentacion::ventanaArrancaOculta()
{
    Escenario e(DemoSolicitudesService::Datos::Representativos);
    QVERIFY(e.cargar(false));
    QCOMPARE(e.ventana->isVisible(), false);
    QCOMPARE(e.ventana->property("visible").toBool(), false);
    // T019: titulo con el nombre visible.
    QCOMPARE(e.ventana->title(), QStringLiteral("Archivo Fiscal"));
    // Oculta, la app sigue funcionando: la lista carga y la pagina existe.
    QTRY_COMPARE(e.vms.solicitudes()->rowCount(), 11);
    QTRY_VERIFY(e.pagina() != nullptr);
    QCOMPARE(e.pagina()->objectName(), QStringLiteral("paginaSolicitudes"));
    procesarEventos();
    QVERIFY(!e.ventana->isVisible()); // nada en QML la muestra por su cuenta
}

// T019 D8: sin aviso la franja no ocupa espacio; con aviso (fijado antes de
// cargar, como el composition root) aparece arriba de la pagina al mostrar
// la ventana, sin mostrarla por su cuenta, se anuncia como alerta con las
// rutas ~/..., sigue visible al navegar y no toma foco.
void TestPresentacion::avisoRaizPaquetesArribaDeLasPaginas()
{
    {
        Escenario e(DemoSolicitudesService::Datos::Representativos);
        QVERIFY(e.cargar());
        QQuickItem* franja = buscar(e.ventana->contentItem(), QStringLiteral("franjaAvisoRaizPaquetes"));
        QVERIFY(franja);
        QVERIFY(!franja->isVisible());
        QCOMPARE(franja->height(), 0.0);
        QQuickItem* loader = buscar(e.ventana->contentItem(), QStringLiteral("paginaActual"));
        QCOMPARE(loader->y(), 0.0);
    }

    Escenario e(DemoSolicitudesService::Datos::Representativos);
    e.vms.avisoRaizPaquetes()->fijar({AvisoRaizPaquetesViewModel::Motivo::Conflicto,
                                      QStringLiteral("~/SAT-CFDI-Downloader/paquetes"),
                                      QStringLiteral("~/Archivo-Fiscal/paquetes"),
                                      QStringLiteral("~/SAT-CFDI-Downloader/paquetes")});
    QVERIFY(e.cargar(false));
    procesarEventos();
    QVERIFY(!e.ventana->isVisible()); // el aviso no abre la ventana (inicio automatico)
    e.ventana->show();
    QVERIFY(QTest::qWaitForWindowExposed(e.ventana));

    QQuickItem* franja = buscar(e.ventana->contentItem(), QStringLiteral("franjaAvisoRaizPaquetes"));
    QVERIFY(franja);
    QTRY_VERIFY(franja->isVisible());
    QVERIFY(franja->height() > 0);
    QCOMPARE(franja->y(), 0.0);
    QQuickItem* loader = buscar(e.ventana->contentItem(), QStringLiteral("paginaActual"));
    QCOMPARE(loader->y(), franja->height());

    QQuickItem* aviso = buscar(franja, QStringLiteral("avisoRaizPaquetes"));
    QVERIFY(aviso);
    QAccessibleInterface* acc = QAccessible::queryAccessibleInterface(aviso);
    QVERIFY(acc);
    QCOMPARE(acc->role(), QAccessible::AlertMessage);
    const QString nombre = acc->text(QAccessible::Name);
    QVERIFY2(nombre.startsWith(QStringLiteral("Los paquetes siguen en la carpeta anterior.")), qPrintable(nombre));
    QVERIFY2(nombre.contains(QStringLiteral("~/Archivo-Fiscal/paquetes")), qPrintable(nombre));
    QVERIFY2(nombre.contains(QStringLiteral("Esta sesión usa ~/SAT-CFDI-Downloader/paquetes")), qPrintable(nombre));
    QVERIFY(!nombre.contains(QDir::homePath()));
    QVERIFY(!aviso->hasActiveFocus());

    // Sigue visible en otra pagina (no se descarta durante la sesion).
    e.vms.app()->mostrarPerfiles();
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaPerfilesSat"));
    QVERIFY(franja->isVisible());
    QCOMPARE(loader->y(), franja->height());
}

void TestPresentacion::estadoVacioConDatosVacios()
{
    Escenario e(DemoSolicitudesService::Datos::Vacio);
    QVERIFY(e.cargar());
    QVERIFY(QTest::qWaitForWindowExposed(e.ventana));
    QTRY_VERIFY(e.vms.solicitudes()->vacio());
    QCOMPARE(e.vms.solicitudes()->rowCount(), 0);
    QCOMPARE(e.vms.solicitudes()->estado(), EstadoLista::Vacia);

    auto* vacio = e.item(QStringLiteral("estadoVacio"));
    QVERIFY(vacio);
    QTRY_VERIFY(vacio->isVisible());
    QVERIFY(!nombreAccesible(vacio).isEmpty());
    QVERIFY(!e.item(QStringLiteral("listaSolicitudes"))->isVisible());
    // Sin filas, el foco inicial va a "Perfiles SAT" (T023.5: sin "Nueva solicitud").
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("botonPerfilesSat"));
}

void TestPresentacion::navegacionListaADetallePorId()
{
    Escenario e(DemoSolicitudesService::Datos::Representativos);
    QVERIFY(e.cargar());
    QVERIFY(QTest::qWaitForWindowExposed(e.ventana));
    QTRY_COMPARE(e.vms.solicitudes()->rowCount(), 11);

    // Solicitud Terminada con paquetes y estado SAT.
    const QModelIndex terminada = e.vms.solicitudes()->match(
        e.vms.solicitudes()->index(0), Rol::EstadoResumenRole, QStringLiteral("Terminada")).constFirst();
    const QString id = terminada.data(Rol::IdRole).toString();

    QVERIFY(!e.vms.app()->abrirDetalle(QStringLiteral("3"))); // un indice no es identidad
    QCOMPARE(e.vms.app()->pagina(), Pagina::Diagnostico);

    QVERIFY(e.vms.app()->abrirDetalle(id));
    QCOMPARE(e.vms.app()->pagina(), Pagina::Detalle);
    QCOMPARE(e.vms.app()->solicitudSeleccionadaId(), id);
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaDetalleSolicitud"));

    SolicitudDetailViewModel* d = e.vms.detalle();
    QTRY_VERIFY(d->cargada());
    QCOMPARE(d->estado(), EstadoDetalle::ConDatos);
    QCOMPARE(d->solicitudId(), id);
    QCOMPARE(d->perfilRfc(), terminada.data(Rol::PerfilRfcRole).toString());
    QCOMPARE(d->estadoLocal(), QStringLiteral("Enviada"));
    QCOMPARE(d->estadoSat().toString(), QStringLiteral("Terminada"));
    QCOMPARE(d->totalPaquetes(), 4);
    QCOMPARE(d->paquetes().size(), 4);

    // Secciones separadas y visibles.
    // T013 (UX-23): secciones en pestanas; cada una visible en su pestana.
    const QList<QPair<const char*, const char*>> secciones = {
        {"seccionMetadata", "datos"}, {"seccionFiltros", "datos"}, {"seccionEstados", "datos"},
        {"seccionPaquetes", "paquetes"}, {"seccionHistorial", "historial"}};
    for (const auto& [seccion, pestana] : secciones) {
        QMetaObject::invokeMethod(e.pagina(), "mostrarPestana", Q_ARG(QVariant, QString::fromLatin1(pestana)));
        auto* item = e.item(QString::fromLatin1(seccion));
        QVERIFY2(item && item->isVisible(), seccion);
    }
    auto* badgeSat = e.item(QStringLiteral("badgeEstadoSat"));
    QCOMPARE(badgeSat->property("texto").toString(), QStringLiteral("Terminada"));

    // Solicitud sin estado SAT: QML recibe null y lo muestra como texto.
    const QModelIndex creada = e.vms.solicitudes()->match(
        e.vms.solicitudes()->index(0), Rol::EstadoResumenRole, QStringLiteral("Creada")).constFirst();
    QVERIFY(e.vms.app()->abrirDetalle(creada.data(Rol::IdRole).toString()));
    QTRY_COMPARE(d->solicitudId(), creada.data(Rol::IdRole).toString());
    QTRY_VERIFY(d->cargada());
    QVERIFY(d->estadoSat().isNull());
    QTRY_COMPARE(e.item(QStringLiteral("badgeEstadoSat"))->property("texto").toString(),
                 QStringLiteral("Sin respuesta del SAT"));
    QMetaObject::invokeMethod(e.pagina(), "mostrarPestana", Q_ARG(QVariant, QStringLiteral("paquetes")));
    QTRY_VERIFY(e.item(QStringLiteral("sinPaquetes"))->isVisible());

    e.vms.app()->mostrarDiagnostico();
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaSolicitudes"));
}

void TestPresentacion::recorridoConTeclado()
{
    Escenario e(DemoSolicitudesService::Datos::Representativos);
    QVERIFY(e.cargar());
    QVERIFY(e.activar());
    QTRY_COMPARE(e.vms.solicitudes()->rowCount(), 11);

    // Lista: foco inicial en la lista; flecha abajo + Return abre el detalle por id.
    QTRY_VERIFY(focoDentroDe(e.ventana, QStringLiteral("listaSolicitudes")));
    QTest::keyClick(e.ventana, Qt::Key_Down);
    const QString idFila1 = e.vms.solicitudes()->index(1).data(Rol::IdRole).toString();
    QTest::keyClick(e.ventana, Qt::Key_Return);
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Detalle);
    QCOMPARE(e.vms.app()->solicitudSeleccionadaId(), idFila1);

    // Detalle: foco en "Regresar"; Tab llega a "Eliminar solicitud"; Escape vuelve.
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("botonRegresar"));
    QTRY_VERIFY(e.vms.detalle()->cargada());
    QTest::keyClick(e.ventana, Qt::Key_Tab);
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("botonEliminarSolicitud"));
    QTest::keyClick(e.ventana, Qt::Key_Escape);
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Diagnostico);

    // Espacio sobre la lista tambien abre el detalle; Return sobre "Regresar" vuelve.
    QTRY_VERIFY(focoDentroDe(e.ventana, QStringLiteral("listaSolicitudes")));
    QTest::keyClick(e.ventana, Qt::Key_Space);
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Detalle);
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("botonRegresar"));
    QTest::keyClick(e.ventana, Qt::Key_Return);
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Diagnostico);

    // T013/T023.5: Tab llega a las acciones del encabezado (Perfiles SAT es
    // la ultima: ya no hay "Nueva solicitud").
    QTRY_VERIFY(focoDentroDe(e.ventana, QStringLiteral("listaSolicitudes")));
    QTest::keyClick(e.ventana, Qt::Key_Tab);
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("botonPerfilesSat"));
}

void TestPresentacion::paginasExponenTextoAccesible()
{
    Escenario e(DemoSolicitudesService::Datos::Representativos);
    QVERIFY(e.cargar());
    QVERIFY(QTest::qWaitForWindowExposed(e.ventana));
    QTRY_COMPARE(e.vms.solicitudes()->rowCount(), 11);

    // Lista: pagina, lista, boton y filas con texto (no solo color).
    QVERIFY(!nombreAccesible(e.pagina()).isEmpty());
    QVERIFY(!nombreAccesible(e.item(QStringLiteral("listaSolicitudes"))).isEmpty());
    QVERIFY(!e.item(QStringLiteral("botonNuevaSolicitud"))); // T023.5: sin "Nueva solicitud"
    auto* fila0 = e.item(QStringLiteral("filaSolicitud_0"));
    QVERIFY(fila0);
    const QString nombreFila = nombreAccesible(fila0);
    QVERIFY2(nombreFila.contains(e.vms.solicitudes()->index(0).data(Rol::PerfilRfcRole).toString()),
             qPrintable(nombreFila));
    // T013 (FilaSolicitud): el nombre empieza con el texto del estado.
    QVERIFY2(nombreFila.startsWith(QStringLiteral("Creada, ")) || nombreFila.contains(QStringLiteral(", ")),
             qPrintable(nombreFila));
    QVERIFY2(nombreFila.contains(QStringLiteral(" paquete")), qPrintable(nombreFila));
    QCOMPARE(QQmlProperty::read(fila0, QStringLiteral("Accessible.role"), qmlContext(fila0)).toInt(),
             int(QAccessible::ListItem));
    const auto badges = fila0->findChildren<QQuickItem*>();
    bool badgeConTexto = false;
    for (QQuickItem* hijo : badges) {
        if (nombreAccesible(hijo).startsWith(QStringLiteral("Estado: "))) {
            badgeConTexto = nombreAccesible(hijo).size() > 8;
        }
    }
    QVERIFY(badgeConTexto);
    // Estados de carga y error tienen texto accesible aunque no esten visibles.
    QVERIFY(!nombreAccesible(e.item(QStringLiteral("estadoCargando"))).isEmpty());
    QCOMPARE(nombreAccesible(e.item(QStringLiteral("botonReintentarLista"))), QStringLiteral("Reintentar"));

    // Detalle: pagina, secciones, badges de estado con texto y eliminar.
    QVERIFY(e.vms.app()->abrirDetalle(e.vms.solicitudes()->index(0).data(Rol::IdRole).toString()));
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaDetalleSolicitud"));
    QTRY_VERIFY(e.vms.detalle()->cargada());
    QVERIFY(!nombreAccesible(e.pagina()).isEmpty());
    QTRY_VERIFY(nombreAccesible(e.item(QStringLiteral("badgeEstadoLocal")))
                    .startsWith(QStringLiteral("Estado local: ")));
    QVERIFY(nombreAccesible(e.item(QStringLiteral("badgeEstadoSat")))
                .startsWith(QStringLiteral("Estado SAT: ")));
    QVERIFY(!nombreAccesible(e.item(QStringLiteral("seccionPaquetes"))).isEmpty());
    QVERIFY(!nombreAccesible(e.item(QStringLiteral("seccionFiltros"))).isEmpty());
    QCOMPARE(nombreAccesible(e.item(QStringLiteral("botonEliminarSolicitud"))),
             QStringLiteral("Eliminar solicitud"));
    QVERIFY(!nombreAccesible(e.item(QStringLiteral("estadoNoEncontrada"))).isEmpty());
}

void TestPresentacion::qmlSinImportsProhibidos()
{
    // Fronteras de capa: QML solo importa QtQuick/Controls/Layouts y archivos
    // del propio modulo; nada de SQL, archivos, red, secretos o SAT. Tampoco
    // conoce detalles de persistencia (eliminado_en, clave de deduplicacion,
    // rutas, Keychain o ZIP).
    const QRegularExpression importacion(QStringLiteral("^\\s*import\\s+(\\S+)"),
                                         QRegularExpression::MultilineOption);
    const QStringList permitidos = {
        QStringLiteral("QtQuick"), QStringLiteral("QtQuick.Controls"),
        QStringLiteral("QtQuick.Layouts"), QStringLiteral("\"Etiquetas.js\""),
        // T013 D1: los controles del modulo son siempre Basic (tambien si el
        // proceso no fija el estilo global).
        QStringLiteral("QtQuick.Controls.Basic"),
        // T005.1 (DA4): selector de archivos de e.firma; entrega QUrl.
        QStringLiteral("QtQuick.Dialogs"),
        // T015.4 (T015 D7): visor del PDF ya generado; solo recibe su URL.
        QStringLiteral("QtQuick.Pdf"),
    };
    const QRegularExpression prohibido(
        QStringLiteral("eliminado_?en|dedup|sql|keychain|\\.zip|file:|XMLHttpRequest|"
                       "LocalStorage|solicitudReferencia"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression colorLiteral(QStringLiteral("[\"']#[0-9a-fA-F]{3,8}[\"']"));
    int archivos = 0;
    QDirIterator it(QStringLiteral(":/qt/qml/SatCfdiDownloader"),
                    {QStringLiteral("*.qml"), QStringLiteral("*.js")}, QDir::Files);
    while (it.hasNext()) {
        QFile archivo(it.next());
        QVERIFY(archivo.open(QIODevice::ReadOnly));
        const QString contenido = QString::fromUtf8(archivo.readAll());
        auto coincidencias = importacion.globalMatch(contenido);
        while (coincidencias.hasNext()) {
            const QString modulo = coincidencias.next().captured(1);
            QVERIFY2(permitidos.contains(modulo),
                     qPrintable(archivo.fileName() + QStringLiteral(": ") + modulo));
        }
        // T013: ningun color literal fuera de Theme.qml (solo tokens).
        if (!archivo.fileName().endsWith(QStringLiteral("/Theme.qml"))) {
            const QRegularExpressionMatch literal = colorLiteral.match(contenido);
            QVERIFY2(!literal.hasMatch(),
                     qPrintable(archivo.fileName() + QStringLiteral(": color literal ") + literal.captured(0)));
        }
        const QRegularExpressionMatch termino = prohibido.match(contenido);
        QVERIFY2(!termino.hasMatch(),
                 qPrintable(archivo.fileName() + QStringLiteral(": ") + termino.captured(0)));
        ++archivos;
    }
    // 31 .qml (Theme, Icono, componentes T013, BotonCopiar, T015.3, VisorPdfCfdi y T019) + Etiquetas.js,
    // mas T023.4: FacturasPage, FranjaMes y ListaMesesFacturas; T023.5: HistorialPage;
    // T025.6: RespaldoPage, FranjaAvisosRespaldo y cuatro dialogos de respaldo.
    QCOMPARE(archivos, 42);
}

// ---------------------------------------------------------------------------
// T003: lista

void TestPresentacion::listaEstadosCargandoErrorVaciaConDatos()
{
    EscenarioAsincrono e;
    QVERIFY(e.cargar());
    QVERIFY(QTest::qWaitForWindowExposed(e.ventana));
    SolicitudesListModel* modelo = e.vms.solicitudes();

    // Cargando: la primera lectura sigue pendiente.
    QCOMPARE(e.solicitudes.lista.size(), 1);
    QCOMPARE(modelo->estado(), EstadoLista::Cargando);
    QTRY_VERIFY(e.pagina() != nullptr);
    QTRY_VERIFY(e.item(QStringLiteral("estadoCargando"))->isVisible());
    QVERIFY(!e.item(QStringLiteral("listaSolicitudes"))->isVisible());
    QVERIFY(!e.item(QStringLiteral("estadoVacio"))->isVisible());

    // Error con reintento.
    e.solicitudes.lista.resolver(0, ResultadoListaSol::fallo(ErrorPersistencia::de(
                                        ErrorPersistencia::Tipo::Almacenamiento, QStringLiteral("x"))));
    QTRY_COMPARE(modelo->estado(), EstadoLista::Error);
    QTRY_VERIFY(e.item(QStringLiteral("estadoError"))->isVisible());
    auto* error = e.item(QStringLiteral("errorLista"));
    QVERIFY(!error->property("text").toString().isEmpty());
    // T013: el error de carga es un EstadoVacio de error, anunciado.
    QVERIFY(nombreAccesible(e.item(QStringLiteral("estadoError"))).startsWith(QStringLiteral("Error: ")));
    // Texto de presentacion, no el mensaje tecnico de ErrorPersistencia.
    QCOMPARE(error->property("text").toString(), modelo->errorMessage());
    QVERIFY(modelo->errorMessage() != QStringLiteral("x"));

    QMetaObject::invokeMethod(e.item(QStringLiteral("botonReintentarLista")), "click");
    QCOMPARE(e.solicitudes.lista.size(), 2);
    QCOMPARE(modelo->estado(), EstadoLista::Cargando);
    QTRY_VERIFY(e.item(QStringLiteral("estadoCargando"))->isVisible());

    // Vacia.
    e.solicitudes.lista.resolver(1, ResultadoListaSol::exito({}));
    QTRY_COMPARE(modelo->estado(), EstadoLista::Vacia);
    QTRY_VERIFY(e.item(QStringLiteral("estadoVacio"))->isVisible());
    QVERIFY(!e.item(QStringLiteral("estadoError"))->isVisible());

    // listaCambiada (commit en otro lado) refresca; un refresco con datos
    // previos no regresa a Cargando.
    emit e.solicitudes.listaCambiada();
    QCOMPARE(e.solicitudes.lista.size(), 3);
    QVERIFY(modelo->cargando());
    QCOMPARE(modelo->estado(), EstadoLista::Vacia);

    const SolicitudId id = SolicitudId::generar();
    e.solicitudes.lista.resolver(2, ResultadoListaSol::exito({resumenDePrueba(id)}));
    QTRY_COMPARE(modelo->estado(), EstadoLista::ConDatos);
    QVERIFY(!modelo->cargando());
    QCOMPARE(modelo->filaDe(id.texto()), 0);
    QTRY_VERIFY(e.item(QStringLiteral("listaSolicitudes"))->isVisible());
    QTRY_COMPARE(e.item(QStringLiteral("listaSolicitudes"))->property("count").toInt(), 1);
    QVERIFY(!e.item(QStringLiteral("estadoVacio"))->isVisible());
}

void TestPresentacion::listaDescartaRespuestaTardia()
{
    EscenarioAsincrono e;
    SolicitudesListModel* modelo = e.vms.solicitudes();
    QCOMPARE(e.solicitudes.lista.size(), 1);
    modelo->refrescar();
    QCOMPARE(e.solicitudes.lista.size(), 2);

    const SolicitudId reciente = SolicitudId::generar();
    e.solicitudes.lista.resolver(1, ResultadoListaSol::exito({resumenDePrueba(reciente)}));
    QTRY_COMPARE(modelo->rowCount(), 1);

    // La respuesta de la primera lectura llega despues y se descarta.
    e.solicitudes.lista.resolver(
        0, ResultadoListaSol::exito({resumenDePrueba(SolicitudId::generar()),
                                     resumenDePrueba(SolicitudId::generar())}));
    procesarEventos();
    QCOMPARE(modelo->rowCount(), 1);
    QCOMPARE(modelo->filaDe(reciente.texto()), 0);
    QCOMPARE(modelo->estado(), EstadoLista::ConDatos);
}

// ---------------------------------------------------------------------------
// T003: perfiles y nueva solicitud

void TestPresentacion::perfilesDescartaRespuestaTardia()
{
    EscenarioAsincrono e;
    NuevaSolicitudViewModel* f = e.vms.nuevaSolicitud();
    QCOMPARE(e.perfiles.listas.size(), 0);
    f->cargarPerfiles();
    f->cargarPerfiles();
    QCOMPARE(e.perfiles.listas.size(), 2);

    QVERIFY(resolverPerfiles(e, 1, {}));
    QTRY_VERIFY(f->sinPerfiles());
    QVERIFY(resolverPerfiles(e, 0, {perfilDePrueba()}));
    procesarEventos();
    QVERIFY(f->sinPerfiles());
    QCOMPARE(f->perfilesDisponibles()->count(), 0);
}

// ---------------------------------------------------------------------------
// T003: detalle

void TestPresentacion::detalleEstadosErrorConDatosYNoEncontrada()
{
    EscenarioAsincrono e;
    QVERIFY(e.cargar());
    QVERIFY(QTest::qWaitForWindowExposed(e.ventana));
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    SolicitudDetailViewModel* d = e.vms.detalle();

    const SolicitudId id = SolicitudId::generar();
    QVERIFY(e.vms.app()->abrirDetalle(id.texto()));
    QCOMPARE(d->estado(), EstadoDetalle::Cargando);
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaDetalleSolicitud"));
    QTRY_VERIFY(e.item(QStringLiteral("estadoCargandoDetalle"))->isVisible());
    QVERIFY(!e.item(QStringLiteral("botonEliminarSolicitud"))->isVisible());

    // Error con reintento.
    ErrorObtener fallo;
    fallo.tipo = ErrorObtener::Tipo::Persistencia;
    e.solicitudes.detalle.resolver(0, SolicitudesService::ResultadoDetalle::fallo(fallo));
    QTRY_COMPARE(d->estado(), EstadoDetalle::Error);
    QTRY_VERIFY(e.item(QStringLiteral("estadoErrorDetalle"))->isVisible());
    QVERIFY(nombreAccesible(e.item(QStringLiteral("estadoErrorDetalle"))).startsWith(QStringLiteral("Error: ")));
    QMetaObject::invokeMethod(e.item(QStringLiteral("botonReintentarDetalle")), "click");
    QCOMPARE(e.solicitudes.detalle.size(), 2);
    QCOMPARE(d->estado(), EstadoDetalle::Cargando);

    // Con datos: filtros, estados, codigos, fechas, paquetes y logs.
    e.solicitudes.detalle.resolver(1, SolicitudesService::ResultadoDetalle::exito(detalleDePrueba(id)));
    QTRY_COMPARE(d->estado(), EstadoDetalle::ConDatos);
    QCOMPARE(d->fechaInicialSat(), QStringLiteral("2026-07-01T00:00:00"));
    QCOMPARE(d->rfcContrapartes(), QStringList{QStringLiteral("XAXX010101000")});
    QCOMPARE(d->tipoComprobante(), QStringLiteral("I"));
    QCOMPARE(d->complemento(), QStringLiteral("nomina12"));
    QCOMPARE(d->codEstatusSolicitud(), QStringLiteral("5000"));
    QCOMPARE(d->numeroCfdi().toLongLong(), 42);
    QVERIFY(d->enviadaEn().isNull());
    QCOMPARE(d->estadoResumen(), QStringLiteral("Terminada"));
    QCOMPARE(d->paquetes().size(), 1);
    QCOMPARE(d->logs().size(), 1);
    QCOMPARE(d->logs().constFirst().toMap().value(QStringLiteral("tipoEvento")).toString(),
             QStringLiteral("solicitud_creada"));
    // Con paquetes la pestana inicial es Paquetes; los filtros estan en Datos.
    QTRY_VERIFY(e.item(QStringLiteral("seccionPaquetes"))->isVisible());
    QMetaObject::invokeMethod(e.pagina(), "mostrarPestana", Q_ARG(QVariant, QStringLiteral("datos")));
    QTRY_VERIFY(e.item(QStringLiteral("seccionFiltros"))->isVisible());
    QVERIFY(!e.item(QStringLiteral("estadoCargandoDetalle"))->isVisible());
    QCOMPARE(e.item(QStringLiteral("campoTipoComprobante"))->property("valor").toString(),
             QStringLiteral("Ingreso"));
    QVERIFY(e.item(QStringLiteral("campoCodigoSolicitud"))->property("valor").toString().startsWith(
        QStringLiteral("5000")));
    QVERIFY(!e.item(QStringLiteral("sinPaquetes"))->isVisible());
    QTRY_VERIFY(e.item(QStringLiteral("eventoLog_0")) != nullptr);
    QVERIFY(e.item(QStringLiteral("eventoLog_0"))->property("text").toString().contains(
        QStringLiteral("Solicitud creada")));
    QVERIFY(e.item(QStringLiteral("botonEliminarSolicitud"))->isVisible());

    // NoEncontrada desde el servicio (id inexistente o eliminado).
    const SolicitudId otro = SolicitudId::generar();
    QVERIFY(e.vms.app()->abrirDetalle(otro.texto()));
    QVERIFY(!d->cargada()); // cambiar de solicitud limpia los datos anteriores
    ErrorObtener noEncontrada;
    noEncontrada.tipo = ErrorObtener::Tipo::NoEncontrada;
    e.solicitudes.detalle.resolver(2, SolicitudesService::ResultadoDetalle::fallo(noEncontrada));
    QTRY_COMPARE(d->estado(), EstadoDetalle::NoEncontrada);
    auto* noEncontradaItem = e.item(QStringLiteral("estadoNoEncontrada"));
    QTRY_VERIFY(noEncontradaItem->isVisible());
    QVERIFY(nombreAccesible(noEncontradaItem).contains(QStringLiteral("no encontrada")));
    QVERIFY(!e.item(QStringLiteral("botonEliminarSolicitud"))->isVisible());
    QVERIFY(!e.item(QStringLiteral("seccionFiltros"))->isVisible());
}

void TestPresentacion::detalleReaccionaASolicitudEliminada()
{
    EscenarioAsincrono e;
    QVERIFY(e.cargar());
    QVERIFY(QTest::qWaitForWindowExposed(e.ventana));
    SolicitudDetailViewModel* d = e.vms.detalle();

    // Con datos visibles: otra parte de la app elimina la solicitud.
    const SolicitudId id = SolicitudId::generar();
    QVERIFY(e.vms.app()->abrirDetalle(id.texto()));
    e.solicitudes.detalle.resolver(0, SolicitudesService::ResultadoDetalle::exito(detalleDePrueba(id)));
    QTRY_COMPARE(d->estado(), EstadoDetalle::ConDatos);
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaDetalleSolicitud"));

    emit e.solicitudes.solicitudEliminada(SolicitudId::generar()); // otra solicitud: sin efecto
    QCOMPARE(d->estado(), EstadoDetalle::ConDatos);
    emit e.solicitudes.solicitudEliminada(id);
    QCOMPARE(d->estado(), EstadoDetalle::NoEncontrada);
    QVERIFY(!d->cargada());
    QTRY_VERIFY(e.item(QStringLiteral("estadoNoEncontrada"))->isVisible());

    // Una lectura en curso al eliminar no restaura el detalle al llegar tarde.
    d->recargar();
    QCOMPARE(e.solicitudes.detalle.size(), 2);
    emit e.solicitudes.solicitudEliminada(id);
    e.solicitudes.detalle.resolver(1, SolicitudesService::ResultadoDetalle::exito(detalleDePrueba(id)));
    procesarEventos();
    QCOMPARE(d->estado(), EstadoDetalle::NoEncontrada);
    QVERIFY(!d->cargada());

    // solicitudActualizada en NoEncontrada no recarga.
    emit e.solicitudes.solicitudActualizada(id);
    QCOMPARE(e.solicitudes.detalle.size(), 2);
}

void TestPresentacion::detalleDescartaRespuestaTardia()
{
    EscenarioAsincrono e;
    SolicitudDetailViewModel* d = e.vms.detalle();
    const SolicitudId primera = SolicitudId::generar();
    const SolicitudId segunda = SolicitudId::generar();

    d->cargar(primera.texto());
    d->cargar(segunda.texto());
    QCOMPARE(e.solicitudes.detalle.size(), 2);
    e.solicitudes.detalle.resolver(1, SolicitudesService::ResultadoDetalle::exito(detalleDePrueba(segunda)));
    QTRY_COMPARE(d->estado(), EstadoDetalle::ConDatos);
    e.solicitudes.detalle.resolver(0, SolicitudesService::ResultadoDetalle::exito(detalleDePrueba(primera)));
    procesarEventos();
    QCOMPARE(d->solicitudId(), segunda.texto());
    QCOMPARE(d->estado(), EstadoDetalle::ConDatos);

    // Una eliminacion pedida para otra solicitud no aplica a la actual.
    d->eliminar();
    QCOMPARE(e.solicitudes.idsEliminar.constLast(), segunda);
    QSignalSpy eliminada(d, &SolicitudDetailViewModel::eliminada);
    d->cargar(primera.texto());
    QVERIFY(!d->eliminando());
    e.solicitudes.eliminacion.resolver(0, SolicitudesService::ResultadoEliminar::exito({true, QDateTime()}));
    procesarEventos();
    QCOMPARE(eliminada.count(), 0);
}

void TestPresentacion::eliminarConConfirmacion()
{
    Escenario e(DemoSolicitudesService::Datos::Representativos);
    QVERIFY(e.cargar());
    QVERIFY(e.activar());
    QTRY_COMPARE(e.vms.solicitudes()->rowCount(), 11);
    const QString id = e.vms.solicitudes()->index(0).data(Rol::IdRole).toString();
    QVERIFY(e.vms.app()->abrirDetalle(id));
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaDetalleSolicitud"));
    QTRY_VERIFY(e.vms.detalle()->cargada());

    // Abrir y cancelar con Escape: no elimina; el foco regresa al boton.
    auto* boton = e.item(QStringLiteral("botonEliminarSolicitud"));
    QTRY_VERIFY(boton->isVisible());
    QObject* dialogo = e.dialogo(QStringLiteral("dialogoEliminar"));
    QVERIFY(dialogo);
    QMetaObject::invokeMethod(boton, "click");
    QTRY_VERIFY(dialogo->property("opened").toBool());
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("dialogoEliminarCancelar"));
    QTest::keyClick(e.ventana, Qt::Key_Escape);
    QTRY_VERIFY(!dialogo->property("visible").toBool());
    QCOMPARE(e.solicitudes.llamadasEliminar, 0);
    QCOMPARE(e.vms.app()->pagina(), Pagina::Detalle);
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("botonEliminarSolicitud"));

    // Abrir con teclado y confirmar: elimina y regresa a la lista.
    QTest::keyClick(e.ventana, Qt::Key_Space);
    QTRY_VERIFY(dialogo->property("opened").toBool());
    QMetaObject::invokeMethod(e.itemDeDialogo(QStringLiteral("dialogoEliminar"),
                                              QStringLiteral("dialogoEliminarConfirmar")),
                              "click");
    QCOMPARE(e.solicitudes.llamadasEliminar, 1);
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Diagnostico);
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaSolicitudes"));
    QTRY_COMPARE(e.vms.solicitudes()->rowCount(), 10);
    QCOMPARE(e.vms.solicitudes()->filaDe(id), -1);

    // Abrir el detalle del id eliminado muestra "solicitud no encontrada".
    QVERIFY(e.vms.app()->abrirDetalle(id));
    QTRY_COMPARE(e.vms.detalle()->estado(), EstadoDetalle::NoEncontrada);
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaDetalleSolicitud"));
    QTRY_VERIFY(e.item(QStringLiteral("estadoNoEncontrada"))->isVisible());
}

QTEST_MAIN(TestPresentacion)

void TestPresentacion::detalleAccionesManualesYEstadosAccesibles()
{
    EscenarioAsincrono e;
    AccionesEspia acciones;
    e.vms.setAccionesSolicitud(&acciones);
    QVERIFY(e.cargar());
    QVERIFY(QTest::qWaitForWindowExposed(e.ventana));
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    SolicitudDetailViewModel* d = e.vms.detalle();

    const SolicitudId id = SolicitudId::generar();
    QVERIFY(e.vms.app()->abrirDetalle(id.texto()));
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaDetalleSolicitud"));

    // Enviada, en proceso, con un paquete Disponible y los eventos nuevos.
    SolicitudDetalle detalle = detalleDePrueba(id);
    detalle.resumen.estadoSat = EstadoSolicitudSat::EnProceso;
    LogResumen noIniciado;
    noIniciado.tipoEvento = TipoEventoLog::EnvioNoIniciado;
    noIniciado.origen = OrigenLog::Usuario;
    noIniciado.creadoEn = detalle.resumen.creadaEn;
    LogResumen suspendida = noIniciado;
    suspendida.tipoEvento = TipoEventoLog::VerificacionSuspendida;
    suspendida.origen = OrigenLog::Worker;
    detalle.logs.append(noIniciado);
    detalle.logs.append(suspendida);
    e.solicitudes.detalle.resolver(0, SolicitudesService::ResultadoDetalle::exito(detalle));
    QTRY_VERIFY(d->cargada());

    QVERIFY(d->puedeVerificar());
    QVERIFY(d->puedeReintentarDescarga());
    QQuickItem* verificar = e.item(QStringLiteral("botonVerificarAhora"));
    QQuickItem* reintentar = e.item(QStringLiteral("botonReintentarDescarga"));
    QTRY_VERIFY(verificar->isVisible());
    QVERIFY(reintentar->isVisible());
    QCOMPARE(nombreAccesible(verificar), QStringLiteral("Verificar ahora"));
    QCOMPARE(nombreAccesible(reintentar), QStringLiteral("Reintentar descarga"));

    QMetaObject::invokeMethod(verificar, "click");
    QMetaObject::invokeMethod(reintentar, "click");
    QCOMPARE(acciones.llamadas, (QStringList{QStringLiteral("verificar:") + id.texto(),
                                             QStringLiteral("descargar:") + id.texto()}));
    QTRY_VERIFY(e.item(QStringLiteral("accionSolicitada"))->isVisible());
    QVERIFY(!d->accionSolicitada().isEmpty());

    // Textos de los eventos nuevos de T007 (Etiquetas.js).
    QTRY_VERIFY(e.item(QStringLiteral("eventoLog_2")));
    QVERIFY(e.item(QStringLiteral("eventoLog_1"))->property("text").toString().contains(
        QStringLiteral("Envío no iniciado; puede reenviarse manualmente")));
    QVERIFY(e.item(QStringLiteral("eventoLog_2"))->property("text").toString().contains(
        QStringLiteral("Verificación automática suspendida; use Verificar ahora")));

    // Terminada y descargada: ninguna accion aplica.
    QVERIFY(e.vms.app()->abrirDetalle(SolicitudId::generar().texto()));
    SolicitudDetalle cerrada = detalleDePrueba(SolicitudId::desdeTexto(d->solicitudId()).value());
    cerrada.paquetes.first().estadoDescarga = EstadoDescarga::Descargado;
    e.solicitudes.detalle.resolver(1, SolicitudesService::ResultadoDetalle::exito(cerrada));
    QTRY_VERIFY(d->cargada());
    QVERIFY(!d->puedeVerificar());
    QVERIFY(!d->puedeReintentarDescarga());
    QTRY_VERIFY(!verificar->isVisible());
    QVERIFY(!reintentar->isVisible());
    QVERIFY(d->accionSolicitada().isEmpty());
    e.vms.setAccionesSolicitud(nullptr);
}

void TestPresentacion::detalleSinAccionesNoMuestraBotones()
{
    EscenarioAsincrono e;
    QVERIFY(e.cargar());
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    SolicitudDetailViewModel* d = e.vms.detalle();
    const SolicitudId id = SolicitudId::generar();
    QVERIFY(e.vms.app()->abrirDetalle(id.texto()));
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaDetalleSolicitud"));
    SolicitudDetalle detalle = detalleDePrueba(id);
    detalle.resumen.estadoSat = EstadoSolicitudSat::Aceptada;
    e.solicitudes.detalle.resolver(0, SolicitudesService::ResultadoDetalle::exito(detalle));
    QTRY_VERIFY(d->cargada());
    QVERIFY(!d->puedeVerificar());
    QVERIFY(!d->puedeReintentarDescarga());
    QVERIFY(!e.item(QStringLiteral("botonVerificarAhora"))->isVisible());
    d->verificarAhora(); // sin acciones: no hace nada
    QVERIFY(d->accionSolicitada().isEmpty());
}

void TestPresentacion::detalleMuestraExistenciaSinCambiarEstado()
{
    EscenarioAsincrono e;
    ExistenciaEspia existencia;
    e.vms.setConsultaExistencia(&existencia);
    QVERIFY(e.cargar());
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    SolicitudDetailViewModel* d = e.vms.detalle();
    const SolicitudId id = SolicitudId::generar();
    QVERIFY(e.vms.app()->abrirDetalle(id.texto()));
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaDetalleSolicitud"));

    // Tres paquetes Descargado y uno Disponible (este no se consulta).
    SolicitudDetalle detalle = detalleDePrueba(id);
    const QStringList ids{QStringLiteral("PAQ_01"), QStringLiteral("PAQ_02"), QStringLiteral("PAQ_03")};
    detalle.paquetes.clear();
    for (const QString& p : ids) {
        PaqueteResumen r;
        r.idPaqueteSat = p;
        r.estadoDescarga = EstadoDescarga::Descargado;
        r.disponibleEn = detalle.resumen.creadaEn;
        r.descargadoEn = detalle.resumen.creadaEn;
        detalle.paquetes.append(r);
    }
    PaqueteResumen disponible;
    disponible.idPaqueteSat = QStringLiteral("PAQ_04");
    disponible.disponibleEn = detalle.resumen.creadaEn;
    detalle.paquetes.append(disponible);
    detalle.resumen.totalPaquetes = int(detalle.paquetes.size());
    e.solicitudes.detalle.resolver(0, SolicitudesService::ResultadoDetalle::exito(detalle));
    QTRY_VERIFY(d->cargada());
    QCOMPARE(existencia.consultados, ids);

    auto existenciaDe = [&](int i) { return d->paquetes().at(i).toMap().value(QStringLiteral("existencia")).toString(); };
    QCOMPARE(existenciaDe(0), QStringLiteral("Comprobando"));
    existencia.resolver(0, ExistenciaPaquete::Presente);
    existencia.resolver(1, ExistenciaPaquete::NoEncontrado);
    existencia.resolver(2, ExistenciaPaquete::ErrorComprobacion);
    QTRY_COMPARE(existenciaDe(2), QStringLiteral("ErrorComprobacion"));
    QCOMPARE(existenciaDe(0), QStringLiteral("Presente"));
    QCOMPARE(existenciaDe(1), QStringLiteral("NoEncontrado"));
    QCOMPARE(existenciaDe(3), QString()); // Disponible: no aplica

    // Textos accesibles por paquete; el estado persistido no cambia.
    QTRY_COMPARE(e.item(QStringLiteral("existenciaPaquete_PAQ_01"))->property("text").toString(),
                 QStringLiteral("Archivo local presente"));
    QCOMPARE(e.item(QStringLiteral("existenciaPaquete_PAQ_02"))->property("text").toString(),
             QStringLiteral("Archivo local no encontrado: el ZIP se movió o se borró fuera de la app."));
    QCOMPARE(e.item(QStringLiteral("existenciaPaquete_PAQ_03"))->property("text").toString(),
             QStringLiteral("No se pudo comprobar el archivo local"));
    QCOMPARE(nombreAccesible(e.item(QStringLiteral("existenciaPaquete_PAQ_02"))),
             QStringLiteral("Archivo local no encontrado: el ZIP se movió o se borró fuera de la app."));
    // Disponible: sin texto de existencia (su mensaje es otro).
    QQuickItem* existencia4 = e.item(QStringLiteral("existenciaPaquete_PAQ_04"));
    QVERIFY(existencia4 == nullptr || !existencia4->isVisible());
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(d->paquetes().at(i).toMap().value(QStringLiteral("estadoDescarga")).toString(),
                 QStringLiteral("Descargado"));
    }
    QCOMPARE(e.solicitudes.detalle.size(), 1); // sin recargas ni escrituras
    e.vms.setConsultaExistencia(nullptr);
}

void TestPresentacion::enviarSoloConCredencialLista()
{
    EscenarioAsincrono e;
    AccionesEspia acciones;
    e.vms.setAccionesSolicitud(&acciones);
    QVERIFY(e.cargar());
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    SolicitudDetailViewModel* d = e.vms.detalle();
    const SolicitudId id = SolicitudId::generar();
    QVERIFY(e.vms.app()->abrirDetalle(id.texto()));
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaDetalleSolicitud"));

    SolicitudDetalle creada;
    creada.resumen = resumenDePrueba(id); // Creada, perfil EKU9003173C9
    const int listasAntes = e.perfiles.listas.size();
    e.solicitudes.detalle.resolver(0, SolicitudesService::ResultadoDetalle::exito(creada));
    QTRY_VERIFY(d->cargada());
    QVERIFY(d->envioVisible());
    QVERIFY(!d->puedeEnviar()); // aun verificando
    QTRY_COMPARE(e.perfiles.listas.size(), listasAntes + 1);

    // Credencial vencida: Enviar visible pero deshabilitado, con el motivo D10.
    QVERIFY(resolverPerfiles(e, listasAntes, {perfilDePrueba()}, EstadoCredencial::Vencida));
    QTRY_COMPARE(d->motivoEnvio(),
                 QStringLiteral("La e.firma de este perfil está vencida. Reemplázala para continuar."));
    QVERIFY(!d->puedeEnviar());
    QQuickItem* enviar = e.item(QStringLiteral("botonEnviarSolicitud"));
    QTRY_VERIFY(enviar->isVisible());
    QVERIFY(!enviar->isEnabled());
    QTRY_VERIFY(e.item(QStringLiteral("motivoEnvio"))->isVisible());
    d->enviar();
    QVERIFY(acciones.llamadas.isEmpty()); // sin trafico

    // La credencial cambia a Lista: se reconsulta y Enviar se habilita.
    emit e.credenciales.credencialCambio(perfilDePrueba().id.texto());
    QTRY_COMPARE(e.perfiles.listas.size(), listasAntes + 2);
    QVERIFY(resolverPerfiles(e, listasAntes + 1, {perfilDePrueba()}, EstadoCredencial::Lista));
    QTRY_VERIFY(d->puedeEnviar());
    QTRY_VERIFY(enviar->isEnabled());
    QVERIFY(d->motivoEnvio().isEmpty());
    QMetaObject::invokeMethod(enviar, "click");
    QCOMPARE(acciones.llamadas, QStringList{QStringLiteral("enviar:") + id.texto()});
    e.vms.setAccionesSolicitud(nullptr);
}

void TestPresentacion::mensajesDelCatalogoYSin5008()
{
    EscenarioAsincrono e;
    AccionesEspia acciones;
    e.vms.setAccionesSolicitud(&acciones);
    QVERIFY(e.cargar());
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    SolicitudDetailViewModel* d = e.vms.detalle();

    auto abrir = [&](int indice, SolicitudDetalle detalle) {
        QVERIFY(e.vms.app()->abrirDetalle(detalle.resumen.id.texto()));
        QTRY_COMPARE(int(e.solicitudes.detalle.size()), indice + 1);
        e.solicitudes.detalle.resolver(indice, SolicitudesService::ResultadoDetalle::exito(detalle));
        QTRY_VERIFY(d->cargada() && d->solicitudId() == detalle.resumen.id.texto());
    };

    // Rechazo 5002 en la creacion.
    SolicitudDetalle fallida;
    fallida.resumen = resumenDePrueba(SolicitudId::generar());
    fallida.resumen.estadoLocal = EstadoLocal::EnvioFallido;
    fallida.codEstatusSolicitud = QStringLiteral("5002");
    abrir(0, fallida);
    QCOMPARE(d->mensajeEstado(), QStringLiteral("El SAT ya no acepta solicitudes con este mismo criterio."));
    QTRY_VERIFY(e.item(QStringLiteral("mensajeEstado")) && e.item(QStringLiteral("mensajeEstado"))->isVisible());

    // Creada tras una falla de autenticacion: texto D10 ya desglosado.
    SolicitudDetalle creadaConError;
    creadaConError.resumen = resumenDePrueba(SolicitudId::generar());
    creadaConError.ultimoError = QStringLiteral("x");
    UltimoErrorDesglosado ue;
    ue.fase = FaseOperacion::Autenticacion;
    ue.mensaje = QStringLiteral("El SAT no aceptó la autenticación con esta e.firma.");
    creadaConError.ultimoErrorDesglosado = ue;
    abrir(1, creadaConError);
    QCOMPARE(d->mensajeEstado(), QStringLiteral("El SAT no aceptó la autenticación con esta e.firma."));

    // Envio incierto.
    SolicitudDetalle incierta;
    incierta.resumen = resumenDePrueba(SolicitudId::generar());
    incierta.resumen.estadoLocal = EstadoLocal::EnvioIncierto;
    abrir(2, incierta);
    QVERIFY(d->mensajeEstado().startsWith(QStringLiteral("No se sabe si el SAT registró la solicitud.")));
    QVERIFY(!d->envioVisible()); // nunca se reenvia el mismo registro

    // Paquetes: 5008 sin Reintentar; vencido; error reintentable.
    SolicitudDetalle conPaquetes = detalleDePrueba(SolicitudId::generar());
    conPaquetes.paquetes.first().estadoDescarga = EstadoDescarga::Error;
    conPaquetes.paquetes.first().codigoDescargaSat = QStringLiteral("5008");
    PaqueteResumen vencido = conPaquetes.paquetes.first();
    vencido.idPaqueteSat = QStringLiteral("PAQ_02");
    vencido.estadoDescarga = EstadoDescarga::Vencido;
    vencido.codigoDescargaSat = QStringLiteral("5007");
    conPaquetes.paquetes.append(vencido);
    abrir(3, conPaquetes);
    QVERIFY(!d->puedeReintentarDescarga()); // solo 5008 y Vencido: nada reintentable
    QTRY_COMPARE(e.item(QStringLiteral("mensajePaquete_PAQ_01"))->property("text").toString(),
                 QStringLiteral("El paquete alcanzó el máximo de descargas permitidas."));
    QCOMPARE(e.item(QStringLiteral("mensajePaquete_PAQ_02"))->property("text").toString(),
             QStringLiteral("El paquete ya no existe en el SAT (vencido). Crea una solicitud nueva para el mismo periodo."));
    QVERIFY(!e.item(QStringLiteral("botonReintentarDescarga"))->isVisible());

    SolicitudDetalle reintentable = detalleDePrueba(SolicitudId::generar());
    reintentable.paquetes.first().estadoDescarga = EstadoDescarga::Error;
    reintentable.paquetes.first().codigoDescargaSat = QStringLiteral("5000");
    abrir(4, reintentable);
    QVERIFY(d->puedeReintentarDescarga());
    QCOMPARE(d->paquetes().constFirst().toMap().value(QStringLiteral("mensaje")).toString(),
             QStringLiteral("No se pudo descargar el paquete. Puedes reintentar."));
    // Sin RFC ni Ids en los mensajes visibles.
    QVERIFY(!d->mensajeEstado().contains(QStringLiteral("EKU9003173C9")));
    e.vms.setAccionesSolicitud(nullptr);
}

void TestPresentacion::sinFacturasEnDiagnostico()
{
    // Rechazada con CodigoEstadoSolicitud 5004: estado y mensajes
    // informativos, sin "rechazó" ni "error"; la lista la resume como
    // Terminada. Con otro codigo sigue siendo Rechazada.
    EscenarioAsincrono e;
    QVERIFY(e.cargar());
    SolicitudResumen resumen = resumenDePrueba(SolicitudId::generar());
    resumen.estadoLocal = EstadoLocal::Enviada;
    resumen.estadoSat = EstadoSolicitudSat::Rechazada;
    resumen.codigoEstadoSolicitud = QStringLiteral("5004");
    resumen.totalPaquetes = 0;
    SolicitudResumen otro = resumenDePrueba(SolicitudId::generar());
    otro.estadoLocal = EstadoLocal::Enviada;
    otro.estadoSat = EstadoSolicitudSat::Rechazada;
    otro.codigoEstadoSolicitud = QStringLiteral("5003");
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({resumen, otro}));
    SolicitudesListModel* lista = e.vms.solicitudes();
    QTRY_COMPARE(lista->rowCount(), 2);
    QCOMPARE(lista->index(lista->filaDe(resumen.id.texto())).data(Rol::EstadoResumenRole).toString(),
             QStringLiteral("Terminada"));
    QCOMPARE(lista->index(lista->filaDe(otro.id.texto())).data(Rol::EstadoResumenRole).toString(),
             QStringLiteral("Rechazada"));

    SolicitudDetailViewModel* d = e.vms.detalle();
    SolicitudDetalle detalle;
    detalle.resumen = resumen;
    detalle.codEstatusSolicitud = QStringLiteral("5000");
    detalle.codigoEstadoSolicitud = QStringLiteral("5004");
    detalle.numeroCfdi = 0;
    QVERIFY(e.vms.app()->abrirDetalle(resumen.id.texto()));
    QTRY_COMPARE(int(e.solicitudes.detalle.size()), 1);
    e.solicitudes.detalle.resolver(0, SolicitudesService::ResultadoDetalle::exito(detalle));
    QTRY_VERIFY(d->cargada());
    QCOMPARE(d->estadoResumen(), QStringLiteral("Terminada"));
    QCOMPARE(d->titularResumen(), QStringLiteral("El SAT no tiene facturas en este periodo"));
    QCOMPARE(d->mensajeEstado(), QStringLiteral("El SAT no tiene facturas en este periodo."));
    for (const QString& t : {d->titularResumen(), d->descripcionResumen(), d->mensajeEstado()}) {
        QVERIFY2(!t.contains(QStringLiteral("rechaz"), Qt::CaseInsensitive)
                     && !t.contains(QStringLiteral("error"), Qt::CaseInsensitive)
                     && !t.contains(QStringLiteral("no encontró")),
                 qPrintable(t));
    }
    QVERIFY(!d->puedeVerificar());

    SolicitudDetalle rechazada = detalle;
    rechazada.resumen = otro;
    rechazada.codigoEstadoSolicitud = QStringLiteral("5003");
    QVERIFY(e.vms.app()->abrirDetalle(otro.id.texto()));
    QTRY_COMPARE(int(e.solicitudes.detalle.size()), 2);
    e.solicitudes.detalle.resolver(1, SolicitudesService::ResultadoDetalle::exito(rechazada));
    QTRY_VERIFY(d->cargada() && d->solicitudId() == otro.id.texto());
    QCOMPARE(d->estadoResumen(), QStringLiteral("Rechazada"));
    QCOMPARE(d->titularResumen(), QStringLiteral("El SAT rechazó la solicitud"));
}

void TestPresentacion::avisoNotificacionesDeshabilitadas()
{
    EscenarioAsincrono e;
    QVERIFY(e.cargar());
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    QQuickItem* aviso = nullptr;
    QTRY_VERIFY((aviso = e.item(QStringLiteral("avisoNotificaciones"))) != nullptr);
    QVERIFY(!aviso->isVisible());
    e.vms.app()->setNotificacionesDeshabilitadas(true);
    QTRY_VERIFY(aviso->isVisible());
    QVERIFY(nombreAccesible(aviso).startsWith(QStringLiteral("Las notificaciones están desactivadas")));
    e.vms.app()->setNotificacionesDeshabilitadas(false);
    QTRY_VERIFY(!aviso->isVisible());
}

void TestPresentacion::finderEnDetalleSegunExistencia()
{
    EscenarioAsincrono e;
    ExistenciaEspia existencia;
    FinderEspia finder;
    e.vms.setConsultaExistencia(&existencia);
    e.vms.setAccionesFinder(&finder);
    QVERIFY(e.cargar());
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    SolicitudDetailViewModel* d = e.vms.detalle();

    // Sin paquetes Descargado: no hay carpeta de la solicitud.
    SolicitudDetalle sinDescargas = detalleDePrueba(SolicitudId::generar()); // PAQ_01 Disponible
    QVERIFY(e.vms.app()->abrirDetalle(sinDescargas.resumen.id.texto()));
    QTRY_COMPARE(int(e.solicitudes.detalle.size()), 1);
    e.solicitudes.detalle.resolver(0, SolicitudesService::ResultadoDetalle::exito(sinDescargas));
    QTRY_VERIFY(d->cargada());
    QVERIFY(!d->puedeAbrirCarpeta());
    QTRY_VERIFY(e.item(QStringLiteral("botonAbrirCarpetaSolicitud")));
    QVERIFY(!e.item(QStringLiteral("botonAbrirCarpetaSolicitud"))->isVisible());
    QVERIFY(!e.item(QStringLiteral("botonMostrarFinder_PAQ_01"))->isVisible());

    // Dos Descargado: uno Presente y otro NoEncontrado.
    SolicitudDetalle detalle = detalleDePrueba(SolicitudId::generar());
    detalle.paquetes.first().estadoDescarga = EstadoDescarga::Descargado;
    detalle.paquetes.first().descargadoEn = detalle.resumen.creadaEn;
    PaqueteResumen segundo = detalle.paquetes.first();
    segundo.idPaqueteSat = QStringLiteral("PAQ_02");
    detalle.paquetes.append(segundo);
    QVERIFY(e.vms.app()->abrirDetalle(detalle.resumen.id.texto()));
    QTRY_COMPARE(int(e.solicitudes.detalle.size()), 2);
    e.solicitudes.detalle.resolver(1, SolicitudesService::ResultadoDetalle::exito(detalle));
    QTRY_VERIFY(d->cargada() && d->solicitudId() == detalle.resumen.id.texto());
    QTRY_COMPARE(existencia.promesas.size(), 2);
    existencia.resolver(0, ExistenciaPaquete::Presente);
    existencia.resolver(1, ExistenciaPaquete::NoEncontrado);

    QQuickItem* mostrar1 = nullptr;
    QTRY_VERIFY((mostrar1 = e.item(QStringLiteral("botonMostrarFinder_PAQ_01"))) && mostrar1->isEnabled());
    QQuickItem* mostrar2 = e.item(QStringLiteral("botonMostrarFinder_PAQ_02"));
    QVERIFY(mostrar2 && mostrar2->isVisible() && !mostrar2->isEnabled());
    QCOMPARE(nombreAccesible(mostrar1), QStringLiteral("Mostrar en Finder"));
    // T013: con todo descargado, "Abrir carpeta" es la accion principal del resumen.
    QQuickItem* carpeta = e.item(QStringLiteral("botonAbrirCarpetaSolicitud"));
    if (!carpeta->isVisible())
        carpeta = e.item(QStringLiteral("accionAbrirCarpeta"));
    QTRY_VERIFY(carpeta->isVisible());
    QCOMPARE(nombreAccesible(carpeta), QStringLiteral("Abrir carpeta de la solicitud"));

    // Un paquete no Presente no se puede mostrar ni via el view model.
    d->mostrarEnFinder(QStringLiteral("PAQ_02"));
    QVERIFY(finder.llamadas.isEmpty());

    // Mostrar: el archivo ya no existe -> mensaje D7 y existencia refrescada.
    QMetaObject::invokeMethod(mostrar1, "click");
    QCOMPARE(finder.llamadas, QStringList{QStringLiteral("paquete:PAQ_01")});
    finder.resolver(0, ResultadoAccionFinder::Estado::NoEncontrado, QStringLiteral("Archivo local no encontrado"));
    QTRY_COMPARE(d->mensajeFinder(), QStringLiteral("Archivo local no encontrado"));
    QTRY_VERIFY(e.item(QStringLiteral("mensajeFinder"))->isVisible());
    QCOMPARE(nombreAccesible(e.item(QStringLiteral("mensajeFinder"))), QStringLiteral("Archivo local no encontrado"));
    QTRY_COMPARE(existencia.promesas.size(), 4); // se volvio a consultar
    QCOMPARE(d->paquetes().at(0).toMap().value(QStringLiteral("estadoDescarga")).toString(),
             QStringLiteral("Descargado"));

    // Carpeta: Finder fallido.
    QMetaObject::invokeMethod(carpeta, "click");
    QCOMPARE(finder.llamadas.last(), QStringLiteral("solicitud"));
    QVERIFY(d->mensajeFinder().isEmpty());
    finder.resolver(1, ResultadoAccionFinder::Estado::Fallido, QStringLiteral("No se pudo abrir Finder"));
    QTRY_COMPARE(d->mensajeFinder(), QStringLiteral("No se pudo abrir Finder"));
    QCOMPARE(e.solicitudes.detalle.size(), 2); // ni recargas ni escrituras
    e.vms.setAccionesFinder(nullptr);
    e.vms.setConsultaExistencia(nullptr);
}

void TestPresentacion::finderCarpetaDePaquetesEnLista()
{
    EscenarioAsincrono e;
    FinderEspia finder;
    QVERIFY(e.cargar());
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    QQuickItem* boton = nullptr;
    QTRY_VERIFY((boton = e.item(QStringLiteral("botonAbrirCarpetaPaquetes"))) != nullptr);
    QVERIFY(!boton->isVisible()); // sin acciones conectadas
    e.vms.setAccionesFinder(&finder);
    QTRY_VERIFY(boton->isVisible());
    QCOMPARE(nombreAccesible(boton), QStringLiteral("Abrir carpeta de paquetes"));
    QMetaObject::invokeMethod(boton, "click");
    QCOMPARE(finder.llamadas, QStringList{QStringLiteral("raiz")});
    finder.resolver(0, ResultadoAccionFinder::Estado::NoEncontrado, QStringLiteral("Carpeta de paquetes no encontrada"));
    QTRY_COMPARE(e.vms.app()->mensajeFinder(), QStringLiteral("Carpeta de paquetes no encontrada"));
    QTRY_VERIFY(e.item(QStringLiteral("mensajeFinderLista"))->isVisible());
    // Exito posterior limpia el aviso.
    QMetaObject::invokeMethod(boton, "click");
    finder.resolver(1, ResultadoAccionFinder::Estado::Mostrado);
    QTRY_VERIFY(e.vms.app()->mensajeFinder().isEmpty());
    e.vms.setAccionesFinder(nullptr);
}

#include "TestPresentacion.moc"

// ---------------------------------------------------------------------------
// T014.1: mejoras de pantalla

namespace {

QAbstractItemModel* filtroDeLaPagina(const Vista& v)
{
    return v.pagina() ? qobject_cast<QAbstractItemModel*>(v.pagina()->property("filtroModelo").value<QObject*>())
                      : nullptr;
}

QStringList idsDe(const QAbstractItemModel* m)
{
    QStringList ids;
    for (int fila = 0; fila < m->rowCount(); ++fila) {
        ids.append(m->index(fila, 0).data(Rol::IdRole).toString());
    }
    return ids;
}

template <typename Pred>
QStringList idsQueCumplen(const QAbstractItemModel* m, Pred pred)
{
    QStringList ids;
    for (int fila = 0; fila < m->rowCount(); ++fila) {
        const QModelIndex i = m->index(fila, 0);
        if (pred(i)) {
            ids.append(i.data(Rol::IdRole).toString());
        }
    }
    return ids;
}

SolicitudResumen resumenConPeriodo(QDate inicial, QDate final_)
{
    SolicitudResumen r = resumenDePrueba(SolicitudId::generar());
    r.fechaInicial = inicial;
    r.fechaFinal = final_;
    return r;
}

SolicitudResumen resumenConPaquetes(int total, int descargados, int pendientes)
{
    SolicitudResumen r = resumenDePrueba(SolicitudId::generar());
    r.estadoLocal = EstadoLocal::Enviada;
    r.estadoSat = EstadoSolicitudSat::Terminada;
    r.totalPaquetes = total;
    r.paquetesDescargados = descargados;
    r.paquetesPendientesDescarga = pendientes;
    return r;
}

void atajo(QQuickWindow* ventana, Qt::Key tecla)
{
    QTest::keyClick(ventana, tecla, Qt::ControlModifier);
    procesarEventos();
}

} // namespace

void TestPresentacion::filtrosLocalesDeLaLista()
{
    Escenario e(DemoSolicitudesService::Datos::Representativos);
    QVERIFY(e.cargar());
    QVERIFY(e.activar());
    SolicitudesListModel* m = e.vms.solicitudes();
    AppViewModel* app = e.vms.app();
    QTRY_COMPARE(m->rowCount(), 11);
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaSolicitudes"));
    QAbstractItemModel* filtro = filtroDeLaPagina(e);
    QVERIFY(filtro);
    const QStringList todos = idsDe(m);
    QCOMPARE(idsDe(filtro), todos);
    QVERIFY(!e.item(QStringLiteral("botonLimpiarFiltros"))->isVisible());

    // Busqueda por parte del RFC, sin distinguir mayusculas, escrita con teclado.
    const QString parte = m->index(0).data(Rol::PerfilRfcRole).toString().left(4).toLower();
    auto* busqueda = e.item(QStringLiteral("campoBusqueda"));
    QTRY_VERIFY(busqueda->isVisible());
    QVERIFY(!nombreAccesible(busqueda).isEmpty());
    busqueda->forceActiveFocus();
    for (const QChar c : parte) {
        QTest::keyClick(e.ventana, c.toLatin1());
    }
    QTRY_COMPARE(app->filtroTexto(), parte);
    const auto coincideTexto = [&](const QModelIndex& i) {
        return i.data(Rol::PerfilRfcRole).toString().contains(parte, Qt::CaseInsensitive)
               || i.data(Rol::PerfilNombreRole).toString().contains(parte, Qt::CaseInsensitive);
    };
    QStringList esperado = idsQueCumplen(m, coincideTexto);
    QVERIFY(!esperado.isEmpty());
    QCOMPARE(idsDe(filtro), esperado); // mismo orden que la fuente
    QVERIFY(e.item(QStringLiteral("botonLimpiarFiltros"))->isVisible());

    // + Estado (desde el combo) y Tipo.
    const QString estado = m->index(0).data(Rol::EstadoResumenRole).toString();
    const QString tipo = m->index(0).data(Rol::TipoDescargaRole).toString();
    auto* comboEstado = e.item(QStringLiteral("filtroEstado"));
    const int indiceEstado = QQmlProperty::read(comboEstado, QStringLiteral("count")).toInt() > 0
        ? [&] {
              int i = -1;
              QMetaObject::invokeMethod(comboEstado, "indexOfValue", Q_RETURN_ARG(int, i), Q_ARG(QVariant, estado));
              return i;
          }()
        : -1;
    QVERIFY(indiceEstado > 0);
    QMetaObject::invokeMethod(comboEstado, "activated", Q_ARG(int, indiceEstado));
    QTRY_COMPARE(app->filtroEstado(), estado);
    app->setFiltroTipo(tipo);
    esperado = idsQueCumplen(m, [&](const QModelIndex& i) {
        return coincideTexto(i) && i.data(Rol::EstadoResumenRole).toString() == estado
               && i.data(Rol::TipoDescargaRole).toString() == tipo;
    });
    QVERIFY(!esperado.isEmpty());
    QCOMPARE(idsDe(filtro), esperado);
    QCOMPARE(m->rowCount(), 11); // la lista persistida no cambia
    QCOMPARE(e.item(QStringLiteral("listaSolicitudes"))->property("count").toInt(), esperado.size());

    // Los filtros se conservan al navegar y volver.
    QVERIFY(app->abrirDetalle(esperado.constFirst()));
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaDetalleSolicitud"));
    app->mostrarDiagnostico();
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaSolicitudes"));
    filtro = filtroDeLaPagina(e);
    QVERIFY(filtro);
    QCOMPARE(idsDe(filtro), esperado);
    QCOMPARE(e.item(QStringLiteral("campoBusqueda"))->property("text").toString(), parte);
    QTRY_COMPARE(e.item(QStringLiteral("filtroEstado"))->property("currentValue").toString(), estado);
    QTRY_COMPARE(e.item(QStringLiteral("filtroTipo"))->property("currentValue").toString(), tipo);

    // Sin coincidencias: estado vacio con "Limpiar filtros".
    app->setFiltroTexto(QStringLiteral("sin-coincidencias-xyz"));
    QTRY_VERIFY(e.item(QStringLiteral("estadoSinCoincidencias"))->isVisible());
    QVERIFY(!e.item(QStringLiteral("listaSolicitudes"))->isVisible());
    QVERIFY(!e.item(QStringLiteral("estadoVacio"))->isVisible());
    QCOMPARE(filtro->rowCount(), 0);
    QCOMPARE(m->rowCount(), 11);
    auto* limpiar = e.item(QStringLiteral("botonLimpiarFiltrosVacio"));
    QCOMPARE(nombreAccesible(limpiar), QStringLiteral("Limpiar filtros"));
    QMetaObject::invokeMethod(limpiar, "click");
    QTRY_COMPARE(idsDe(filtro), todos);
    QVERIFY(app->filtroTexto().isEmpty() && app->filtroEstado().isEmpty() && app->filtroTipo().isEmpty()
            && app->filtroMes().isEmpty());
    QTRY_VERIFY(e.item(QStringLiteral("listaSolicitudes"))->isVisible());
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("campoBusqueda"));
}

void TestPresentacion::filtroPorMesIncluyePeriodosQueCruzan()
{
    SolicitudesServiceAsincrono servicio;
    SolicitudesListModel modelo(&servicio);
    const SolicitudResumen cruza = resumenConPeriodo(QDate(2026, 6, 15), QDate(2026, 8, 10));
    const SolicitudResumen julio = resumenConPeriodo(QDate(2026, 7, 1), QDate(2026, 7, 31));
    const SolicitudResumen septiembre = resumenConPeriodo(QDate(2026, 9, 1), QDate(2026, 9, 30));
    const SolicitudResumen cambioDeAnio = resumenConPeriodo(QDate(2025, 12, 20), QDate(2026, 1, 5));
    servicio.lista.resolver(0, ResultadoListaSol::exito({cruza, julio, septiembre, cambioDeAnio}));
    QTRY_COMPARE(modelo.rowCount(), 4);

    SolicitudesFiltroModel filtro;
    QSignalSpy cambios(&filtro, &SolicitudesFiltroModel::countChanged);
    filtro.setFuente(&modelo);
    QCOMPARE(filtro.mesesDisponibles(),
             (QStringList{QStringLiteral("2026-09"), QStringLiteral("2026-08"), QStringLiteral("2026-07"),
                          QStringLiteral("2026-06"), QStringLiteral("2026-01"), QStringLiteral("2025-12")}));
    const auto ids = [](std::initializer_list<SolicitudResumen> rs) {
        QStringList r;
        for (const SolicitudResumen& s : rs) {
            r.append(s.id.texto());
        }
        return r;
    };
    QCOMPARE(idsDe(&filtro), ids({cruza, julio, septiembre, cambioDeAnio}));
    QVERIFY(!filtro.hayFiltros());

    filtro.setMes(QStringLiteral("2026-06"));
    QCOMPARE(idsDe(&filtro), ids({cruza}));
    QVERIFY(filtro.hayFiltros());
    filtro.setMes(QStringLiteral("2026-07"));
    QCOMPARE(idsDe(&filtro), ids({cruza, julio}));
    filtro.setMes(QStringLiteral("2026-08"));
    QCOMPARE(idsDe(&filtro), ids({cruza}));
    filtro.setMes(QStringLiteral("2025-12"));
    QCOMPARE(idsDe(&filtro), ids({cambioDeAnio}));
    filtro.setMes(QStringLiteral("2026-01"));
    QCOMPARE(idsDe(&filtro), ids({cambioDeAnio}));
    filtro.setMes(QStringLiteral("2026-05"));
    QCOMPARE(filtro.count(), 0);
    QVERIFY(cambios.count() > 0);
    filtro.setMes(QString());
    QCOMPARE(idsDe(&filtro), ids({cruza, julio, septiembre, cambioDeAnio}));
    QCOMPARE(filtro.idEn(1), julio.id.texto());
    QCOMPARE(filtro.idEn(9), QString());
    QCOMPARE(FormatoFechas().mesAnio(QStringLiteral("2026-09")), QStringLiteral("sep 2026"));
    QCOMPARE(FormatoFechas().mesAnio(QStringLiteral("2026-13")), QString());
}

void TestPresentacion::conteoDePaquetesEnLaLista()
{
    EscenarioAsincrono e;
    QVERIFY(e.cargar());
    QVERIFY(QTest::qWaitForWindowExposed(e.ventana));
    const SolicitudResumen ninguno = resumenConPaquetes(3, 0, 0);
    SolicitudResumen dos = resumenConPaquetes(3, 2, 1);
    const SolicitudResumen todos = resumenConPaquetes(3, 3, 0);
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({ninguno, dos, todos}));
    QTRY_COMPARE(e.vms.solicitudes()->rowCount(), 3);
    QCOMPARE(e.vms.solicitudes()->index(1).data(Rol::PaquetesDescargadosRole).toInt(), 2);
    QCOMPARE(e.vms.solicitudes()->index(1).data(Rol::PaquetesPendientesDescargaRole).toInt(), 1);

    const auto en = [&](int fila, const char* nombre) {
        return buscar(e.item(QStringLiteral("filaSolicitud_%1").arg(fila)), QString::fromLatin1(nombre));
    };
    QTRY_VERIFY(e.item(QStringLiteral("filaSolicitud_2")));
    QCOMPARE(en(0, "paquetesFila")->property("text").toString(), QStringLiteral("0/3"));
    QVERIFY(en(0, "barraPaquetesFila")->isVisible());
    QVERIFY(!en(0, "checkPaquetesFila")->isVisible());
    QVERIFY(!en(0, "pendienteFila")->isVisible());
    QCOMPARE(en(1, "paquetesFila")->property("text").toString(), QStringLiteral("2/3"));
    QVERIFY(en(1, "barraPaquetesFila")->isVisible());
    QVERIFY(en(1, "pendienteFila")->isVisible());
    QVERIFY(nombreAccesible(e.item(QStringLiteral("filaSolicitud_1"))).contains(QStringLiteral("Pendiente de descarga")));
    QCOMPARE(en(2, "paquetesFila")->property("text").toString(), QStringLiteral("3"));
    QVERIFY(en(2, "checkPaquetesFila")->isVisible());
    QVERIFY(!en(2, "barraPaquetesFila")->isVisible());
    QVERIFY(!en(2, "pendienteFila")->isVisible());

    // Tras una descarga, la solicitud se actualiza y el conteo cambia.
    emit e.solicitudes.solicitudActualizada(dos.id);
    dos.paquetesDescargados = 3;
    dos.paquetesPendientesDescarga = 0;
    QTRY_COMPARE(e.solicitudes.lista.size(), 2);
    e.solicitudes.lista.resolver(1, ResultadoListaSol::exito({ninguno, dos, todos}));
    QTRY_COMPARE(en(1, "paquetesFila")->property("text").toString(), QStringLiteral("3"));
    QVERIFY(en(1, "checkPaquetesFila")->isVisible());
    QVERIFY(!en(1, "pendienteFila")->isVisible());
}

void TestPresentacion::atajosDeTeclado()
{
    EscenarioAsincrono e;
    AccionesEspia acciones;
    e.vms.setAccionesSolicitud(&acciones);
    QVERIFY(e.cargar());
    QVERIFY(e.activar());
    AppViewModel* app = e.vms.app();
    const SolicitudResumen resumen = resumenDePrueba(SolicitudId::generar());
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({resumen}));
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaSolicitudes"));
    // Foco inicial en la lista tambien si la carga termina despues.
    QTRY_VERIFY(focoDentroDe(e.ventana, QStringLiteral("listaSolicitudes")));

    // ToolTips con el atajo.
    QCOMPARE(e.item(QStringLiteral("botonPerfilesSat"))->property("atajo").toString(), QStringLiteral("⌘2"));

    // ⌘F enfoca la busqueda; con el campo enfocado no actua ningun otro atajo.
    atajo(e.ventana, Qt::Key_F);
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("campoBusqueda"));
    atajo(e.ventana, Qt::Key_N);
    atajo(e.ventana, Qt::Key_2);
    QCOMPARE(app->pagina(), Pagina::Diagnostico);
    // ⌘1 no aplica en la lista.
    e.item(QStringLiteral("listaSolicitudes"))->forceActiveFocus();
    atajo(e.ventana, Qt::Key_1);
    QCOMPARE(app->pagina(), Pagina::Diagnostico);

    // T023.5: ⌘N ya no abre nada.
    atajo(e.ventana, Qt::Key_N);
    QCOMPARE(app->pagina(), Pagina::Diagnostico);

    // ⌘2 abre Perfiles SAT; ⌘1 regresa.
    QTRY_VERIFY(e.ventana->activeFocusItem());
    e.item(QStringLiteral("listaSolicitudes"))->forceActiveFocus();
    atajo(e.ventana, Qt::Key_2);
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaPerfilesSat"));
    QTRY_VERIFY(e.ventana->activeFocusItem());
    e.pagina()->forceActiveFocus();
    // T023.5: sin pantalla Facturas en esta prueba, ⌘1 no navega.
    atajo(e.ventana, Qt::Key_1);
    QCOMPARE(app->pagina(), Pagina::Perfiles);
    app->mostrarDiagnostico();
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaSolicitudes"));

    // Detalle: ⌘R ejecuta la accion principal (Reintentar descarga si aplica).
    QVERIFY(app->abrirDetalle(resumen.id.texto()));
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaDetalleSolicitud"));
    SolicitudDetalle detalle = detalleDePrueba(resumen.id);
    detalle.resumen.estadoSat = EstadoSolicitudSat::EnProceso;
    e.solicitudes.detalle.resolver(0, SolicitudesService::ResultadoDetalle::exito(detalle));
    QTRY_VERIFY(e.vms.detalle()->cargada());
    QVERIFY(e.vms.detalle()->puedeReintentarDescarga());
    QCOMPARE(e.item(QStringLiteral("botonReintentarDescarga"))->property("atajo").toString(), QStringLiteral("⌘R"));
    QTRY_VERIFY(e.ventana->activeFocusItem());
    e.item(QStringLiteral("botonEliminarSolicitud"))->forceActiveFocus();
    atajo(e.ventana, Qt::Key_R);
    QCOMPARE(acciones.llamadas, QStringList{QStringLiteral("descargar:") + resumen.id.texto()});

    // ⌘⌫ solo abre la confirmacion.
    QObject* dialogo = e.dialogo(QStringLiteral("dialogoEliminar"));
    atajo(e.ventana, Qt::Key_Backspace);
    QTRY_VERIFY(dialogo->property("opened").toBool());
    QVERIFY(e.solicitudes.idsEliminar.isEmpty());

    // Con el dialogo abierto ningun atajo actua.
    atajo(e.ventana, Qt::Key_R);
    atajo(e.ventana, Qt::Key_N);
    atajo(e.ventana, Qt::Key_1);
    atajo(e.ventana, Qt::Key_2);
    atajo(e.ventana, Qt::Key_Backspace);
    QCOMPARE(acciones.llamadas.size(), 1);
    QCOMPARE(app->pagina(), Pagina::Detalle);
    QVERIFY(dialogo->property("opened").toBool());
    QVERIFY(e.solicitudes.idsEliminar.isEmpty());

    // Escape cierra y devuelve el foco; nada se elimina.
    QTest::keyClick(e.ventana, Qt::Key_Escape);
    QTRY_VERIFY(!dialogo->property("visible").toBool());
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("botonEliminarSolicitud"));
    QVERIFY(e.solicitudes.idsEliminar.isEmpty());

    // Sin reintento, ⌘R es Verificar ahora.
    detalle.paquetes.first().estadoDescarga = EstadoDescarga::Descargado;
    e.vms.detalle()->recargar();
    QTRY_COMPARE(e.solicitudes.detalle.size(), 2);
    e.solicitudes.detalle.resolver(1, SolicitudesService::ResultadoDetalle::exito(detalle));
    QTRY_VERIFY(!e.vms.detalle()->puedeReintentarDescarga());
    QVERIFY(e.vms.detalle()->puedeVerificar());
    atajo(e.ventana, Qt::Key_R);
    QCOMPARE(acciones.llamadas.size(), 2);
    QCOMPARE(acciones.llamadas.at(1), QStringLiteral("verificar:") + resumen.id.texto());

    // T023.5: Escape ("Atras") regresa del detalle a su origen (Diagnostico).
    QTest::keyClick(e.ventana, Qt::Key_Escape);
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaSolicitudes"));

    // T023.5: Perfiles SAT es raiz; Escape va a Facturas (sin pantalla
    // Facturas en esta prueba no navega).
    e.item(QStringLiteral("listaSolicitudes"))->forceActiveFocus();
    atajo(e.ventana, Qt::Key_2);
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaPerfilesSat"));
    QTRY_VERIFY(e.ventana->activeFocusItem());
    e.pagina()->forceActiveFocus();
    QTest::keyClick(e.ventana, Qt::Key_Escape);
    QCOMPARE(app->pagina(), Pagina::Perfiles);
    e.vms.setAccionesSolicitud(nullptr);
}

void TestPresentacion::copiarIdentificadores()
{
    EscenarioAsincrono e;
    QVERIFY(e.cargar());
    QVERIFY(e.activar());
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    const SolicitudId id = SolicitudId::generar();
    QVERIFY(e.vms.app()->abrirDetalle(id.texto()));
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaDetalleSolicitud"));
    const SolicitudDetalle detalle = detalleDePrueba(id);
    e.solicitudes.detalle.resolver(0, SolicitudesService::ResultadoDetalle::exito(detalle));
    QTRY_VERIFY(e.vms.detalle()->cargada());
    QGuiApplication::clipboard()->clear();

    // Nombre del paquete (pestana Paquetes).
    QMetaObject::invokeMethod(e.pagina(), "mostrarPestana", Q_ARG(QVariant, QStringLiteral("paquetes")));
    QQuickItem* paquete = nullptr;
    QTRY_VERIFY((paquete = e.item(QStringLiteral("botonCopiarPaquete_PAQ_01"))) && paquete->isVisible());
    QCOMPARE(nombreAccesible(paquete), QStringLiteral("Copiar Nombre del paquete"));
    QMetaObject::invokeMethod(paquete, "click");
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("PAQ_01"));
    QVERIFY(paquete->property("copiado").toBool());
    QCOMPARE(nombreAccesible(paquete), QStringLiteral("Copiado"));

    // Id de solicitud SAT completo (pestana Datos).
    QMetaObject::invokeMethod(e.pagina(), "mostrarPestana", Q_ARG(QVariant, QStringLiteral("datos")));
    QQuickItem* campo = nullptr;
    QTRY_VERIFY((campo = e.item(QStringLiteral("campoIdSolicitudSat"))) && campo->isVisible());
    QQuickItem* copiar = buscar(campo, QStringLiteral("botonCopiar"));
    QVERIFY(copiar && copiar->isVisible());
    QCOMPARE(nombreAccesible(copiar), QStringLiteral("Copiar Id de solicitud SAT"));
    QMetaObject::invokeMethod(copiar, "click");
    QCOMPARE(QGuiApplication::clipboard()->text(), detalle.idSolicitudSat);
    QCOMPARE(copiar->property("textoConfirmacion").toString(),
             QStringLiteral("Id de solicitud SAT copiado al portapapeles."));
    QCOMPARE(nombreAccesible(copiar), QStringLiteral("Copiado"));
}

// ---------------------------------------------------------------------------
// T014.2: reintento por paquete

void TestPresentacion::reintentoPorPaquete()
{
    EscenarioAsincrono e;
    AccionesEspia acciones;
    e.vms.setAccionesSolicitud(&acciones);
    QVERIFY(e.cargar());
    QVERIFY(e.activar());
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    SolicitudDetailViewModel* d = e.vms.detalle();
    const SolicitudId id = SolicitudId::generar();
    QVERIFY(e.vms.app()->abrirDetalle(id.texto()));
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaDetalleSolicitud"));

    // PAQ_01 Error reintentable, PAQ_02 Error con 5008, PAQ_03 Descargado.
    SolicitudDetalle detalle = detalleDePrueba(id);
    PaqueteResumen error = detalle.paquetes.first();
    error.estadoDescarga = EstadoDescarga::Error;
    error.codigoDescargaSat = QStringLiteral("5004");
    error.puedeReintentar = true;
    PaqueteResumen maximo = error;
    maximo.idPaqueteSat = QStringLiteral("PAQ_02");
    maximo.codigoDescargaSat = QStringLiteral("5008");
    maximo.puedeReintentar = false; // regla del DTO: Error con 5008
    PaqueteResumen descargado = error;
    descargado.idPaqueteSat = QStringLiteral("PAQ_03");
    descargado.estadoDescarga = EstadoDescarga::Descargado;
    descargado.codigoDescargaSat.reset();
    descargado.descargadoEn = detalle.resumen.creadaEn;
    descargado.puedeReintentar = false;
    detalle.paquetes = {error, maximo, descargado};
    e.solicitudes.detalle.resolver(0, SolicitudesService::ResultadoDetalle::exito(detalle));
    QTRY_VERIFY(d->cargada());
    QMetaObject::invokeMethod(e.pagina(), "mostrarPestana", Q_ARG(QVariant, QStringLiteral("paquetes")));

    QQuickItem* reintentar = nullptr;
    QTRY_VERIFY((reintentar = e.item(QStringLiteral("botonReintentarPaquete_PAQ_01"))) && reintentar->isVisible());
    QCOMPARE(nombreAccesible(reintentar), QStringLiteral("Reintentar paquete PAQ_01"));
    QCOMPARE(d->paquetes().at(0).toMap().value(QStringLiteral("puedeReintentar")).toBool(), true);
    // 5008 nunca ofrece reintento; Descargado tampoco.
    QVERIFY(!e.item(QStringLiteral("botonReintentarPaquete_PAQ_02"))->isVisible());
    QVERIFY(!e.item(QStringLiteral("botonReintentarPaquete_PAQ_03"))->isVisible());
    QCOMPARE(d->paquetes().at(1).toMap().value(QStringLiteral("puedeReintentar")).toBool(), false);
    QVERIFY(!e.item(QStringLiteral("reintentoPendiente_PAQ_01"))->isVisible());
    // "Reintentar descarga" de la solicitud se mantiene.
    QVERIFY(e.item(QStringLiteral("botonReintentarDescarga"))->isVisible());

    // Solo ese paquete, con el id de la solicitud.
    QMetaObject::invokeMethod(reintentar, "click");
    QCOMPARE(acciones.llamadas,
             QStringList{QStringLiteral("descargarPaquete:") + id.texto() + QStringLiteral("/PAQ_01")});
    QVERIFY(d->accionSolicitada().contains(QStringLiteral("queda pendiente")));
    QTRY_VERIFY(e.item(QStringLiteral("accionSolicitada"))->isVisible());
    d->reintentarDescargaPaquete(QStringLiteral("PAQ_02"));
    d->reintentarDescargaPaquete(QStringLiteral("PAQ_03"));
    d->reintentarDescargaPaquete(QStringLiteral("NO_EXISTE"));
    QCOMPARE(acciones.llamadas.size(), 1);

    // Monitoreo en pausa: el paquete queda pendiente y se ve como tal.
    detalle.paquetes[0].reintentoPendiente = true;
    d->recargar();
    QTRY_COMPARE(e.solicitudes.detalle.size(), 2);
    e.solicitudes.detalle.resolver(1, SolicitudesService::ResultadoDetalle::exito(detalle));
    QTRY_VERIFY(d->paquetes().at(0).toMap().value(QStringLiteral("reintentoPendiente")).toBool());
    QTRY_VERIFY(e.item(QStringLiteral("reintentoPendiente_PAQ_01"))->isVisible());
    QVERIFY(!e.item(QStringLiteral("botonReintentarPaquete_PAQ_01"))->isVisible());
    QVERIFY(nombreAccesible(e.item(QStringLiteral("filaPaquete_PAQ_01"))).contains(QStringLiteral("Reintento pendiente")));
    e.vms.setAccionesSolicitud(nullptr);
    // Sin acciones no se ofrece reintento aunque el DTO lo permita.
    QVERIFY(!d->paquetes().at(0).toMap().value(QStringLiteral("puedeReintentar")).toBool());
}

// ---------------------------------------------------------------------------
// T014.3: primer uso y vencimiento de e.firma

namespace {

struct ConsultaPrimerUsoEspia final : ConsultaPrimerUso {
    QList<std::shared_ptr<QPromise<ResultadoPrimerUso>>> promesas;
    QFuture<ResultadoPrimerUso> consultar() override
    {
        auto p = std::make_shared<QPromise<ResultadoPrimerUso>>();
        p->start();
        promesas.append(p);
        return p->future();
    }
    void resolver(int i, EstadoPrimerUso e)
    {
        promesas.at(i)->addResult(ResultadoPrimerUso::exito(e));
        promesas.at(i)->finish();
    }
    void fallar(int i)
    {
        promesas.at(i)->addResult(ResultadoPrimerUso::fallo(
            ErrorPersistencia::de(ErrorPersistencia::Tipo::Almacenamiento, QStringLiteral("x"))));
        promesas.at(i)->finish();
    }
};

QString iconoDePaso(const Vista& v, const QString& paso)
{
    QQuickItem* icono = buscar(v.item(paso), QStringLiteral("iconoPaso"));
    return icono ? icono->property("nombre").toString() : QString();
}

QString varianteDe(const Vista& v, const QString& nombre)
{
    QQuickItem* boton = v.item(nombre);
    return boton ? boton->property("variante").toString() : QString();
}

} // namespace

void TestPresentacion::guiaPrimerUsoPorPasos()
{
    EscenarioAsincrono e;
    ConsultaPrimerUsoEspia consulta;
    QVERIFY(e.cargar());
    QVERIFY(e.activar());
    AppViewModel* app = e.vms.app();
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    QTRY_VERIFY(e.item(QStringLiteral("estadoVacio")) && e.item(QStringLiteral("estadoVacio"))->isVisible());

    // Instalacion sin perfiles: tres pasos sin marcar; el primero es la accion principal.
    e.vms.setConsultaPrimerUso(&consulta);
    QCOMPARE(consulta.promesas.size(), 1);
    consulta.resolver(0, {false, false, false});
    QTRY_VERIFY(app->mostrarGuiaPrimerUso());
    QTRY_VERIFY(e.item(QStringLiteral("guiaPrimerUso"))->isVisible());
    QVERIFY(!e.item(QStringLiteral("estadoVacio"))->isVisible());
    for (const char* paso : {"pasoPerfil", "pasoEFirma", "pasoSolicitud"}) {
        QCOMPARE(iconoDePaso(e, QString::fromLatin1(paso)), QStringLiteral("circle-dashed"));
        QVERIFY(nombreAccesible(e.item(QString::fromLatin1(paso))).endsWith(QStringLiteral("pendiente")));
    }
    QCOMPARE(nombreAccesible(e.item(QStringLiteral("pasoPerfil"))),
             QStringLiteral("Paso 1 de 3: Crea un perfil SAT con el RFC del contribuyente, pendiente"));
    QCOMPARE(varianteDe(e, QStringLiteral("boton_pasoPerfil")), QStringLiteral("primario"));
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("boton_pasoPerfil"));

    // Cada paso abre su pantalla.
    QMetaObject::invokeMethod(e.item(QStringLiteral("boton_pasoPerfil")), "click");
    QTRY_COMPARE(app->pagina(), Pagina::Perfiles);
    app->mostrarDiagnostico();
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaSolicitudes"));

    // Perfil creado: paso 1 marcado. Un refresco conserva el ultimo resultado
    // valido (sin parpadeo): no pasa a "cargando".
    emit e.perfiles.perfilesCambiaron();
    QCOMPARE(consulta.promesas.size(), 2);
    QVERIFY(!app->primerUsoCargando());
    QVERIFY(app->mostrarGuiaPrimerUso());
    QTRY_VERIFY(e.item(QStringLiteral("guiaPrimerUso"))->isVisible());
    consulta.resolver(1, {true, false, false});
    QTRY_COMPARE(iconoDePaso(e, QStringLiteral("pasoPerfil")), QStringLiteral("check-circle"));
    QVERIFY(nombreAccesible(e.item(QStringLiteral("pasoPerfil"))).endsWith(QStringLiteral("hecho")));
    QCOMPARE(iconoDePaso(e, QStringLiteral("pasoEFirma")), QStringLiteral("circle-dashed"));
    QCOMPARE(varianteDe(e, QStringLiteral("boton_pasoEFirma")), QStringLiteral("primario"));
    QCOMPARE(varianteDe(e, QStringLiteral("boton_pasoPerfil")), QStringLiteral("secundario"));
    QMetaObject::invokeMethod(e.item(QStringLiteral("boton_pasoEFirma")), "click");
    QTRY_COMPARE(app->pagina(), Pagina::Perfiles);
    app->mostrarDiagnostico();
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaSolicitudes"));

    // e.firma registrada: paso 2 marcado; el siguiente es la primera solicitud.
    emit e.credenciales.credencialCambio(perfilDePrueba().id.texto());
    QTRY_COMPARE(consulta.promesas.size(), 3);
    consulta.resolver(2, {true, true, false});
    QTRY_COMPARE(iconoDePaso(e, QStringLiteral("pasoEFirma")), QStringLiteral("check-circle"));
    QCOMPARE(varianteDe(e, QStringLiteral("boton_pasoSolicitud")), QStringLiteral("primario"));
    // T023.5: el paso lleva a Facturas ("Ver facturas"); sin pantalla
    // Facturas en esta prueba no navega.
    QCOMPARE(nombreAccesible(e.item(QStringLiteral("boton_pasoSolicitud"))), QStringLiteral("Ver facturas"));

    // Primera solicitud: la guia desaparece y se ve la lista.
    const int lecturas = int(e.solicitudes.lista.size());
    emit e.solicitudes.listaCambiada();
    QTRY_COMPARE(int(e.solicitudes.lista.size()), lecturas + 1);
    e.solicitudes.lista.resolver(lecturas, ResultadoListaSol::exito({resumenDePrueba(SolicitudId::generar())}));
    QTRY_COMPARE(consulta.promesas.size(), 4);
    consulta.resolver(3, {true, true, true});
    QTRY_VERIFY(!app->mostrarGuiaPrimerUso());
    QTRY_VERIFY(e.item(QStringLiteral("listaSolicitudes"))->isVisible());
    QVERIFY(!e.item(QStringLiteral("guiaPrimerUso"))->isVisible());
    e.vms.setConsultaPrimerUso(nullptr);
}

void TestPresentacion::guiaPrimerUsoCargandoOConErrorNoSeMuestra()
{
    EscenarioAsincrono e;
    ConsultaPrimerUsoEspia consulta;
    QVERIFY(e.cargar());
    QVERIFY(QTest::qWaitForWindowExposed(e.ventana));
    AppViewModel* app = e.vms.app();
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    QTRY_VERIFY(e.item(QStringLiteral("estadoVacio")) && e.item(QStringLiteral("estadoVacio"))->isVisible());

    // Sin consulta (shell sin root): estado vacio normal.
    QVERIFY(!app->mostrarGuiaPrimerUso());
    QVERIFY(!app->primerUsoCargando());
    QVERIFY(!e.item(QStringLiteral("guiaPrimerUso"))->isVisible());

    // Cargando: sin guia.
    e.vms.setConsultaPrimerUso(&consulta);
    QVERIFY(app->primerUsoCargando());
    QVERIFY(!app->mostrarGuiaPrimerUso());
    procesarEventos();
    QVERIFY(!e.item(QStringLiteral("guiaPrimerUso"))->isVisible());
    QVERIFY(e.item(QStringLiteral("estadoVacio"))->isVisible());

    // Error: sin guia, estado vacio normal.
    consulta.fallar(0);
    QTRY_VERIFY(app->primerUsoError());
    QVERIFY(!app->primerUsoCargando());
    QVERIFY(!app->mostrarGuiaPrimerUso());
    QVERIFY(!e.item(QStringLiteral("guiaPrimerUso"))->isVisible());
    QVERIFY(e.item(QStringLiteral("estadoVacio"))->isVisible());

    // Reintento tras el error: carga de nuevo; una respuesta tardia se descarta.
    app->refrescarPrimerUso();
    QVERIFY(app->primerUsoCargando());
    app->refrescarPrimerUso();
    QCOMPARE(consulta.promesas.size(), 3);
    consulta.resolver(1, {false, false, false}); // tardia
    procesarEventos();
    QVERIFY(app->primerUsoCargando());
    QVERIFY(!e.item(QStringLiteral("guiaPrimerUso"))->isVisible());
    consulta.resolver(2, {true, false, false});
    QTRY_VERIFY(app->mostrarGuiaPrimerUso());
    QCOMPARE(app->primerUsoPasos(), (QVariantList{true, false, false}));
    QTRY_VERIFY(e.item(QStringLiteral("guiaPrimerUso"))->isVisible());
    e.vms.setConsultaPrimerUso(nullptr);
}

void TestPresentacion::guiaPrimerUsoRefrescoFallidoLaOculta()
{
    EscenarioAsincrono e;
    ConsultaPrimerUsoEspia consulta;
    QVERIFY(e.cargar());
    QVERIFY(QTest::qWaitForWindowExposed(e.ventana));
    AppViewModel* app = e.vms.app();
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    e.vms.setConsultaPrimerUso(&consulta);
    consulta.resolver(0, {true, false, false});
    QTRY_VERIFY(e.item(QStringLiteral("guiaPrimerUso")) && e.item(QStringLiteral("guiaPrimerUso"))->isVisible());

    // Un refresco que falla oculta la guia (estado vacio normal).
    emit e.perfiles.perfilesCambiaron();
    QCOMPARE(consulta.promesas.size(), 2);
    QVERIFY(app->mostrarGuiaPrimerUso()); // mientras refresca, el ultimo resultado valido
    consulta.fallar(1);
    QTRY_VERIFY(app->primerUsoError());
    QVERIFY(!app->mostrarGuiaPrimerUso());
    QTRY_VERIFY(!e.item(QStringLiteral("guiaPrimerUso"))->isVisible());
    QVERIFY(e.item(QStringLiteral("estadoVacio"))->isVisible());

    // Tras el error, el siguiente refresco es una carga sin guia hasta resolver.
    app->refrescarPrimerUso();
    QVERIFY(app->primerUsoCargando());
    QVERIFY(!app->mostrarGuiaPrimerUso());
    consulta.resolver(2, {true, true, false});
    QTRY_VERIFY(app->mostrarGuiaPrimerUso());
    e.vms.setConsultaPrimerUso(nullptr);
}

void TestPresentacion::badgeConElMismoRelojQueElAviso()
{
    // Reloj fijo, distinto de la hora real: el badge debe usarlo (no el del sistema).
    const QDateTime ahora(QDate(2026, 10, 1), QTime(10, 0), QTimeZone::UTC);
    const RelojUtc reloj = [ahora] { return ahora; };
    ServiciosAsincronos s;
    Vista v(&s.solicitudes, &s.perfiles, &s.credenciales, reloj);
    QVERIFY(v.cargar());
    QVERIFY(QTest::qWaitForWindowExposed(v.ventana));
    s.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    v.vms.app()->mostrarPerfiles();
    QTRY_COMPARE(v.pagina()->objectName(), QStringLiteral("paginaPerfilesSat"));
    QTRY_COMPARE(s.perfiles.listas.size(), 1);

    const PerfilResumen perfil = fakes::FakePerfilesSatService::perfil("AAA010101AAA", "Por vencer");
    const QDateTime vigencia(QDate(2026, 10, 8), QTime(12, 0), QTimeZone::UTC);
    const int base = int(s.credenciales.resumenes.size());
    s.perfiles.listas.resolver(0, ResultadoListaPerfiles::exito({perfil}));
    QTRY_COMPARE(int(s.credenciales.resumenes.size()), base + 1);
    s.credenciales.resolverResumen(base, EstadoCredencial::Lista, vigencia);

    // La misma regla y el mismo reloj que AvisoVencimientoEFirma::evaluar().
    const std::optional<int> delAviso = vencimientoefirma::diasParaVencer(vigencia, reloj());
    QCOMPARE(delAviso, std::optional<int>(7));
    QCOMPARE(vencimientoefirma::umbralPara(*delAviso), vencimientoefirma::kUmbralUrgente);
    PerfilesSatViewModel* p = v.vms.perfiles();
    QTRY_COMPARE(p->perfiles()->index(0).data(PerfilesSatListModel::DiasParaVencerRole).toInt(), *delAviso);
    QQuickItem* badge = nullptr;
    QTRY_VERIFY((badge = buscar(v.item(QStringLiteral("filaPerfil_0")), QStringLiteral("badgeVencimientoPerfil")))
                && badge->isVisible());
    QCOMPARE(badge->property("texto").toString(), QStringLiteral("Vence en 7 días"));
    QVERIFY(p->seleccionar(perfil.id.texto()));
    QTRY_COMPARE(p->seleccionDiasParaVencer(), *delAviso);
    QTRY_COMPARE(v.item(QStringLiteral("badgeVencimientoSeleccion"))->property("texto").toString(),
                 QStringLiteral("Vence en 7 días"));
}

void TestPresentacion::badgeVencimientoEnPerfiles()
{
    EscenarioAsincrono e;
    QVERIFY(e.cargar());
    QVERIFY(e.activar());
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    e.vms.app()->mostrarPerfiles();
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaPerfilesSat"));
    QTRY_COMPARE(e.perfiles.listas.size(), 1);

    // Dos perfiles Lista: uno vence en ~10 dias (con badge) y otro en un ano (sin badge).
    const PerfilResumen porVencer = fakes::FakePerfilesSatService::perfil("AAA010101AAA", "Por vencer");
    const PerfilResumen vigente = fakes::FakePerfilesSatService::perfil("BBB010101BBB", "Vigente");
    const QDateTime ahora = QDateTime::currentDateTimeUtc();
    const int base = int(e.credenciales.resumenes.size());
    e.perfiles.listas.resolver(0, ResultadoListaPerfiles::exito({porVencer, vigente}));
    QTRY_COMPARE(int(e.credenciales.resumenes.size()), base + 2);
    for (int i = 0; i < 2; ++i) {
        const bool esPorVencer = e.credenciales.idsResumen.at(base + i) == porVencer.id;
        e.credenciales.resolverResumen(base + i, EstadoCredencial::Lista,
                                       ahora.addDays(esPorVencer ? 10 : 400));
    }
    PerfilesSatViewModel* p = e.vms.perfiles();
    const int fila = p->perfiles()->filaDe(porVencer.id.texto());
    const int otra = p->perfiles()->filaDe(vigente.id.texto());
    QTRY_VERIFY(p->perfiles()->index(fila).data(PerfilesSatListModel::DiasParaVencerRole).toInt() >= 0);
    const int dias = p->perfiles()->index(fila).data(PerfilesSatListModel::DiasParaVencerRole).toInt();
    QVERIFY(dias >= 9 && dias <= 10);
    QCOMPARE(p->perfiles()->index(otra).data(PerfilesSatListModel::DiasParaVencerRole).toInt(), -1);

    const QString esperado = QStringLiteral("Vence en %1 días").arg(dias);
    QQuickItem* badge = nullptr;
    QTRY_VERIFY((badge = buscar(e.item(QStringLiteral("filaPerfil_%1").arg(fila)),
                                QStringLiteral("badgeVencimientoPerfil")))
                && badge->isVisible());
    QCOMPARE(badge->property("texto").toString(), esperado);
    QCOMPARE(badge->property("tono").toString(), QStringLiteral("advertencia"));
    QCOMPARE(badge->property("icono").toString(), QStringLiteral("calendar-exclamation"));
    QVERIFY(nombreAccesible(e.item(QStringLiteral("filaPerfil_%1").arg(fila))).contains(esperado));
    QVERIFY(!buscar(e.item(QStringLiteral("filaPerfil_%1").arg(otra)), QStringLiteral("badgeVencimientoPerfil"))
                 ->isVisible());

    // Grupo e.firma del perfil seleccionado.
    QVERIFY(p->seleccionar(porVencer.id.texto()));
    QTRY_COMPARE(p->seleccionDiasParaVencer(), dias);
    QQuickItem* grupo = nullptr;
    QTRY_VERIFY((grupo = e.item(QStringLiteral("badgeVencimientoSeleccion"))) && grupo->isVisible());
    QCOMPARE(grupo->property("texto").toString(), esperado);
    QVERIFY(p->seleccionar(vigente.id.texto()));
    QTRY_VERIFY(!e.item(QStringLiteral("badgeVencimientoSeleccion"))->isVisible());
}

// ---------------------------------------------------------------------------
// T014.4: destinos de acciones de notificacion

void TestPresentacion::notificacionAbreDetalleExistente()
{
    EscenarioAsincrono e;
    QVERIFY(e.cargar());
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    AppViewModel* app = e.vms.app();
    SolicitudDetailViewModel* d = e.vms.detalle();
    const SolicitudId id = SolicitudId::generar();

    app->abrirSolicitudNotificada(id.texto());
    QCOMPARE(app->pagina(), Pagina::Detalle);
    QCOMPARE(app->solicitudSeleccionadaId(), id.texto());
    QCOMPARE(d->estado(), EstadoDetalle::Cargando);
    e.solicitudes.detalle.resolver(0, SolicitudesService::ResultadoDetalle::exito(detalleDePrueba(id)));
    QTRY_COMPARE(d->estado(), EstadoDetalle::ConDatos);
    QCOMPARE(app->pagina(), Pagina::Detalle);
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaDetalleSolicitud"));

    // Ya cargada: una eliminacion posterior se ve en el detalle como siempre
    // (el respaldo a la lista solo aplica a la carga de la notificacion).
    emit e.solicitudes.solicitudEliminada(id);
    QCOMPARE(d->estado(), EstadoDetalle::NoEncontrada);
    QCOMPARE(app->pagina(), Pagina::Detalle);

    // Un error de lectura se muestra en el detalle (no es "no existe").
    const SolicitudId otra = SolicitudId::generar();
    app->abrirSolicitudNotificada(otra.texto());
    ErrorObtener fallo;
    fallo.tipo = ErrorObtener::Tipo::Persistencia;
    e.solicitudes.detalle.resolver(1, SolicitudesService::ResultadoDetalle::fallo(fallo));
    QTRY_COMPARE(d->estado(), EstadoDetalle::Error);
    QCOMPARE(app->pagina(), Pagina::Detalle);
}

void TestPresentacion::notificacionSolicitudInexistenteAbreLista()
{
    EscenarioAsincrono e;
    QVERIFY(e.cargar());
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    AppViewModel* app = e.vms.app();
    SolicitudDetailViewModel* d = e.vms.detalle();
    ErrorObtener noEncontrada;
    noEncontrada.tipo = ErrorObtener::Tipo::NoEncontrada;

    app->abrirSolicitudNotificada(SolicitudId::generar().texto());
    QCOMPARE(app->pagina(), Pagina::Detalle);
    e.solicitudes.detalle.resolver(0, SolicitudesService::ResultadoDetalle::fallo(noEncontrada));
    QTRY_COMPARE(d->estado(), EstadoDetalle::NoEncontrada);
    QCOMPARE(app->pagina(), Pagina::Diagnostico);
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaSolicitudes"));
    QVERIFY(app->mensajeFinder().isEmpty()); // sin error
    QVERIFY(e.item(QStringLiteral("mensajeFinderLista")));
    QVERIFY(!e.item(QStringLiteral("mensajeFinderLista"))->isVisible());

    // Si el usuario navega antes de que termine la carga, no se le mueve.
    app->abrirSolicitudNotificada(SolicitudId::generar().texto());
    app->mostrarPerfiles();
    e.solicitudes.detalle.resolver(1, SolicitudesService::ResultadoDetalle::fallo(noEncontrada));
    QTRY_COMPARE(d->estado(), EstadoDetalle::NoEncontrada);
    QCOMPARE(app->pagina(), Pagina::Perfiles);

    // Una navegacion explicita al detalle sustituye el respaldo: NoEncontrada
    // se muestra en el detalle como en T003.
    app->abrirSolicitudNotificada(SolicitudId::generar().texto());
    const SolicitudId explicita = SolicitudId::generar();
    QVERIFY(app->abrirDetalle(explicita.texto()));
    e.solicitudes.detalle.resolver(3, SolicitudesService::ResultadoDetalle::fallo(noEncontrada));
    QTRY_COMPARE(d->estado(), EstadoDetalle::NoEncontrada);
    QCOMPARE(app->pagina(), Pagina::Detalle);
}

void TestPresentacion::notificacionIdNoCanonicoAbreLista()
{
    EscenarioAsincrono e;
    QVERIFY(e.cargar());
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    AppViewModel* app = e.vms.app();
    const QString mayusculas = SolicitudId::generar().texto().toUpper();
    for (const QString& id : {QString(), QStringLiteral("no-es-uuid"), mayusculas,
                              QStringLiteral("{") + SolicitudId::generar().texto() + QStringLiteral("}")}) {
        app->mostrarPerfiles();
        app->abrirSolicitudNotificada(id);
        QCOMPARE(app->pagina(), Pagina::Diagnostico);
    }
    QCOMPARE(e.solicitudes.detalle.size(), 0); // no se consulto nada
    QVERIFY(app->solicitudSeleccionadaId().isEmpty());
    QVERIFY(app->mensajeFinder().isEmpty());
}

void TestPresentacion::notificacionPerfilSeleccionadoTrasCarga()
{
    EscenarioAsincrono e;
    QVERIFY(e.cargar());
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    AppViewModel* app = e.vms.app();
    PerfilesSatViewModel* p = e.vms.perfiles();
    const PerfilResumen uno = fakes::FakePerfilesSatService::perfil("AAA010101AAA", "Uno");
    const PerfilResumen dos = fakes::FakePerfilesSatService::perfil("BBB010101BBB", "Dos");

    // Seleccionar antes de que la lista cargue: se aplica al terminar.
    app->mostrarPerfilSeleccionado(dos.id.texto());
    QCOMPARE(app->pagina(), Pagina::Perfiles);
    QCOMPARE(p->modo(), PerfilesSatViewModel::Modo::Ninguno);
    QTRY_COMPARE(e.perfiles.listas.size(), 1);
    e.perfiles.listas.resolver(0, ResultadoListaPerfiles::exito({uno, dos}));
    QTRY_COMPARE(p->modo(), PerfilesSatViewModel::Modo::Edicion);
    QCOMPARE(p->perfilId(), dos.id.texto());
    QCOMPARE(p->rfc(), QStringLiteral("BBB010101BBB"));
    QVERIFY(p->errorListaMessage().isEmpty());
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaPerfilesSat"));

    // Ya en Perfiles: otra notificacion recarga y cambia la seleccion; una
    // carga tardia anterior no la pisa.
    app->mostrarPerfilSeleccionado(uno.id.texto());
    QTRY_COMPARE(e.perfiles.listas.size(), 2);
    QCOMPARE(p->perfilId(), dos.id.texto()); // aun no cargo
    e.perfiles.listas.resolver(1, ResultadoListaPerfiles::exito({uno, dos}));
    QTRY_COMPARE(p->perfilId(), uno.id.texto());

    // Una seleccion del usuario antes de terminar la carga gana.
    app->mostrarPerfilSeleccionado(dos.id.texto());
    QTRY_COMPARE(e.perfiles.listas.size(), 3);
    p->nuevo();
    e.perfiles.listas.resolver(2, ResultadoListaPerfiles::exito({uno, dos}));
    procesarEventos();
    QCOMPARE(p->modo(), PerfilesSatViewModel::Modo::Nuevo);

    // Una carga fallida descarta la seleccion pendiente.
    app->mostrarDiagnostico();
    app->mostrarPerfilSeleccionado(dos.id.texto());
    QTRY_COMPARE(e.perfiles.listas.size(), 4);
    e.perfiles.listas.resolver(3, ResultadoListaPerfiles::fallo(ErrorPersistencia::de(ErrorPersistencia::Tipo::Almacenamiento, QStringLiteral("fake"))));
    QTRY_VERIFY(!p->errorListaMessage().isEmpty());
    QCOMPARE(p->modo(), PerfilesSatViewModel::Modo::Ninguno);
}

void TestPresentacion::notificacionPerfilInexistenteSinSeleccion()
{
    EscenarioAsincrono e;
    QVERIFY(e.cargar());
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    AppViewModel* app = e.vms.app();
    PerfilesSatViewModel* p = e.vms.perfiles();
    const PerfilResumen uno = fakes::FakePerfilesSatService::perfil("AAA010101AAA", "Uno");

    // Estando en Perfiles con otro perfil seleccionado.
    app->mostrarPerfiles();
    QTRY_COMPARE(e.perfiles.listas.size(), 1);
    e.perfiles.listas.resolver(0, ResultadoListaPerfiles::exito({uno}));
    QTRY_VERIFY(p->seleccionar(uno.id.texto()));

    app->mostrarPerfilSeleccionado(PerfilId::generar().texto());
    QTRY_COMPARE(e.perfiles.listas.size(), 2);
    e.perfiles.listas.resolver(1, ResultadoListaPerfiles::exito({uno}));
    QTRY_COMPARE(p->modo(), PerfilesSatViewModel::Modo::Ninguno);
    QVERIFY(p->perfilId().isEmpty());
    QVERIFY(p->errorListaMessage().isEmpty());
    QCOMPARE(p->estadoLista(), PerfilesSatViewModel::EstadoLista::ConDatos);
    QCOMPARE(app->pagina(), Pagina::Perfiles);

    // Id que no es UUID: igual, sin seleccion ni error.
    app->mostrarPerfilSeleccionado(QStringLiteral("no-es-uuid"));
    QTRY_COMPARE(e.perfiles.listas.size(), 3);
    e.perfiles.listas.resolver(2, ResultadoListaPerfiles::exito({uno}));
    QTRY_VERIFY(!p->cargando());
    QCOMPARE(p->modo(), PerfilesSatViewModel::Modo::Ninguno);
    QVERIFY(p->errorListaMessage().isEmpty());
}

void TestPresentacion::notificacionMostrarCarpetaSolicitudEnFinder()
{
    EscenarioAsincrono e;
    FinderEspia finder;
    e.vms.setAccionesFinder(&finder);
    QVERIFY(e.cargar());
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    AppViewModel* app = e.vms.app();
    SolicitudDetailViewModel* d = e.vms.detalle();
    QSignalSpy aviso(app, &AppViewModel::avisoFinderSolicitudPresentado);
    const SolicitudId id = SolicitudId::generar();

    // Id no canonico: nada.
    app->mostrarCarpetaSolicitudEnFinder(QStringLiteral("no-es-uuid"));
    QVERIFY(finder.llamadas.isEmpty());

    // Finder la muestra: no se navega ni se avisa.
    app->mostrarCarpetaSolicitudEnFinder(id.texto());
    QCOMPARE(finder.llamadas, QStringList{QStringLiteral("solicitud")});
    finder.resolver(0, ResultadoAccionFinder::Estado::Mostrado);
    procesarEventos();
    QCOMPARE(app->pagina(), Pagina::Diagnostico);
    QCOMPARE(aviso.count(), 0);
    QCOMPARE(e.solicitudes.detalle.size(), 0);

    // Finder fallido: detalle de la solicitud con el aviso D7.
    app->mostrarCarpetaSolicitudEnFinder(id.texto());
    finder.resolver(1, ResultadoAccionFinder::Estado::Fallido, QStringLiteral("No se pudo abrir Finder"));
    QTRY_COMPARE(aviso.count(), 1);
    QCOMPARE(app->pagina(), Pagina::Detalle);
    QCOMPARE(d->mensajeFinder(), QStringLiteral("No se pudo abrir Finder"));
    e.solicitudes.detalle.resolver(0, SolicitudesService::ResultadoDetalle::exito(detalleDePrueba(id)));
    QTRY_COMPARE(d->estado(), EstadoDetalle::ConDatos);
    QCOMPARE(d->mensajeFinder(), QStringLiteral("No se pudo abrir Finder"));
    QTRY_VERIFY(e.item(QStringLiteral("mensajeFinder")) && e.item(QStringLiteral("mensajeFinder"))->isVisible());

    // La solicitud ya no existe: la lista con el aviso, sin error de detalle.
    const SolicitudId borrada = SolicitudId::generar();
    app->mostrarCarpetaSolicitudEnFinder(borrada.texto());
    finder.resolver(2, ResultadoAccionFinder::Estado::NoEncontrado, QStringLiteral("Carpeta no encontrada"));
    QTRY_COMPARE(aviso.count(), 2);
    ErrorObtener noEncontrada;
    noEncontrada.tipo = ErrorObtener::Tipo::NoEncontrada;
    e.solicitudes.detalle.resolver(1, SolicitudesService::ResultadoDetalle::fallo(noEncontrada));
    QTRY_COMPARE(app->pagina(), Pagina::Diagnostico);
    QCOMPARE(app->mensajeFinder(), QStringLiteral("Carpeta no encontrada"));
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaSolicitudes"));
    QTRY_VERIFY(e.item(QStringLiteral("mensajeFinderLista")) && e.item(QStringLiteral("mensajeFinderLista"))->isVisible());

    // Solo la ultima peticion cuenta.
    app->mostrarCarpetaSolicitudEnFinder(id.texto());
    app->mostrarCarpetaSolicitudEnFinder(id.texto());
    finder.resolver(3, ResultadoAccionFinder::Estado::Fallido, QStringLiteral("viejo"));
    procesarEventos();
    QCOMPARE(aviso.count(), 2);
    finder.resolver(4, ResultadoAccionFinder::Estado::Mostrado);
    procesarEventos();
    QCOMPARE(aviso.count(), 2);
    e.vms.setAccionesFinder(nullptr);
}

void TestPresentacion::atajosDeFacturas()
{
    // T023.4: Cmd+1/2/R/F/Arriba/Abajo con Facturas; inactivos con un dialogo
    // abierto o con un campo editable con foco.
    using namespace pruebasfacturas;
    EscenarioAsincrono e;
    FuenteFalsa fuente;
    PreferenciasMemoria preferencias;
    fuente.cfdisPorMes[QStringLiteral("2026-08")] = {
        cfdi(TipoDescarga::Emitidos, 1, QStringLiteral("2026-08-03T10:00:00"), QStringLiteral("PERFIL"),
             QStringLiteral("CLIENTE SINTETICO"))};
    e.vms.setFuenteFacturas(&fuente, &preferencias);
    e.vms.facturas()->setIntervaloBusqueda(0);
    QVERIFY(e.cargar());
    QVERIFY(e.activar());
    AppViewModel* app = e.vms.app();
    FacturasViewModel* f = e.vms.facturas();
    QCOMPARE(app->pagina(), Pagina::Facturas);
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaFacturas"));
    QTRY_COMPARE(f->mes(), 8);
    QTRY_COMPARE(f->franja()->estado(), FranjaMesViewModel::Estado::SinDescargar);
    QTRY_VERIFY(e.item(QStringLiteral("listaFacturas")) && e.item(QStringLiteral("listaFacturas"))->isVisible());
    auto enfocarLista = [&e] {
        e.item(QStringLiteral("listaFacturas"))->forceActiveFocus();
        procesarEventos();
    };
    enfocarLista();

    // Cmd+Arriba/Abajo: mes anterior y siguiente.
    atajo(e.ventana, Qt::Key_Up);
    QTRY_COMPARE(f->mes(), 7);
    atajo(e.ventana, Qt::Key_Down);
    QTRY_COMPARE(f->mes(), 8);
    // Cmd+R: accion de la franja.
    QTRY_VERIFY(f->franja()->accionHabilitada());
    enfocarLista();
    atajo(e.ventana, Qt::Key_R);
    QTRY_COMPARE(fuente.accionesPlanificadas.size(), 1);
    QTRY_VERIFY(!f->franja()->ejecutando());

    // Cmd+F enfoca la busqueda; con el campo enfocado no actua ningun atajo.
    atajo(e.ventana, Qt::Key_F);
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("campoBusquedaFacturas"));
    atajo(e.ventana, Qt::Key_Up);
    atajo(e.ventana, Qt::Key_2);
    atajo(e.ventana, Qt::Key_R);
    QCOMPARE(f->mes(), 8);
    QCOMPARE(app->pagina(), Pagina::Facturas);
    QCOMPARE(fuente.accionesPlanificadas.size(), 1);

    // Cmd+2 Perfiles SAT y Cmd+1 de vuelta a Facturas.
    enfocarLista();
    atajo(e.ventana, Qt::Key_2);
    QTRY_COMPARE(app->pagina(), Pagina::Perfiles);
    atajo(e.ventana, Qt::Key_1);
    QTRY_COMPARE(app->pagina(), Pagina::Facturas);
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaFacturas"));

    // Con el dialogo de confirmacion abierto no actua ningun atajo.
    PreplanGuiado conConfirmacion;
    CandidatoGuiado candidato;
    candidato.direccion = TipoDescarga::Emitidos;
    candidato.duplicado.clasificacion = ClasificacionDuplicado::RequiereConfirmacion;
    conConfirmacion.candidatos = {candidato};
    fuente.planes.append(FuenteFacturasMes::ResultadoPreplan::exito(conConfirmacion));
    QTRY_VERIFY(f->franja()->accionHabilitada());
    QTRY_VERIFY(e.item(QStringLiteral("listaFacturas")) && e.item(QStringLiteral("listaFacturas"))->isVisible());
    enfocarLista();
    atajo(e.ventana, Qt::Key_R);
    QTRY_VERIFY(f->confirmacionPendiente());
    QTRY_VERIFY(e.pagina()->property("dialogoAbierto").toBool());
    const int planificaciones = int(fuente.accionesPlanificadas.size());
    atajo(e.ventana, Qt::Key_Up);
    atajo(e.ventana, Qt::Key_2);
    atajo(e.ventana, Qt::Key_R);
    QCOMPARE(f->mes(), 8);
    QCOMPARE(app->pagina(), Pagina::Facturas);
    QCOMPARE(int(fuente.accionesPlanificadas.size()), planificaciones);
    f->cancelarConfirmacion();
    QTRY_VERIFY(!e.pagina()->property("dialogoAbierto").toBool());
}
