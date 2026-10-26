// T024.2: la barrera esperarExportacion termina con el resultado esperado del
// ExportacionFacturasService real sobre los puertos en memoria de
// tools/fakes-materiales, y falla con mensaje por timeout, sin exportacion
// abierta, al volver al dialogo por un error del plan o del destino, con un
// resultado distinto (cancelada) o si la grabacion se interrumpe. Sin QML ni
// grabacion.

#include "BarreraExportacion.h"

#include "ExportacionSintetica.h"
#include "FuentesSinteticas.h"
#include "ServiciosFijos.h" // tools/capturas-manual

#include "presentation/viewmodels/ExportacionFacturasViewModel.h"
#include "presentation/viewmodels/FacturasViewModel.h"
#include "presentation/viewmodels/PresentacionViewModels.h"

#include <QTest>

using namespace satcfdi;

namespace {

struct Grafo {
    explicit Grafo(bool retener)
        : Grafo(datos(retener))
    {
    }
    explicit Grafo(const materiales::DatosExportacion& d)
        : exportacion(fuente, d)
    {
        vms.setFuenteFacturas(&fuente, &preferencias);
        vms.setExportacionFacturas(exportacion.servicio(), [this] { return fuente.ahoraUtc(); });
        vms.facturas()->activar();
    }
    ~Grafo() { vms.setExportacionFacturas(nullptr); }

    static materiales::DatosExportacion datos(bool retener)
    {
        materiales::DatosExportacion d;
        d.retenerAlIniciar = retener;
        return d;
    }

    // Dialogo de "Exportar este mes" de septiembre 2026 (con el plan resuelto).
    bool abrirSinDestino()
    {
        if (!vms.facturas()->seleccionarMes(2026, 9)) {
            return false;
        }
        ExportacionFacturasViewModel* e = vms.exportacionFacturas();
        e->abrirMesActual();
        return QTest::qWaitFor([e] { return e->estado() != ExportacionFacturasViewModel::Estado::Planeando; }, 2000);
    }

    // ... con destino sintetico, listo para iniciar.
    bool abrirDialogo()
    {
        if (!abrirSinDestino()) {
            return false;
        }
        ExportacionFacturasViewModel* e = vms.exportacionFacturas();
        e->elegirDestino(QUrl::fromLocalFile(QStringLiteral("/Users/usuario/Documentos")));
        return QTest::qWaitFor([e] { return e->puedeIniciar(); }, 2000);
    }

    capturas::SolicitudesFijas solicitudes;
    capturas::PerfilesFijos perfiles;
    capturas::CredencialesFijas credenciales;
    materiales::FuenteFacturasSintetica fuente;
    materiales::PreferenciasFacturasMemoria preferencias;
    materiales::ExportacionSintetica exportacion;
    PresentacionViewModels vms{&solicitudes, &perfiles, &credenciales};
};

const std::function<bool()> kBombear = [] { return true; };
const QString kExito = QStringLiteral("exito");

} // namespace

class TestBarreraExportacion : public QObject {
    Q_OBJECT

private slots:
    void terminaConElResultado()
    {
        Grafo g(false);
        QVERIFY(g.abrirDialogo());
        ExportacionFacturasViewModel* e = g.vms.exportacionFacturas();
        const int previstas = e->conteoPrevisto();
        QVERIFY(previstas > 0);
        e->iniciar();
        QCOMPARE(e->estado(), ExportacionFacturasViewModel::Estado::Exportando);
        QCOMPARE(video::esperarExportacion(e, kExito, 10000, kBombear), QString());
        QCOMPARE(e->estado(), ExportacionFacturasViewModel::Estado::Resultado);
        QCOMPARE(e->exportadas(), previstas);
        QCOMPARE(e->omitidas(), 0);
    }

    void timeoutConElTrabajoRetenido()
    {
        Grafo g(true);
        QVERIFY(g.abrirDialogo());
        ExportacionFacturasViewModel* e = g.vms.exportacionFacturas();
        e->iniciar();
        QVERIFY(video::esperarExportacion(e, kExito, 200, kBombear).startsWith(QStringLiteral("timeout de 200 ms")));
        QCOMPARE(e->estado(), ExportacionFacturasViewModel::Estado::Exportando);
        g.exportacion.liberar();
        QCOMPARE(video::esperarExportacion(e, kExito, 10000, kBombear), QString());
        QCOMPARE(e->estado(), ExportacionFacturasViewModel::Estado::Resultado);
    }

    void sinExportacionAbierta()
    {
        Grafo g(false);
        QCOMPARE(video::esperarExportacion(g.vms.exportacionFacturas(), kExito, 100, kBombear),
                 QStringLiteral("no hay una exportacion abierta"));
        QCOMPARE(video::esperarExportacion(nullptr, kExito, 100, kBombear),
                 QStringLiteral("la exportacion no esta disponible"));
    }

    void errorDelPlan()
    {
        materiales::DatosExportacion d = Grafo::datos(false);
        d.errorAlPlanear = ErrorPersistencia::Tipo::Almacenamiento;
        Grafo g(d);
        QVERIFY(g.abrirSinDestino());
        ExportacionFacturasViewModel* e = g.vms.exportacionFacturas();
        QCOMPARE(e->estado(), ExportacionFacturasViewModel::Estado::Dialogo);
        QVERIFY(!e->errorPlan().isEmpty());
        const QString error = video::esperarExportacion(e, kExito, 2000, kBombear);
        QVERIFY2(error.startsWith(QStringLiteral("la exportacion volvio al dialogo por un error del plan: ")),
                 qPrintable(error));
    }

    void errorDelDestino()
    {
        materiales::DatosExportacion d = Grafo::datos(false);
        d.errorAlReservar = exportacion::ErrorDestinoExportacion::Codigo::DestinoExistente;
        Grafo g(d);
        QVERIFY(g.abrirDialogo());
        ExportacionFacturasViewModel* e = g.vms.exportacionFacturas();
        e->iniciar();
        const QString error = video::esperarExportacion(e, kExito, 10000, kBombear);
        QVERIFY2(error.startsWith(QStringLiteral("la exportacion volvio al dialogo por un error del destino: ")),
                 qPrintable(error));
        QCOMPARE(e->estado(), ExportacionFacturasViewModel::Estado::Dialogo);
    }

    void cancelada()
    {
        Grafo g(true);
        QVERIFY(g.abrirDialogo());
        ExportacionFacturasViewModel* e = g.vms.exportacionFacturas();
        e->iniciar();
        e->cancelar();
        g.exportacion.liberar();
        const QString error = video::esperarExportacion(e, kExito, 10000, kBombear);
        QVERIFY2(error.startsWith(QStringLiteral("resultado \"neutro\" (Exportación cancelada); se esperaba \"exito\"")),
                 qPrintable(error));
        // El guion puede declarar la cancelacion como resultado esperado.
        QCOMPARE(video::esperarExportacion(e, QStringLiteral("neutro"), 100, kBombear), QString());
    }

    void interrumpida()
    {
        Grafo g(true);
        QVERIFY(g.abrirDialogo());
        ExportacionFacturasViewModel* e = g.vms.exportacionFacturas();
        e->iniciar();
        QCOMPARE(video::esperarExportacion(e, kExito, 10000, [] { return false; }), QStringLiteral("interrumpida"));
        g.exportacion.liberar();
    }

    void resultadoDesconocido()
    {
        Grafo g(false);
        QCOMPARE(video::esperarExportacion(g.vms.exportacionFacturas(), QStringLiteral("listo"), 100, kBombear),
                 QStringLiteral("resultado esperado desconocido \"listo\""));
    }
};

QTEST_GUILESS_MAIN(TestBarreraExportacion)
#include "TestBarreraExportacion.moc"
