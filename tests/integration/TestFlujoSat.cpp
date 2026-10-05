// T009 (plataforma): flujo completo con el root real, OperacionesSatProductivo
// real, FakeSatGateway (sin red), FilesystemPackageStorage real en un
// directorio temporal, FakeSecretStore (credencial Lista) y FakeOSIntegration:
// crear, enviar, verificar (Terminada, N paquetes), descargar (Descargado) y
// notificaciones (una Terminada y una Descarga completa; permiso denegado sin
// efectos).

#include "app_core/AppBootstrapper.h"
#include "app_core/AppCompositionRoot.h"

#include "application/operaciones/OperacionExecutor.h"
#include "application/operaciones/WorkerLocal.h"
#include "application/profiles/CredencialesSatService.h"
#include "application/profiles/PerfilesSatService.h"
#include "application/requests/SolicitudesService.h"

#include "fakes/FakeOSIntegration.h"
#include "fakes/FakeProgramador.h"
#include "fakes/FakeSatGateway.h"
#include "fakes/FakeSecretStore.h"

#include <QDir>
#include <QDirIterator>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <memory>
#include <optional>

using namespace satcfdi;

namespace {

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, 5000) || f.isCanceled()) {
        return std::nullopt;
    }
    return f.result();
}

struct Flujo {
    explicit Flujo(OSIntegration::NotificationStatus permiso)
    {
        os.setNotificationStatus(permiso);
        gateway.ahora = reloj.funcion();
        AppBootstrapper::Opciones opciones;
        opciones.directorioDatos = tmp.path();
        const auto arranque = AppBootstrapper(opciones).preparar();
        ok = arranque.esExito();
        if (!ok) {
            return;
        }
        OpcionesMonitoreo m;
        m.satGateway = &gateway; // OperacionesSatProductivo real sobre el fake
        m.reloj = reloj.funcion();
        m.programadorEjecutor = &programadorEjecutor;
        m.programadorWorker = &programadorWorker;
        root = std::make_unique<AppCompositionRoot>(arranque.valor().rutaBase, secretos, m);
    }

    // Perfil con e.firma Lista (FakeSecretStore) y solicitud de recibidos.
    std::optional<SolicitudId> prepararSolicitud()
    {
        const auto perfil = esperar(root->perfiles().crear(secretos.rfcCertificado, QStringLiteral("Perfil")));
        if (!perfil || !perfil->esExito()) {
            return std::nullopt;
        }
        EntradaEFirma e;
        e.rutaCertificado = QStringLiteral("/ficticio/efirma.cer");
        e.rutaLlavePrivada = QStringLiteral("/ficticio/efirma.key");
        e.contrasena = BufferSecreto::desdeBytes(secretos.contrasenaValida.constData(),
                                                 static_cast<std::size_t>(secretos.contrasenaValida.size()));
        const auto importada = esperar(root->credenciales().importar(perfil->valor().id, std::move(e)));
        if (!importada || !importada->esExito()) {
            return std::nullopt;
        }
        NuevaSolicitudRequest r;
        r.perfilId = perfil->valor().id;
        r.tipoDescarga = TipoDescarga::Recibidos;
        r.fechaInicial = QDate(2026, 9, 1);
        r.fechaFinal = QDate(2026, 9, 1);
        const auto creada = esperar(root->solicitudes().crear(r));
        if (!creada || !creada->esExito()) {
            return std::nullopt;
        }
        return creada->valor();
    }

    std::optional<SolicitudDetalle> detalle(const SolicitudId& id)
    {
        const auto d = esperar(root->solicitudes().obtener(id));
        return d && d->esExito() ? std::optional(d->valor()) : std::nullopt;
    }

    int notificaciones(const QString& tipo) const
    {
        int n = 0;
        for (const auto& x : os.notificacionesPedidas) {
            n += x.tipo == tipo ? 1 : 0;
        }
        return n;
    }

    QTemporaryDir tmp;
    bool ok = false;
    fakes::FakeReloj reloj{QDateTime(QDate(2026, 10, 5), QTime(12, 0), QTimeZone::UTC)};
    fakes::FakeProgramador programadorEjecutor{reloj};
    fakes::FakeProgramador programadorWorker{reloj};
    fakes::FakeSecretStore secretos;
    fakes::FakeSatGateway gateway;
    fakes::FakeOSIntegration os;
    int salidas = 0;
    std::unique_ptr<AppCompositionRoot> root;
};

} // namespace

class TestFlujoSat : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral(
            "^(?!Populating font family aliases took|This plugin does not support ).*")));
    }

    void flujoCompleto_data();
    void flujoCompleto();
};

void TestFlujoSat::flujoCompleto_data()
{
    QTest::addColumn<OSIntegration::NotificationStatus>("permiso");
    QTest::newRow("permiso concedido") << OSIntegration::NotificationStatus::Granted;
    QTest::newRow("permiso denegado") << OSIntegration::NotificationStatus::Denied;
}

void TestFlujoSat::flujoCompleto()
{
    QFETCH(OSIntegration::NotificationStatus, permiso);
    Flujo f(permiso);
    QVERIFY(f.ok);
    const auto id = f.prepararSolicitud();
    QVERIFY(id);
    // Creada y persistida antes de cualquier llamada SAT.
    QCOMPARE(f.detalle(*id)->resumen.estadoLocal, EstadoLocal::Creada);
    QCOMPARE(f.gateway.llamadas(fakes::FakeSatGateway::Op::Crear), 0);

    QVERIFY(f.root->cargar());
    f.root->iniciarCicloDeVida(f.os, nullptr, [&f] { ++f.salidas; });

    // Enviar (via ejecutor): Enviada con IdSolicitud SAT.
    f.root->worker().enviar(*id);
    QTRY_COMPARE(f.detalle(*id)->resumen.estadoLocal, EstadoLocal::Enviada);
    QCOMPARE(f.gateway.llamadas(fakes::FakeSatGateway::Op::Crear), 1);
    QVERIFY(f.detalle(*id)->idSolicitudSat.has_value());

    // Verificacion debida: Terminada con 2 paquetes; descargas automaticas.
    f.reloj.fijar(f.reloj.ahora().addSecs(11 * 60));
    f.root->worker().ejecutarCiclo();
    QTRY_COMPARE(f.detalle(*id)->resumen.estadoSat, std::optional(EstadoSolicitudSat::Terminada));
    QTRY_COMPARE(f.detalle(*id)->paquetes.size(), 2);
    for (int i = 0; i < 4; ++i) {
        f.root->worker().ejecutarCiclo();
        QTest::qWait(0);
    }
    QTRY_VERIFY([&] {
        const auto d = f.detalle(*id);
        if (!d) {
            return false;
        }
        for (const PaqueteResumen& p : d->paquetes) {
            if (p.estadoDescarga != EstadoDescarga::Descargado) {
                return false;
            }
        }
        return true;
    }());

    // ZIP finales en la raiz real (<data-dir>/paquetes), fuera de SQLite.
    int zips = 0;
    QDirIterator it(f.root->raizPaquetes(), {QStringLiteral("*.zip")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ++zips;
    }
    QCOMPARE(zips, 2);

    // Verificar de nuevo una solicitud ya Terminada no repite la notificacion.
    f.root->worker().verificarAhora(*id);
    f.root->worker().ejecutarCiclo();
    QTRY_COMPARE(f.notificaciones(QStringLiteral("descarga_completa")), 1);
    QCOMPARE(f.notificaciones(QStringLiteral("terminada")), 1);
    for (const auto& n : f.os.notificacionesPedidas) {
        QVERIFY(!n.titulo.contains(f.secretos.rfcCertificado) && !n.cuerpo.contains(f.secretos.rfcCertificado));
    }

    if (permiso == OSIntegration::NotificationStatus::Granted) {
        QCOMPARE(f.os.notificacionesEntregadas.size(), f.os.notificacionesPedidas.size());
    } else {
        // Permiso denegado: nada se entrega y el flujo es identico.
        QVERIFY(f.os.notificacionesEntregadas.isEmpty());
    }
    QCOMPARE(f.detalle(*id)->resumen.estadoLocal, EstadoLocal::Enviada);
}

#include "TestFlujoSat.moc"

int ejecutarTestFlujoSat(int argc, char* argv[])
{
    TestFlujoSat prueba;
    return QTest::qExec(&prueba, argc, argv);
}
