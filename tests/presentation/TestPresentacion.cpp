// satcfdi_presentation_tests: carga el modulo QML real (SatCfdiDownloader/Main)
// con view models inyectados por setInitialProperties y verifica roles,
// estados, navegacion, teclado y accesibilidad. Cualquier warning (incluidos
// los de carga QML) hace fallar la prueba.

#include "SolicitudesServiceEspia.h"

#include "application/profiles/DemoPerfilesSatService.h"
#include "AppViewModel.h"
#include "NuevaSolicitudViewModel.h"
#include "PresentacionViewModels.h"
#include "SolicitudDetailViewModel.h"
#include "SolicitudesListModel.h"

#include <QAccessible>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace satcfdi;
using Pagina = AppViewModel::Pagina;
using Rol = SolicitudesListModel::Rol;

namespace {

// Grafo de prueba equivalente al composition root. Orden de miembros:
// servicios -> view models -> engine (el engine se destruye primero).
struct Escenario {
    explicit Escenario(DemoSolicitudesService::Datos datos)
        : perfiles(DemoPerfilesSatService::perfilesDemo())
        , solicitudes(DemoPerfilesSatService::perfilesDemo(), datos)
        , vms(&solicitudes, &perfiles)
    {
    }

    bool cargar()
    {
        engine.setInitialProperties(vms.initialProperties());
        engine.loadFromModule("SatCfdiDownloader", "Main");
        if (engine.rootObjects().size() != 1) {
            return false;
        }
        ventana = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
        return ventana != nullptr;
    }

    QQuickItem* pagina() const;

    DemoPerfilesSatService perfiles;
    SolicitudesServiceEspia solicitudes;
    PresentacionViewModels vms;
    QQmlApplicationEngine engine;
    QQuickWindow* ventana = nullptr;
};

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

QQuickItem* Escenario::pagina() const
{
    QQuickItem* loader = buscar(ventana->contentItem(), QStringLiteral("paginaActual"));
    return loader ? qvariant_cast<QQuickItem*>(loader->property("item")) : nullptr;
}

} // namespace

class TestPresentacion : public QObject {
    Q_OBJECT

private slots:
    void init();

    void rolesDelModelo();
    void cargaQmlSinWarnings();
    void estadoVacioConDatosVacios();
    void navegacionListaADetallePorId();
    void envioValidoAbreDetalleYApareceEnLista();
    void envioInvalidoMuestraErrorYNoEnvia();
    void recorridoConTeclado();
    void paginasExponenTextoAccesible();
    void qmlSinImportsProhibidos();
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
        "estadoLocal", "estadoSat", "estadoResumen", "creadaEn", "totalPaquetes",
    };
    const QList<QByteArray> nombres = modelo->roleNames().values();
    QCOMPARE(nombres.size(), esperados.size());
    for (const QByteArray& rol : esperados) {
        QVERIFY2(nombres.contains(rol), rol.constData());
    }

    QTRY_COMPARE(modelo->rowCount(), 11);
    QVERIFY(!modelo->vacio());

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

    auto* lista = buscar(e.pagina(), QStringLiteral("listaSolicitudes"));
    QVERIFY(lista);
    QTRY_COMPARE(lista->property("count").toInt(), 11);
    QVERIFY(lista->isVisible());
    QVERIFY(!buscar(e.pagina(), QStringLiteral("estadoVacio"))->isVisible());
    QVERIFY(e.ventana->minimumWidth() > 0 && e.ventana->minimumHeight() > 0);
}

void TestPresentacion::estadoVacioConDatosVacios()
{
    Escenario e(DemoSolicitudesService::Datos::Vacio);
    QVERIFY(e.cargar());
    QVERIFY(QTest::qWaitForWindowExposed(e.ventana));
    QTRY_VERIFY(e.vms.solicitudes()->vacio());
    QCOMPARE(e.vms.solicitudes()->rowCount(), 0);

    auto* vacio = buscar(e.pagina(), QStringLiteral("estadoVacio"));
    QVERIFY(vacio);
    QTRY_VERIFY(vacio->isVisible());
    QVERIFY(!nombreAccesible(vacio).isEmpty());
    QVERIFY(!buscar(e.pagina(), QStringLiteral("listaSolicitudes"))->isVisible());
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
    QCOMPARE(d->solicitudId(), id);
    QCOMPARE(d->perfilRfc(), terminada.data(Rol::PerfilRfcRole).toString());
    QCOMPARE(d->estadoLocal(), QStringLiteral("Enviada"));
    QCOMPARE(d->estadoSat().toString(), QStringLiteral("Terminada"));
    QCOMPARE(d->totalPaquetes(), 4);
    QCOMPARE(d->paquetes().size(), 4);

    // Las tres secciones estan separadas y visibles.
    for (const char* seccion : {"seccionMetadata", "seccionEstados", "seccionPaquetes"}) {
        auto* item = buscar(e.pagina(), QString::fromLatin1(seccion));
        QVERIFY2(item && item->isVisible(), seccion);
    }
    auto* badgeSat = buscar(e.pagina(), QStringLiteral("badgeEstadoSat"));
    QCOMPARE(badgeSat->property("texto").toString(), QStringLiteral("Terminada"));

    // Solicitud sin estado SAT: QML recibe null y lo muestra como texto.
    const QModelIndex creada = e.vms.solicitudes()->match(
        e.vms.solicitudes()->index(0), Rol::EstadoResumenRole, QStringLiteral("Creada")).constFirst();
    QVERIFY(e.vms.app()->abrirDetalle(creada.data(Rol::IdRole).toString()));
    QTRY_COMPARE(d->solicitudId(), creada.data(Rol::IdRole).toString());
    QTRY_VERIFY(d->cargada());
    QVERIFY(d->estadoSat().isNull());
    QTRY_COMPARE(buscar(e.pagina(), QStringLiteral("badgeEstadoSat"))->property("texto").toString(),
                 QStringLiteral("Sin respuesta del SAT"));
    QVERIFY(buscar(e.pagina(), QStringLiteral("sinPaquetes"))->isVisible());

    e.vms.app()->mostrarLista();
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaSolicitudes"));
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
    QSignalSpy enviado(f, &NuevaSolicitudViewModel::submitted);
    f->setPerfilId(f->perfilesDisponibles()->index(0).data(PerfilesDisponiblesModel::IdRole).toString());
    f->setTipoDescarga(QStringLiteral("Recibidos"));
    f->setFechaInicial(QStringLiteral("2026-07-01"));
    f->setFechaFinal(QStringLiteral("2026-07-31"));
    f->setRfcContraparte(QStringLiteral("xaxx010101000"));
    QVERIFY(f->canSubmit());
    QVERIFY(f->errorMessage().isEmpty());

    QMetaObject::invokeMethod(buscar(e.pagina(), QStringLiteral("botonCrearSolicitud")), "click");
    QTRY_COMPARE(enviado.count(), 1);
    const QString id = enviado.at(0).at(0).toString();
    QCOMPARE(e.solicitudes.llamadasCrear, 1);

    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Detalle);
    QCOMPARE(e.vms.app()->solicitudSeleccionadaId(), id);
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaDetalleSolicitud"));
    QTRY_VERIFY(e.vms.detalle()->cargada());
    QCOMPARE(e.vms.detalle()->solicitudId(), id);
    QCOMPARE(e.vms.detalle()->fechaInicial(), QStringLiteral("2026-07-01"));
    QCOMPARE(e.vms.detalle()->tipoDescarga(), QStringLiteral("Recibidos"));
    QCOMPARE(e.vms.detalle()->rfcContraparte(), QStringLiteral("XAXX010101000"));

    QMetaObject::invokeMethod(buscar(e.pagina(), QStringLiteral("botonRegresar")), "click");
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaSolicitudes"));
    QTRY_COMPARE(e.vms.solicitudes()->rowCount(), 12);
    QCOMPARE(e.vms.solicitudes()->filaDe(id), 0);
    QCOMPARE(e.vms.solicitudes()->index(0).data(Rol::EstadoResumenRole).toString(),
             QStringLiteral("Creada"));
    QTRY_COMPARE(buscar(e.pagina(), QStringLiteral("listaSolicitudes"))->property("count").toInt(), 12);
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
    auto* mensaje = buscar(e.pagina(), QStringLiteral("mensajeError"));
    QVERIFY(mensaje);
    QVERIFY(!mensaje->isVisible()); // formulario nuevo: sin error todavia
    QVERIFY(!f->canSubmit());

    // Sin perfil.
    QMetaObject::invokeMethod(buscar(e.pagina(), QStringLiteral("botonCrearSolicitud")), "click");
    QVERIFY(!f->errorMessage().isEmpty());
    QTRY_VERIFY(mensaje->isVisible());
    QCOMPARE(mensaje->property("text").toString(), f->errorMessage());
    QVERIFY(nombreAccesible(mensaje).contains(f->errorMessage()));

    // Perfil valido, rango de fechas invertido.
    f->setPerfilId(f->perfilesDisponibles()->index(1).data(PerfilesDisponiblesModel::IdRole).toString());
    f->setFechaInicial(QStringLiteral("2026-08-31"));
    f->setFechaFinal(QStringLiteral("2026-08-01"));
    QVERIFY(!f->canSubmit());
    QVERIFY(f->errorMessage().contains(QStringLiteral("fecha final")));
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

    QCOMPARE(e.solicitudes.llamadasCrear, 0);
    QCOMPARE(enviado.count(), 0);
    QCOMPARE(e.vms.app()->pagina(), Pagina::Nueva);
    QTRY_VERIFY(mensaje->isVisible());
}

void TestPresentacion::recorridoConTeclado()
{
    Escenario e(DemoSolicitudesService::Datos::Representativos);
    QVERIFY(e.cargar());
    e.ventana->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(e.ventana));
    QTRY_COMPARE(e.vms.solicitudes()->rowCount(), 11);

    // Lista: foco inicial en la lista; flecha abajo + Return abre el detalle por id.
    QTRY_VERIFY(focoDentroDe(e.ventana, QStringLiteral("listaSolicitudes")));
    QTest::keyClick(e.ventana, Qt::Key_Down);
    const QString idFila1 = e.vms.solicitudes()->index(1).data(Rol::IdRole).toString();
    QTest::keyClick(e.ventana, Qt::Key_Return);
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Detalle);
    QCOMPARE(e.vms.app()->solicitudSeleccionadaId(), idFila1);

    // Detalle: foco en "Regresar"; Escape vuelve a la lista.
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("botonRegresar"));
    QTest::keyClick(e.ventana, Qt::Key_Escape);
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Lista);

    // Espacio sobre la lista tambien abre el detalle; Return sobre "Regresar" vuelve.
    QTRY_VERIFY(focoDentroDe(e.ventana, QStringLiteral("listaSolicitudes")));
    QTest::keyClick(e.ventana, Qt::Key_Space);
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Detalle);
    QTRY_COMPARE(objectNameConFoco(e.ventana), QStringLiteral("botonRegresar"));
    QTest::keyClick(e.ventana, Qt::Key_Return);
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Lista);

    // Tab lleva a "Nueva solicitud"; Espacio la activa.
    QTRY_VERIFY(focoDentroDe(e.ventana, QStringLiteral("listaSolicitudes")));
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

    // Tab recorre los campos en orden hasta "Crear solicitud".
    const QStringList esperados = {
        QStringLiteral("campoTipoDescarga"), QStringLiteral("campoFechaInicial"),
        QStringLiteral("campoFechaFinal"), QStringLiteral("campoRfcContraparte"),
        QStringLiteral("botonCrearSolicitud"),
    };
    QStringList visitados;
    for (int i = 0; i < 12 && objectNameConFoco(e.ventana) != esperados.constLast(); ++i) {
        QTest::keyClick(e.ventana, Qt::Key_Tab);
        const QString actual = objectNameConFoco(e.ventana);
        if (esperados.contains(actual)) {
            visitados.append(actual);
        }
    }
    QCOMPARE(visitados, esperados);

    // Espacio envia; se abre el detalle de la nueva solicitud.
    QTest::keyClick(e.ventana, Qt::Key_Space);
    QTRY_COMPARE(e.vms.app()->pagina(), Pagina::Detalle);
    QCOMPARE(e.solicitudes.llamadasCrear, 1);
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
    QVERIFY(!nombreAccesible(buscar(e.pagina(), QStringLiteral("listaSolicitudes"))).isEmpty());
    QCOMPARE(nombreAccesible(buscar(e.pagina(), QStringLiteral("botonNuevaSolicitud"))),
             QStringLiteral("Nueva solicitud"));
    auto* fila0 = buscar(e.pagina(), QStringLiteral("filaSolicitud_0"));
    QVERIFY(fila0);
    const QString nombreFila = nombreAccesible(fila0);
    QVERIFY2(nombreFila.contains(e.vms.solicitudes()->index(0).data(Rol::PerfilRfcRole).toString()),
             qPrintable(nombreFila));
    QVERIFY2(nombreFila.contains(QStringLiteral("estado ")), qPrintable(nombreFila));
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

    // Formulario: pagina y controles.
    e.vms.app()->mostrarNueva();
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaNuevaSolicitud"));
    QVERIFY(!nombreAccesible(e.pagina()).isEmpty());
    for (const char* control : {"campoPerfil", "campoTipoDescarga", "campoFechaInicial",
                                "campoFechaFinal", "campoRfcContraparte", "botonCrearSolicitud",
                                "botonRegresar"}) {
        QVERIFY2(!nombreAccesible(buscar(e.pagina(), QString::fromLatin1(control))).isEmpty(), control);
    }

    // Detalle: pagina, secciones y badges de estado con texto.
    QVERIFY(e.vms.app()->abrirDetalle(e.vms.solicitudes()->index(0).data(Rol::IdRole).toString()));
    QTRY_COMPARE(e.pagina()->objectName(), QStringLiteral("paginaDetalleSolicitud"));
    QTRY_VERIFY(e.vms.detalle()->cargada());
    QVERIFY(!nombreAccesible(e.pagina()).isEmpty());
    QTRY_VERIFY(nombreAccesible(buscar(e.pagina(), QStringLiteral("badgeEstadoLocal")))
                    .startsWith(QStringLiteral("Estado local: ")));
    QVERIFY(nombreAccesible(buscar(e.pagina(), QStringLiteral("badgeEstadoSat")))
                .startsWith(QStringLiteral("Estado SAT: ")));
    QVERIFY(!nombreAccesible(buscar(e.pagina(), QStringLiteral("seccionPaquetes"))).isEmpty());
}

void TestPresentacion::qmlSinImportsProhibidos()
{
    // Fronteras de capa: QML solo importa QtQuick/Controls/Layouts y archivos
    // del propio modulo; nada de SQL, archivos, red, secretos o SAT.
    const QRegularExpression importacion(QStringLiteral("^\\s*import\\s+(\\S+)"),
                                         QRegularExpression::MultilineOption);
    const QStringList permitidos = {
        QStringLiteral("QtQuick"), QStringLiteral("QtQuick.Controls"),
        QStringLiteral("QtQuick.Layouts"), QStringLiteral("\"Etiquetas.js\""),
    };
    int archivos = 0;
    QDirIterator it(QStringLiteral(":/qt/qml/SatCfdiDownloader"), {QStringLiteral("*.qml")},
                    QDir::Files);
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
        ++archivos;
    }
    QCOMPARE(archivos, 8);
}

QTEST_MAIN(TestPresentacion)

#include "TestPresentacion.moc"
