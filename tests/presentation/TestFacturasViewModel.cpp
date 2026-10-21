// T023.4 (presentacion C++): FacturasViewModel, FranjaMesViewModel,
// MesesFacturasModel y FacturasListModel con una FuenteFacturasMes falsa
// (respuestas listas o pendientes), reloj fijo y evaluaciones reales de
// evaluarCoberturaMensual sobre evidencia sintetica. Sin QML.

#include "presentation/viewmodels/AppViewModel.h"
#include "presentation/viewmodels/FacturasListModel.h"
#include "presentation/viewmodels/FacturasViewModel.h"
#include "presentation/viewmodels/FranjaMesViewModel.h"
#include "presentation/viewmodels/MesesFacturasModel.h"
#include "presentation/viewmodels/PerfilesDisponiblesModel.h"
#include "presentation/viewmodels/PresentacionViewModels.h"

#include "domain/solicitudes/OperacionSat.h"

#include "fakes/FakeCredencialesSatService.h"
#include "fakes/FakePerfilesSatService.h"
#include "FuenteFacturasFake.h"
#include "ServiciosAsincronosFake.h"

#include <QHash>
#include <QPromise>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

#include <functional>
#include <memory>

using namespace satcfdi;

namespace {

using namespace pruebasfacturas;
using Estado = FranjaMesViewModel::Estado;
using Accion = FranjaMesViewModel::Accion;
using D = TipoDescarga;

struct Entorno {
    FuenteFalsa fuente;
    PreferenciasMemoria preferencias;
    FacturasViewModel vm;

    Entorno()
    {
        vm.setIntervaloBusqueda(0);
        vm.setFuente(&fuente);
        vm.setPreferencias(&preferencias);
    }

    bool activar()
    {
        vm.activar();
        return QTest::qWaitFor([&] { return vm.franja()->estado() != Estado::Cargando && vm.mes() != 0; }, 3000);
    }

    bool esperarFranja(Estado e)
    {
        return QTest::qWaitFor([&] { return vm.franja()->estado() == e; }, 3000);
    }
};

PreplanGuiado preplanConConfirmacion(D d)
{
    PreplanGuiado p;
    p.perfil = kPerfilA;
    p.mes = *MesCalendario::crear(2026, 8);
    CandidatoGuiado c;
    c.direccion = d;
    c.duplicado.clasificacion = ClasificacionDuplicado::RequiereConfirmacion;
    p.candidatos = {c};
    return p;
}

// Textos visibles de la pantalla en su estado actual.
QStringList textosVisibles(const FacturasViewModel& vm)
{
    const FranjaMesViewModel* f = vm.franja();
    QStringList t{f->estadoTexto(), f->titular(), f->descripcion(), f->textoAccion(), f->motivoDeshabilitada(),
                  f->etapa(), f->nota(), f->hasta(), f->causa(), f->textoEnlace(), f->aviso(), f->nombreAccesible(),
                  vm.tituloMes(), vm.textoPestanaEmitidas(), vm.textoPestanaRecibidas(), vm.encabezadoContraparte(),
                  vm.placeholderBusqueda(), vm.textoVista(), vm.motivoEFirma(), vm.confirmacionTitulo(),
                  vm.confirmacionTexto(), vm.textoConfirmar(), vm.textoCancelar(), vm.anuncio()};
    for (const FilaMesFacturas& m : vm.meses()->filas()) {
        t << m.nombre << m.estadoTexto << m.textoConteo << m.nombreAccesible;
    }
    for (const FilaFactura& r : vm.modelo()->filas()) {
        t << r.nombreAccesible << r.contraparte;
    }
    return t;
}

} // namespace

class TestFacturasViewModel : public QObject {
    Q_OBJECT

private slots:
    void seleccionInicialUltimoPerfilYMesAnterior();
    void eneroSeleccionaDiciembreYPerfilInvalidoUsaElPrimero();
    void listaConSoloContraparteYPestanas();
    void totalConFormatoYDetalle();
    void mesesConEstadoConteoYNombreAccesible();
    void navegacionDeMesesSinFuturos();
    void franjaPorEstado();
    void franjaMesEnCursoHastaYActualizar();
    void reenvioDeDescargasSinEnviar();
    void eFirmaNoListaRegistrar();
    void accionConConfirmacionPorDireccion();
    void planDesactualizadoReplaneaUnaVez();
    void cambioDePerfilConservaMesYGuardaPreferencia();
    void sinBloquearYErrorDeLectura();
    void anuncioUnaVezPorCambioDeEstado();
    void respuestasDesordenadasGanaLaUltima();
    void vocabularioSinTerminosTecnicos();
    void preparandoConReservasSinLibres();
    void appViewModelFacturasPorOmision();
};

void TestFacturasViewModel::seleccionInicialUltimoPerfilYMesAnterior()
{
    Entorno e;
    e.preferencias.valor = kPerfilB.texto();
    QVERIFY(e.activar());
    QCOMPARE(e.vm.perfilId(), kPerfilB.texto());
    QCOMPARE(e.vm.perfilIndice(), 1);
    QCOMPARE(e.vm.anio(), 2026);
    QCOMPARE(e.vm.mes(), 8); // 15 sep -> agosto
    QCOMPARE(e.vm.tituloMes(), QStringLiteral("Agosto 2026"));
    QCOMPARE(e.vm.perfiles()->count(), 2);
}

void TestFacturasViewModel::eneroSeleccionaDiciembreYPerfilInvalidoUsaElPrimero()
{
    Entorno e;
    e.fuente.ahora = utc(2027, 1, 10, 18);
    e.preferencias.valor = QStringLiteral("0f000000-0000-4000-8000-0000000000ee"); // ya no existe
    QVERIFY(e.activar());
    QCOMPARE(e.vm.perfilId(), kPerfilA.texto());
    QCOMPARE(e.vm.anio(), 2026);
    QCOMPARE(e.vm.mes(), 12);
    // Sin perfiles: vista y franja sin perfil.
    Entorno vacio;
    vacio.fuente.listaPerfiles.clear();
    vacio.vm.activar();
    QTRY_COMPARE(vacio.vm.franja()->estado(), Estado::SinPerfil);
    QCOMPARE(vacio.vm.vista(), QStringLiteral("sinPerfil"));
    QVERIFY(!vacio.vm.hayPerfiles());
}

void TestFacturasViewModel::listaConSoloContraparteYPestanas()
{
    Entorno e;
    e.fuente.cfdisPorMes[QStringLiteral("2026-08")] = {
        cfdi(D::Emitidos, 1, QStringLiteral("2026-08-03T10:00:00"), QStringLiteral("PERFIL A"), QStringLiteral("CLIENTE UNO")),
        cfdi(D::Emitidos, 2, QStringLiteral("2026-08-04T10:00:00"), QStringLiteral("PERFIL A"), QString()),
        cfdi(D::Recibidos, 3, QStringLiteral("2026-08-05T10:00:00"), QStringLiteral("PROVEEDOR UNO"), QStringLiteral("PERFIL A"),
             TipoComprobante::Egreso)};
    QVERIFY(e.activar());
    QTRY_COMPARE(e.vm.vista(), QStringLiteral("lista"));
    QCOMPARE(e.vm.textoPestanaEmitidas(), QStringLiteral("Emitidas (2)"));
    QCOMPARE(e.vm.textoPestanaRecibidas(), QStringLiteral("Recibidas (1)"));
    QCOMPARE(e.vm.encabezadoContraparte(), QStringLiteral("Receptor"));
    QCOMPARE(e.vm.modelo()->count(), 2);
    // Emitidas: el receptor; sin nombre, su RFC.
    QCOMPARE(e.vm.modelo()->filas().at(0).contraparte, QStringLiteral("CLIENTE UNO"));
    QCOMPARE(e.vm.modelo()->filas().at(1).contraparte, kRfcB);
    QVERIFY(e.vm.modelo()->filas().at(0).nombreAccesible.startsWith(QStringLiteral("Ingreso, ")));
    QVERIFY(e.vm.modelo()->filas().at(0).nombreAccesible.contains(QStringLiteral("CLIENTE UNO")));
    QVERIFY(!e.vm.modelo()->filas().at(0).nombreAccesible.contains(QStringLiteral("PERFIL A")));
    QVERIFY(e.vm.placeholderBusqueda().contains(QStringLiteral("receptor")));
    // Recibidas: el emisor.
    e.vm.setDireccion(FacturasViewModel::Direccion::Recibidas);
    QTRY_COMPARE(e.vm.modelo()->count(), 1);
    QCOMPARE(e.vm.encabezadoContraparte(), QStringLiteral("Emisor"));
    QCOMPARE(e.vm.modelo()->filas().at(0).contraparte, QStringLiteral("PROVEEDOR UNO"));
    QCOMPARE(e.vm.modelo()->uuidEn(0), QStringLiteral("AAAAAAAA-0000-4000-8000-000000000003"));
    QCOMPARE(e.fuente.consultas.last().direccion, std::optional(D::Recibidos));
    // Busqueda y filtro sobre la pestana activa; Limpiar filtros.
    e.vm.setTexto(QStringLiteral("nadie"));
    QTRY_COMPARE(e.vm.vista(), QStringLiteral("sinCoincidencias"));
    QVERIFY(e.vm.hayFiltros());
    e.vm.limpiarFiltros();
    QTRY_COMPARE(e.vm.modelo()->count(), 1);
    e.vm.setTipo(QStringLiteral("I"));
    QTRY_COMPARE(e.vm.modelo()->count(), 0);
    QCOMPARE(e.fuente.consultas.last().tipo, std::optional(TipoComprobante::Ingreso));
}

void TestFacturasViewModel::totalConFormatoYDetalle()
{
    // Formato del prototipo: miles con coma, decimales tal cual el XML.
    QCOMPARE(formatearTotalFactura(QStringLiteral("1200.10")), QStringLiteral("$1,200.10"));
    QCOMPARE(formatearTotalFactura(QStringLiteral("1234567.125")), QStringLiteral("$1,234,567.125"));
    QCOMPARE(formatearTotalFactura(QStringLiteral("999")), QStringLiteral("$999"));
    QCOMPARE(formatearTotalFactura(QStringLiteral("0.5")), QStringLiteral("$0.5"));
    QCOMPARE(formatearTotalFactura(QStringLiteral("-1000.00")), QStringLiteral("-$1,000.00"));
    QCOMPARE(formatearTotalFactura(QString()), QString());
    QCOMPARE(formatearTotalFactura(QStringLiteral("1e3")), QStringLiteral("1e3")); // no decimal simple: tal cual

    Entorno e;
    e.fuente.cfdisPorMes[QStringLiteral("2026-08")] = {
        cfdi(D::Emitidos, 1, QStringLiteral("2026-08-03T10:00:00"), QStringLiteral("A"), QStringLiteral("CLIENTE"))};
    QVERIFY(e.activar());
    QTRY_COMPARE(e.vm.modelo()->count(), 1);
    FacturasListModel* m = e.vm.modelo();
    const QModelIndex i = m->index(0);
    QCOMPARE(m->data(i, FacturasListModel::TotalRole).toString(), QStringLiteral("$1,160.00"));
    QCOMPARE(m->data(i, FacturasListModel::TotalXmlRole).toString(), QStringLiteral("1160.00"));
    QCOMPARE(m->data(i, FacturasListModel::DetalleTotalRole).toString(), QStringLiteral("MXN · PUE"));
    QCOMPARE(m->data(i, FacturasListModel::MetodoPagoRole).toString(), QStringLiteral("PUE"));
    QVERIFY(m->filas().first().nombreAccesible.endsWith(QStringLiteral("$1,160.00 MXN")));
    QVERIFY(m->roleNames().values().contains("detalleTotal"));
}

void TestFacturasViewModel::mesesConEstadoConteoYNombreAccesible()
{
    Entorno e;
    const MesCalendario agosto = *MesCalendario::crear(2026, 8);
    e.fuente.evidenciaPorMes[agosto.texto()] = {completa(D::Emitidos, agosto), completa(D::Recibidos, agosto)};
    e.fuente.porMes[7] = 214;
    QVERIFY(e.activar());
    MesesFacturasModel* m = e.vm.meses();
    QCOMPARE(m->count(), 12);
    QTRY_COMPARE(m->filas().at(7).estado, QStringLiteral("Disponibles"));
    QTRY_COMPARE(m->filas().at(7).textoConteo, QStringLiteral("214 facturas"));
    QCOMPARE(m->filas().at(7).nombreAccesible, QStringLiteral("agosto 2026, Disponibles, 214 facturas"));
    QVERIFY(m->filas().at(7).seleccionado);
    QCOMPARE(m->filas().at(7).icono, QStringLiteral("check-circle"));
    // Septiembre en curso; octubre en adelante, futuros.
    QVERIFY(!m->filas().at(8).futuro);
    QVERIFY(m->filas().at(9).futuro);
    QCOMPARE(m->filas().at(9).estadoTexto, QStringLiteral("Aún no empieza"));
    QVERIFY(m->filas().at(9).textoConteo.isEmpty());
    QTRY_COMPARE(m->filas().at(0).estado, QStringLiteral("SinDescargar"));
    QCOMPARE(m->filaDe(8), 7);
}

void TestFacturasViewModel::navegacionDeMesesSinFuturos()
{
    Entorno e;
    QVERIFY(e.activar());
    QVERIFY(e.vm.puedeMesSiguiente()); // agosto -> septiembre (en curso)
    QVERIFY(e.vm.moverMes(1));
    QCOMPARE(e.vm.mes(), 9);
    QVERIFY(!e.vm.puedeMesSiguiente());
    QVERIFY(!e.vm.moverMes(1));
    QVERIFY(!e.vm.seleccionarMes(2026, 10)); // futuro
    QVERIFY(e.vm.moverMes(-1));
    QCOMPARE(e.vm.mes(), 8);
    QVERIFY(e.vm.cambiarAnio(-1));
    QCOMPARE(e.vm.anio(), 2025);
    QCOMPARE(e.vm.mes(), 8);
    QVERIFY(e.vm.puedeAnioSiguiente());
    QVERIFY(e.vm.cambiarAnio(1));
    QCOMPARE(e.vm.anio(), 2026);
    // Cambiar de mes limpia la busqueda.
    e.vm.setTexto(QStringLiteral("x"));
    QVERIFY(e.vm.seleccionarMes(2026, 7));
    QVERIFY(!e.vm.hayFiltros());
}

void TestFacturasViewModel::franjaPorEstado()
{
    const MesCalendario agosto = *MesCalendario::crear(2026, 8);
    struct Caso {
        const char* nombre;
        QList<EvidenciaSolicitudMensual> evidencia;
        Estado estado;
        Accion accion;
        bool habilitada;
    };
    const QList<Caso> casos = {
        {"sin descargar", {}, Estado::SinDescargar, Accion::DescargarFacturas, true},
        {"preparando", {enCurso(D::Emitidos, agosto), enCurso(D::Recibidos, agosto)}, Estado::Preparando,
         Accion::DescargarFacturas, false},
        {"disponibles", {completa(D::Emitidos, agosto), completa(D::Recibidos, agosto)}, Estado::Disponibles,
         Accion::Ninguna, false},
        {"parcial cerrado", {completa(D::Emitidos, agosto)}, Estado::DisponiblesParcialmente, Accion::CompletarMes, true},
        {"atencion reintentable", {fallida(D::Emitidos, agosto), completa(D::Recibidos, agosto)},
         Estado::RequiereAtencion, Accion::Reintentar, true},
        {"atencion 5005", {fallida(D::Emitidos, agosto, "5005"), completa(D::Recibidos, agosto)},
         Estado::RequiereAtencion, Accion::VerDetalles, true},
    };
    for (const Caso& c : casos) {
        Entorno e;
        e.fuente.evidenciaPorMes[agosto.texto()] = c.evidencia;
        QVERIFY(e.activar());
        const QByteArray n(c.nombre);
        QVERIFY2(e.esperarFranja(c.estado), n.constData());
        const FranjaMesViewModel* f = e.vm.franja();
        QVERIFY2(f->accion() == c.accion, n.constData());
        QVERIFY2(f->accionHabilitada() == c.habilitada, n.constData());
        QVERIFY2(!f->titular().isEmpty() && !f->estadoTexto().isEmpty() && !f->icono().isEmpty(), n.constData());
        if (c.estado == Estado::Preparando) {
            QVERIFY(f->ocupado());
            QVERIFY(!f->etapa().isEmpty());
            QCOMPARE(f->nota(), QStringLiteral("Puedes cerrar la ventana; te avisaremos."));
            QVERIFY(!f->motivoDeshabilitada().isEmpty());
            QCOMPARE(e.vm.vista(), QStringLiteral("llenandose"));
        }
        if (c.estado == Estado::RequiereAtencion) {
            QVERIFY2(!f->causa().isEmpty(), n.constData());
        }
        if (c.accion == Accion::Reintentar) {
            QCOMPARE(f->textoEnlace(), QStringLiteral("Ver detalles técnicos"));
            QCOMPARE(f->causa(), QStringLiteral("El SAT rechazó la petición de las emitidas."));
        }
        if (c.accion == Accion::VerDetalles) {
            QSignalSpy detalles(&e.vm, &FacturasViewModel::detallesTecnicosSolicitados);
            e.vm.ejecutarAccion();
            QCOMPARE(detalles.count(), 1);
            QCOMPARE(detalles.at(0).at(2).toInt(), 8);
        }
        if (c.estado == Estado::SinDescargar) {
            QCOMPARE(f->titular(), QStringLiteral("Las facturas de agosto 2026 no están en este equipo"));
            QCOMPARE(e.vm.vista(), QStringLiteral("sinFacturas"));
        }
    }
    // Mes futuro (no seleccionable) y mes en curso sin evidencia.
    Entorno f;
    QVERIFY(f.activar());
    QVERIFY(f.vm.seleccionarMes(2026, 9));
    QVERIFY(f.esperarFranja(Estado::SinDescargar));
}

void TestFacturasViewModel::franjaMesEnCursoHastaYActualizar()
{
    // 15 sep 12:00 Centro; cobertura hasta el 10 a las 14:05.
    Entorno e;
    const MesCalendario sept = *MesCalendario::crear(2026, 9);
    e.fuente.evidenciaPorMes[sept.texto()] = {evidencia(D::Emitidos, sept.inicioSat(), QStringLiteral("2026-09-10T14:05:00")),
                                              evidencia(D::Recibidos, sept.inicioSat(), QStringLiteral("2026-09-10T14:05:00"))};
    QVERIFY(e.activar());
    QVERIFY(e.vm.seleccionarMes(2026, 9));
    QVERIFY(e.esperarFranja(Estado::DisponiblesParcialmente));
    const FranjaMesViewModel* f = e.vm.franja();
    QCOMPARE(f->accion(), Accion::ActualizarMes);
    QCOMPARE(f->hasta(), QStringLiteral("10 de septiembre, 14:05"));
    QCOMPARE(f->titular(), QStringLiteral("Facturas disponibles hasta el 10 de septiembre, 14:05"));
    e.vm.ejecutarAccion();
    QTRY_COMPARE(e.fuente.accionesPlanificadas.size(), 1);
    QCOMPARE(e.fuente.accionesPlanificadas.first(), AccionGuiada::ActualizarMes);
    QTRY_COMPARE(e.fuente.decisionesEjecutadas.size(), 1);

    // Cubierto hoy: Disponibles, sin accion el mismo dia.
    Entorno g;
    g.fuente.evidenciaPorMes[sept.texto()] = {evidencia(D::Emitidos, sept.inicioSat(), QStringLiteral("2026-09-15T08:00:00")),
                                              evidencia(D::Recibidos, sept.inicioSat(), QStringLiteral("2026-09-15T08:00:00"))};
    QVERIFY(g.activar());
    QVERIFY(g.vm.seleccionarMes(2026, 9));
    QVERIFY(g.esperarFranja(Estado::Disponibles));
    QCOMPARE(g.vm.franja()->accion(), Accion::Ninguna);
    QVERIFY(g.vm.franja()->titular().startsWith(QStringLiteral("Facturas disponibles hasta el 15 de septiembre")));
}

void TestFacturasViewModel::reenvioDeDescargasSinEnviar()
{
    // La app se cerro tras guardar: ambas en Creada. Preparando, pero con la
    // accion del mes habilitada (reenvia sin crear nuevas).
    Entorno e;
    const MesCalendario agosto = *MesCalendario::crear(2026, 8);
    e.fuente.evidenciaPorMes[agosto.texto()] = {enCurso(D::Emitidos, agosto, EstadoLocal::Creada),
                                                enCurso(D::Recibidos, agosto, EstadoLocal::Creada)};
    QVERIFY(e.activar());
    QVERIFY(e.esperarFranja(Estado::Preparando));
    QCOMPARE(e.vm.franja()->accion(), Accion::DescargarFacturas);
    QVERIFY(e.vm.franja()->accionHabilitada());
    e.vm.ejecutarAccion();
    QTRY_COMPARE(e.fuente.decisionesEjecutadas.size(), 1);
    QCOMPARE(e.fuente.accionesPlanificadas.first(), AccionGuiada::DescargarFacturas);
}

void TestFacturasViewModel::eFirmaNoListaRegistrar()
{
    Entorno e;
    e.fuente.listaPerfiles[0].credencial = EstadoCredencial::SinCredencial;
    e.fuente.cfdisPorMes[QStringLiteral("2026-08")] = {
        cfdi(D::Emitidos, 1, QStringLiteral("2026-08-03T10:00:00"), QStringLiteral("PERFIL A"), QStringLiteral("CLIENTE"))};
    QVERIFY(e.activar());
    QVERIFY(e.esperarFranja(Estado::SinDescargar));
    QVERIFY(!e.vm.eFirmaLista());
    QVERIFY(!e.vm.motivoEFirma().isEmpty());
    QTRY_COMPARE(e.vm.modelo()->count(), 1); // facturas visibles
    QCOMPARE(e.vm.franja()->accion(), Accion::RegistrarEFirma);
    QCOMPARE(e.vm.franja()->textoAccion(), QStringLiteral("Registrar e.firma…"));
    QCOMPARE(e.vm.franja()->motivoDeshabilitada(), QStringLiteral("Registra la e.firma del perfil para descargar."));
    QSignalSpy registrar(&e.vm, &FacturasViewModel::registrarEFirmaSolicitado);
    e.vm.ejecutarAccion();
    QCOMPARE(registrar.count(), 1);
    QCOMPARE(registrar.at(0).at(0).toString(), kPerfilA.texto());
    QVERIFY(e.fuente.accionesPlanificadas.isEmpty());

    // EFirmaNoLista al planificar (cambio de credencial): aviso y sin crear.
    Entorno g;
    QVERIFY(g.activar());
    QVERIFY(g.esperarFranja(Estado::SinDescargar));
    ErrorDescargaGuiada err;
    err.tipo = ErrorDescargaGuiada::Tipo::EFirmaNoLista;
    g.fuente.planes.append(FuenteFacturasMes::ResultadoPreplan::fallo(err));
    g.vm.ejecutarAccion();
    QTRY_COMPARE(g.vm.franja()->aviso(), QStringLiteral("Registra la e.firma del perfil para descargar."));
    QVERIFY(g.fuente.decisionesEjecutadas.isEmpty());
}

void TestFacturasViewModel::accionConConfirmacionPorDireccion()
{
    Entorno e;
    QVERIFY(e.activar());
    QVERIFY(e.esperarFranja(Estado::SinDescargar));
    PreplanGuiado p = preplanConConfirmacion(D::Emitidos);
    CandidatoGuiado r;
    r.direccion = D::Recibidos;
    r.duplicado.clasificacion = ClasificacionDuplicado::RequiereConfirmacion;
    p.candidatos.append(r);
    e.fuente.planes.append(FuenteFacturasMes::ResultadoPreplan::exito(p));
    EjecucionGuiada ej;
    ej.canceladas = {D::Emitidos};
    e.fuente.ejecuciones.append(FuenteFacturasMes::ResultadoEjecucion::exito(ej));
    e.vm.ejecutarAccion();
    QTRY_VERIFY(e.vm.confirmacionPendiente());
    QCOMPARE(e.vm.confirmacionTitulo(), QStringLiteral("¿Descargar de nuevo las emitidas de agosto 2026?"));
    QCOMPARE(e.vm.textoCancelar(), QStringLiteral("Omitir emitidas"));
    QCOMPARE(e.vm.textoConfirmar(), QStringLiteral("Descargar de nuevo"));
    QVERIFY(e.vm.franja()->ejecutando());
    QVERIFY(!e.vm.franja()->accionHabilitada());
    e.vm.cancelarConfirmacion(); // emitidas
    QVERIFY(e.vm.confirmacionPendiente());
    QCOMPARE(e.vm.textoCancelar(), QStringLiteral("Omitir recibidas"));
    QVERIFY(e.fuente.decisionesEjecutadas.isEmpty()); // aun falta recibidas
    e.vm.confirmar(); // recibidas
    QTRY_COMPARE(e.fuente.decisionesEjecutadas.size(), 1);
    const DecisionesGuiadas d = e.fuente.decisionesEjecutadas.first();
    QCOMPARE(d.emitidos, std::optional(DecisionConfirmacion::Cancelar));
    QCOMPARE(d.recibidos, std::optional(DecisionConfirmacion::Confirmar));
    QVERIFY(!e.vm.confirmacionPendiente());
    QTRY_COMPARE(e.vm.franja()->aviso(), QStringLiteral("Se omitieron las emitidas."));
    QVERIFY(!e.vm.franja()->ejecutando());
}

void TestFacturasViewModel::planDesactualizadoReplaneaUnaVez()
{
    Entorno e;
    QVERIFY(e.activar());
    QVERIFY(e.esperarFranja(Estado::SinDescargar));
    ErrorDescargaGuiada des;
    des.tipo = ErrorDescargaGuiada::Tipo::PlanDesactualizado;
    PreplanGuiado actual;
    actual.evaluacion = evaluarCoberturaMensual(kPerfilA, *MesCalendario::crear(2026, 8), e.fuente.ahora, {});
    des.preplanActual = actual;
    e.fuente.ejecuciones = {FuenteFacturasMes::ResultadoEjecucion::fallo(des),
                            FuenteFacturasMes::ResultadoEjecucion::exito({})};
    e.vm.ejecutarAccion();
    QTRY_COMPARE(e.fuente.decisionesEjecutadas.size(), 2);
    QCOMPARE(e.fuente.accionesPlanificadas.size(), 2);
    QTRY_VERIFY(!e.vm.franja()->ejecutando());
    QVERIFY(e.vm.franja()->aviso().isEmpty());

    // Dos veces seguidas: segunda vez muestra el aviso, sin tercer intento.
    Entorno g;
    QVERIFY(g.activar());
    QVERIFY(g.esperarFranja(Estado::SinDescargar));
    g.fuente.ejecuciones = {FuenteFacturasMes::ResultadoEjecucion::fallo(des),
                            FuenteFacturasMes::ResultadoEjecucion::fallo(des)};
    g.vm.ejecutarAccion();
    QTRY_VERIFY(!g.vm.franja()->aviso().isEmpty());
    QCOMPARE(g.fuente.decisionesEjecutadas.size(), 2);
}

void TestFacturasViewModel::cambioDePerfilConservaMesYGuardaPreferencia()
{
    Entorno e;
    QVERIFY(e.activar());
    QVERIFY(e.vm.seleccionarMes(2026, 5));
    e.vm.seleccionarPerfil(1);
    QCOMPARE(e.vm.perfilId(), kPerfilB.texto());
    QCOMPARE(e.preferencias.valor, std::optional(kPerfilB.texto()));
    QCOMPARE(e.vm.mes(), 8); // primer mes visto de B: el mes por omision
    QVERIFY(e.vm.seleccionarPerfilId(kPerfilA.texto()));
    QCOMPARE(e.vm.mes(), 5); // D16: ultimo mes visto de A en la sesion
    QVERIFY(!e.vm.seleccionarPerfilId(QStringLiteral("no-existe")));
}

void TestFacturasViewModel::sinBloquearYErrorDeLectura()
{
    Entorno e;
    e.fuente.retenerDisponibilidad = true;
    e.vm.activar();
    QTRY_VERIFY(e.vm.mes() != 0);
    // La disponibilidad sigue pendiente: la franja carga sin bloquear.
    QCOMPARE(e.vm.franja()->estado(), Estado::Cargando);
    QTRY_COMPARE(e.vm.vista(), QStringLiteral("sinFacturas"));
    // La primera es la del mes elegido (las demas, del resto del anio).
    QVERIFY(!e.fuente.disponibilidadesPendientes.isEmpty());
    e.fuente.disponibilidadesPendientes.first()->addResult(FuenteFacturasMes::ResultadoDisponibilidad::fallo(
        ErrorPersistencia::de(ErrorPersistencia::Tipo::Almacenamiento, QStringLiteral("disco"))));
    e.fuente.disponibilidadesPendientes.first()->finish();
    QTRY_COMPARE(e.vm.franja()->estado(), Estado::Error);
    QCOMPARE(e.vm.franja()->accion(), Accion::ReintentarLectura);

    Entorno g;
    g.fuente.fallarCatalogo = true;
    QVERIFY(g.activar());
    QTRY_COMPARE(g.vm.vista(), QStringLiteral("error"));
    QVERIFY(!g.vm.textoVista().isEmpty());
    g.fuente.fallarCatalogo = false;
    g.vm.recargar();
    QTRY_COMPARE(g.vm.vista(), QStringLiteral("sinFacturas"));
}

void TestFacturasViewModel::anuncioUnaVezPorCambioDeEstado()
{
    Entorno e;
    const MesCalendario agosto = *MesCalendario::crear(2026, 8);
    QVERIFY(e.activar());
    QVERIFY(e.esperarFranja(Estado::SinDescargar));
    QSignalSpy anuncios(&e.vm, &FacturasViewModel::anuncioChanged);
    // Cambio de estado del mismo mes (llega por datosCambiados): un anuncio.
    e.fuente.evidenciaPorMes[agosto.texto()] = {enCurso(D::Emitidos, agosto), enCurso(D::Recibidos, agosto)};
    emit e.fuente.datosCambiados(PerfilId());
    QTRY_COMPARE(e.vm.franja()->estado(), Estado::Preparando);
    QCOMPARE(anuncios.count(), 1);
    QVERIFY(e.vm.anuncio().startsWith(QStringLiteral("Preparando. ")));
    // Otra recarga sin cambio de estado: sin anuncio.
    emit e.fuente.datosCambiados(kPerfilA);
    QTest::qWait(20);
    QCOMPARE(anuncios.count(), 1);
    // Cambiar de mes no se anuncia.
    QVERIFY(e.vm.seleccionarMes(2026, 7));
    QVERIFY(e.esperarFranja(Estado::SinDescargar));
    QCOMPARE(anuncios.count(), 1);
}

void TestFacturasViewModel::respuestasDesordenadasGanaLaUltima()
{
    const MesCalendario agosto = *MesCalendario::crear(2026, 8);
    const MesCalendario enero = *MesCalendario::crear(2026, 1);
    auto responder = [](const std::shared_ptr<QPromise<FuenteFacturasMes::ResultadoDisponibilidad>>& p,
                        const EvaluacionCoberturaMensual& e) {
        p->addResult(FuenteFacturasMes::ResultadoDisponibilidad::exito(e));
        p->finish();
    };
    Entorno e;
    QVERIFY(e.activar());
    QVERIFY(e.esperarFranja(Estado::SinDescargar));
    QTRY_VERIFY(e.vm.meses()->count() == 12 && e.vm.meses()->filas().at(0).estado == QStringLiteral("SinDescargar"));
    const auto preparando = evaluarCoberturaMensual(kPerfilA, agosto, e.fuente.ahora,
                                                    {enCurso(D::Emitidos, agosto), enCurso(D::Recibidos, agosto)});
    const auto sinDescargar = evaluarCoberturaMensual(kPerfilA, agosto, e.fuente.ahora, {});

    // Disponibilidad del mes elegido: dos recargas; la vieja llega al final.
    e.fuente.retenerDisponibilidad = true;
    e.fuente.retenerConteosAnio = true;
    emit e.fuente.datosCambiados(kPerfilA);
    emit e.fuente.datosCambiados(kPerfilA);
    QCOMPARE(e.fuente.disponibilidadesPendientes.size(), 2);
    responder(e.fuente.disponibilidadesPendientes.at(1), preparando);
    QTRY_COMPARE(e.vm.franja()->estado(), Estado::Preparando);
    responder(e.fuente.disponibilidadesPendientes.at(0), sinDescargar);
    QTest::qWait(20);
    QCOMPARE(e.vm.franja()->estado(), Estado::Preparando);
    QCOMPARE(e.vm.meses()->filas().at(7).estado, QStringLiteral("Preparando"));

    // Conteos anuales: el nuevo (300) gana al viejo (100) aunque llegue antes.
    QCOMPARE(e.fuente.conteosPendientes.size(), 2);
    CatalogoMensual nuevo;
    nuevo.conteos.porMes = std::array<int, 12>{};
    (*nuevo.conteos.porMes)[7] = 300;
    CatalogoMensual viejo = nuevo;
    (*viejo.conteos.porMes)[7] = 100;
    e.fuente.conteosPendientes.at(1)->addResult(FuenteFacturasMes::ResultadoCatalogo::exito(nuevo));
    e.fuente.conteosPendientes.at(1)->finish();
    QTRY_COMPARE(e.vm.meses()->filas().at(7).conteo, 300);
    e.fuente.conteosPendientes.at(0)->addResult(FuenteFacturasMes::ResultadoCatalogo::exito(viejo));
    e.fuente.conteosPendientes.at(0)->finish();
    QTest::qWait(20);
    QCOMPARE(e.vm.meses()->filas().at(7).conteo, 300);

    // Demas meses del anio: dos recargas (Reintentar); gana la ultima.
    e.fuente.disponibilidadesPendientes.clear();
    e.fuente.mesesPendientes.clear();
    e.fuente.retenerConteosAnio = false;
    e.vm.recargar();
    e.vm.recargar();
    QList<int> eneros;
    for (qsizetype i = 0; i < e.fuente.mesesPendientes.size(); ++i) {
        if (e.fuente.mesesPendientes.at(i) == 1) {
            eneros.append(int(i));
        }
    }
    QCOMPARE(eneros.size(), 2);
    const auto eneroPreparando = evaluarCoberturaMensual(
        kPerfilA, enero, e.fuente.ahora, {enCurso(D::Emitidos, enero), enCurso(D::Recibidos, enero)});
    const auto eneroSin = evaluarCoberturaMensual(kPerfilA, enero, e.fuente.ahora, {});
    responder(e.fuente.disponibilidadesPendientes.at(eneros.at(1)), eneroPreparando);
    QTRY_COMPARE(e.vm.meses()->filas().at(0).estado, QStringLiteral("Preparando"));
    responder(e.fuente.disponibilidadesPendientes.at(eneros.at(0)), eneroSin);
    QTest::qWait(20);
    QCOMPARE(e.vm.meses()->filas().at(0).estado, QStringLiteral("Preparando"));
}

void TestFacturasViewModel::vocabularioSinTerminosTecnicos()
{
    const QRegularExpression prohibido(QStringLiteral("solicitud|paquete|verificaci"),
                                       QRegularExpression::CaseInsensitiveOption);
    const MesCalendario agosto = *MesCalendario::crear(2026, 8);
    const QList<QList<EvidenciaSolicitudMensual>> escenarios = {
        {},
        {enCurso(D::Emitidos, agosto), enCurso(D::Recibidos, agosto)},
        {completa(D::Emitidos, agosto), completa(D::Recibidos, agosto)},
        {completa(D::Emitidos, agosto)},
        {fallida(D::Emitidos, agosto)},
        {fallida(D::Emitidos, agosto, "5002")},
        {fallida(D::Emitidos, agosto, "5005")},
    };
    for (const auto& ev : escenarios) {
        for (bool lista : {true, false}) {
            Entorno e;
            e.fuente.listaPerfiles[0].credencial = lista ? EstadoCredencial::Lista : EstadoCredencial::Vencida;
            e.fuente.evidenciaPorMes[agosto.texto()] = ev;
            e.fuente.cfdisPorMes[agosto.texto()] = {
                cfdi(D::Emitidos, 1, QStringLiteral("2026-08-03T10:00:00"), QStringLiteral("A"), QStringLiteral("B"))};
            QVERIFY(e.activar());
            QTRY_VERIFY(e.vm.modelo()->count() == 1);
            for (const QString& t : textosVisibles(e.vm)) {
                QVERIFY2(!prohibido.match(t).hasMatch(), qPrintable(t));
            }
        }
    }
    // Causas de atencion en lenguaje de usuario (todas).
    for (const char* codigo : {"301", "5002", "5005"}) {
        Entorno e;
        e.fuente.evidenciaPorMes[agosto.texto()] = {fallida(D::Recibidos, agosto, codigo)};
        QVERIFY(e.activar());
        QVERIFY(e.esperarFranja(Estado::RequiereAtencion));
        QVERIFY(!prohibido.match(e.vm.franja()->causa()).hasMatch());
    }
    // Confirmacion y vistas.
    Entorno c;
    QVERIFY(c.activar());
    QVERIFY(c.esperarFranja(Estado::SinDescargar));
    c.fuente.planes.append(FuenteFacturasMes::ResultadoPreplan::exito(preplanConConfirmacion(D::Recibidos)));
    c.vm.ejecutarAccion();
    QTRY_VERIFY(c.vm.confirmacionPendiente());
    for (const QString& t : textosVisibles(c.vm)) {
        QVERIFY2(!prohibido.match(t).hasMatch(), qPrintable(t));
    }
}

void TestFacturasViewModel::appViewModelFacturasPorOmision()
{
    fakes::FakePerfilesSatService perfiles;
    fakes::FakeCredencialesSatService credenciales;
    SolicitudesServiceAsincrono solicitudes;
    PresentacionViewModels vms(&solicitudes, &perfiles, &credenciales);
    AppViewModel* app = vms.app();
    QCOMPARE(app->pagina(), AppViewModel::Pagina::Diagnostico); // sin fuente: sin Facturas
    app->mostrarFacturas();
    QCOMPARE(app->pagina(), AppViewModel::Pagina::Diagnostico);
    FuenteFalsa fuente;
    PreferenciasMemoria preferencias;
    vms.setFuenteFacturas(&fuente, &preferencias);
    QCOMPARE(app->pagina(), AppViewModel::Pagina::Facturas);
    QVERIFY(vms.initialProperties().contains(QStringLiteral("facturasViewModel")));
    QTRY_VERIFY(vms.facturas()->mes() != 0); // activado al entrar
    // "Registrar e.firma..." abre Perfiles.
    emit vms.facturas()->registrarEFirmaSolicitado(kPerfilA.texto());
    QCOMPARE(app->pagina(), AppViewModel::Pagina::Perfiles);
    app->mostrarFacturas();
    QCOMPARE(app->pagina(), AppViewModel::Pagina::Facturas);
    emit vms.facturas()->detallesTecnicosSolicitados(kPerfilA.texto(), 2026, 8, QString());
    QCOMPARE(app->pagina(), AppViewModel::Pagina::Diagnostico);
}

void TestFacturasViewModel::preparandoConReservasSinLibres()
{
    Entorno e;
    e.fuente.ajustar = [](EvaluacionCoberturaMensual& ev) {
        for (CoberturaDireccion* d : {&ev.emitidos, &ev.recibidos}) {
            const SolicitudId id = SolicitudId::generar();
            for (HuecoCobertura& h : d->huecos) {
                h.reservadoPor = {id};
                h.libres.clear();
            }
        }
        ev.estado = EstadoMes::Preparando;
    };
    QVERIFY(e.activar());
    QVERIFY(e.esperarFranja(Estado::Preparando));
    // Reservas en curso sin segmentos libres ni descargas sin enviar: la accion
    // se muestra deshabilitada con su motivo.
    QCOMPARE(e.vm.franja()->accion(), Accion::DescargarFacturas);
    QVERIFY(!e.vm.franja()->accionHabilitada());
    QVERIFY(!e.vm.franja()->motivoDeshabilitada().isEmpty());
}

QTEST_MAIN(TestFacturasViewModel)
#include "TestFacturasViewModel.moc"
