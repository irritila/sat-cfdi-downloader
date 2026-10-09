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

#include "CredencialesSatServiceListo.h"
#include "ServiciosAsincronosFake.h"
#include "SolicitudesServiceEspia.h"
#include "fakes/FakeCredencialesSatService.h"
#include "fakes/FakePerfilesSatService.h"

#include "application/profiles/DemoPerfilesSatService.h"
#include "AccionesFinder.h"
#include "AccionesSolicitud.h"
#include "ConsultaExistenciaPaquetes.h"
#include "AppViewModel.h"
#include "NuevaSolicitudViewModel.h"
#include "PresentacionViewModels.h"
#include "SolicitudDetailViewModel.h"
#include "SolicitudesListModel.h"

#include <QAccessible>
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
          CredencialesSatService* credenciales)
        : vms(solicitudes, perfiles, credenciales)
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

EvaluacionDuplicado evaluacion(ClasificacionDuplicado clasificacion, MotivoDuplicado motivo)
{
    EvaluacionDuplicado e;
    e.clasificacion = clasificacion;
    e.motivo = motivo;
    if (clasificacion != ClasificacionDuplicado::Libre) {
        e.solicitudReferencia = SolicitudId::generar();
    }
    return e;
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

// Escenario asincrono en la pagina "Nueva solicitud" con un perfil listo
// seleccionado y fechas validas.
bool prepararFormulario(EscenarioAsincrono& e)
{
    if (!e.cargar() || !e.activar()) {
        return false;
    }
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    // Construir no consulta perfiles (ni el almacen de credenciales).
    if (e.perfiles.listas.size() != 0) {
        return false;
    }
    e.vms.app()->mostrarNueva();
    if (e.perfiles.listas.size() != 1 || !resolverPerfiles(e, 0, {perfilDePrueba()})) {
        return false;
    }
    NuevaSolicitudViewModel* f = e.vms.nuevaSolicitud();
    if (!QTest::qWaitFor([&] { return f->perfilesDisponibles()->count() == 1; })) {
        return false;
    }
    if (!QTest::qWaitFor([&] {
            return e.pagina() && e.pagina()->objectName() == QStringLiteral("paginaNuevaSolicitud");
        })) {
        return false;
    }
    f->setPerfilId(perfilDePrueba().id.texto());
    f->setFechaInicial(QStringLiteral("2026-07-01"));
    f->setFechaFinal(QStringLiteral("2026-07-31"));
    return f->canSubmit();
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
    void nuevaSolicitudDesdeCppConVentanaOculta();
    void estadoVacioConDatosVacios();
    void navegacionListaADetallePorId();
    void combosMuestranValorInicial();
    void envioValidoAbreDetalleYApareceEnLista();
    void envioInvalidoMuestraErrorYNoEnvia();
    void recorridoConTeclado();
    void paginasExponenTextoAccesible();
    void qmlSinImportsProhibidos();

    // T003: lista
    void listaEstadosCargandoErrorVaciaConDatos();
    void listaDescartaRespuestaTardia();

    // T003: perfiles y nueva solicitud
    void sinPerfilesListosMuestraAdministrarPerfiles();
    void perfilesDescartaRespuestaTardia();
    void duplicadoRequiereConfirmacionYSeConfirma();
    void duplicadoRequiereConfirmacionYSeCancela();
    void crearDevuelveRequiereConfirmacionPorCarrera();
    void duplicadoBloqueadoMuestraError();
    void filtroInvalidoMuestraMensajes();
    void nuevaSolicitudDescartaRespuestasTardias();

    // T003: detalle
    void detalleEstadosErrorConDatosYNoEncontrada();
    void detalleReaccionaASolicitudEliminada();
    void detalleDescartaRespuestaTardia();
    void eliminarConConfirmacion();

    // T007: acciones manuales, envio tras crear y textos de eventos nuevos
    void envioTrasCrearSeEncola();
    void detalleAccionesManualesYEstadosAccesibles();
    void detalleSinAccionesNoMuestraBotones();

    // T008 D11
    void detalleMuestraExistenciaSinCambiarEstado();

    // T009
    void enviarSoloConCredencialLista();
    void mensajesDelCatalogoYSin5008();
    void avisoNotificacionesDeshabilitadas();

    // T009.1
    void finderEnDetalleSegunExistencia();
    void finderCarpetaDePaquetesEnLista();
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
    QCOMPARE(e.vms.app()->pagina(), Pagina::Lista);
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
    // Oculta, la app sigue funcionando: la lista carga y la pagina existe.
    QTRY_COMPARE(e.vms.solicitudes()->rowCount(), 11);
    QTRY_VERIFY(e.pagina() != nullptr);
    QCOMPARE(e.pagina()->objectName(), QStringLiteral("paginaSolicitudes"));
    procesarEventos();
    QVERIFY(!e.ventana->isVisible()); // nada en QML la muestra por su cuenta
}

void TestPresentacion::nuevaSolicitudDesdeCppConVentanaOculta()
{
    Escenario e(DemoSolicitudesService::Datos::Representativos);
    QVERIFY(e.cargar(false));
    QTRY_COMPARE(e.vms.solicitudes()->rowCount(), 11);

    // Lo que hace AppLifecycleController para "Nueva solicitud" del menu bar.
    QVERIFY(QMetaObject::invokeMethod(e.vms.app(), "mostrarNueva")); // invocable
    QCOMPARE(e.vms.app()->pagina(), Pagina::Nueva);
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaNuevaSolicitud"));
    QVERIFY(!e.ventana->isVisible());
    QTRY_COMPARE(e.vms.nuevaSolicitud()->perfilesDisponibles()->count(), 2);

    // Despues se muestra y enfoca: el formulario queda listo con foco en perfil.
    e.ventana->show();
    QVERIFY(e.activar());
    QVERIFY(e.ventana->isVisible());
    QCOMPARE(e.pagina()->objectName(), QStringLiteral("paginaNuevaSolicitud"));
    QVERIFY(e.item(QStringLiteral("campoPerfil"))->isVisible());
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("campoPerfil"));

    // Mostrar de nuevo con el formulario abierto lo reinicia sin warnings.
    e.vms.nuevaSolicitud()->setRfcContraparte(QStringLiteral("XAXX010101000"));
    e.vms.app()->mostrarNueva();
    QVERIFY(e.vms.nuevaSolicitud()->rfcContraparte().isEmpty());
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
    // Sin filas, el foco inicial va a "Nueva solicitud".
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("botonNuevaSolicitud"));
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
    QCOMPARE(e.vms.app()->pagina(), Pagina::Lista);

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

    e.vms.app()->mostrarLista();
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaSolicitudes"));
}

void TestPresentacion::combosMuestranValorInicial()
{
    // T012: al abrir el formulario los ComboBox muestran el valor inicial del
    // view model (Emitidos, Todos) sin que el usuario elija nada.
    Escenario e(DemoSolicitudesService::Datos::Representativos);
    QVERIFY(e.cargar());
    QVERIFY(QTest::qWaitForWindowExposed(e.ventana));
    e.vms.app()->mostrarNueva();
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaNuevaSolicitud"));
    QCOMPARE(e.vms.nuevaSolicitud()->tipoDescarga(), QStringLiteral("Emitidos"));
    QCOMPARE(e.vms.nuevaSolicitud()->tipoComprobante(), QString());

    auto* tipo = e.item(QStringLiteral("campoTipoDescarga"));
    auto* comprobante = e.item(QStringLiteral("campoTipoComprobante"));
    // T013 (UX-15): el tipo de descarga es un SelectorSegmentado.
    QTRY_COMPARE(tipo->property("valor").toString(), QStringLiteral("Emitidos"));
    QVERIFY(e.item(QStringLiteral("segmento_Emitidos")) != nullptr);
    QTRY_COMPARE(comprobante->property("displayText").toString(), QStringLiteral("Todos"));
    QCOMPARE(comprobante->property("currentIndex").toInt(), 0);

    // Tras reiniciar con la pagina abierta (valores sin cambio) se conservan.
    e.vms.nuevaSolicitud()->setTipoDescarga(QStringLiteral("Recibidos"));
    QTRY_COMPARE(tipo->property("valor").toString(), QStringLiteral("Recibidos"));
    e.vms.app()->mostrarNueva();
    QTRY_COMPARE(e.item(QStringLiteral("campoTipoDescarga"))->property("valor").toString(),
                 QStringLiteral("Emitidos"));
}

void TestPresentacion::envioValidoAbreDetalleYApareceEnLista()
{
    Escenario e(DemoSolicitudesService::Datos::Representativos);
    QVERIFY(e.cargar());
    QVERIFY(QTest::qWaitForWindowExposed(e.ventana));
    QTRY_COMPARE(e.vms.solicitudes()->rowCount(), 11);

    e.vms.app()->mostrarNueva();
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaNuevaSolicitud"));

    NuevaSolicitudViewModel* f = e.vms.nuevaSolicitud();
    QTRY_COMPARE(f->perfilesDisponibles()->count(), 2); // solo activos
    QVERIFY(!f->sinPerfiles());
    QVERIFY(!e.item(QStringLiteral("seccionSinPerfiles"))->isVisible());
    QSignalSpy enviado(f, &NuevaSolicitudViewModel::submitted);
    f->setPerfilId(f->perfilesDisponibles()->index(0).data(PerfilesDisponiblesModel::IdRole).toString());
    f->setTipoDescarga(QStringLiteral("Recibidos"));
    f->setFechaInicial(QStringLiteral("2026-07-01"));
    f->setFechaFinal(QStringLiteral("2026-07-31"));
    f->setRfcContraparte(QStringLiteral("xaxx010101000"));
    f->setTipoComprobante(QStringLiteral("I"));
    f->setTipoComprobante(QStringLiteral("X")); // fuera del catalogo: se ignora
    QCOMPARE(f->tipoComprobante(), QStringLiteral("I"));
    QVERIFY(f->canSubmit());
    QVERIFY(f->errorMessage().isEmpty());

    QMetaObject::invokeMethod(e.item(QStringLiteral("botonCrearSolicitud")), "click");
    QTRY_COMPARE(enviado.count(), 1);
    const QString id = enviado.at(0).at(0).toString();
    QCOMPARE(e.solicitudes.llamadasEvaluar, 1);
    QCOMPARE(e.solicitudes.confirmaciones,
             QList<ConfirmacionDuplicado>{ConfirmacionDuplicado::SinConfirmar});

    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Detalle);
    QCOMPARE(e.vms.app()->solicitudSeleccionadaId(), id);
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaDetalleSolicitud"));
    QTRY_VERIFY(e.vms.detalle()->cargada());
    QCOMPARE(e.vms.detalle()->solicitudId(), id);
    QCOMPARE(e.vms.detalle()->fechaInicial(), QStringLiteral("2026-07-01"));
    QCOMPARE(e.vms.detalle()->tipoDescarga(), QStringLiteral("Recibidos"));
    QCOMPARE(e.vms.detalle()->rfcContraparte(), QStringLiteral("XAXX010101000"));
    QCOMPARE(e.vms.detalle()->tipoComprobante(), QStringLiteral("I"));

    QMetaObject::invokeMethod(e.item(QStringLiteral("botonRegresar")), "click");
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaSolicitudes"));
    QTRY_COMPARE(e.vms.solicitudes()->rowCount(), 12);
    QCOMPARE(e.vms.solicitudes()->filaDe(id), 0);
    QCOMPARE(e.vms.solicitudes()->index(0).data(Rol::EstadoResumenRole).toString(),
             QStringLiteral("Creada"));
    QTRY_COMPARE(e.item(QStringLiteral("listaSolicitudes"))->property("count").toInt(), 12);
}

void TestPresentacion::envioInvalidoMuestraErrorYNoEnvia()
{
    Escenario e(DemoSolicitudesService::Datos::Representativos);
    QVERIFY(e.cargar());
    QVERIFY(QTest::qWaitForWindowExposed(e.ventana));
    e.vms.app()->mostrarNueva();
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaNuevaSolicitud"));

    NuevaSolicitudViewModel* f = e.vms.nuevaSolicitud();
    QTRY_COMPARE(f->perfilesDisponibles()->count(), 2);
    QSignalSpy enviado(f, &NuevaSolicitudViewModel::submitted);
    // T013 (UX-18): los errores de validacion van bajo su campo.
    auto* mensaje = e.item(QStringLiteral("mensajeError"));
    auto* filaPerfil = e.item(QStringLiteral("filaPerfil"));
    QVERIFY(mensaje && filaPerfil);
    QVERIFY(!mensaje->isVisible()); // formulario nuevo: sin error todavia
    QVERIFY(filaPerfil->property("error").toString().isEmpty());
    QVERIFY(!f->canSubmit());

    // Sin perfil.
    QMetaObject::invokeMethod(e.item(QStringLiteral("botonCrearSolicitud")), "click");
    QVERIFY(!f->errorMessage().isEmpty());
    QCOMPARE(f->campoConError(), QStringLiteral("perfil"));
    QTRY_COMPARE(filaPerfil->property("error").toString(), f->errorMessage());
    QVERIFY(!mensaje->isVisible()); // un error con campo no va en el aviso general
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("campoPerfil"));

    // Perfil valido, rango de fechas invertido.
    f->setPerfilId(f->perfilesDisponibles()->index(1).data(PerfilesDisponiblesModel::IdRole).toString());
    f->setFechaInicial(QStringLiteral("2026-08-31"));
    f->setFechaFinal(QStringLiteral("2026-08-01"));
    QVERIFY(!f->canSubmit());
    QVERIFY(f->errorMessage().contains(QStringLiteral("fecha final")));
    QCOMPARE(f->campoConError(), QStringLiteral("fechaFinal"));
    QTRY_COMPARE(e.item(QStringLiteral("filaPeriodo"))->property("error").toString(), f->errorMessage());
    QVERIFY(e.item(QStringLiteral("campoFechaFinal"))->property("conError").toBool());
    f->submit();

    // Fecha con formato invalido.
    f->setFechaFinal(QStringLiteral("31/08/2026"));
    QVERIFY(!f->canSubmit());
    f->submit();

    // Perfil inactivo o desconocido (no esta entre los disponibles).
    f->setFechaFinal(QStringLiteral("2026-09-30"));
    QVERIFY(f->canSubmit());
    f->setPerfilId(QStringLiteral("00000000-0000-4000-9000-000000000003"));
    QVERIFY(!f->canSubmit());
    f->submit();

    QCOMPARE(e.solicitudes.llamadasEvaluar, 0);
    QCOMPARE(e.solicitudes.llamadasCrear(), 0);
    QCOMPARE(enviado.count(), 0);
    QCOMPARE(e.vms.app()->pagina(), Pagina::Nueva);
    QCOMPARE(f->campoConError(), QStringLiteral("perfil"));
    QTRY_VERIFY(!filaPerfil->property("error").toString().isEmpty());
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
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Lista);

    // Espacio sobre la lista tambien abre el detalle; Return sobre "Regresar" vuelve.
    QTRY_VERIFY(focoDentroDe(e.ventana, QStringLiteral("listaSolicitudes")));
    QTest::keyClick(e.ventana, Qt::Key_Space);
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Detalle);
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("botonRegresar"));
    QTest::keyClick(e.ventana, Qt::Key_Return);
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Lista);

    // T013: Tab recorre las acciones del encabezado en orden (carpeta,
    // Perfiles SAT, Nueva solicitud); Espacio activa "Nueva solicitud".
    QTRY_VERIFY(focoDentroDe(e.ventana, QStringLiteral("listaSolicitudes")));
    QTest::keyClick(e.ventana, Qt::Key_Tab);
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("botonPerfilesSat"));
    QTest::keyClick(e.ventana, Qt::Key_Tab);
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("botonNuevaSolicitud"));
    QTest::keyClick(e.ventana, Qt::Key_Space);
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Nueva);

    // Formulario: foco inicial en perfil; flecha abajo elige el primero.
    NuevaSolicitudViewModel* f = e.vms.nuevaSolicitud();
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("campoPerfil"));
    QTRY_COMPARE(f->perfilesDisponibles()->count(), 2);
    QTest::keyClick(e.ventana, Qt::Key_Down);
    QTRY_VERIFY(!f->perfilId().isEmpty());
    QVERIFY(f->canSubmit());

    // Tab recorre los campos en orden hasta "Crear solicitud" (incluye los
    // opcionales tipo de comprobante y complemento).
    const QStringList esperados = {
        QStringLiteral("campoTipoDescarga"),   QStringLiteral("campoFechaInicial"),
        QStringLiteral("campoFechaFinal"),     QStringLiteral("campoRfcContraparte"),
        QStringLiteral("campoTipoComprobante"), QStringLiteral("campoComplemento"),
        QStringLiteral("botonCrearSolicitud"),
    };
    QStringList visitados;
    for (int i = 0; i < 16 && objectNameConFoco(e.ventana) != esperados.constLast(); ++i) {
        QTest::keyClick(e.ventana, Qt::Key_Tab);
        // El segmentado recibe el foco en su segmento activo (T013).
        const QString actual = focoDentroDe(e.ventana, QStringLiteral("campoTipoDescarga"))
                                   ? QStringLiteral("campoTipoDescarga") : objectNameConFoco(e.ventana);
        if (esperados.contains(actual)) {
            visitados.append(actual);
        }
    }
    QCOMPARE(visitados, esperados);

    // Espacio envia; se abre el detalle de la nueva solicitud.
    QTest::keyClick(e.ventana, Qt::Key_Space);
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Detalle);
    QCOMPARE(e.solicitudes.llamadasCrear(), 1);
    const QString nuevoId = e.vms.app()->solicitudSeleccionadaId();
    QTRY_VERIFY(e.vms.detalle()->cargada());
    QCOMPARE(e.vms.detalle()->solicitudId(), nuevoId);

    // Escape vuelve a la lista con la nueva solicitud al inicio.
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("botonRegresar"));
    QTest::keyClick(e.ventana, Qt::Key_Escape);
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Lista);
    QCOMPARE(e.vms.solicitudes()->filaDe(nuevoId), 0);

    // Escape desde el formulario tambien regresa.
    e.vms.app()->mostrarNueva();
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("campoPerfil"));
    QTest::keyClick(e.ventana, Qt::Key_Escape);
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Lista);
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
    QCOMPARE(nombreAccesible(e.item(QStringLiteral("botonNuevaSolicitud"))),
             QStringLiteral("Nueva solicitud"));
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

    // Formulario: pagina y controles.
    e.vms.app()->mostrarNueva();
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaNuevaSolicitud"));
    QVERIFY(!nombreAccesible(e.pagina()).isEmpty());
    for (const char* control : {"campoPerfil", "campoTipoDescarga", "campoFechaInicial",
                                "campoFechaFinal", "campoRfcContraparte", "campoTipoComprobante",
                                "campoComplemento", "botonCrearSolicitud", "botonRegresar",
                                "botonAdministrarPerfiles", "botonPerfilesSat"}) {
        QVERIFY2(!nombreAccesible(e.item(QString::fromLatin1(control))).isEmpty(), control);
    }
    QVERIFY(!nombreAccesible(e.itemDeDialogo(QStringLiteral("dialogoDuplicado"),
                                             QStringLiteral("dialogoDuplicadoContenido")))
                 .isEmpty());

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
    QCOMPARE(archivos, 27); // 26 .qml (Theme, Icono y componentes T013) + Etiquetas.js
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

void TestPresentacion::sinPerfilesListosMuestraAdministrarPerfiles()
{
    EscenarioAsincrono e;
    QVERIFY(e.cargar());
    QVERIFY(e.activar());
    e.solicitudes.lista.resolver(0, ResultadoListaSol::exito({}));
    NuevaSolicitudViewModel* f = e.vms.nuevaSolicitud();
    QCOMPARE(e.perfiles.listas.size(), 0); // nada al construir

    e.vms.app()->mostrarNueva();
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaNuevaSolicitud"));
    QCOMPARE(e.perfiles.listas.size(), 1);
    QVERIFY(f->cargandoPerfiles());
    QVERIFY(!f->sinPerfiles()); // aun no se sabe

    // Ninguno listo: inactivo con e.firma Lista, activo sin e.firma, activo vencido.
    PerfilResumen inactivo = fakes::FakePerfilesSatService::perfil("AAA010101AAA", "Inactivo", false);
    PerfilResumen sinEFirma = fakes::FakePerfilesSatService::perfil("BBB010101BBB", "Sin e.firma");
    PerfilResumen vencido = fakes::FakePerfilesSatService::perfil("CCC010101CCC", "Vencido");
    e.perfiles.listas.resolver(0, ResultadoListaPerfiles::exito({inactivo, sinEFirma, vencido}));
    QTRY_COMPARE(e.credenciales.resumenes.size(), 3);
    e.credenciales.resolverResumen(0, EstadoCredencial::Lista);
    e.credenciales.resolverResumen(1, EstadoCredencial::SinCredencial);
    e.credenciales.resolverResumen(2, EstadoCredencial::Vencida);
    QTRY_VERIFY(f->sinPerfiles());
    QVERIFY(!f->cargandoPerfiles());
    QCOMPARE(f->perfilesDisponibles()->count(), 0);

    // "Administrar perfiles SAT" visible y con foco; ya no existe el perfil simulado.
    QTRY_VERIFY(e.item(QStringLiteral("seccionSinPerfiles"))->isVisible());
    QVERIFY(e.item(QStringLiteral("seccionPerfilSimulado")) == nullptr);
    QVERIFY(e.item(QStringLiteral("botonCrearPerfilSimulado")) == nullptr);
    QVERIFY(!e.item(QStringLiteral("campoPerfil"))->isEnabled());
    auto* administrar = e.item(QStringLiteral("botonAdministrarPerfiles"));
    QCOMPARE(nombreAccesible(administrar), QStringLiteral("Administrar perfiles SAT"));
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("botonAdministrarPerfiles"));
    QVERIFY(!e.perfiles.historial.contains(QStringLiteral("crearPerfilSimulado")));

    // Un perfil queda listo (credencialCambio): el selector lo ofrece.
    e.credenciales.emitirCambio(sinEFirma.id);
    QTRY_COMPARE(e.perfiles.listas.size(), 2);
    QVERIFY(resolverPerfiles(e, 1, {sinEFirma}));
    QTRY_VERIFY(!f->sinPerfiles());
    QCOMPARE(f->perfilesDisponibles()->count(), 1);
    QCOMPARE(f->perfilesDisponibles()->filaDe(sinEFirma.id.texto()), 0);
    QTRY_VERIFY(!e.item(QStringLiteral("seccionSinPerfiles"))->isVisible());
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("campoPerfil"));

    // De vuelta sin listos, Enter en "Administrar perfiles SAT" abre Perfiles.
    emit e.perfiles.perfilesCambiaron();
    QTRY_COMPARE(e.perfiles.listas.size(), 3);
    e.perfiles.listas.resolver(2, ResultadoListaPerfiles::exito({}));
    QTRY_VERIFY(f->sinPerfiles());
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("botonAdministrarPerfiles"));
    QTest::keyClick(e.ventana, Qt::Key_Return);
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Perfiles);
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaPerfilesSat"));
}

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

void TestPresentacion::duplicadoRequiereConfirmacionYSeConfirma()
{
    EscenarioAsincrono e;
    QVERIFY(prepararFormulario(e));
    NuevaSolicitudViewModel* f = e.vms.nuevaSolicitud();
    QSignalSpy enviado(f, &NuevaSolicitudViewModel::submitted);
    f->setTipoComprobante(QStringLiteral("N"));
    f->setComplemento(QStringLiteral(" nomina12 "));

    QMetaObject::invokeMethod(e.item(QStringLiteral("botonCrearSolicitud")), "click");
    QCOMPARE(e.solicitudes.requestsEvaluar.size(), 1);
    const NuevaSolicitudRequest evaluado = e.solicitudes.requestsEvaluar.constFirst();
    QCOMPARE(evaluado.perfilId, perfilDePrueba().id);
    QCOMPARE(evaluado.tipoComprobante, std::optional<QString>(QStringLiteral("N")));
    QCOMPARE(evaluado.complemento, std::optional<QString>(QStringLiteral("nomina12")));
    QVERIFY(f->ocupado());
    QVERIFY(!f->canSubmit());
    QTRY_VERIFY(!e.item(QStringLiteral("botonCrearSolicitud"))->isEnabled());

    e.solicitudes.evaluacion.resolver(
        0, SolicitudesService::ResultadoEvaluarDuplicado::exito(evaluacion(
               ClasificacionDuplicado::RequiereConfirmacion, MotivoDuplicado::SolicitudEliminada)));
    QTRY_VERIFY(f->confirmacionPendiente());
    QVERIFY(!f->ocupado());
    QVERIFY(!f->motivoDuplicado().isEmpty());
    QVERIFY(e.solicitudes.confirmaciones.isEmpty()); // nada se crea sin confirmar

    QObject* dialogo = e.dialogo(QStringLiteral("dialogoDuplicado"));
    QVERIFY(dialogo);
    QTRY_VERIFY(dialogo->property("opened").toBool());
    auto* mensaje = e.itemDeDialogo(QStringLiteral("dialogoDuplicado"),
                                    QStringLiteral("dialogoDuplicadoMensaje"));
    QVERIFY(mensaje->property("text").toString().contains(f->motivoDuplicado()));
    // Foco inicial en la opcion segura.
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("dialogoDuplicadoCancelar"));
    QTest::keyClick(e.ventana, Qt::Key_Tab);
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("dialogoDuplicadoConfirmar"));

    // Confirmar crea con el snapshot evaluado y ConfirmacionDuplicado::Confirmada.
    QTest::keyClick(e.ventana, Qt::Key_Return);
    QCOMPARE(e.solicitudes.confirmaciones,
             QList<ConfirmacionDuplicado>{ConfirmacionDuplicado::Confirmada});
    const NuevaSolicitudRequest creado = e.solicitudes.requestsCrear.constFirst();
    QCOMPARE(creado.perfilId, evaluado.perfilId);
    QCOMPARE(creado.fechaInicial, evaluado.fechaInicial);
    QCOMPARE(creado.fechaFinal, evaluado.fechaFinal);
    QCOMPARE(creado.tipoComprobante, evaluado.tipoComprobante);
    QCOMPARE(creado.complemento, evaluado.complemento);
    QVERIFY(!f->confirmacionPendiente());
    QVERIFY(f->ocupado());
    QTRY_VERIFY(!dialogo->property("visible").toBool());

    const SolicitudId id = SolicitudId::generar();
    e.solicitudes.creacion.resolver(0, SolicitudesService::ResultadoCrear::exito(id));
    QTRY_COMPARE(enviado.count(), 1);
    QCOMPARE(enviado.at(0).at(0).toString(), id.texto());
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Detalle);
}

void TestPresentacion::duplicadoRequiereConfirmacionYSeCancela()
{
    EscenarioAsincrono e;
    QVERIFY(prepararFormulario(e));
    NuevaSolicitudViewModel* f = e.vms.nuevaSolicitud();

    f->submit();
    e.solicitudes.evaluacion.resolver(
        0, SolicitudesService::ResultadoEvaluarDuplicado::exito(evaluacion(
               ClasificacionDuplicado::RequiereConfirmacion, MotivoDuplicado::EnvioIncierto)));
    QObject* dialogo = e.dialogo(QStringLiteral("dialogoDuplicado"));
    QTRY_VERIFY(dialogo->property("opened").toBool());
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("dialogoDuplicadoCancelar"));

    // Escape cancela: no hay creacion, el formulario se puede volver a enviar
    // y el foco regresa a "Crear solicitud".
    QTest::keyClick(e.ventana, Qt::Key_Escape);
    QTRY_VERIFY(!f->confirmacionPendiente());
    QTRY_VERIFY(!dialogo->property("visible").toBool());
    QVERIFY(e.solicitudes.confirmaciones.isEmpty());
    QVERIFY(f->canSubmit());
    QVERIFY(f->errorMessage().isEmpty());
    QCOMPARE(e.vms.app()->pagina(), Pagina::Nueva);
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("botonCrearSolicitud"));

    // Segundo intento: Cancelar con clic y luego Espacio sobre el boton.
    f->submit();
    QCOMPARE(e.solicitudes.requestsEvaluar.size(), 2);
    e.solicitudes.evaluacion.resolver(
        1, SolicitudesService::ResultadoEvaluarDuplicado::exito(evaluacion(
               ClasificacionDuplicado::RequiereConfirmacion, MotivoDuplicado::EnvioIncierto)));
    QTRY_VERIFY(dialogo->property("opened").toBool());
    QMetaObject::invokeMethod(e.itemDeDialogo(QStringLiteral("dialogoDuplicado"),
                                              QStringLiteral("dialogoDuplicadoCancelar")),
                              "click");
    QTRY_VERIFY(!f->confirmacionPendiente());
    QVERIFY(e.solicitudes.confirmaciones.isEmpty());

    // confirmarDuplicado() sin confirmacion pendiente no hace nada.
    f->confirmarDuplicado();
    QVERIFY(e.solicitudes.confirmaciones.isEmpty());
}

void TestPresentacion::crearDevuelveRequiereConfirmacionPorCarrera()
{
    EscenarioAsincrono e;
    QVERIFY(prepararFormulario(e));
    NuevaSolicitudViewModel* f = e.vms.nuevaSolicitud();

    f->submit();
    e.solicitudes.evaluacion.resolver(
        0, SolicitudesService::ResultadoEvaluarDuplicado::exito(
               evaluacion(ClasificacionDuplicado::Libre, MotivoDuplicado::SinCoincidencias)));
    // Libre -> crear() (fachada: crearLocal SinConfirmar).
    QTRY_COMPARE(e.solicitudes.confirmaciones.size(), 1);
    QCOMPARE(e.solicitudes.confirmaciones.constFirst(), ConfirmacionDuplicado::SinConfirmar);
    QVERIFY(f->ocupado());

    // La transaccion reclasifica: otra solicitud equivalente aparecio.
    e.solicitudes.creacion.resolver(
        0, SolicitudesService::ResultadoCrear::fallo(ErrorCrear::requiereConfirmacion(evaluacion(
               ClasificacionDuplicado::RequiereConfirmacion, MotivoDuplicado::SolicitudSinExito))));
    QTRY_VERIFY(f->confirmacionPendiente());
    QObject* dialogo = e.dialogo(QStringLiteral("dialogoDuplicado"));
    QTRY_VERIFY(dialogo->property("opened").toBool());

    QMetaObject::invokeMethod(e.itemDeDialogo(QStringLiteral("dialogoDuplicado"),
                                              QStringLiteral("dialogoDuplicadoConfirmar")),
                              "click");
    QCOMPARE(e.solicitudes.confirmaciones.size(), 2);
    QCOMPARE(e.solicitudes.confirmaciones.at(1), ConfirmacionDuplicado::Confirmada);
    QCOMPARE(e.solicitudes.requestsCrear.at(1).perfilId, e.solicitudes.requestsCrear.at(0).perfilId);
}

void TestPresentacion::duplicadoBloqueadoMuestraError()
{
    EscenarioAsincrono e;
    QVERIFY(prepararFormulario(e));
    NuevaSolicitudViewModel* f = e.vms.nuevaSolicitud();

    f->submit();
    const EvaluacionDuplicado bloqueado =
        evaluacion(ClasificacionDuplicado::Bloqueado, MotivoDuplicado::SolicitudEnCurso);
    e.solicitudes.evaluacion.resolver(0, SolicitudesService::ResultadoEvaluarDuplicado::exito(bloqueado));
    QTRY_VERIFY(!f->errorMessage().isEmpty());
    QVERIFY(!f->ocupado());
    QVERIFY(!f->confirmacionPendiente());
    QVERIFY(e.solicitudes.confirmaciones.isEmpty()); // Bloqueado nunca se crea
    QCOMPARE(f->solicitudExistenteId(), bloqueado.solicitudReferencia->texto());

    auto* mensaje = e.item(QStringLiteral("mensajeError"));
    QTRY_VERIFY(mensaje->isVisible());
    QVERIFY(nombreAccesible(mensaje).contains(f->errorMessage()));
    QVERIFY(!e.dialogo(QStringLiteral("dialogoDuplicado"))->property("visible").toBool());

    // Accion para ver la solicitud existente (por id).
    auto* ver = e.item(QStringLiteral("botonVerSolicitudExistente"));
    QTRY_VERIFY(ver->isVisible());
    QVERIFY(!nombreAccesible(ver).isEmpty());
    QMetaObject::invokeMethod(ver, "click");
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Detalle);
    QCOMPARE(e.vms.app()->solicitudSeleccionadaId(), bloqueado.solicitudReferencia->texto());
}

void TestPresentacion::filtroInvalidoMuestraMensajes()
{
    EscenarioAsincrono e;
    QVERIFY(prepararFormulario(e));
    NuevaSolicitudViewModel* f = e.vms.nuevaSolicitud();
    f->setRfcContraparte(QStringLiteral("NOVALIDO"));

    f->submit();
    const QString mensaje1 = QStringLiteral("El RFC contraparte no es válido.");
    const QString mensaje2 = QStringLiteral("El complemento contiene caracteres reservados.");
    e.solicitudes.evaluacion.resolver(
        0, SolicitudesService::ResultadoEvaluarDuplicado::fallo(ErrorCrear::filtroInvalido(
               {{ErrorSolicitudCanonica::Codigo::RfcContraparteInvalido, QStringLiteral("rfc_emisor"), mensaje1},
                {ErrorSolicitudCanonica::Codigo::CaracterReservado, QStringLiteral("complemento"), mensaje2}})));
    QTRY_VERIFY(f->errorMessage().contains(mensaje1));
    QVERIFY(f->errorMessage().contains(mensaje2));
    QVERIFY(!f->ocupado());
    // T013: el error de filtro va bajo el campo del primer filtro invalido.
    QCOMPARE(f->campoConError(), QStringLiteral("rfcContraparte"));
    QTRY_COMPARE(e.item(QStringLiteral("filaRfcContraparte"))->property("error").toString(), f->errorMessage());

    // Tambien si el error llega desde crear() (validacion autoritativa).
    f->setRfcContraparte(QStringLiteral("XAXX010101000"));
    QVERIFY(f->errorMessage().isEmpty()); // editar limpia el error de servicio
    f->submit();
    e.solicitudes.evaluacion.resolver(
        1, SolicitudesService::ResultadoEvaluarDuplicado::exito(
               evaluacion(ClasificacionDuplicado::Libre, MotivoDuplicado::SinCoincidencias)));
    QTRY_COMPARE(e.solicitudes.creacion.size(), 1);
    e.solicitudes.creacion.resolver(
        0, SolicitudesService::ResultadoCrear::fallo(ErrorCrear::filtroInvalido(
               {{ErrorSolicitudCanonica::Codigo::TipoComprobanteInvalido,
                 QStringLiteral("tipo_comprobante"), QStringLiteral("Tipo de comprobante invalido.")}})));
    QTRY_VERIFY(f->errorMessage().contains(QStringLiteral("Tipo de comprobante invalido.")));
    QVERIFY(!f->ocupado());
}

void TestPresentacion::nuevaSolicitudDescartaRespuestasTardias()
{
    EscenarioAsincrono e;
    QVERIFY(prepararFormulario(e));
    NuevaSolicitudViewModel* f = e.vms.nuevaSolicitud();
    QSignalSpy enviado(f, &NuevaSolicitudViewModel::submitted);

    // Evaluacion invalidada por reiniciar(): su respuesta no crea nada.
    f->submit();
    QVERIFY(f->ocupado());
    f->reiniciar();
    QVERIFY(!f->ocupado());
    e.solicitudes.evaluacion.resolver(
        0, SolicitudesService::ResultadoEvaluarDuplicado::exito(
               evaluacion(ClasificacionDuplicado::Libre, MotivoDuplicado::SinCoincidencias)));
    procesarEventos();
    QVERIFY(e.solicitudes.confirmaciones.isEmpty());
    QVERIFY(!f->ocupado());

    // Creacion invalidada por reiniciar(): no navega ni muestra error.
    QVERIFY(resolverPerfiles(e, e.perfiles.listas.size() - 1, {perfilDePrueba()}));
    QTRY_COMPARE(f->perfilesDisponibles()->count(), 1);
    f->setPerfilId(perfilDePrueba().id.texto());
    QVERIFY(f->canSubmit());
    f->submit();
    e.solicitudes.evaluacion.resolver(
        1, SolicitudesService::ResultadoEvaluarDuplicado::exito(
               evaluacion(ClasificacionDuplicado::Libre, MotivoDuplicado::SinCoincidencias)));
    QTRY_COMPARE(e.solicitudes.creacion.size(), 1);
    f->reiniciar();
    e.solicitudes.creacion.resolver(0, SolicitudesService::ResultadoCrear::exito(SolicitudId::generar()));
    procesarEventos();
    QCOMPARE(enviado.count(), 0);
    QVERIFY(f->errorMessage().isEmpty());
    QCOMPARE(e.vms.app()->pagina(), Pagina::Nueva);

    // Confirmacion pendiente invalidada al editar el formulario: un
    // RequiereConfirmacion tardio de otra evaluacion no abre el dialogo.
    QVERIFY(resolverPerfiles(e, e.perfiles.listas.size() - 1, {perfilDePrueba()}));
    QTRY_COMPARE(f->perfilesDisponibles()->count(), 1);
    f->setPerfilId(perfilDePrueba().id.texto());
    f->submit();
    f->cancelarDuplicado(); // invalida la evaluacion en curso
    e.solicitudes.evaluacion.resolver(
        2, SolicitudesService::ResultadoEvaluarDuplicado::exito(evaluacion(
               ClasificacionDuplicado::RequiereConfirmacion, MotivoDuplicado::SolicitudEliminada)));
    procesarEventos();
    QVERIFY(!f->confirmacionPendiente());
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
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Lista);
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

void TestPresentacion::envioTrasCrearSeEncola()
{
    Escenario e(DemoSolicitudesService::Datos::Representativos);
    AccionesEspia acciones;
    e.vms.setAccionesSolicitud(&acciones);
    QVERIFY(e.cargar());
    e.vms.app()->mostrarNueva();
    NuevaSolicitudViewModel* f = e.vms.nuevaSolicitud();
    QTRY_COMPARE(f->perfilesDisponibles()->count(), 2);
    QSignalSpy enviado(f, &NuevaSolicitudViewModel::submitted);
    f->setPerfilId(f->perfilesDisponibles()->index(0).data(PerfilesDisponiblesModel::IdRole).toString());
    f->setFechaInicial(QStringLiteral("2026-07-01"));
    f->setFechaFinal(QStringLiteral("2026-07-31"));
    QVERIFY(f->canSubmit());
    f->submit();
    QTRY_COMPARE(enviado.count(), 1);
    // D4: el envio se pide una vez, para la solicitud creada, al crearla.
    QCOMPARE(acciones.llamadas, QStringList{QStringLiteral("enviar:") + enviado.at(0).at(0).toString()});
    e.vms.setAccionesSolicitud(nullptr);
}

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
