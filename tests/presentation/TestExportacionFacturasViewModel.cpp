// T026.3 (corte 1): ExportacionFacturasViewModel sobre el
// ExportacionFacturasService REAL (hilo propio) con ZIP sinteticos reales
// (libzip, TestExportacionServicioComun.h), FakeDestinoExportacion en memoria,
// FakeCfdiPdfRenderer y un puerto de plan controlado por la prueba (respuestas
// listas o pendientes). FacturasViewModel real sobre FuenteFalsa. Datos
// sinteticos: XAXX010101000, XEXX010101000, /Users/usuario y UUID inventados.

#include "presentation/viewmodels/CatalogoMensajes.h"
#include "presentation/viewmodels/ExportacionFacturasViewModel.h"
#include "presentation/viewmodels/FacturasListModel.h"
#include "presentation/viewmodels/FacturasViewModel.h"
#include "presentation/viewmodels/HorizonteFacturas.h"
#include "presentation/viewmodels/PresentacionViewModels.h"

#include "fakes/FakeCfdiPdfRenderer.h"
#include "fakes/FakeCredencialesSatService.h"
#include "fakes/FakeDestinoExportacion.h"
#include "fakes/FakePerfilesSatService.h"
#include "fakes/LectorCfdiCompuerta.h"
#include "fakes/PromesasPendientes.h"
#include "integration/TestExportacionServicioComun.h"
#include "FuenteFacturasFake.h"
#include "ServiciosAsincronosFake.h"

#include <QLoggingCategory>
#include <QMutex>
#include <QSignalSpy>
#include <QTest>

#include <atomic>
#include <cerrno>
#include <memory>
#include <optional>

using namespace satcfdi;

namespace {

using pruebasfacturas::FuenteFalsa;
using pruebasfacturas::PreferenciasMemoria;
using Vm = ExportacionFacturasViewModel;
using ResultadoPlanPersistencia = Resultado<PlanExportacion, ErrorPersistencia>;

const QString kCarpeta = QStringLiteral("/Users/usuario/Documentos");
const MesCalendario kAgosto2026 = *MesCalendario::crear(2026, 8);

// ---- Captura de logs ----------------------------------------------------------

QMutex g_mutexLogs;
QStringList g_logs;
QtMessageHandler g_anterior = nullptr;

void capturarLog(QtMsgType tipo, const QMessageLogContext& contexto, const QString& mensaje)
{
    {
        QMutexLocker l(&g_mutexLogs);
        g_logs.append(QString::fromLatin1(contexto.category ? contexto.category : "") + QLatin1Char(' ') + mensaje);
    }
    if (g_anterior) {
        g_anterior(tipo, contexto, mensaje);
    }
}

struct CapturaLogs {
    CapturaLogs()
    {
        QMutexLocker l(&g_mutexLogs);
        g_logs.clear();
        g_anterior = qInstallMessageHandler(capturarLog);
        QLoggingCategory::setFilterRules(QStringLiteral("satcfdi.*=true"));
    }
    ~CapturaLogs()
    {
        QLoggingCategory::setFilterRules(QString());
        qInstallMessageHandler(g_anterior);
    }
    QStringList lineas() const
    {
        QMutexLocker l(&g_mutexLogs);
        return g_logs;
    }
};

// Escritor ZIP que falla al agregar la primera entrada (error Zip no
// relacionado con el destino).
class EscritorZipQueFalla final : public exportacion::EscritorZipExportacion {
public:
    using Error = exportacion::ErrorZipExportacion;
    Resultado<Exito, Error> abrir(exportacion::ArchivoParcialExportacion&, QDateTime, std::function<bool()>) override
    {
        return Resultado<Exito, Error>::exito(Exito{});
    }
    Resultado<Exito, Error> agregar(const QString&, QByteArrayView) override
    {
        return Resultado<Exito, Error>::fallo(Error::de(Error::Codigo::Compresion, 4242));
    }
    Resultado<exportacion::ResumenZipExportacion, Error> cerrar() override
    {
        return Resultado<exportacion::ResumenZipExportacion, Error>::fallo(Error::de(Error::Codigo::Estado));
    }
    void abortar() noexcept override {}
};

// Escenarios de sondas (seguridad 1b).
enum class EscenarioSondas {
    ColisionesYPublicada,
    SinEspacio,
    ReservaConErrno,
    ErrorZip,
    CanceladaExportando,
    CanceladaEsperandoNombre,
    ErrorPlan,
};

// ---- Entorno --------------------------------------------------------------------

struct Entorno {
    exportacionpruebas::Banco banco;
    exportacionpruebas::Banco::Paquete paquete;
    std::shared_ptr<fakes::FakeDestinoExportacion> destino = std::make_shared<fakes::FakeDestinoExportacion>();
    std::shared_ptr<fakes::FakeCfdiPdfRenderer::Estado> pdf = std::make_shared<fakes::FakeCfdiPdfRenderer::Estado>();
    FuenteFalsa fuente;
    PreferenciasMemoria preferencias;
    FacturasViewModel facturas;

    // Plan: listo (planDe) o pendiente (retener) por cada llamada.
    bool retener = false;
    std::function<PlanExportacion(const AlcanceExportacion&)> planDe;
    QList<AlcanceExportacion> alcances;
    fakes::PromesasPendientes<ResultadoPlanPersistencia> pendientes;

    // Opcionales (antes de preparar): lector con compuerta, escritor ZIP
    // propio y fallo del plan.
    bool conCompuerta = false;
    std::shared_ptr<fakes::LectorCfdiCompuerta> compuerta;
    exportacion::FabricaEscritorZip fabricaEscritor;
    std::optional<ErrorPersistencia> errorPlan;

    std::unique_ptr<ExportacionFacturasService> servicio;
    Vm vm;

    Entorno()
    {
        facturas.setIntervaloBusqueda(0);
        facturas.setFuente(&fuente);
        facturas.setPreferencias(&preferencias);
    }

    ~Entorno()
    {
        vm.setServicio(nullptr);
        if (compuerta) {
            compuerta->liberar(); // el hilo no queda bloqueado al cerrar
        }
        servicio.reset(); // une su hilo antes que el banco y el destino
    }

    bool preparar()
    {
        if (!banco.preparar()) {
            return false;
        }
        paquete = banco.paquete({exportacionpruebas::xml40(1), exportacionpruebas::xml40(2)});
        if (paquete.id.isEmpty()) {
            return false;
        }
        planDe = [this](const AlcanceExportacion&) { return plan(2); };
        std::shared_ptr<CfdiArchiveReader> lector = exportacionpruebas::prestado(banco.lector);
        if (conCompuerta) {
            compuerta = std::make_shared<fakes::LectorCfdiCompuerta>(lector);
            lector = compuerta;
        }
        PuertosExportacionFacturas p = exportacionpruebas::puertos(
            destino, std::move(lector), exportacionpruebas::prestado(*banco.archivos),
            fakes::FakeCfdiPdfRenderer::fabrica(pdf));
        if (fabricaEscritor) {
            p.fabricaEscritor = fabricaEscritor;
        }
        p.planear = [this](const AlcanceExportacion& a) {
            alcances.append(a);
            if (errorPlan) {
                return QtFuture::makeReadyValueFuture(ResultadoPlanPersistencia::fallo(*errorPlan));
            }
            if (retener) {
                return pendientes.nueva();
            }
            return QtFuture::makeReadyValueFuture(ResultadoPlanPersistencia::exito(planDe(a)));
        };
        servicio = std::make_unique<ExportacionFacturasService>(std::move(p));
        vm.setServicio(servicio.get(), [this] { return fuente.ahora; });
        vm.setFacturas(&facturas);
        facturas.activar();
        return QTest::qWaitFor([&] { return facturas.mes() != 0 && facturas.modelo()->count() >= 0
                                            && facturas.vista() != u"cargando"; },
                               3000);
    }

    // Plan con las primeras `n` facturas del paquete sintetico (0..2).
    PlanExportacion plan(int n) const
    {
        PlanExportacion p;
        for (int i = 0; i < n; ++i) {
            p.facturas.append(banco.factura(paquete, i, i + 1));
        }
        return p;
    }

    bool esperarEstado(Vm::Estado e, int ms = 10000)
    {
        return QTest::qWaitFor([&] { return vm.estado() == e; }, ms);
    }

    // Abre el mes actual, elige la carpeta sintetica y espera el dialogo listo.
    bool abrirListo()
    {
        vm.abrirMesActual();
        if (!esperarEstado(Vm::Estado::Dialogo)) {
            return false;
        }
        vm.elegirDestino(QUrl::fromLocalFile(kCarpeta));
        return vm.puedeIniciar();
    }

    // Resolver la llamada `i` de planear con un plan de `n` facturas.
    void resolver(int i, int n) { pendientes.resolver(i, ResultadoPlanPersistencia::exito(plan(n))); }
};

void agregarFilas(FuenteFalsa& fuente)
{
    using pruebasfacturas::cfdi;
    fuente.cfdisPorMes[QStringLiteral("2026-08")] = {
        cfdi(TipoDescarga::Emitidos, 1, QStringLiteral("2026-08-03T10:00:00"), QStringLiteral("Emisor A"),
             QStringLiteral("Receptor Uno")),
        cfdi(TipoDescarga::Emitidos, 2, QStringLiteral("2026-08-04T10:00:00"), QStringLiteral("Emisor A"),
             QStringLiteral("Receptor Dos")),
        cfdi(TipoDescarga::Recibidos, 3, QStringLiteral("2026-08-05T10:00:00"), QStringLiteral("Proveedor Tres"),
             QStringLiteral("Receptor A")),
    };
}

QStringList textosVisibles(const Vm& vm)
{
    QStringList t{vm.errorRango(),    vm.textoAlcance(),         vm.aviso(),           vm.textoConteo(),
                  vm.errorPlan(),     vm.nombreCarpetaDestino(), vm.errorDestino(),    vm.textoFase(),
                  vm.textoProgreso(), vm.varianteResultado(),    vm.tituloResultado(), vm.descripcionResultado(),
                  vm.advertenciaResultado()};
    t << vm.avisosPlan() << vm.detallesResultado();
    return t;
}

} // namespace

class TestExportacionFacturasViewModel : public QObject {
    Q_OBJECT

private slots:
    void horizonteCompartido();
    void sinServicioNoDisponible();
    void exportarMesConPlanYResultado();
    void conteoCeroDeshabilitaExportar();
    void direccionYModoReplanean();
    void planObsoletoSeDescarta();
    void cambioDePerfilCierraPlanPendiente();
    void seleccionPorIdentidad();
    void rangoValidaHorizonte();
    void colisionAlReservarPideOtroNombre();
    void colisionAlPublicarUsaPublicarComo();
    void cancelarEsperandoNombreDescartaParcial();
    void resultadoConYSinExportadas();
    void sondasAusentesDeLogsYTextos_data();
    void sondasAusentesDeLogsYTextos();
    void publicarSinEsperaUsaTextoPropio();
    void vocabularioSinTerminosTecnicos();
    void cableadoEnPresentacion();
};

void TestExportacionFacturasViewModel::horizonteCompartido()
{
    using namespace horizonte_facturas;
    const MesCalendario curso = *MesCalendario::crear(2026, 9);
    const auto m = [](int a, int mes) { return *MesCalendario::crear(a, mes); };
    QVERIFY(navegable(m(2026, 9), curso));
    QVERIFY(navegable(m(2020, 1), curso));
    QVERIFY(!navegable(m(2019, 12), curso));
    QVERIFY(!navegable(m(2026, 10), curso));
    QCOMPARE(validarRango(m(2026, 9), m(2026, 9), curso), ErrorRango::Ninguno); // mismo mes
    QCOMPARE(validarRango(m(2026, 1), m(2026, 9), curso), ErrorRango::Ninguno);
    QCOMPARE(validarRango(m(2026, 9), m(2026, 10), curso), ErrorRango::Futuro);
    QCOMPARE(validarRango(m(2026, 9), m(2026, 8), curso), ErrorRango::Invertido);
    QCOMPARE(validarRango(m(2019, 12), m(2026, 1), curso), ErrorRango::FueraDelHorizonte);
}

void TestExportacionFacturasViewModel::sinServicioNoDisponible()
{
    FuenteFalsa fuente;
    FacturasViewModel facturas;
    facturas.setFuente(&fuente);
    Vm vm;
    vm.setFacturas(&facturas);
    facturas.activar();
    QTRY_VERIFY(facturas.mes() != 0);
    QVERIFY(!vm.disponible());
    QVERIFY(!vm.puedeExportar());
    vm.abrirMesActual();
    QCOMPARE(vm.estado(), Vm::Estado::Inactivo);
}

void TestExportacionFacturasViewModel::exportarMesConPlanYResultado()
{
    Entorno e;
    QVERIFY(e.preparar());
    QVERIFY(e.vm.disponible());
    QVERIFY(e.vm.puedeExportar());
    QCOMPARE(e.facturas.mes(), 8);

    QSignalSpy estados(&e.vm, &Vm::estadoChanged);
    e.vm.abrirMesActual(); // plan listo: Planeando -> Dialogo (pendiente en planObsoletoSeDescarta)
    QVERIFY(e.vm.dialogoAbierto());
    QVERIFY(!e.vm.puedeExportar());
    QVERIFY(e.esperarEstado(Vm::Estado::Dialogo));
    QVERIFY(estados.size() >= 2);

    QCOMPARE(e.alcances.size(), 1);
    const auto* mes = std::get_if<AlcanceMesExportacion>(&e.alcances.first());
    QVERIFY(mes);
    QCOMPARE(mes->perfil, pruebasfacturas::kPerfilA);
    QCOMPARE(mes->mes, kAgosto2026);
    QCOMPARE(mes->direccion, std::optional(TipoDescarga::Emitidos)); // pestana actual (D8)
    QCOMPARE(e.vm.modo(), Vm::Modo::Mes);
    QCOMPARE(e.vm.direccion(), Vm::Direccion::Emitidas);
    QVERIFY(e.vm.mostrarDireccion());
    QCOMPARE(e.vm.textoAlcance(), QStringLiteral("Agosto 2026"));
    QCOMPARE(e.vm.aviso(), catalogo::avisoExportacion());
    QVERIFY(e.vm.aviso().startsWith(QStringLiteral("El archivo que vas a crear contiene tus facturas completas")));
    QVERIFY(e.vm.hayPlan());
    QCOMPARE(e.vm.conteoPrevisto(), 2);
    QCOMPARE(e.vm.textoConteo(), QStringLiteral("Se exportarán 2 facturas."));
    QCOMPARE(e.vm.nombreSugerido(), QStringLiteral("Archivo-Fiscal-2026-08.zip"));
    QCOMPARE(e.vm.nombreZip(), e.vm.nombreSugerido());
    QVERIFY(!e.vm.puedeIniciar()); // falta carpeta

    // Carpeta: solo su nombre es visible; la URL inicial conserva la ruta.
    e.vm.elegirDestino(QUrl(QStringLiteral("https://ejemplo.invalid/carpeta")));
    QVERIFY(!e.vm.hayCarpetaDestino());
    QVERIFY(!e.vm.errorDestino().isEmpty());
    e.vm.elegirDestino(QUrl::fromLocalFile(kCarpeta));
    QVERIFY(e.vm.errorDestino().isEmpty());
    QCOMPARE(e.vm.nombreCarpetaDestino(), QStringLiteral("Documentos"));
    QCOMPARE(e.vm.carpetaInicial(), QUrl::fromLocalFile(kCarpeta));
    QVERIFY(e.vm.puedeIniciar());

    e.vm.setConPdf(true);
    e.pdf->fallar = false;
    QSignalSpy progreso(&e.vm, &Vm::progresoChanged);
    e.vm.iniciar();
    QCOMPARE(e.vm.estado(), Vm::Estado::Exportando);
    QVERIFY(e.vm.ocupado());
    e.vm.cerrarDialogo(); // con trabajo en curso no cierra
    QCOMPARE(e.vm.estado(), Vm::Estado::Exportando);
    QVERIFY(e.esperarEstado(Vm::Estado::Resultado));
    QVERIFY(progreso.size() >= 2);
    QCOMPARE(e.alcances.size(), 1); // iniciar no vuelve a planear (D16)

    QVERIFY(e.vm.hayResultado());
    QCOMPARE(e.vm.varianteResultado(), QStringLiteral("exito"));
    QCOMPARE(e.vm.tituloResultado(), QStringLiteral("Exportación terminada"));
    QCOMPARE(e.vm.descripcionResultado(), QStringLiteral("Se exportaron 2 facturas con 2 PDF."));
    QCOMPARE(e.vm.exportadas(), 2);
    QCOMPARE(e.vm.sinPdf(), 0);
    QCOMPARE(e.vm.omitidas(), 0);
    QVERIFY(e.vm.detallesResultado().isEmpty());
    QVERIFY(e.vm.advertenciaResultado().isEmpty());
    QCOMPARE(e.vm.nombrePublicado(), QStringLiteral("Archivo-Fiscal-2026-08.zip"));
    const auto zip = e.destino->archivo(kCarpeta, QStringLiteral("Archivo-Fiscal-2026-08.zip"));
    QVERIFY(zip);
    const auto entradas = exportacionpruebas::leerZipBytes(*zip);
    QVERIFY(entradas);
    QCOMPARE(entradas->size(), 4); // 2 XML + 2 PDF

    e.vm.cerrarDialogo();
    QCOMPARE(e.vm.estado(), Vm::Estado::Inactivo);
    QVERIFY(!e.vm.hayResultado());
    QVERIFY(e.vm.nombrePublicado().isEmpty());
    QVERIFY(!e.vm.hayPlan());
    QVERIFY(e.vm.puedeExportar());
}

void TestExportacionFacturasViewModel::conteoCeroDeshabilitaExportar()
{
    Entorno e;
    QVERIFY(e.preparar());
    e.planDe = [&e](const AlcanceExportacion&) {
        PlanExportacion p = e.plan(0);
        p.omisiones = {OmisionMesExportacion{TipoDescarga::Emitidos, kAgosto2026, MotivoOmisionMes::DescargasPendientes},
                       OmisionFacturaExportacion{TipoDescarga::Emitidos, kAgosto2026, QStringLiteral("sin-uuid-1"),
                                                 MotivoOmisionFactura::NoDisponibleEnEsteEquipo}};
        return p;
    };
    QVERIFY(!e.abrirListo());
    QCOMPARE(e.vm.conteoPrevisto(), 0);
    QCOMPARE(e.vm.textoConteo(), QStringLiteral("No hay facturas para exportar"));
    QCOMPARE(e.vm.omitidasPrevistas(), 1);
    QCOMPARE(e.vm.lineasMesConAviso(), 1);
    QCOMPARE(e.vm.avisosPlan().size(), 2);
    QVERIFY(e.vm.hayCarpetaDestino());
    QVERIFY(!e.vm.puedeIniciar());
    e.vm.iniciar();
    QCOMPARE(e.vm.estado(), Vm::Estado::Dialogo);
    QCOMPARE(e.destino->reservas(), 0);
}

void TestExportacionFacturasViewModel::direccionYModoReplanean()
{
    Entorno e;
    QVERIFY(e.preparar());
    e.facturas.setDireccion(FacturasViewModel::Direccion::Recibidas);
    e.vm.abrirMesActual();
    QVERIFY(e.esperarEstado(Vm::Estado::Dialogo));
    QCOMPARE(e.vm.direccion(), Vm::Direccion::Recibidas); // pestana actual
    e.vm.setConPdf(true); // no replanea
    QCOMPARE(e.alcances.size(), 1);
    e.vm.setDireccion(Vm::Direccion::Ambas);
    QVERIFY(e.esperarEstado(Vm::Estado::Dialogo));
    QCOMPARE(e.alcances.size(), 2);
    QCOMPARE(std::get<AlcanceMesExportacion>(e.alcances.last()).direccion, std::nullopt);

    e.vm.cambiarModo(Vm::Modo::Seleccion);
    QVERIFY(e.esperarEstado(Vm::Estado::Dialogo));
    QVERIFY(!e.vm.mostrarDireccion()); // D8: sin selector con facturas elegidas
    QVERIFY(std::holds_alternative<AlcanceSeleccionExportacion>(e.alcances.last()));
    QCOMPARE(e.vm.textoAlcance(), QStringLiteral("0 facturas elegidas"));
    const QString sugerido = e.vm.nombreSugerido();
    QCOMPARE(sugerido, QStringLiteral("Archivo-Fiscal-seleccion-2026-09-15.zip"));
    e.vm.setDireccion(Vm::Direccion::Emitidas); // sin efecto en el plan de la seleccion
    QCOMPARE(e.alcances.size(), 3);

    // Un nombre editado por el usuario se conserva al cambiar el alcance.
    e.vm.setNombreZip(QStringLiteral("Mis facturas"));
    e.vm.cambiarModo(Vm::Modo::Rango);
    QVERIFY(e.esperarEstado(Vm::Estado::Dialogo));
    QCOMPARE(e.vm.nombreZip(), QStringLiteral("Mis facturas"));
    QCOMPARE(e.vm.nombreSugerido(), QStringLiteral("Archivo-Fiscal-2026-08.zip"));
}

void TestExportacionFacturasViewModel::planObsoletoSeDescarta()
{
    Entorno e;
    QVERIFY(e.preparar());
    e.retener = true;
    e.vm.abrirMesActual();
    QCOMPARE(e.vm.estado(), Vm::Estado::Planeando);
    e.vm.setDireccion(Vm::Direccion::Recibidas);
    QCOMPARE(e.pendientes.size(), 2);
    e.resolver(1, 1); // el vigente
    QVERIFY(e.esperarEstado(Vm::Estado::Dialogo));
    QCOMPARE(e.vm.conteoPrevisto(), 1);
    e.resolver(0, 2); // obsoleto: se descarta
    QTest::qWait(50);
    QCOMPARE(e.vm.conteoPrevisto(), 1);

    // Cerrar con un plan pendiente: la respuesta tardia no reabre nada.
    e.vm.setDireccion(Vm::Direccion::Ambas);
    QCOMPARE(e.vm.estado(), Vm::Estado::Planeando);
    QVERIFY(!e.vm.hayPlan());
    e.vm.cancelar();
    QCOMPARE(e.vm.estado(), Vm::Estado::Inactivo);
    e.resolver(2, 2);
    QTest::qWait(50);
    QCOMPARE(e.vm.estado(), Vm::Estado::Inactivo);
    QVERIFY(!e.vm.hayPlan());

    // Un error del plan se muestra y planear() reintenta.
    e.vm.abrirMesActual();
    e.pendientes.resolver(3, ResultadoPlanPersistencia::fallo(
                                 ErrorPersistencia::de(ErrorPersistencia::Tipo::Almacenamiento, QStringLiteral("x"))));
    QVERIFY(e.esperarEstado(Vm::Estado::Dialogo));
    QVERIFY(!e.vm.hayPlan());
    QVERIFY(!e.vm.errorPlan().isEmpty());
    e.vm.planear();
    QCOMPARE(e.vm.estado(), Vm::Estado::Planeando);
    QVERIFY(e.vm.errorPlan().isEmpty());
    e.resolver(4, 2);
    QVERIFY(e.esperarEstado(Vm::Estado::Dialogo));
    QCOMPARE(e.vm.conteoPrevisto(), 2);
}

void TestExportacionFacturasViewModel::cambioDePerfilCierraPlanPendiente()
{
    Entorno e;
    QVERIFY(e.preparar());
    e.retener = true;
    e.vm.abrirMesActual();
    QCOMPARE(e.vm.estado(), Vm::Estado::Planeando);
    e.facturas.seleccionarPerfil(1);
    QTRY_COMPARE(e.facturas.perfilId(), pruebasfacturas::kPerfilB.texto());
    QCOMPARE(e.vm.estado(), Vm::Estado::Inactivo);
    e.resolver(0, 2);
    QTest::qWait(50);
    QCOMPARE(e.vm.estado(), Vm::Estado::Inactivo);
    QVERIFY(!e.vm.hayPlan());
}

void TestExportacionFacturasViewModel::seleccionPorIdentidad()
{
    Entorno e;
    agregarFilas(e.fuente);
    QVERIFY(e.preparar());
    QTRY_COMPARE(e.facturas.modelo()->count(), 2); // emitidas de agosto

    QSignalSpy cambios(&e.vm, &Vm::seleccionChanged);
    e.vm.iniciarSeleccion();
    QVERIFY(e.vm.seleccionando());
    QVERIFY(!e.vm.alternarSeleccion(QStringLiteral("pa-99"), 99)); // no esta en la lista
    QVERIFY(e.vm.alternarSeleccion(QStringLiteral("pa-1"), 1));
    QVERIFY(e.vm.estaSeleccionada(QStringLiteral("pa-1"), 1));
    QVERIFY(!e.vm.estaSeleccionada(QStringLiteral("pa-2"), 2));
    const int revision = e.vm.revisionSeleccion();

    // Pestana: la fila elegida ya no se ve, pero sigue elegida.
    e.facturas.setDireccion(FacturasViewModel::Direccion::Recibidas);
    QTRY_COMPARE(e.facturas.modelo()->count(), 1);
    QVERIFY(e.vm.estaSeleccionada(QStringLiteral("pa-1"), 1));
    QVERIFY(e.vm.alternarSeleccion(QStringLiteral("pa-3"), 3));
    // Filtro y recarga.
    e.facturas.setTexto(QStringLiteral("nadie coincide"));
    QTRY_COMPARE(e.facturas.modelo()->count(), 0);
    e.facturas.recargar();
    QCOMPARE(e.vm.conteoSeleccionadas(), 2);
    e.facturas.limpiarFiltros();
    QTRY_COMPARE(e.facturas.modelo()->count(), 1);
    QVERIFY(e.vm.estaSeleccionada(QStringLiteral("pa-3"), 3));
    QVERIFY(e.vm.revisionSeleccion() > revision);

    // Alternar dos veces la misma fila la quita.
    e.facturas.setDireccion(FacturasViewModel::Direccion::Emitidas);
    QTRY_COMPARE(e.facturas.modelo()->count(), 2);
    QVERIFY(e.vm.alternarSeleccion(QStringLiteral("pa-2"), 2));
    QVERIFY(e.vm.alternarSeleccion(QStringLiteral("pa-2"), 2));
    QCOMPARE(e.vm.conteoSeleccionadas(), 2);

    // El alcance lleva exactamente lo elegido, con su direccion, mes y UUID.
    e.vm.abrirSeleccion();
    QVERIFY(e.esperarEstado(Vm::Estado::Dialogo));
    QCOMPARE(e.vm.textoAlcance(), QStringLiteral("2 facturas elegidas"));
    const auto* sel = std::get_if<AlcanceSeleccionExportacion>(&e.alcances.last());
    QVERIFY(sel);
    QCOMPARE(sel->perfil, pruebasfacturas::kPerfilA);
    QCOMPARE(sel->facturas.size(), 2);
    QCOMPARE(sel->facturas.at(0).referencia, (ReferenciaFactura{QStringLiteral("pa-1"), 1}));
    QCOMPARE(sel->facturas.at(0).direccion, TipoDescarga::Emitidos);
    QCOMPARE(sel->facturas.at(0).mes, kAgosto2026);
    QCOMPARE(sel->facturas.at(0).uuid, QStringLiteral("AAAAAAAA-0000-4000-8000-000000000001"));
    QCOMPARE(sel->facturas.at(1).referencia, (ReferenciaFactura{QStringLiteral("pa-3"), 3}));
    QCOMPARE(sel->facturas.at(1).direccion, TipoDescarga::Recibidos);
    // Con el dialogo abierto la seleccion queda fija.
    QVERIFY(!e.vm.alternarSeleccion(QStringLiteral("pa-2"), 2));
    e.vm.cerrarDialogo();

    // Cambiar de perfil vacia la seleccion (D14).
    e.facturas.seleccionarPerfil(1);
    QTRY_COMPARE(e.facturas.perfilId(), pruebasfacturas::kPerfilB.texto());
    QCOMPARE(e.vm.conteoSeleccionadas(), 0);
    QVERIFY(!e.vm.estaSeleccionada(QStringLiteral("pa-1"), 1));

    e.vm.terminarSeleccion();
    QVERIFY(!e.vm.seleccionando());
    QVERIFY(cambios.size() >= 6);
}

void TestExportacionFacturasViewModel::rangoValidaHorizonte()
{
    Entorno e; // hoy: 15 sep 2026 (Centro); horizonte 2020-01..2026-09
    QVERIFY(e.preparar());
    e.vm.abrirRango();
    QVERIFY(e.esperarEstado(Vm::Estado::Dialogo));
    QCOMPARE(e.vm.modo(), Vm::Modo::Rango);
    QCOMPARE(e.vm.desdeMes(), 8);
    QCOMPARE(e.vm.hastaMes(), 8);
    QVERIFY(e.vm.rangoValido());
    QCOMPARE(e.alcances.size(), 1);

    struct Caso {
        int da, dm, ha, hm;
        bool valido;
    };
    const QList<Caso> casos = {
        {2026, 9, 2026, 10, false}, // futuro
        {2026, 10, 2026, 10, false},
        {2026, 9, 2026, 8, false},  // invertido
        {2019, 12, 2026, 1, false}, // fuera del horizonte
        {2026, 13, 2026, 9, false}, // mes invalido
        {2020, 1, 2026, 9, true},
        {2026, 9, 2026, 9, true}, // mismo mes en ambos extremos
    };
    for (const Caso& c : casos) {
        const int llamadas = int(e.alcances.size());
        e.vm.fijarRango(c.da, c.dm, c.ha, c.hm);
        QVERIFY(e.esperarEstado(Vm::Estado::Dialogo));
        QCOMPARE(e.vm.rangoValido(), c.valido);
        QCOMPARE(e.vm.errorRango().isEmpty(), c.valido);
        QCOMPARE(e.vm.hayPlan(), c.valido);
        QCOMPARE(int(e.alcances.size()), llamadas + (c.valido ? 1 : 0));
    }
    const auto* r = std::get_if<AlcanceRangoExportacion>(&e.alcances.last());
    QVERIFY(r);
    QCOMPARE(r->desde, *MesCalendario::crear(2026, 9));
    QCOMPARE(r->hasta, *MesCalendario::crear(2026, 9));
    QCOMPARE(e.vm.nombreSugerido(), QStringLiteral("Archivo-Fiscal-2026-09.zip"));
    e.vm.fijarRango(2026, 7, 2026, 9);
    QVERIFY(e.esperarEstado(Vm::Estado::Dialogo));
    QCOMPARE(e.vm.nombreSugerido(), QStringLiteral("Archivo-Fiscal-2026-07_2026-09.zip"));
    QCOMPARE(e.vm.textoAlcance(), QStringLiteral("Julio 2026 a septiembre 2026"));
    QCOMPARE(e.vm.errorRango(), QString());
    e.vm.fijarRango(2026, 9, 2026, 7);
    QCOMPARE(e.vm.errorRango(), QStringLiteral("El mes inicial debe ser anterior o igual al mes final."));
    QVERIFY(!e.vm.puedeIniciar());
}

void TestExportacionFacturasViewModel::colisionAlReservarPideOtroNombre()
{
    Entorno e;
    QVERIFY(e.preparar());
    e.destino->ponerArchivo(kCarpeta, QStringLiteral("Archivo-Fiscal-2026-08.zip"), "previo");
    QVERIFY(e.abrirListo());
    e.vm.iniciar();
    QVERIFY(e.esperarEstado(Vm::Estado::Dialogo));
    QCOMPARE(e.vm.errorDestino(), catalogo::textoDestinoExistente());
    QVERIFY(!e.vm.hayResultado());
    QVERIFY(e.vm.hayPlan());
    QCOMPARE(e.vm.conteoPrevisto(), 2);
    QCOMPARE(e.vm.nombreCarpetaDestino(), QStringLiteral("Documentos"));
    QCOMPARE(*e.destino->archivo(kCarpeta, QStringLiteral("Archivo-Fiscal-2026-08.zip")), QByteArray("previo"));

    // Otro nombre (saneado al iniciar) y mismo plan: no se vuelve a planear.
    e.vm.setNombreZip(QStringLiteral("  Otro: nombre.zip.ZIP "));
    QVERIFY(e.vm.puedeIniciar());
    e.vm.iniciar();
    QCOMPARE(e.vm.nombreZip(), QStringLiteral("Otro- nombre.zip"));
    QVERIFY(e.esperarEstado(Vm::Estado::Resultado));
    QCOMPARE(e.vm.nombrePublicado(), QStringLiteral("Otro- nombre.zip"));
    QCOMPARE(e.alcances.size(), 1);
    QCOMPARE(e.destino->reservas(), 1);
    QVERIFY(e.destino->archivo(kCarpeta, QStringLiteral("Otro- nombre.zip")));
}

void TestExportacionFacturasViewModel::colisionAlPublicarUsaPublicarComo()
{
    Entorno e;
    QVERIFY(e.preparar());
    auto primera = std::make_shared<std::atomic<bool>>(true);
    auto* destino = e.destino.get();
    e.destino->antesDeRenombrar = [destino, primera] {
        if (primera->exchange(false)) { // aparece entre el dialogo y el rename
            destino->ponerArchivo(kCarpeta, QStringLiteral("Archivo-Fiscal-2026-08.zip"), "ajeno");
        }
    };
    QVERIFY(e.abrirListo());
    e.vm.iniciar();
    QVERIFY(e.esperarEstado(Vm::Estado::EsperandoNombre));
    QCOMPARE(e.vm.errorDestino(), catalogo::textoDestinoExistente());
    QVERIFY(!e.vm.ocupado());
    QCOMPARE(e.destino->parcialesVivos(), 1); // el parcial se conserva
    e.vm.elegirDestino(QUrl::fromLocalFile(QStringLiteral("/Users/usuario/Otra"))); // la carpeta ya no cambia
    QCOMPARE(e.vm.nombreCarpetaDestino(), QStringLiteral("Documentos"));

    e.vm.setNombreZip(QStringLiteral("Segundo"));
    e.vm.publicarConNombre();
    QVERIFY(e.esperarEstado(Vm::Estado::Resultado));
    QCOMPARE(e.vm.tituloResultado(), QStringLiteral("Exportación terminada"));
    QCOMPARE(e.vm.nombrePublicado(), QStringLiteral("Segundo.zip"));
    QCOMPARE(e.destino->reservas(), 1); // mismo parcial (publicarComo)
    QCOMPARE(e.destino->parcialesVivos(), 0);
    QCOMPARE(*e.destino->archivo(kCarpeta, QStringLiteral("Archivo-Fiscal-2026-08.zip")), QByteArray("ajeno"));
    QVERIFY(e.destino->archivo(kCarpeta, QStringLiteral("Segundo.zip")));
    QCOMPARE(e.alcances.size(), 1);
}

void TestExportacionFacturasViewModel::cancelarEsperandoNombreDescartaParcial()
{
    Entorno e;
    QVERIFY(e.preparar());
    auto* destino = e.destino.get();
    e.destino->antesDeRenombrar = [destino] {
        destino->ponerArchivo(kCarpeta, QStringLiteral("Archivo-Fiscal-2026-08.zip"));
    };
    QVERIFY(e.abrirListo());
    e.vm.iniciar();
    QVERIFY(e.esperarEstado(Vm::Estado::EsperandoNombre));
    e.vm.cerrarDialogo(); // no cierra con un parcial pendiente
    QCOMPARE(e.vm.estado(), Vm::Estado::EsperandoNombre);
    e.vm.cancelar();
    QVERIFY(e.esperarEstado(Vm::Estado::Resultado));
    QCOMPARE(e.vm.tituloResultado(), QStringLiteral("Exportación cancelada"));
    QCOMPARE(e.vm.varianteResultado(), QStringLiteral("neutro"));
    QVERIFY(e.vm.nombrePublicado().isEmpty());
    QCOMPARE(e.destino->parcialesVivos(), 0);
    e.vm.cancelar(); // en Resultado: cierra
    QCOMPARE(e.vm.estado(), Vm::Estado::Inactivo);
}

void TestExportacionFacturasViewModel::resultadoConYSinExportadas()
{
    // Con exportadas: una sin PDF (renderer falla), una omitida y una linea de mes.
    {
        Entorno e;
        QVERIFY(e.preparar());
        const exportacionpruebas::Banco::Paquete borrado = e.banco.paquete({exportacionpruebas::xml40(3)});
        const FacturaPlaneada planeadaBorrada = e.banco.factura(borrado, 0, 3); // huella antes de borrar
        e.planDe = [&e, planeadaBorrada](const AlcanceExportacion&) {
            PlanExportacion p = e.plan(1);
            p.facturas.append(planeadaBorrada);
            p.omisiones = {OmisionMesExportacion{TipoDescarga::Emitidos, kAgosto2026,
                                                 MotivoOmisionMes::CoberturaIncompleta}};
            return p;
        };
        QVERIFY(QFile::remove(borrado.absoluta));
        e.pdf->fallar = true;
        QVERIFY(e.abrirListo());
        e.vm.setConPdf(true);
        e.vm.iniciar();
        QVERIFY(e.esperarEstado(Vm::Estado::Resultado));
        QCOMPARE(e.vm.exportadas(), 1);
        QCOMPARE(e.vm.sinPdf(), 1);
        QCOMPARE(e.vm.omitidas(), 1);
        QCOMPARE(e.vm.tituloResultado(), QStringLiteral("Exportación terminada"));
        QCOMPARE(e.vm.descripcionResultado(), QStringLiteral("Se exportó 1 factura."));
        QCOMPARE(e.vm.detallesResultado(),
                 (QStringList{QStringLiteral("1 factura se exportó sin PDF."), QStringLiteral("1 factura no se exportó."),
                              QStringLiteral("1 mes tiene descargas pendientes o está incompleto."),
                              QStringLiteral("El detalle está en omitidas.txt, dentro del archivo ZIP.")}));
    }
    // D22: todas fallan al leerse -> ZIP solo con omitidas.txt y texto propio.
    {
        Entorno e;
        QVERIFY(e.preparar());
        e.planDe = [plan = e.plan(2)](const AlcanceExportacion&) { return plan; }; // huella antes de borrar
        QVERIFY(QFile::remove(e.paquete.absoluta));
        QVERIFY(e.abrirListo());
        e.vm.iniciar();
        QVERIFY(e.esperarEstado(Vm::Estado::Resultado));
        QCOMPARE(e.vm.exportadas(), 0);
        QCOMPARE(e.vm.omitidas(), 2);
        QCOMPARE(e.vm.varianteResultado(), QStringLiteral("advertencia"));
        QCOMPARE(e.vm.tituloResultado(), QStringLiteral("No se exportó ninguna factura"));
        QVERIFY(e.vm.descripcionResultado().contains(QStringLiteral("solo contiene la lista")));
        QVERIFY(!e.vm.nombrePublicado().isEmpty());
        const auto zip = e.destino->archivo(kCarpeta, e.vm.nombrePublicado());
        QVERIFY(zip);
        const auto entradas = exportacionpruebas::leerZipBytes(*zip);
        QVERIFY(entradas);
        QCOMPARE(entradas->keys(), QStringList{QStringLiteral("omitidas.txt")});
    }
    // Advertencia de durabilidad y error no corregible.
    {
        Entorno e;
        QVERIFY(e.preparar());
        e.destino->falla = fakes::FakeDestinoExportacion::Falla::SincronizarCarpeta;
        QVERIFY(e.abrirListo());
        e.vm.iniciar();
        QVERIFY(e.esperarEstado(Vm::Estado::Resultado));
        QCOMPARE(e.vm.varianteResultado(), QStringLiteral("advertencia"));
        QVERIFY(!e.vm.advertenciaResultado().isEmpty());
        e.vm.cerrarDialogo();
        e.destino->falla = fakes::FakeDestinoExportacion::Falla::Renombrar;
        QVERIFY(e.abrirListo());
        e.vm.setNombreZip(QStringLiteral("Otro")); // el anterior ya existe
        e.vm.iniciar();
        QVERIFY(e.esperarEstado(Vm::Estado::Resultado));
        QCOMPARE(e.vm.varianteResultado(), QStringLiteral("error"));
        QCOMPARE(e.vm.tituloResultado(), QStringLiteral("No se pudo exportar"));
        QVERIFY(e.vm.nombrePublicado().isEmpty());
        QVERIFY(e.vm.detallesResultado().isEmpty());
    }
}

void TestExportacionFacturasViewModel::sondasAusentesDeLogsYTextos_data()
{
    QTest::addColumn<int>("escenario");
    QTest::newRow("colisiones-y-publicada") << int(EscenarioSondas::ColisionesYPublicada);
    QTest::newRow("destino-sin-espacio") << int(EscenarioSondas::SinEspacio);
    QTest::newRow("reserva-con-errno") << int(EscenarioSondas::ReservaConErrno);
    QTest::newRow("error-zip") << int(EscenarioSondas::ErrorZip);
    QTest::newRow("cancelada-exportando") << int(EscenarioSondas::CanceladaExportando);
    QTest::newRow("cancelada-esperando-nombre") << int(EscenarioSondas::CanceladaEsperandoNombre);
    QTest::newRow("error-plan") << int(EscenarioSondas::ErrorPlan);
}

// Sondas (D20, seguridad 1): RFC, UUID, /Users/usuario, nombre de archivo
// hostil, IdPaquete del ZIP sintetico y la identidad de Facturas (paqueteId
// `pa-1`, posicion 1) no aparecen en los logs capturados ni en los textos del
// VM (salvo nombreZip y nombrePublicado, que solo muestra la ventana). Las
// notificaciones se prueban con el root real en TestExportacionCicloDeVida
// (C9): este VM no tiene notificador.
void TestExportacionFacturasViewModel::sondasAusentesDeLogsYTextos()
{
    QFETCH(int, escenario);
    const auto caso = EscenarioSondas(escenario);
    const QString hostil = QStringLiteral("Facturas $(whoami) ‮gpj.exe;rm -rf ~");
    const QString saneado = sanearNombreZip(hostil);
    const QString mensajePlan = QStringLiteral("disco /Users/usuario/Datos XAXX010101000 pa-1");
    QStringList textos;
    QStringList sondas;
    {
        CapturaLogs captura;
        Entorno e;
        agregarFilas(e.fuente);
        e.conCompuerta = caso == EscenarioSondas::CanceladaExportando;
        if (caso == EscenarioSondas::ErrorZip) {
            e.fabricaEscritor = [] { return std::make_unique<EscritorZipQueFalla>(); };
        }
        QVERIFY(e.preparar());
        QTRY_COMPARE(e.facturas.modelo()->count(), 2);
        if (caso == EscenarioSondas::ErrorPlan) {
            e.errorPlan = ErrorPersistencia::de(ErrorPersistencia::Tipo::Almacenamiento, mensajePlan);
        }
        // Seleccion por la identidad de Facturas (pa-1, 1).
        QVERIFY(e.vm.alternarSeleccion(QStringLiteral("pa-1"), 1));
        e.vm.abrirSeleccion();
        if (caso == EscenarioSondas::ErrorPlan) {
            QTRY_VERIFY(!e.vm.errorPlan().isEmpty());
            QVERIFY(!e.vm.puedeIniciar());
            textos << textosVisibles(e.vm);
        } else {
            QVERIFY(e.esperarEstado(Vm::Estado::Dialogo));
            e.vm.elegirDestino(QUrl::fromLocalFile(kCarpeta));
            e.vm.setNombreZip(hostil);
            auto* destino = e.destino.get();
            switch (caso) {
            case EscenarioSondas::ColisionesYPublicada: {
                // Colision al reservar, colision al publicar y publicarComo.
                e.destino->ponerArchivo(kCarpeta, saneado);
                e.vm.iniciar();
                QVERIFY(e.esperarEstado(Vm::Estado::Dialogo));
                QCOMPARE(e.vm.errorDestino(), catalogo::textoDestinoExistente());
                textos << textosVisibles(e.vm);
                const QString segundo = hostil + QStringLiteral(" 2");
                e.destino->antesDeRenombrar = [destino, segundo] {
                    destino->ponerArchivo(kCarpeta, sanearNombreZip(segundo));
                };
                e.vm.setNombreZip(segundo);
                e.vm.iniciar();
                QVERIFY(e.esperarEstado(Vm::Estado::EsperandoNombre));
                textos << textosVisibles(e.vm);
                e.destino->antesDeRenombrar = {};
                e.vm.setNombreZip(hostil + QStringLiteral(" 3"));
                e.vm.publicarConNombre();
                QVERIFY(e.esperarEstado(Vm::Estado::Resultado));
                QCOMPARE(e.vm.tituloResultado(), QStringLiteral("Exportación terminada"));
                QCOMPARE(e.vm.nombrePublicado(), sanearNombreZip(hostil + QStringLiteral(" 3"))); // solo la ventana
                break;
            }
            case EscenarioSondas::SinEspacio:
            case EscenarioSondas::ReservaConErrno:
            case EscenarioSondas::ErrorZip:
                if (caso == EscenarioSondas::SinEspacio) {
                    e.destino->falla = fakes::FakeDestinoExportacion::Falla::DiscoLleno; // ENOSPC
                    e.destino->limiteEscritura = 64;
                } else if (caso == EscenarioSondas::ReservaConErrno) {
                    e.destino->falla = fakes::FakeDestinoExportacion::Falla::Crear; // EIO
                }
                e.vm.iniciar();
                QVERIFY(e.esperarEstado(Vm::Estado::Resultado));
                QCOMPARE(e.vm.tituloResultado(), QStringLiteral("No se pudo exportar"));
                QVERIFY(e.vm.nombrePublicado().isEmpty());
                if (caso == EscenarioSondas::SinEspacio) {
                    QVERIFY(e.vm.descripcionResultado().startsWith(QStringLiteral("No hay espacio suficiente")));
                }
                break;
            case EscenarioSondas::CanceladaExportando:
                e.compuerta->cerrar();
                e.vm.iniciar();
                QVERIFY(e.compuerta->esperarBloqueo());
                QCOMPARE(e.vm.estado(), Vm::Estado::Exportando);
                textos << textosVisibles(e.vm);
                e.vm.cancelar();
                e.compuerta->liberar();
                QVERIFY(e.esperarEstado(Vm::Estado::Resultado));
                QCOMPARE(e.vm.tituloResultado(), QStringLiteral("Exportación cancelada"));
                break;
            case EscenarioSondas::CanceladaEsperandoNombre:
                e.destino->antesDeRenombrar = [destino, saneado] { destino->ponerArchivo(kCarpeta, saneado); };
                e.vm.iniciar();
                QVERIFY(e.esperarEstado(Vm::Estado::EsperandoNombre));
                textos << textosVisibles(e.vm);
                e.vm.cancelar();
                QVERIFY(e.esperarEstado(Vm::Estado::Resultado));
                QCOMPARE(e.vm.tituloResultado(), QStringLiteral("Exportación cancelada"));
                break;
            case EscenarioSondas::ErrorPlan: break;
            }
            textos << textosVisibles(e.vm);
            if (caso != EscenarioSondas::ColisionesYPublicada) {
                // Sin ZIP publicado ni `.partial` (cancelar o fallar).
                QVERIFY(e.vm.nombrePublicado().isEmpty());
                QCOMPARE(e.destino->parcialesVivos(), 0);
                QVERIFY(!e.destino->archivo(kCarpeta, saneado)
                        || caso == EscenarioSondas::CanceladaEsperandoNombre); // el ajeno sigue
            }
        }
        sondas << e.paquete.id << e.paquete.relativa << e.banco.dir.path();
        const QStringList lineas = captura.lineas();
        QVERIFY(!lineas.filter(QStringLiteral("satcfdi.cfdi.exportacion")).isEmpty());
        textos << lineas;
    }
    sondas << QStringLiteral("XAXX010101000") << QStringLiteral("XEXX010101000") << QStringLiteral("/Users/usuario")
           << QStringLiteral("Users/usuario") << hostil << saneado << QStringLiteral("whoami")
           << QStringLiteral("gpj") << QStringLiteral(".partial") << mensajePlan;
    // Identidad de Facturas: el paqueteId cubre cualquier forma "(pa-1, 1)";
    // la posicion sola (1) no es una sonda util, su nombre si.
    sondas << QStringLiteral("pa-1") << QStringLiteral("posicion") << QStringLiteral("posición");
    for (const int n : {1, 2}) {
        sondas << exportacionpruebas::uuidN(n) << exportacionpruebas::uuidN(n).toLower()
               << exportacionpruebas::uuidN(n).left(8);
    }
    sondas << QStringLiteral("AAAAAAAA-0000-4000-8000-000000000001") << QStringLiteral("AAAAAAAA");
    for (const QString& texto : std::as_const(textos)) {
        for (const QString& sonda : std::as_const(sondas)) {
            QVERIFY2(!texto.contains(sonda, Qt::CaseInsensitive),
                     qPrintable(QStringLiteral("sonda %1 en texto o log").arg(sondas.indexOf(sonda))));
        }
    }
}

// Seguridad 2: publicarComo() con error Estado (el servicio ya no espera un
// nombre) tiene texto propio, no el del plan.
void TestExportacionFacturasViewModel::publicarSinEsperaUsaTextoPropio()
{
    QCOMPARE(catalogo::textoErrorPublicarExportacion(ErrorExportacion::Codigo::Detenido),
             catalogo::textoErrorPlanExportacion(ErrorExportacion::Codigo::Detenido));
    QVERIFY(catalogo::textoErrorPublicarExportacion(ErrorExportacion::Codigo::Estado)
            != catalogo::textoErrorPlanExportacion(ErrorExportacion::Codigo::Estado));

    Entorno e;
    QVERIFY(e.preparar());
    auto* destino = e.destino.get();
    e.destino->antesDeRenombrar = [destino] {
        destino->ponerArchivo(kCarpeta, QStringLiteral("Archivo-Fiscal-2026-08.zip"));
    };
    QVERIFY(e.abrirListo());
    e.vm.iniciar();
    QVERIFY(e.esperarEstado(Vm::Estado::EsperandoNombre));
    e.servicio->cancelar(); // el servicio deja de esperar; el VM aun no lo sabe
    e.vm.setNombreZip(QStringLiteral("Otro"));
    e.vm.publicarConNombre();
    QCOMPARE(e.vm.errorDestino(), catalogo::textoErrorPublicarExportacion(ErrorExportacion::Codigo::Estado));
    QVERIFY(!e.vm.errorDestino().contains(QStringLiteral("No se pudieron leer")));
    QVERIFY(e.esperarEstado(Vm::Estado::Resultado));
    QCOMPARE(e.vm.tituloResultado(), QStringLiteral("Exportación cancelada"));
    QCOMPARE(e.destino->parcialesVivos(), 0);
}

void TestExportacionFacturasViewModel::vocabularioSinTerminosTecnicos()
{
    QStringList textos;
    {
        Entorno e;
        QVERIFY(e.preparar());
        e.vm.abrirRango();
        QVERIFY(e.esperarEstado(Vm::Estado::Dialogo));
        for (const auto& r : QList<QList<int>>{{2026, 10, 2026, 10}, {2026, 9, 2026, 8}, {2010, 1, 2026, 1}}) {
            e.vm.fijarRango(r[0], r[1], r[2], r[3]);
            textos << textosVisibles(e.vm);
        }
        e.vm.cerrarDialogo();
        QVERIFY(e.abrirListo());
        textos << textosVisibles(e.vm);
        e.vm.iniciar();
        QVERIFY(e.esperarEstado(Vm::Estado::Resultado));
        textos << textosVisibles(e.vm);
    }
    // Catalogo completo (todas las ramas).
    using E = ResultadoExportacion::Estado;
    using C = exportacion::ErrorDestinoExportacion::Codigo;
    for (const E estado : {E::Publicada, E::SinFacturas, E::Cancelada, E::Error}) {
        for (const int exportadas : {0, 3}) {
            catalogo::ResumenExportacion r;
            r.estado = estado;
            r.exportadas = exportadas;
            r.sinPdf = r.omitidas = r.lineasMes = 2;
            r.advertenciaDurabilidad = true;
            r.error = ErrorExportacion::Codigo::Destino;
            r.destino = C::EspacioInsuficiente;
            const auto m = catalogo::mensajeExportacion(r);
            textos << m.titulo << m.descripcion << catalogo::detallesExportacion(r)
                   << catalogo::advertenciaExportacion(r);
        }
    }
    for (int c = int(C::CarpetaNoDisponible); c <= int(C::Interno); ++c) {
        textos << catalogo::textoErrorDestinoCorregible(C(c));
    }
    for (int c = int(ErrorExportacion::Codigo::EnCurso); c <= int(ErrorExportacion::Codigo::Interno); ++c) {
        textos << catalogo::textoErrorPlanExportacion(ErrorExportacion::Codigo(c));
        textos << catalogo::textoErrorPublicarExportacion(ErrorExportacion::Codigo(c));
    }
    for (const auto f : {FaseExportacion::Preparando, FaseExportacion::Xml, FaseExportacion::Pdf,
                         FaseExportacion::Finalizando}) {
        textos << catalogo::textoFase(f);
    }
    textos << catalogo::avisosPlanExportacion(2, 2) << catalogo::textoConteoPrevisto(1);
    for (const QString& t : std::as_const(textos)) {
        for (const char* prohibido : {"solicitud", "paquete", "verificaci"}) {
            QVERIFY2(!t.contains(QLatin1String(prohibido), Qt::CaseInsensitive), qPrintable(t));
        }
    }
}

void TestExportacionFacturasViewModel::cableadoEnPresentacion()
{
    fakes::FakePerfilesSatService perfiles;
    fakes::FakeCredencialesSatService credenciales;
    SolicitudesServiceAsincrono solicitudes;
    PresentacionViewModels vms(&solicitudes, &perfiles, &credenciales);
    QVERIFY(vms.exportacionFacturas());
    QVERIFY(!vms.exportacionFacturas()->disponible());
    QCOMPARE(vms.exportacionFacturas()->parent(), &vms);
}

QTEST_MAIN(TestExportacionFacturasViewModel)
#include "TestExportacionFacturasViewModel.moc"
