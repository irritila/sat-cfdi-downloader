// satcfdi_explorador_tests (T015.3): ExploradorCfdiViewModel, CfdiListModel,
// "Ver facturas" del detalle y la navegacion del explorador, sobre
// ConsultaCatalogoFake (promesas manuales) y fakes de servicios. Sin QML (la
// pagina se prueba en satcfdi_presentation_quicktests).

#include "ConsultaCatalogoFake.h"
#include "ServiciosAsincronosFake.h"
#include "fakes/FakeCredencialesSatService.h"
#include "fakes/FakePerfilesSatService.h"

#include "AccionesFinder.h"
#include "AppViewModel.h"
#include "CfdiListModel.h"
#include "ExploradorCfdiViewModel.h"
#include "PresentacionViewModels.h"
#include "SolicitudDetailViewModel.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

using namespace satcfdi;
using fakes::ConsultaCatalogoFake;
using fakes::DatosCfdi;
using Estado = ExploradorCfdiViewModel::Estado;
using Rol = CfdiListModel::Rol;

namespace {

const QString kId = QStringLiteral("00000000-0000-4000-8000-000000000003");
const QString kOtroId = QStringLiteral("00000000-0000-4000-8000-000000000004");

SolicitudId id(const QString& texto)
{
    return SolicitudId::desdeTexto(texto).value();
}

void procesarEventos()
{
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
    }
}

ResumenIndiceSolicitud resumen(int total, bool indexando = false)
{
    ResumenIndiceSolicitud r;
    r.paquetesDescargados = 1;
    r.paquetesIndexados = indexando ? 0 : 1;
    r.paquetesPendientes = indexando ? 1 : 0;
    r.cfdiTotal = total;
    r.indexando = indexando;
    return r;
}

QStringList clavesLineas(const ExploradorCfdiViewModel& vm)
{
    QStringList r;
    for (const QVariant& l : vm.lineasEstado()) {
        r.append(l.toMap().value(QStringLiteral("clave")).toString());
    }
    return r;
}

class FinderEspia final : public AccionesFinder {
public:
    int carpetasSolicitud = 0;
    ResultadoAccionFinder resultado{ResultadoAccionFinder::Estado::Mostrado, {}};
    QFuture<ResultadoAccionFinder> mostrarPaquete(const SolicitudId&, const QString&) override
    {
        return QtFuture::makeReadyValueFuture(resultado);
    }
    QFuture<ResultadoAccionFinder> abrirCarpetaSolicitud(const SolicitudId&) override
    {
        ++carpetasSolicitud;
        return QtFuture::makeReadyValueFuture(resultado);
    }
    QFuture<ResultadoAccionFinder> abrirCarpetaPaquetes() override
    {
        return QtFuture::makeReadyValueFuture(resultado);
    }
};

// Explorador abierto con su consulta fake (sin retardo de busqueda por omision).
struct Explorador {
    Explorador()
    {
        vm.setConsulta(&consulta);
        vm.setIntervaloBusqueda(0);
    }
    // Abre kId y resuelve resumen y lista iniciales.
    void abrirCon(const QList<CfdiListado>& filas, const ResumenIndiceSolicitud& r)
    {
        QVERIFY(vm.abrir(kId));
        consulta.resolverResumen(consulta.resumenes.size() - 1, r);
        consulta.resolverLista(consulta.listas.size() - 1, filas);
        QTRY_COMPARE(vm.estado(), Estado::ConDatos);
    }

    ConsultaCatalogoFake consulta;
    ExploradorCfdiViewModel vm;
};

} // namespace

class TestExploradorCfdi : public QObject {
    Q_OBJECT

private slots:
    void abrirCargaListaYResumen();
    void filasConColumnasYNombreAccesible();
    void busquedaConRetardoUnaSolaConsulta();
    void ultimaRespuestaGana();
    void filtroPorTipoYSinCoincidencias();
    void precedenciaDeEstados();
    void errorDeLecturaYReintentar();
    void lineasDeEstadoEnOrden();
    void recargaConIndiceCambiadoSinParpadeo();
    void actividadSoloRecargaResumen();
    void otraSolicitudReiniciaFiltrosYLista();
    void previsualizarExponeIntencion();
    void abrirCarpetaUsaFinder();
    void verFacturasSoloConPaquetesDescargados();
    void navegacionDelExplorador();
};

namespace {
// Detalle de la solicitud kId con paquetes en los estados dados.
SolicitudDetalle detalleConPaquetes(const QList<EstadoDescarga>& estados)
{
    SolicitudDetalle d;
    d.resumen.id = id(kId);
    d.resumen.perfilRfc = QStringLiteral("XAXX010101000");
    d.resumen.tipoDescarga = TipoDescarga::Recibidos;
    d.resumen.fechaInicial = QDate(2026, 9, 1);
    d.resumen.fechaFinal = QDate(2026, 9, 30);
    d.resumen.estadoLocal = EstadoLocal::Enviada;
    d.resumen.estadoSat = EstadoSolicitudSat::Terminada;
    d.resumen.creadaEn = QDateTime(QDate(2026, 10, 1), QTime(10, 0), QTimeZone::UTC);
    int n = 0;
    for (EstadoDescarga e : estados) {
        PaqueteResumen p;
        p.idPaqueteSat = QStringLiteral("PAQ_%1").arg(++n);
        p.estadoDescarga = e;
        p.disponibleEn = d.resumen.creadaEn;
        if (e == EstadoDescarga::Descargado) {
            p.descargadoEn = d.resumen.creadaEn;
        }
        d.paquetes.append(p);
    }
    d.resumen.totalPaquetes = int(d.paquetes.size());
    return d;
}

struct Grafo {
    fakes::FakePerfilesSatService perfiles;
    fakes::FakeCredencialesSatService credenciales;
    SolicitudesServiceAsincrono solicitudes;
    ConsultaCatalogoFake catalogo;
    PresentacionViewModels vms{&solicitudes, &perfiles, &credenciales};
};
} // namespace

void TestExploradorCfdi::abrirCargaListaYResumen()
{
    Explorador e;
    QCOMPARE(e.vm.estado(), Estado::Ninguno);
    QVERIFY(!e.vm.abrir(QStringLiteral("no-es-uuid")));
    QVERIFY(e.vm.abrir(kId));
    QCOMPARE(e.vm.estado(), Estado::Cargando);
    QCOMPARE(e.vm.vista(), QStringLiteral("cargando"));
    QCOMPARE(e.consulta.revisadas.size(), 1); // revalida huellas al abrir
    QCOMPARE(e.consulta.listas.size(), 1);
    QCOMPARE(e.consulta.resumenes.size(), 1);
    QCOMPARE(e.consulta.filtros.constFirst().orden, OrdenListaCfdi::FechaDescendente);
    QVERIFY(e.consulta.filtros.constFirst().texto.isEmpty());
    QVERIFY(!e.consulta.filtros.constFirst().tipo.has_value());

    // Solo la lista: aun Cargando (falta el resumen).
    e.consulta.resolverLista(0, fakes::cfdis(3));
    procesarEventos();
    QCOMPARE(e.vm.estado(), Estado::Cargando);
    e.consulta.resolverResumen(0, resumen(3));
    QTRY_COMPARE(e.vm.estado(), Estado::ConDatos);
    QCOMPARE(e.vm.vista(), QStringLiteral("lista"));
    // El conteo coincide con el indice.
    QCOMPARE(e.vm.modelo()->count(), 3);
    QCOMPARE(e.vm.totalCfdi(), 3);
    QVERIFY(!e.vm.actualizando());
    QVERIFY(e.vm.lineasEstado().isEmpty());
}

void TestExploradorCfdi::filasConColumnasYNombreAccesible()
{
    Explorador e;
    DatosCfdi completo;
    DatosCfdi ilegible;
    ilegible.posicion = 1;
    ilegible.tipo = TipoComprobante::Desconocido;
    ilegible.fecha = QString();
    ilegible.serie = QString();
    ilegible.folio = QStringLiteral("77");
    ilegible.emisorNombre = QString();
    ilegible.total = QString();
    ilegible.uuid = QString();
    ilegible.estado = EstadoParseo::Ilegible;
    e.abrirCon({fakes::cfdi(completo), fakes::cfdi(ilegible)}, resumen(2));

    const CfdiListModel* m = e.vm.modelo();
    const QModelIndex f0 = m->index(0);
    QCOMPARE(m->data(f0, Rol::FechaRole).toString(), QStringLiteral("3 sep 2026"));
    QCOMPARE(m->data(f0, Rol::TipoRole).toString(), QStringLiteral("I"));
    QCOMPARE(m->data(f0, Rol::TipoTextoRole).toString(), QStringLiteral("Ingreso"));
    QCOMPARE(m->data(f0, Rol::SerieFolioRole).toString(), QStringLiteral("A-12"));
    QCOMPARE(m->data(f0, Rol::EmisorRfcRole).toString(), QStringLiteral("XAXX010101000"));
    QCOMPARE(m->data(f0, Rol::EmisorNombreRole).toString(), QStringLiteral("Empresa Ejemplo"));
    QCOMPARE(m->data(f0, Rol::ReceptorRfcRole).toString(), QStringLiteral("XEXX010101000"));
    QCOMPARE(m->data(f0, Rol::ReceptorNombreRole).toString(), QStringLiteral("Cliente Ejemplo"));
    QCOMPARE(m->data(f0, Rol::TotalRole).toString(), QStringLiteral("1160.00")); // texto del XML
    QCOMPARE(m->data(f0, Rol::MonedaRole).toString(), QStringLiteral("MXN"));
    QCOMPARE(m->data(f0, Rol::MetodoPagoRole).toString(), QStringLiteral("PUE"));
    QCOMPARE(m->data(f0, Rol::UuidCortoRole).toString(), QStringLiteral("0A1B2C3D"));
    QCOMPARE(m->uuidEn(0), QStringLiteral("0A1B2C3D-0000-4000-8000-000000000001"));
    QCOMPARE(m->data(f0, Rol::EstadoParseoRole).toString(), QStringLiteral("Completo"));
    // Nombre accesible "tipo, fecha, emisor, total".
    QCOMPARE(m->data(f0, Rol::NombreAccesibleRole).toString(),
             QStringLiteral("Ingreso, 3 sep 2026, Empresa Ejemplo XAXX010101000, 1160.00 MXN"));

    const QModelIndex f1 = m->index(1);
    QCOMPARE(m->data(f1, Rol::SerieFolioRole).toString(), QStringLiteral("77"));
    QCOMPARE(m->data(f1, Rol::EstadoParseoRole).toString(), QStringLiteral("Ilegible"));
    QCOMPARE(m->data(f1, Rol::UuidCortoRole).toString(), QString());
    QCOMPARE(m->data(f1, Rol::NombreAccesibleRole).toString(),
             QStringLiteral("Sin tipo, sin fecha, XAXX010101000, sin total, ilegible"));
    QCOMPARE(m->roleNames().value(Rol::NombreAccesibleRole), QByteArray("nombreAccesible"));
}

void TestExploradorCfdi::busquedaConRetardoUnaSolaConsulta()
{
    Explorador e;
    e.abrirCon(fakes::cfdis(3), resumen(3));
    e.vm.setIntervaloBusqueda(40);
    const int antes = e.consulta.listas.size();
    e.vm.setTexto(QStringLiteral("x"));
    e.vm.setTexto(QStringLiteral("xa"));
    e.vm.setTexto(QStringLiteral("xaxx "));
    QVERIFY(e.vm.hayFiltros());
    procesarEventos();
    QCOMPARE(e.consulta.listas.size(), antes); // aun en el retardo
    QTRY_COMPARE(e.consulta.listas.size(), antes + 1);
    QTest::qWait(80);
    QCOMPARE(e.consulta.listas.size(), antes + 1); // una sola consulta
    QCOMPARE(e.consulta.filtros.constLast().texto, QStringLiteral("xaxx")); // recortado
    // Mientras llega la respuesta la lista anterior sigue visible.
    QCOMPARE(e.vm.estado(), Estado::ConDatos);
    QCOMPARE(e.vm.modelo()->count(), 3);
    QVERIFY(e.vm.actualizando());
    e.consulta.resolverLista(antes, fakes::cfdis(1));
    QTRY_COMPARE(e.vm.modelo()->count(), 1);
    QVERIFY(!e.vm.actualizando());
}

void TestExploradorCfdi::ultimaRespuestaGana()
{
    Explorador e;
    e.abrirCon(fakes::cfdis(3), resumen(3));
    const int base = e.consulta.listas.size();
    e.vm.setTipo(QStringLiteral("E")); // al instante
    e.vm.setTexto(QStringLiteral("cliente"));
    QTRY_COMPARE(e.consulta.listas.size(), base + 2);
    // Llega primero la ultima y despues la anterior: gana la ultima.
    e.consulta.resolverLista(base + 1, fakes::cfdis(2, QStringLiteral("PAQ_B")));
    QTRY_COMPARE(e.vm.modelo()->count(), 2);
    e.consulta.resolverLista(base, fakes::cfdis(5, QStringLiteral("PAQ_A")));
    procesarEventos();
    QCOMPARE(e.vm.modelo()->count(), 2);
    QCOMPARE(e.vm.modelo()->filaEn(0)->paqueteId, QStringLiteral("PAQ_B"));
}

void TestExploradorCfdi::filtroPorTipoYSinCoincidencias()
{
    Explorador e;
    e.abrirCon(fakes::cfdis(3), resumen(3));
    const int base = e.consulta.listas.size();
    QSignalSpy filtros(&e.vm, &ExploradorCfdiViewModel::filtrosChanged);
    e.vm.setTipo(QStringLiteral("E"));
    QCOMPARE(filtros.size(), 1);
    QCOMPARE(e.consulta.listas.size(), base + 1); // sin retardo
    QCOMPARE(e.consulta.filtros.constLast().tipo, std::optional(TipoComprobante::Egreso));
    e.consulta.resolverLista(base, {});
    QTRY_COMPARE(e.vm.vista(), QStringLiteral("sinCoincidencias"));
    QVERIFY(e.vm.hayFiltros());

    e.vm.limpiarFiltros();
    QVERIFY(!e.vm.hayFiltros());
    QCOMPARE(e.vm.tipo(), QString());
    QCOMPARE(e.consulta.listas.size(), base + 2);
    QVERIFY(!e.consulta.filtros.constLast().tipo.has_value());
    QVERIFY(e.consulta.filtros.constLast().texto.isEmpty());
    e.consulta.resolverLista(base + 1, fakes::cfdis(3));
    QTRY_COMPARE(e.vm.vista(), QStringLiteral("lista"));
}

void TestExploradorCfdi::precedenciaDeEstados()
{
    // Lista vacia mientras se indexa: sin estado vacio.
    {
        Explorador e;
        e.abrirCon({}, resumen(0, true));
        QCOMPARE(e.vm.vista(), QStringLiteral("indexando"));
        QCOMPARE(clavesLineas(e.vm), QStringList{QStringLiteral("indexando")});
    }
    // Cero CFDI y nada indexandose: "Sin facturas".
    {
        Explorador e;
        e.abrirCon({}, resumen(0, false));
        QCOMPARE(e.vm.vista(), QStringLiteral("sinFacturas"));
    }
    // Con CFDI indexados la lista se ve aunque siga indexando.
    {
        Explorador e;
        e.abrirCon(fakes::cfdis(2), resumen(2, true));
        QCOMPARE(e.vm.vista(), QStringLiteral("lista"));
        QVERIFY(e.vm.indexando());
    }
    // Filtros sin coincidencias ganan a "indexando".
    {
        Explorador e;
        e.abrirCon(fakes::cfdis(2), resumen(2, true));
        e.vm.setTipo(QStringLiteral("P"));
        e.consulta.resolverLista(e.consulta.listas.size() - 1, {});
        QTRY_COMPARE(e.vm.vista(), QStringLiteral("sinCoincidencias"));
    }
    // Error gana a todo (resumen ilegible aunque la lista llegue).
    {
        Explorador e;
        QVERIFY(e.vm.abrir(kId));
        e.consulta.resolverLista(0, fakes::cfdis(2));
        e.consulta.fallarResumen(0);
        QTRY_COMPARE(e.vm.vista(), QStringLiteral("error"));
        QCOMPARE(e.vm.estado(), Estado::Error);
        QVERIFY(!e.vm.errorMessage().isEmpty());
    }
    // Un future cancelado cuenta como error de lectura.
    {
        Explorador e;
        QVERIFY(e.vm.abrir(kId));
        e.consulta.resolverResumen(0, resumen(1));
        e.consulta.listas.cancelar(0);
        QTRY_COMPARE(e.vm.estado(), Estado::Error);
        QVERIFY(!e.vm.actualizando());
    }
}

void TestExploradorCfdi::errorDeLecturaYReintentar()
{
    Explorador e;
    e.abrirCon(fakes::cfdis(2), resumen(2));
    // Error en una recarga: reemplaza la lista.
    emit e.consulta.indiceCambiado(id(kId));
    e.consulta.fallarLista(e.consulta.listas.size() - 1);
    e.consulta.resolverResumen(e.consulta.resumenes.size() - 1, resumen(2));
    QTRY_COMPARE(e.vm.vista(), QStringLiteral("error"));
    // Con error, las senales no recargan (espera Reintentar).
    const int listas = e.consulta.listas.size();
    emit e.consulta.indiceCambiado(id(kId));
    QCOMPARE(e.consulta.listas.size(), listas);

    e.vm.recargar();
    QCOMPARE(e.vm.vista(), QStringLiteral("cargando"));
    QCOMPARE(e.consulta.listas.size(), listas + 1);
    e.consulta.resolverLista(listas, fakes::cfdis(2));
    e.consulta.resolverResumen(e.consulta.resumenes.size() - 1, resumen(2));
    QTRY_COMPARE(e.vm.vista(), QStringLiteral("lista"));
}

void TestExploradorCfdi::lineasDeEstadoEnOrden()
{
    Explorador e;
    ResumenIndiceSolicitud r;
    r.paquetesDescargados = 6;
    r.paquetesPendientes = 2;
    r.paquetesNoDisponibles = 1;
    r.paquetesIlegibles = 3;
    r.cfdiTotal = 10;
    r.cfdiIlegibles = 2;
    r.cfdiNoSoportados = 1;
    r.indexando = true;
    e.abrirCon(fakes::cfdis(10), r);
    QCOMPARE(clavesLineas(e.vm), (QStringList{QStringLiteral("indexando"), QStringLiteral("noDisponibles"),
                                              QStringLiteral("ilegibles"), QStringLiteral("cfdiIlegibles")}));
    const QVariantList l = e.vm.lineasEstado();
    QCOMPARE(l.at(0).toMap().value(QStringLiteral("texto")).toString(), QStringLiteral("Indexando 2 paquetes…"));
    QCOMPARE(l.at(0).toMap().value(QStringLiteral("accionCarpeta")).toBool(), false);
    QCOMPARE(l.at(1).toMap().value(QStringLiteral("texto")).toString(),
             QStringLiteral("1 paquete no disponible (ZIP faltante)"));
    QCOMPARE(l.at(1).toMap().value(QStringLiteral("accionCarpeta")).toBool(), true);
    QCOMPARE(l.at(2).toMap().value(QStringLiteral("texto")).toString(), QStringLiteral("3 paquetes ilegibles"));
    QCOMPARE(l.at(2).toMap().value(QStringLiteral("accionCarpeta")).toBool(), true);
    QCOMPARE(l.at(3).toMap().value(QStringLiteral("conteo")).toInt(), 3);
    QCOMPARE(l.at(3).toMap().value(QStringLiteral("texto")).toString(),
             QStringLiteral("3 CFDI ilegibles o no soportados"));

    // Sin indexacion ni problemas: sin lineas.
    Explorador limpio;
    limpio.abrirCon(fakes::cfdis(1), resumen(1));
    QVERIFY(limpio.vm.lineasEstado().isEmpty());
}

void TestExploradorCfdi::recargaConIndiceCambiadoSinParpadeo()
{
    Explorador e;
    e.abrirCon(fakes::cfdis(3), resumen(3, true));
    CfdiListModel* m = e.vm.modelo();
    QSignalSpy reinicios(m, &QAbstractItemModel::modelAboutToBeReset);
    QSignalSpy insertadas(m, &QAbstractItemModel::rowsInserted);
    QSignalSpy quitadas(m, &QAbstractItemModel::rowsRemoved);
    QSignalSpy estados(&e.vm, &ExploradorCfdiViewModel::estadoChanged);
    QSignalSpy vistas(&e.vm, &ExploradorCfdiViewModel::vistaChanged);

    // Otra solicitud: no recarga.
    const int listas = e.consulta.listas.size();
    emit e.consulta.indiceCambiado(id(kOtroId));
    QCOMPARE(e.consulta.listas.size(), listas);

    emit e.consulta.indiceCambiado(id(kId));
    QCOMPARE(e.consulta.listas.size(), listas + 1);
    QCOMPARE(e.consulta.revisadas.size(), 1); // revisarSolicitud solo al abrir
    QVERIFY(e.vm.actualizando());
    QCOMPARE(e.vm.estado(), Estado::ConDatos);
    QCOMPARE(m->count(), 3);

    // Llega un CFDI nuevo entre los existentes.
    QList<CfdiListado> nuevas = fakes::cfdis(3);
    DatosCfdi d;
    d.paquete = QStringLiteral("PAQ_02");
    d.uuid = QStringLiteral("0A1B2C3D-0000-4000-8000-0000000000ff");
    nuevas.insert(1, fakes::cfdi(d));
    e.consulta.resolverLista(listas, nuevas);
    e.consulta.resolverResumen(e.consulta.resumenes.size() - 1, resumen(4, false));
    QTRY_COMPARE(m->count(), 4);
    QVERIFY(!e.vm.actualizando());
    QCOMPARE(reinicios.size(), 0);
    QCOMPARE(quitadas.size(), 0);
    QCOMPARE(insertadas.size(), 1);
    QCOMPARE(insertadas.constFirst().at(1).toInt(), 1);
    QCOMPARE(estados.size(), 0); // nunca volvio a Cargando
    QCOMPARE(vistas.size(), 0);  // siempre "lista"
    QCOMPARE(m->filaEn(0)->posicion, 0);
    QCOMPARE(m->filaEn(1)->paqueteId, QStringLiteral("PAQ_02"));
    QCOMPARE(e.vm.totalCfdi(), 4);
    QVERIFY(!e.vm.indexando());

    // Mismos CFDI con un dato cambiado: solo dataChanged.
    QSignalSpy cambios(m, &QAbstractItemModel::dataChanged);
    nuevas[2].resumen.total = ImporteCfdi::desdeTexto(u"99.00");
    emit e.consulta.indiceCambiado(id(kId));
    e.consulta.resolverLista(e.consulta.listas.size() - 1, nuevas);
    e.consulta.resolverResumen(e.consulta.resumenes.size() - 1, resumen(4, false));
    QTRY_COMPARE(cambios.size(), 1);
    QCOMPARE(cambios.constFirst().at(0).toModelIndex().row(), 2);
    QCOMPARE(insertadas.size(), 1);
    QCOMPARE(reinicios.size(), 0);
}

void TestExploradorCfdi::actividadSoloRecargaResumen()
{
    Explorador e;
    e.abrirCon(fakes::cfdis(1), resumen(1, true));
    const int listas = e.consulta.listas.size();
    const int resumenes = e.consulta.resumenes.size();
    emit e.consulta.actividadCambiada();
    QCOMPARE(e.consulta.listas.size(), listas);
    QCOMPARE(e.consulta.resumenes.size(), resumenes + 1);
    e.consulta.resolverResumen(resumenes, resumen(1, false));
    QTRY_VERIFY(!e.vm.indexando());

    // Al salir de la pagina deja de recargar.
    e.vm.desactivar();
    emit e.consulta.actividadCambiada();
    emit e.consulta.indiceCambiado(id(kId));
    QCOMPARE(e.consulta.resumenes.size(), resumenes + 1);
    QCOMPARE(e.consulta.listas.size(), listas);
}

void TestExploradorCfdi::otraSolicitudReiniciaFiltrosYLista()
{
    Explorador e;
    e.abrirCon(fakes::cfdis(2), resumen(2));
    e.vm.setTipo(QStringLiteral("I"));
    e.consulta.resolverLista(e.consulta.listas.size() - 1, fakes::cfdis(2));
    QTRY_VERIFY(!e.vm.actualizando());

    // Reabrir la misma solicitud conserva filtros y lista (sin Cargando).
    QVERIFY(e.vm.abrir(kId));
    QCOMPARE(e.vm.estado(), Estado::ConDatos);
    QCOMPARE(e.vm.tipo(), QStringLiteral("I"));
    QCOMPARE(e.consulta.filtros.constLast().tipo, std::optional(TipoComprobante::Ingreso));

    // Otra solicitud: lista y filtros vacios, Cargando.
    QVERIFY(e.vm.abrir(kOtroId));
    QCOMPARE(e.vm.estado(), Estado::Cargando);
    QCOMPARE(e.vm.modelo()->count(), 0);
    QVERIFY(!e.vm.hayFiltros());
    QCOMPARE(e.consulta.solicitudesListadas.constLast(), id(kOtroId));
    QCOMPARE(e.consulta.revisadas.size(), 3);
}

void TestExploradorCfdi::previsualizarExponeIntencion()
{
    Explorador e;
    e.abrirCon(fakes::cfdis(3, QStringLiteral("PAQ_09")), resumen(3));
    QSignalSpy intencion(&e.vm, &ExploradorCfdiViewModel::previsualizacionSolicitada);
    QVERIFY(e.vm.previsualizar(2));
    QCOMPARE(intencion.size(), 1);
    QCOMPARE(intencion.constFirst().at(0).toString(), kId);
    QCOMPARE(intencion.constFirst().at(1).toString(), QStringLiteral("PAQ_09"));
    QCOMPARE(intencion.constFirst().at(2).toInt(), 2);
    QVERIFY(!e.vm.previsualizar(7));
    QCOMPARE(intencion.size(), 1);
}

void TestExploradorCfdi::abrirCarpetaUsaFinder()
{
    Explorador e;
    QVERIFY(!e.vm.puedeAbrirCarpeta());
    FinderEspia finder;
    e.vm.setAccionesFinder(&finder);
    e.abrirCon({}, resumen(0));
    QVERIFY(e.vm.puedeAbrirCarpeta());
    finder.resultado = {ResultadoAccionFinder::Estado::NoEncontrado, QStringLiteral("La carpeta ya no existe.")};
    e.vm.abrirCarpetaSolicitud();
    QCOMPARE(finder.carpetasSolicitud, 1);
    QTRY_COMPARE(e.vm.mensajeFinder(), QStringLiteral("La carpeta ya no existe."));
    finder.resultado = {ResultadoAccionFinder::Estado::Mostrado, {}};
    e.vm.abrirCarpetaSolicitud();
    QTRY_COMPARE(e.vm.mensajeFinder(), QString());
}

void TestExploradorCfdi::verFacturasSoloConPaquetesDescargados()
{
    Grafo g;
    SolicitudDetailViewModel* detalle = g.vms.detalle();

    // Sin consulta del indice: nunca hay "Ver facturas".
    detalle->cargar(kId);
    g.solicitudes.detalle.resolverUltima(SolicitudesService::ResultadoDetalle::exito(
        detalleConPaquetes({EstadoDescarga::Descargado})));
    QTRY_VERIFY(detalle->cargada());
    QVERIFY(!detalle->puedeVerFacturas());

    g.vms.setConsultaCatalogo(&g.catalogo);
    QVERIFY(detalle->puedeVerFacturas());
    QCOMPARE(g.catalogo.resumenes.size(), 1);
    QVERIFY(detalle->totalFacturas().isNull()); // aun sin numero
    g.catalogo.resolverResumen(0, resumen(7, true));
    QTRY_COMPARE(detalle->totalFacturas().toInt(), 7);
    QVERIFY(detalle->indexandoFacturas());

    // indiceCambiado de la solicitud actualiza el numero.
    emit g.catalogo.indiceCambiado(id(kId));
    QCOMPARE(g.catalogo.resumenes.size(), 2);
    g.catalogo.resolverResumen(1, resumen(9, false));
    QTRY_COMPARE(detalle->totalFacturas().toInt(), 9);
    QVERIFY(!detalle->indexandoFacturas());
    // Un error de lectura deja "Ver facturas" sin numero.
    emit g.catalogo.actividadCambiada();
    g.catalogo.fallarResumen(2);
    QTRY_VERIFY(detalle->totalFacturas().isNull());

    // Sin paquetes Descargado (Disponible, Error, Vencido): sin accion ni consulta.
    const int resumenes = g.catalogo.resumenes.size();
    detalle->recargar();
    g.solicitudes.detalle.resolverUltima(SolicitudesService::ResultadoDetalle::exito(
        detalleConPaquetes({EstadoDescarga::Disponible, EstadoDescarga::Error, EstadoDescarga::Vencido})));
    QTRY_VERIFY(!detalle->puedeVerFacturas());
    QCOMPARE(g.catalogo.resumenes.size(), resumenes);
    QVERIFY(detalle->totalFacturas().isNull());

    // Sin paquetes.
    detalle->recargar();
    g.solicitudes.detalle.resolverUltima(SolicitudesService::ResultadoDetalle::exito(detalleConPaquetes({})));
    QTRY_COMPARE(detalle->totalPaquetes(), 0);
    QVERIFY(!detalle->puedeVerFacturas());
}

void TestExploradorCfdi::navegacionDelExplorador()
{
    Grafo g;
    AppViewModel* app = g.vms.app();
    ExploradorCfdiViewModel* explorador = g.vms.explorador();
    // Sin consulta del indice no se navega.
    QVERIFY(!app->abrirFacturas(kId));
    QCOMPARE(app->pagina(), AppViewModel::Pagina::Lista);

    g.vms.setConsultaCatalogo(&g.catalogo);
    QVERIFY(app->abrirDetalle(kId));
    g.solicitudes.detalle.resolverUltima(SolicitudesService::ResultadoDetalle::exito(
        detalleConPaquetes({EstadoDescarga::Descargado})));
    QTRY_VERIFY(g.vms.detalle()->cargada());
    QVERIFY(!app->abrirFacturas(QStringLiteral("no-es-uuid")));
    QCOMPARE(app->pagina(), AppViewModel::Pagina::Detalle);

    const int obtenciones = int(g.solicitudes.idsObtener.size());
    QVERIFY(app->abrirFacturas(kId));
    QCOMPARE(app->pagina(), AppViewModel::Pagina::Facturas);
    QCOMPARE(explorador->solicitudId(), kId);
    QCOMPARE(g.solicitudes.idsObtener.size(), obtenciones); // el detalle ya era de kId
    QCOMPARE(g.catalogo.revisadas.size(), 1);

    // Regresar: detalle de la misma solicitud, sin recargarlo; el explorador
    // deja de recargar.
    app->regresarDeFacturas();
    QCOMPARE(app->pagina(), AppViewModel::Pagina::Detalle);
    QCOMPARE(g.solicitudes.idsObtener.size(), obtenciones);
    const int listas = g.catalogo.listas.size();
    emit g.catalogo.indiceCambiado(id(kId));
    QCOMPARE(g.catalogo.listas.size(), listas);

    // La solicitud se elimina con el explorador abierto: detalle "no encontrada".
    QVERIFY(app->abrirFacturas(kId));
    emit g.solicitudes.solicitudEliminada(id(kId));
    QTRY_COMPARE(app->pagina(), AppViewModel::Pagina::Detalle);
    QCOMPARE(g.vms.detalle()->estado(), SolicitudDetailViewModel::Estado::NoEncontrada);
    QCOMPARE(g.vms.initialProperties().value(QStringLiteral("exploradorViewModel")).value<QObject*>(),
             static_cast<QObject*>(explorador));
}

QTEST_MAIN(TestExploradorCfdi)
#include "TestExploradorCfdi.moc"
