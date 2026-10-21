// T023.5 (presentacion C++): regla del historial (HistorialDescargas),
// HistorialViewModel y navegacion de AppViewModel (Historial, Diagnostico,
// "Atras", Facturas por mes y Reintentar) con fuentes falsas, reloj fijo y
// evaluaciones reales de evaluarCoberturaMensual. Datos sinteticos; sin QML.

#include "presentation/viewmodels/AppViewModel.h"
#include "presentation/viewmodels/FacturasViewModel.h"
#include "presentation/viewmodels/FranjaMesViewModel.h"
#include "presentation/viewmodels/FuenteHistorial.h"
#include "presentation/viewmodels/HistorialListModel.h"
#include "presentation/viewmodels/HistorialViewModel.h"
#include "presentation/viewmodels/PresentacionViewModels.h"

#include "application/cfdi/HistorialDescargas.h"
#include "domain/cfdi/PlanDescargaGuiada.h"

#include "fakes/FakeCredencialesSatService.h"
#include "fakes/FakePerfilesSatService.h"
#include "FuenteFacturasFake.h"
#include "ServiciosAsincronosFake.h"

#include <QMetaEnum>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

using namespace satcfdi;

namespace {

using namespace pruebasfacturas;
using D = TipoDescarga;
using Pagina = AppViewModel::Pagina;

const MesCalendario kAgosto = *MesCalendario::crear(2026, 8);
const MesCalendario kJulio = *MesCalendario::crear(2026, 7);
const QDateTime kAhora = utc(2026, 9, 15, 18); // como FuenteFalsa

// Lote con los tramos de `evidencias` (todas del mes); `eliminadas`: indices
// cuyas solicitudes se eliminaron.
LoteGuiado lote(MesCalendario mes, const QList<EvidenciaSolicitudMensual>& evidencias, QList<int> eliminadas = {})
{
    LoteGuiado l;
    l.id = SolicitudId::generar().texto();
    l.perfil = kPerfilA;
    l.mes = mes;
    l.creadoEn = utc(2026, 9, 1, 15);
    QList<EstadoResumen> vigentes;
    for (qsizetype i = 0; i < evidencias.size(); ++i) {
        const SolicitudPersistida& s = evidencias.at(i).solicitud;
        TramoLoteConsultado t;
        t.tramo = TramoLoteGuiado{s.tipoCfdi, s.id, s.fechaInicialSat, s.fechaFinalSat};
        t.eliminada = eliminadas.contains(int(i));
        if (!t.eliminada) {
            t.estado = derivarEstadoResumen(s.estadoLocal, s.estadoSolicitudSat);
            vigentes.append(*t.estado);
        }
        l.eliminacionParcial = l.eliminacionParcial || t.eliminada;
        l.tramos.append(t);
    }
    std::stable_sort(l.tramos.begin(), l.tramos.end(), [](const TramoLoteConsultado& a, const TramoLoteConsultado& b) {
        return a.tramo.direccion < b.tramo.direccion;
    });
    l.resultado = resultadoLote(vigentes);
    return l;
}

// Evaluacion del mes con la evidencia vigente (las eliminadas no cuentan).
EvaluacionCoberturaMensual evaluar(MesCalendario mes, QList<EvidenciaSolicitudMensual> evidencias,
                                   QList<int> eliminadas = {})
{
    for (qsizetype i = evidencias.size() - 1; i >= 0; --i) {
        if (eliminadas.contains(int(i))) {
            evidencias.removeAt(i);
        }
    }
    return evaluarCoberturaMensual(kPerfilA, mes, kAhora, evidencias);
}

const QRegularExpression kProhibido(QStringLiteral("solicitud|paquete|verificaci|IdSolicitud|IdPaquete|500[0-9]"),
                                    QRegularExpression::CaseInsensitiveOption);

class HistorialFalso final : public FuenteHistorial {
public:
    QList<EntradaHistorial> entradas;
    bool fallar = false;
    int lecturas = 0;
    QFuture<ResultadoEntradas> historial(const PerfilId&) override
    {
        ++lecturas;
        if (fallar) {
            return QtFuture::makeReadyValueFuture(
                ResultadoEntradas::fallo(ErrorPersistencia::de(ErrorPersistencia::Tipo::Almacenamiento, QStringLiteral("x"))));
        }
        return QtFuture::makeReadyValueFuture(ResultadoEntradas::exito(entradas));
    }
};

EntradaHistorial entrada(MesCalendario mes, ResultadoHistorial r, int emitidas, int recibidas, int hora)
{
    EntradaHistorial e;
    e.loteId = SolicitudId::generar().texto();
    e.perfil = kPerfilA;
    e.mes = mes;
    e.creadoEn = utc(2026, 9, 2, hora);
    e.resultado = r;
    e.emitidas = emitidas;
    e.recibidas = recibidas;
    e.tieneEmitidas = e.tieneRecibidas = true;
    e.diagnosticoEmitidas = SolicitudId::generar();
    e.diagnosticoRecibidas = SolicitudId::generar();
    return e;
}

struct Grafo {
    fakes::FakePerfilesSatService perfiles;
    fakes::FakeCredencialesSatService credenciales;
    SolicitudesServiceAsincrono solicitudes;
    FuenteFalsa fuente;
    PreferenciasMemoria preferencias;
    HistorialFalso historial;
    PresentacionViewModels vms{&solicitudes, &perfiles, &credenciales};

    Grafo()
    {
        vms.facturas()->setIntervaloBusqueda(0);
        vms.setFuenteFacturas(&fuente, &preferencias);
        vms.setFuenteHistorial(&historial);
    }
    AppViewModel* app() { return vms.app(); }
    bool facturasListas()
    {
        return QTest::qWaitFor([&] { return vms.facturas()->mes() != 0; }, 3000);
    }
};

} // namespace

class TestHistorialViewModel : public QObject {
    Q_OBJECT

private slots:
    void reglaResultadosDeLote();
    void reglaEliminacionYSinEvaluacion();
    void reglaDescargaAnterior();
    void filasTextosYVocabulario();
    void accionesDeFila();
    void vistasYRecarga();
    void navegacionHistorialDiagnosticoYAtras();
    void detallesTecnicosDesdeFacturas();
    void reintentarDesdeHistorial();
    void irAMesPendienteAntesDeCargarPerfiles();
    void sinRutaANuevaSolicitud();
    void sinInformacionEsSinFacturas(); // T023.6 (D22)
};

void TestHistorialViewModel::reglaResultadosDeLote()
{
    const QHash<SolicitudId, int> sinCfdi;
    // Completa / Sin facturas.
    {
        const QList<EvidenciaSolicitudMensual> ev{completa(D::Emitidos, kAgosto), completa(D::Recibidos, kAgosto)};
        const auto evaluacion = evaluar(kAgosto, ev);
        QHash<SolicitudId, int> conteos{{ev.at(0).solicitud.id, 3}, {ev.at(1).solicitud.id, 5}};
        EntradaHistorial e = entradaDeLote(lote(kAgosto, ev), &evaluacion, conteos);
        QCOMPARE(e.resultado, ResultadoHistorial::Completa);
        QCOMPARE(e.emitidas, 3);
        QCOMPARE(e.recibidas, 5);
        QVERIFY(!e.puedeReintentar);
        QCOMPARE(e.diagnosticoEmitidas, std::optional(ev.at(0).solicitud.id));
        QCOMPARE(e.diagnosticoRecibidas, std::optional(ev.at(1).solicitud.id));
        QCOMPARE(entradaDeLote(lote(kAgosto, ev), &evaluacion, sinCfdi).resultado, ResultadoHistorial::SinFacturas);
    }
    // En preparacion.
    {
        const QList<EvidenciaSolicitudMensual> ev{enCurso(D::Emitidos, kAgosto), completa(D::Recibidos, kAgosto)};
        const auto evaluacion = evaluar(kAgosto, ev);
        QCOMPARE(entradaDeLote(lote(kAgosto, ev), &evaluacion, sinCfdi).resultado, ResultadoHistorial::EnPreparacion);
    }
    // Requiere atencion (gana sobre en curso); Reintentar si el mes lo ofrece.
    {
        const QList<EvidenciaSolicitudMensual> ev{fallida(D::Emitidos, kAgosto), enCurso(D::Recibidos, kAgosto)};
        const auto evaluacion = evaluar(kAgosto, ev);
        EntradaHistorial e = entradaDeLote(lote(kAgosto, ev), &evaluacion, sinCfdi);
        QCOMPARE(e.resultado, ResultadoHistorial::RequiereAtencion);
        QCOMPARE(e.puedeReintentar, ofertaDescargaGuiada(evaluacion).accion.has_value());
        QCOMPARE(e.diagnosticoEmitidas, std::optional(ev.at(0).solicitud.id)); // la de la causa
    }
    // Cubierta por otra descarga (causa superada, ADR 0020): el lote viejo
    // fallo y un lote posterior cubrio su tramo.
    {
        const EvidenciaSolicitudMensual vieja = fallida(D::Emitidos, kAgosto);
        const EvidenciaSolicitudMensual recibidas = completa(D::Recibidos, kAgosto);
        const EvidenciaSolicitudMensual nueva = completa(D::Emitidos, kAgosto); // creada despues
        const auto evaluacion = evaluar(kAgosto, {vieja, recibidas, nueva});
        QCOMPARE(evaluacion.estado, EstadoMes::Disponibles);
        EntradaHistorial e = entradaDeLote(lote(kAgosto, {vieja, recibidas}), &evaluacion, sinCfdi);
        QCOMPARE(e.resultado, ResultadoHistorial::NoCompletada);
        QVERIFY(!e.puedeReintentar);
        QCOMPARE(e.diagnosticoEmitidas, std::optional(vieja.solicitud.id));
    }
}

void TestHistorialViewModel::reglaEliminacionYSinEvaluacion()
{
    const QList<EvidenciaSolicitudMensual> ev{fallida(D::Emitidos, kAgosto), completa(D::Recibidos, kAgosto)};
    // Se elimina la fallida: conserva mes y fecha, recalcula sin ella.
    const auto evaluacion = evaluar(kAgosto, ev, {0});
    const LoteGuiado l = lote(kAgosto, ev, {0});
    EntradaHistorial e = entradaDeLote(l, &evaluacion, {{ev.at(1).solicitud.id, 4}});
    QVERIFY(e.eliminacionParcial);
    QCOMPARE(e.mes, std::optional(kAgosto));
    QCOMPARE(e.creadoEn, l.creadoEn);
    QCOMPARE(e.resultado, ResultadoHistorial::Completa);
    QVERIFY(!e.tieneEmitidas);
    QVERIFY(!e.diagnosticoEmitidas);
    QCOMPARE(e.recibidas, 4);
    // Todas eliminadas.
    const EntradaHistorial todas = entradaDeLote(lote(kAgosto, ev, {0, 1}), &evaluacion, {});
    QCOMPARE(todas.resultado, ResultadoHistorial::Eliminada);
    QVERIFY(!todas.diagnosticoEmitidas && !todas.diagnosticoRecibidas);
    // Sin evaluacion del mes (lectura fallida): por estados de solicitud.
    QCOMPARE(entradaDeLote(lote(kAgosto, ev), nullptr, {}).resultado, ResultadoHistorial::RequiereAtencion);
    QCOMPARE(entradaDeLote(lote(kAgosto, {enCurso(D::Emitidos, kAgosto)}), nullptr, {}).resultado,
             ResultadoHistorial::EnPreparacion);
}

void TestHistorialViewModel::reglaDescargaAnterior()
{
    SolicitudSinLote s;
    s.id = SolicitudId::generar();
    s.direccion = D::Recibidos;
    s.fechaInicialSat = QStringLiteral("2026-01-01T00:00:00");
    s.fechaFinalSat = QStringLiteral("2026-03-15T23:59:59");
    s.creadaEn = utc(2026, 3, 20, 16);
    s.conFiltros = true;
    EntradaHistorial e = entradaAnterior(kPerfilA, s, {{s.id, 12}});
    QCOMPARE(e.tipo, EntradaHistorial::Tipo::Anterior);
    QCOMPARE(e.resultado, ResultadoHistorial::Anterior);
    QVERIFY(!e.mes); // varios meses
    QCOMPARE(e.desde, QDate(2026, 1, 1));
    QCOMPARE(e.hasta, QDate(2026, 3, 15));
    QCOMPARE(e.recibidas, 12);
    QVERIFY(e.tieneRecibidas && !e.tieneEmitidas);
    QCOMPARE(e.diagnosticoRecibidas, std::optional(s.id));
    s.fechaFinalSat = QStringLiteral("2026-01-31T23:59:59");
    QCOMPARE(entradaAnterior(kPerfilA, s, {}).mes, MesCalendario::crear(2026, 1));
}

void TestHistorialViewModel::filasTextosYVocabulario()
{
    HistorialFalso fuente;
    EntradaHistorial parcial = entrada(kJulio, ResultadoHistorial::Completa, 35, 57, 10);
    parcial.eliminacionParcial = true;
    EntradaHistorial sinFacturas = entrada(*MesCalendario::crear(2026, 2), ResultadoHistorial::SinFacturas, 0, 0, 9);
    EntradaHistorial cubierta = entrada(*MesCalendario::crear(2026, 5), ResultadoHistorial::NoCompletada, 0, 0, 8);
    EntradaHistorial eliminada = entrada(*MesCalendario::crear(2026, 4), ResultadoHistorial::Eliminada, 0, 0, 7);
    eliminada.tieneEmitidas = eliminada.tieneRecibidas = false;
    eliminada.diagnosticoEmitidas.reset();
    eliminada.diagnosticoRecibidas.reset();
    EntradaHistorial anterior;
    anterior.tipo = EntradaHistorial::Tipo::Anterior;
    anterior.perfil = kPerfilA;
    anterior.resultado = ResultadoHistorial::Anterior;
    anterior.desde = QDate(2026, 1, 1);
    anterior.hasta = QDate(2026, 3, 15);
    anterior.creadoEn = utc(2026, 3, 20, 16);
    anterior.tieneEmitidas = true;
    anterior.emitidas = 12;
    anterior.diagnosticoEmitidas = SolicitudId::generar();
    fuente.entradas = {entrada(kAgosto, ResultadoHistorial::EnPreparacion, 0, 0, 12), parcial,
                       entrada(*MesCalendario::crear(2026, 6), ResultadoHistorial::RequiereAtencion, 32, 0, 11),
                       sinFacturas, cubierta, eliminada, anterior};
    fuente.entradas[2].puedeReintentar = true;

    HistorialViewModel vm;
    vm.setFuente(&fuente);
    vm.mostrarPerfil(kPerfilA.texto(), kRfcA, QStringLiteral("Perfil A"));
    vm.activar();
    QTRY_COMPARE(vm.vista(), QStringLiteral("lista"));
    QCOMPARE(vm.subtitulo(), kRfcA + QStringLiteral(" — Perfil A"));
    const HistorialListModel* m = vm.modelo();
    QCOMPARE(m->count(), 7);
    const FilaHistorial& f0 = m->filas().at(0);
    QCOMPARE(f0.titulo, QStringLiteral("Agosto 2026"));
    QCOMPARE(f0.fecha, QStringLiteral("2 sep 2026, 06:00")); // 12:00 UTC = 06:00 Centro
    QCOMPARE(f0.textoFecha, QStringLiteral("Descarga del 2 sep 2026, 06:00"));
    QCOMPARE(f0.resultadoTexto, QStringLiteral("En preparación"));
    QCOMPARE(f0.icono, QStringLiteral("clock"));
    QCOMPARE(f0.textoConteos, QStringLiteral("0 emitidas, 0 recibidas"));
    QVERIFY(f0.puedeVerFacturas && !f0.puedeReintentar && f0.eligeDireccion);
    QCOMPARE(m->filas().at(1).detalle, QStringLiteral("Parte de esta descarga se eliminó"));
    QCOMPARE(m->filas().at(1).textoConteos, QStringLiteral("35 emitidas, 57 recibidas"));
    QCOMPARE(m->filas().at(2).resultadoTexto, QStringLiteral("Requiere atención"));
    QCOMPARE(m->filas().at(2).tono, QStringLiteral("error"));
    QVERIFY(m->filas().at(2).puedeReintentar);
    QCOMPARE(m->filas().at(3).resultadoTexto, QStringLiteral("Sin facturas en el SAT"));
    QCOMPARE(m->filas().at(3).textoConteos, QStringLiteral("El SAT no tenía facturas de este mes"));
    QCOMPARE(m->filas().at(4).resultadoTexto, QStringLiteral("Cubierta por otra descarga"));
    QVERIFY(!m->filas().at(4).puedeReintentar);
    const FilaHistorial& fe = m->filas().at(5);
    QCOMPARE(fe.resultadoTexto, QStringLiteral("Eliminada"));
    QVERIFY(!fe.puedeVerFacturas && !fe.puedeReintentar && fe.puedeVerDetalles && !fe.eligeDireccion);
    const FilaHistorial& fa = m->filas().at(6);
    QCOMPARE(fa.resultadoTexto, QStringLiteral("Descarga anterior"));
    QCOMPARE(fa.titulo, QStringLiteral("1 ene 2026 – 15 mar 2026"));
    QCOMPARE(fa.textoConteos, QStringLiteral("Solo emitidas: 12"));
    QCOMPARE(fa.detalle, QStringLiteral("Hecha con una versión anterior"));
    QVERIFY(fa.anterior && !fa.puedeVerFacturas && fa.puedeVerDetalles && !fa.eligeDireccion);
    QVERIFY(f0.nombreAccesible.startsWith(QStringLiteral("Agosto 2026, En preparación, descarga del ")));
    QCOMPARE(m->data(m->index(0), Qt::AccessibleTextRole).toString(), f0.nombreAccesible);
    QCOMPARE(vm.anuncio(), QStringLiteral("7 descargas"));

    // Vocabulario: ningun texto visible del historial menciona terminos SAT.
    QStringList textos{vm.subtitulo(), vm.textoVista(), vm.anuncio()};
    for (const FilaHistorial& f : m->filas()) {
        textos << f.titulo << f.fecha << f.textoFecha << f.resultadoTexto << f.textoConteos << f.detalle
               << f.nombreAccesible;
    }
    for (const QString& t : std::as_const(textos)) {
        QVERIFY2(!kProhibido.match(t).hasMatch(), qPrintable(t));
    }
}

void TestHistorialViewModel::accionesDeFila()
{
    HistorialFalso fuente;
    EntradaHistorial ambas = entrada(kAgosto, ResultadoHistorial::RequiereAtencion, 1, 2, 12);
    ambas.puedeReintentar = true;
    EntradaHistorial soloRecibidas = entrada(kJulio, ResultadoHistorial::Completa, 0, 2, 11);
    soloRecibidas.tieneEmitidas = false;
    soloRecibidas.diagnosticoEmitidas.reset();
    EntradaHistorial eliminada = entrada(kJulio, ResultadoHistorial::Eliminada, 0, 0, 10);
    eliminada.diagnosticoEmitidas.reset();
    eliminada.diagnosticoRecibidas.reset();
    fuente.entradas = {ambas, soloRecibidas, eliminada};
    HistorialViewModel vm;
    vm.setFuente(&fuente);
    vm.mostrarPerfil(kPerfilA.texto(), kRfcA, QString());
    vm.activar();
    QTRY_COMPARE(vm.modelo()->count(), 3);
    QSignalSpy facturas(&vm, &HistorialViewModel::facturasMesSolicitadas);
    QSignalSpy reintento(&vm, &HistorialViewModel::reintentoSolicitado);
    QSignalSpy diagnostico(&vm, &HistorialViewModel::diagnosticoSolicitado);

    QVERIFY(vm.verFacturas(0));
    QCOMPARE(facturas.takeFirst(), (QVariantList{kPerfilA.texto(), 2026, 8}));
    QVERIFY(vm.reintentar(0));
    QCOMPARE(reintento.takeFirst(), (QVariantList{kPerfilA.texto(), 2026, 8}));
    QVERIFY(!vm.reintentar(1));
    QVERIFY(!vm.verDetallesTecnicos(0)); // hay emitidas y recibidas: hay que elegir
    QVERIFY(vm.verDetallesTecnicos(0, HistorialViewModel::Direccion::Recibidas));
    QCOMPARE(diagnostico.takeFirst().at(0).toString(), ambas.diagnosticoRecibidas->texto());
    QVERIFY(vm.verDetallesTecnicos(1));
    QCOMPARE(diagnostico.takeFirst().at(0).toString(), soloRecibidas.diagnosticoRecibidas->texto());
    QVERIFY(!vm.verDetallesTecnicos(1, HistorialViewModel::Direccion::Emitidas));
    QVERIFY(!vm.verFacturas(2));
    QVERIFY(vm.verDetallesTecnicos(2)); // Eliminada: la lista de Diagnostico
    QCOMPARE(diagnostico.takeFirst().at(0).toString(), QString());
    QVERIFY(!vm.verFacturas(9));
    QVERIFY(facturas.isEmpty() && reintento.isEmpty() && diagnostico.isEmpty());
}

void TestHistorialViewModel::vistasYRecarga()
{
    HistorialFalso fuente;
    HistorialViewModel vm;
    vm.setFuente(&fuente);
    vm.activar();
    QCOMPARE(vm.vista(), QStringLiteral("sinPerfil"));
    vm.mostrarPerfil(kPerfilA.texto(), kRfcA, QString());
    QTRY_COMPARE(vm.vista(), QStringLiteral("vacio"));
    QVERIFY(!vm.textoVista().isEmpty());
    fuente.entradas = {entrada(kAgosto, ResultadoHistorial::Completa, 1, 1, 12)};
    emit fuente.datosCambiados(PerfilId()); // cambio con la pagina visible
    QTRY_COMPARE(vm.vista(), QStringLiteral("lista"));
    fuente.fallar = true;
    vm.recargar();
    QCOMPARE(vm.vista(), QStringLiteral("lista")); // conserva la lista
    QCOMPARE(vm.modelo()->count(), 1);
    vm.desactivar();
    const int lecturas = fuente.lecturas;
    emit fuente.datosCambiados(kPerfilA);
    QCOMPARE(fuente.lecturas, lecturas); // oculta: no recarga
    HistorialViewModel otro;
    otro.setFuente(&fuente);
    otro.mostrarPerfil(kPerfilB.texto(), kRfcB, QString());
    otro.activar();
    QTRY_COMPARE(otro.vista(), QStringLiteral("error"));
}

void TestHistorialViewModel::navegacionHistorialDiagnosticoYAtras()
{
    Grafo g;
    QVERIFY(g.facturasListas());
    g.vms.facturas()->seleccionarPerfilId(kPerfilB.texto());
    AppViewModel* app = g.app();
    QCOMPARE(app->pagina(), Pagina::Facturas);
    QVERIFY(!app->puedeRegresar());
    QVERIFY(g.vms.initialProperties().contains(QStringLiteral("historialViewModel")));
    QVERIFY(!g.vms.initialProperties().contains(QStringLiteral("nuevaSolicitudViewModel")));

    // Cmd+3: Historial del perfil de Facturas; Atras -> Facturas.
    g.historial.entradas = {entrada(kAgosto, ResultadoHistorial::Completa, 1, 1, 12)};
    app->mostrarHistorial();
    QCOMPARE(app->pagina(), Pagina::Historial);
    QCOMPARE(g.vms.historial()->perfilId(), kPerfilB.texto());
    QCOMPARE(app->textoRegresar(), QStringLiteral("Facturas"));
    QTRY_COMPARE(g.vms.historial()->modelo()->count(), 1);

    // Ver detalles tecnicos -> Detalle; Atras -> Historial -> Facturas.
    QVERIFY(g.vms.historial()->verDetallesTecnicos(0, HistorialViewModel::Direccion::Emitidas));
    QCOMPARE(app->pagina(), Pagina::Detalle);
    QCOMPARE(app->solicitudSeleccionadaId(), g.historial.entradas.at(0).diagnosticoEmitidas->texto());
    QCOMPARE(app->textoRegresar(), QStringLiteral("Historial"));
    // Cmd+3 desde el detalle recorta la pila (sin ciclos).
    app->mostrarHistorial();
    QCOMPARE(app->pagina(), Pagina::Historial);
    QCOMPARE(app->textoRegresar(), QStringLiteral("Facturas"));
    QVERIFY(g.vms.historial()->verDetallesTecnicos(0, HistorialViewModel::Direccion::Recibidas));
    app->regresar();
    QCOMPARE(app->pagina(), Pagina::Historial);
    app->regresar();
    QCOMPARE(app->pagina(), Pagina::Facturas);
    QVERIFY(!app->puedeRegresar());

    // "Ir > Diagnostico" -> lista -> Detalle; Atras -> Diagnostico -> Facturas.
    app->mostrarDiagnostico();
    QCOMPARE(app->pagina(), Pagina::Diagnostico);
    QCOMPARE(app->textoRegresar(), QStringLiteral("Facturas"));
    const QString id = SolicitudId::generar().texto();
    QVERIFY(app->abrirDetalle(id));
    QCOMPARE(app->textoRegresar(), QStringLiteral("Diagnóstico"));
    app->regresar();
    QCOMPARE(app->pagina(), Pagina::Diagnostico);
    app->regresar();
    QCOMPARE(app->pagina(), Pagina::Facturas);

    // Desde Perfiles: Diagnostico regresa a Perfiles.
    app->mostrarPerfiles();
    app->mostrarDiagnostico();
    QCOMPARE(app->textoRegresar(), QStringLiteral("Perfiles SAT"));
    app->regresar();
    QCOMPARE(app->pagina(), Pagina::Perfiles);

    // Historial -> Ver facturas: Facturas en ese perfil y mes.
    app->mostrarHistorial();
    QTRY_COMPARE(g.vms.historial()->modelo()->count(), 1);
    QVERIFY(g.vms.historial()->verFacturas(0));
    QCOMPARE(app->pagina(), Pagina::Facturas);
    QTRY_COMPARE(g.vms.facturas()->perfilId(), kPerfilB.texto());
    QCOMPARE(g.vms.facturas()->mes(), 8);
}

void TestHistorialViewModel::detallesTecnicosDesdeFacturas()
{
    Grafo g;
    const EvidenciaSolicitudMensual falla = fallida(D::Recibidos, kAgosto);
    g.fuente.evidenciaPorMes.insert(kAgosto.texto(), {completa(D::Emitidos, kAgosto), falla});
    QVERIFY(g.facturasListas());
    FacturasViewModel* f = g.vms.facturas();
    f->recargar(); // la evidencia se fijo despues de la primera carga
    QTRY_COMPARE(f->franja()->estado(), FranjaMesViewModel::Estado::RequiereAtencion);
    QSignalSpy detalles(f, &FacturasViewModel::detallesTecnicosSolicitados);
    f->verDetallesTecnicos();
    QCOMPARE(detalles.count(), 1);
    QCOMPARE(detalles.at(0).at(3).toString(), falla.solicitud.id.texto()); // la de la causa
    QCOMPARE(g.app()->pagina(), Pagina::Detalle);
    QCOMPARE(g.app()->solicitudSeleccionadaId(), falla.solicitud.id.texto());
    g.app()->regresar();
    QCOMPARE(g.app()->pagina(), Pagina::Facturas);

    // Mes sin descargas: la lista de Diagnostico.
    QVERIFY(f->seleccionarMes(2026, 7));
    QTRY_COMPARE(f->franja()->estado(), FranjaMesViewModel::Estado::SinDescargar);
    f->verDetallesTecnicos();
    QCOMPARE(detalles.at(1).at(3).toString(), QString());
    QCOMPARE(g.app()->pagina(), Pagina::Diagnostico);
}

void TestHistorialViewModel::reintentarDesdeHistorial()
{
    Grafo g;
    g.fuente.evidenciaPorMes.insert(kJulio.texto(), {completa(D::Emitidos, kJulio), fallida(D::Recibidos, kJulio)});
    QVERIFY(g.facturasListas());
    QTRY_COMPARE(g.vms.facturas()->mes(), 8);
    EntradaHistorial e = entrada(kJulio, ResultadoHistorial::RequiereAtencion, 1, 0, 12);
    e.puedeReintentar = true;
    g.historial.entradas = {e};
    g.app()->mostrarHistorial();
    QTRY_COMPARE(g.vms.historial()->modelo()->count(), 1);
    QVERIFY(g.vms.historial()->reintentar(0));
    QCOMPARE(g.app()->pagina(), Pagina::Facturas);
    QTRY_COMPARE(g.vms.facturas()->mes(), 7);
    // La accion del mes (la de la franja: Reintentar) se planifica una vez.
    QTRY_COMPARE(g.fuente.accionesPlanificadas.size(), 1);
    QTest::qWait(20);
    QCOMPARE(g.fuente.accionesPlanificadas.size(), 1);
}

void TestHistorialViewModel::irAMesPendienteAntesDeCargarPerfiles()
{
    FuenteFalsa fuente;
    PreferenciasMemoria preferencias;
    FacturasViewModel vm;
    vm.setFuente(&fuente);
    vm.setPreferencias(&preferencias);
    vm.irAMes(kPerfilB.texto(), 2026, 7); // app arrancada por una notificacion
    QCOMPARE(vm.mes(), 0);
    vm.activar();
    QTRY_COMPARE(vm.perfilId(), kPerfilB.texto());
    QTRY_COMPARE(vm.mes(), 7);
    // Perfil inexistente: conserva la seleccion sin error; mes futuro: el perfil con su mes.
    vm.irAMes(PerfilId::generar().texto(), 2026, 5);
    QCOMPARE(vm.perfilId(), kPerfilB.texto());
    QCOMPARE(vm.mes(), 7);
    vm.irAMes(kPerfilA.texto(), 2027, 1);
    QTRY_COMPARE(vm.perfilId(), kPerfilA.texto());
    QCOMPARE(vm.anio(), 2026);
}

void TestHistorialViewModel::sinRutaANuevaSolicitud()
{
    const QMetaEnum paginas = QMetaEnum::fromType<AppViewModel::Pagina>();
    for (int i = 0; i < paginas.keyCount(); ++i) {
        const QString clave = QString::fromLatin1(paginas.key(i));
        QVERIFY2(clave != QStringLiteral("Nueva") && clave != QStringLiteral("Lista"), qPrintable(clave));
    }
    const QMetaObject* mo = &AppViewModel::staticMetaObject;
    QCOMPARE(mo->indexOfMethod("mostrarNueva()"), -1);
    QCOMPARE(mo->indexOfMethod("mostrarLista()"), -1);
    QVERIFY(mo->indexOfMethod("mostrarHistorial()") >= 0);
    QVERIFY(mo->indexOfMethod("mostrarDiagnostico()") >= 0);
    QVERIFY(mo->indexOfMethod("regresar()") >= 0);
}

void TestHistorialViewModel::sinInformacionEsSinFacturas()
{
    // Rechazada con CodigoEstadoSolicitud 5004 en ambas direcciones, sin la
    // marca de conjunto vacio (filas previas a T023.6).
    auto sinInfo = [](D d) {
        EvidenciaSolicitudMensual e = completa(d, kAgosto);
        e.solicitud.estadoSolicitudSat = EstadoSolicitudSat::Rechazada;
        e.solicitud.codigoEstadoSolicitud = QStringLiteral("5004");
        e.solicitud.conjuntoPaquetesVacioConfirmadoEn.reset();
        return e;
    };
    const QList<EvidenciaSolicitudMensual> ev{sinInfo(D::Emitidos), sinInfo(D::Recibidos)};
    const auto evaluacion = evaluar(kAgosto, ev);
    QCOMPARE(evaluacion.estado, EstadoMes::Disponibles);
    const EntradaHistorial e = entradaDeLote(lote(kAgosto, ev), &evaluacion, {});
    QCOMPARE(e.resultado, ResultadoHistorial::SinFacturas);
    QVERIFY(!e.puedeReintentar);
    // Resultado del lote sin evaluacion (como lo calcula el repositorio).
    QCOMPARE(resultadoLote({derivarEstadoResumen(EstadoLocal::Enviada, EstadoSolicitudSat::Rechazada,
                                                 QStringLiteral("5004"))}),
             ResultadoLote::Terminado);

    // Facturas: el mes queda Disponibles con 0 facturas, sin atencion.
    Grafo g;
    g.fuente.evidenciaPorMes.insert(kAgosto.texto(), ev);
    QVERIFY(g.facturasListas());
    FacturasViewModel* f = g.vms.facturas();
    f->recargar();
    QTRY_COMPARE(f->franja()->estado(), FranjaMesViewModel::Estado::Disponibles);
    QCOMPARE(f->conteoEmitidas() + f->conteoRecibidas(), 0);
    QCOMPARE(f->franja()->accion(), FranjaMesViewModel::Accion::Ninguna);
}

QTEST_MAIN(TestHistorialViewModel)
#include "TestHistorialViewModel.moc"
