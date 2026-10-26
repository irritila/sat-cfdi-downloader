// T023.3 (integracion): DescargaGuiadaService con el root real,
// OperacionExecutor y WorkerLocal reales, OperacionesSatProductivo sobre
// FakeSatGateway (sin red, sin SAT real), FakeSecretStore y SQLite temporal:
// gate de e.firma por el ejecutor, envio de las solicitudes creadas y
// relectura de 5002/5005 persistidos por el ejecutor (ADR 0019).

#include "app_core/AppBootstrapper.h"
#include "app_core/AppCompositionRoot.h"

#include "application/cfdi/CatalogoMensualCfdiService.h"
#include "application/cfdi/DescargaGuiadaService.h"
#include "application/operaciones/WorkerLocal.h"
#include "application/profiles/CredencialesSatService.h"
#include "application/profiles/PerfilesSatService.h"

#include "fakes/FakeOSIntegration.h"
#include "fakes/FakeProgramador.h"
#include "fakes/FakeSatGateway.h"
#include "fakes/FakeSecretStore.h"

#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <memory>
#include <optional>

using namespace satcfdi;

namespace {

using Op = fakes::FakeSatGateway::Op;
using Paso = fakes::FakeSatGateway::Paso;
const MesCalendario kSeptiembre = *MesCalendario::crear(2026, 9);

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, 5000) || f.isCanceled() || f.resultCount() == 0) {
        return std::nullopt;
    }
    return f.result();
}

RespuestaCreacion creacion(const char* cod, std::optional<QString> id = std::nullopt)
{
    return {QString::fromLatin1(cod), QStringLiteral("mensaje sintetico"), std::move(id)};
}

struct Entorno {
    Entorno()
    {
        gateway.ahora = reloj.funcion();
        AppBootstrapper::Opciones opciones;
        opciones.directorioDatos = tmp.path();
        const auto arranque = AppBootstrapper(opciones).preparar();
        if (!arranque.esExito()) {
            return;
        }
        rutaBase = arranque.valor().rutaBase;
        ok = crearRoot();
    }

    // Root sobre la misma base (tambien para simular un reinicio).
    bool crearRoot()
    {
        root.reset();
        OpcionesMonitoreo m;
        m.satGateway = &gateway;
        m.reloj = reloj.funcion();
        m.programadorEjecutor = &programadorEjecutor;
        m.programadorWorker = &programadorWorker;
        m.programadorVencimiento = &programadorVencimiento;
        root = std::make_unique<AppCompositionRoot>(rutaBase, secretos, m);
        if (!root->cargar()) {
            return false;
        }
        root->iniciarCicloDeVida(os, nullptr, [] {});
        return true;
    }

    // Perfil (RFC del certificado sintetico) con o sin e.firma Lista.
    std::optional<PerfilId> perfil(bool conEFirma)
    {
        const auto p = esperar(root->perfiles().crear(secretos.rfcCertificado, QStringLiteral("Perfil")));
        if (!p || !p->esExito()) {
            return std::nullopt;
        }
        if (conEFirma) {
            EntradaEFirma e;
            e.rutaCertificado = QStringLiteral("/ficticio/efirma.cer");
            e.rutaLlavePrivada = QStringLiteral("/ficticio/efirma.key");
            e.contrasena = BufferSecreto::desdeBytes(secretos.contrasenaValida.constData(),
                                                     static_cast<std::size_t>(secretos.contrasenaValida.size()));
            const auto importada = esperar(root->credenciales().importar(p->valor().id, std::move(e)));
            if (!importada || !importada->esExito()) {
                return std::nullopt;
            }
        }
        return p->valor().id;
    }

    std::optional<EvaluacionCoberturaMensual> disponibilidad(const PerfilId& perfil)
    {
        const auto r = esperar(root->catalogoMensual().disponibilidad(perfil, kSeptiembre,
                                                                      CatalogoMensualCfdiService::Revalidacion::No));
        return r && r->esExito() ? std::optional(r->valor()) : std::nullopt;
    }

    // Ejecuta la descarga guiada y espera a que el ejecutor envie todo.
    std::optional<EjecucionGuiada> descargar(const PerfilId& perfil)
    {
        const auto pre = esperar(root->descargaGuiada().planificar(perfil, kSeptiembre, AccionGuiada::DescargarFacturas));
        if (!pre || !pre->esExito()) {
            return std::nullopt;
        }
        const auto ej = esperar(root->descargaGuiada().ejecutar(pre->valor(), {}));
        if (!ej || !ej->esExito()) {
            return std::nullopt;
        }
        const bool enviadas = QTest::qWaitFor(
            [&] {
                const auto d = disponibilidad(perfil);
                return d && d->emitidos.solicitudesEnCurso.size() + d->recibidos.solicitudesEnCurso.size()
                                + d->causas.size()
                            >= 2
                       && gateway.llamadas(Op::Crear) >= 2;
            },
            5000);
        return enviadas ? std::optional(ej->valor()) : std::nullopt;
    }

    QTemporaryDir tmp;
    QString rutaBase;
    bool ok = false;
    fakes::FakeReloj reloj{QDateTime(QDate(2026, 10, 5), QTime(12, 0), QTimeZone::UTC)};
    fakes::FakeProgramador programadorEjecutor{reloj};
    fakes::FakeProgramador programadorWorker{reloj};
    fakes::FakeProgramador programadorVencimiento{reloj};
    fakes::FakeSecretStore secretos;
    fakes::FakeSatGateway gateway;
    fakes::FakeOSIntegration os;
    std::unique_ptr<AppCompositionRoot> root;
};

} // namespace

class TestDescargaGuiada : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral(
            "^(?!Populating font family aliases took|This plugin does not support ).*")));
    }

    void creaYEnviaAmbasDirecciones();
    void con5002LaOtraSigueYNoSeRecreaLaAfectada();
    void con5005LaAtencionLlevaAlDiagnostico();
    void eFirmaNoListaPorElGateDelEjecutor();
    void cierreEntreCommitYEncoladoSeReenviaConLaAccion();
    void mesConArchivosEnAmbasDireccionesNotificaDosVeces();
};

void TestDescargaGuiada::creaYEnviaAmbasDirecciones()
{
    Entorno e;
    QVERIFY(e.ok);
    const auto perfil = e.perfil(true);
    QVERIFY(perfil);
    e.gateway.programar(Op::Crear, Paso::conCreacion(creacion("5000", QStringLiteral("aaaaaaaa-0000-4000-8000-000000000001"))));
    e.gateway.programar(Op::Crear, Paso::conCreacion(creacion("5000", QStringLiteral("aaaaaaaa-0000-4000-8000-000000000002"))));
    const auto ej = e.descargar(*perfil);
    QVERIFY(ej);
    QCOMPARE(ej->creadas.size(), 2);
    QVERIFY(ej->loteId);
    const auto d = e.disponibilidad(*perfil);
    QVERIFY(d);
    QCOMPARE(d->estado, EstadoMes::Preparando);
    QCOMPARE(e.gateway.llamadas(Op::Crear), 2);
    // Repetir: ya enviadas, la accion no se ofrece y no crea ni envia nada.
    const auto pre = esperar(e.root->descargaGuiada().planificar(*perfil, kSeptiembre, AccionGuiada::DescargarFacturas));
    QVERIFY(pre && !pre->esExito());
    QCOMPARE(pre->error().tipo, ErrorDescargaGuiada::Tipo::AccionNoDisponible);
    QCOMPARE(pre->error().preplanActual->oferta.accion, std::nullopt);
    // Los lotes del historial.
    const auto lotes = esperar(e.root->catalogoMensual().lotes(*perfil, kSeptiembre));
    QVERIFY(lotes && lotes->esExito());
    QCOMPARE(lotes->valor().size(), 1);
    QCOMPARE(lotes->valor().first().tramos.size(), 2);
}

void TestDescargaGuiada::con5002LaOtraSigueYNoSeRecreaLaAfectada()
{
    Entorno e;
    QVERIFY(e.ok);
    const auto perfil = e.perfil(true);
    QVERIFY(perfil);
    // Se envian en orden: emitidos (5002) y recibidos (aceptada).
    e.gateway.programar(Op::Crear, Paso::conCreacion(creacion("5002")));
    e.gateway.programar(Op::Crear, Paso::conCreacion(creacion("5000", QStringLiteral("aaaaaaaa-0000-4000-8000-000000000003"))));
    QVERIFY(e.descargar(*perfil));
    const auto d = e.disponibilidad(*perfil);
    QVERIFY(d);
    QCOMPARE(d->estado, EstadoMes::RequiereAtencion);
    QCOMPARE(d->causa->causa, CausaAtencion::Codigo5002);
    QCOMPARE(d->causa->direccion, TipoDescarga::Emitidos);
    QCOMPARE(d->recibidos.solicitudesEnCurso.size(), 1);
    const OfertaDescargaGuiada o = ofertaDescargaGuiada(*d);
    QCOMPARE(o.direccionesInhibidas, QList<TipoDescarga>{TipoDescarga::Emitidos});
    // No se ofrece recrear emitidos (y recibidos ya esta enviada): la accion
    // se rechaza y no hay llamadas SAT nuevas.
    QCOMPARE(o.accion, std::nullopt);
    const auto plan = planificarDescargaGuiada(*d, AccionGuiada::CompletarMes);
    QVERIFY(!plan);
    const auto pre = esperar(e.root->descargaGuiada().planificar(*perfil, kSeptiembre, AccionGuiada::DescargarFacturas));
    QVERIFY(pre && !pre->esExito());
    QCOMPARE(pre->error().tipo, ErrorDescargaGuiada::Tipo::AccionNoDisponible);
    QCOMPARE(pre->error().preplanActual->oferta.direccionesInhibidas, QList<TipoDescarga>{TipoDescarga::Emitidos});
    QCOMPARE(e.gateway.llamadas(Op::Crear), 2);
}

void TestDescargaGuiada::con5005LaAtencionLlevaAlDiagnostico()
{
    Entorno e;
    QVERIFY(e.ok);
    const auto perfil = e.perfil(true);
    QVERIFY(perfil);
    e.gateway.programar(Op::Crear, Paso::conCreacion(creacion("5000", QStringLiteral("aaaaaaaa-0000-4000-8000-000000000004"))));
    e.gateway.programar(Op::Crear, Paso::conCreacion(creacion("5005")));
    QVERIFY(e.descargar(*perfil));
    const auto d = e.disponibilidad(*perfil);
    QVERIFY(d);
    QCOMPARE(d->estado, EstadoMes::RequiereAtencion);
    QCOMPARE(d->causa->causa, CausaAtencion::Codigo5005);
    const OfertaDescargaGuiada o = ofertaDescargaGuiada(*d);
    QCOMPARE(o.diagnostico5005.size(), 1);
    QCOMPARE(o.diagnostico5005.first().direccion, TipoDescarga::Recibidos);
    QCOMPARE(o.direccionesInhibidas, QList<TipoDescarga>{TipoDescarga::Recibidos});
}

void TestDescargaGuiada::eFirmaNoListaPorElGateDelEjecutor()
{
    Entorno e;
    QVERIFY(e.ok);
    const auto perfil = e.perfil(false); // sin e.firma
    QVERIFY(perfil);
    const auto pre = esperar(e.root->descargaGuiada().planificar(*perfil, kSeptiembre, AccionGuiada::DescargarFacturas));
    QVERIFY(pre && !pre->esExito());
    QCOMPARE(pre->error().tipo, ErrorDescargaGuiada::Tipo::EFirmaNoLista);
    QCOMPARE(pre->error().estadoCredencial, std::optional(EstadoCredencial::SinCredencial));
    const auto d = e.disponibilidad(*perfil);
    QVERIFY(d);
    QCOMPARE(d->estado, EstadoMes::SinDescargar); // no se creo nada
    QCOMPARE(e.gateway.llamadas(Op::Crear), 0);
}

void TestDescargaGuiada::cierreEntreCommitYEncoladoSeReenviaConLaAccion()
{
    Entorno e;
    QVERIFY(e.ok);
    const auto perfil = e.perfil(true);
    QVERIFY(perfil);
    // Corte simulado: el worker ya no encola (salida en curso) cuando la
    // accion confirma; las solicitudes quedan comprometidas en Creada.
    e.root->worker().detener();
    const auto pre = esperar(e.root->descargaGuiada().planificar(*perfil, kSeptiembre, AccionGuiada::DescargarFacturas));
    QVERIFY(pre && pre->esExito());
    const auto ej = esperar(e.root->descargaGuiada().ejecutar(pre->valor(), {}));
    QVERIFY(ej && ej->esExito());
    QCOMPARE(ej->valor().creadas.size(), 2);
    QTest::qWait(50);
    QCOMPARE(e.gateway.llamadas(Op::Crear), 0);

    // Reinicio: ADR 0017, el worker nunca envia una Creada por su cuenta.
    QVERIFY(e.crearRoot());
    e.root->worker().ejecutarCiclo();
    QTest::qWait(100);
    QCOMPARE(e.gateway.llamadas(Op::Crear), 0);
    const auto d = e.disponibilidad(*perfil);
    QVERIFY(d);
    QCOMPARE(d->estado, EstadoMes::Preparando);
    QCOMPARE(d->emitidos.sinEnviar.size() + d->recibidos.sinEnviar.size(), 2);
    // La accion sigue ofrecida: el usuario la repite y se reenvian sin crear nada.
    const OfertaDescargaGuiada o = ofertaDescargaGuiada(*d);
    QCOMPARE(o.accion, std::optional(AccionGuiada::DescargarFacturas));
    e.gateway.programar(Op::Crear, Paso::conCreacion(creacion("5000", QStringLiteral("aaaaaaaa-0000-4000-8000-000000000005"))));
    e.gateway.programar(Op::Crear, Paso::conCreacion(creacion("5000", QStringLiteral("aaaaaaaa-0000-4000-8000-000000000006"))));
    const auto otra = esperar(e.root->descargaGuiada().planificar(*perfil, kSeptiembre, *o.accion));
    QVERIFY(otra && otra->esExito());
    QVERIFY(!otra->valor().creaAlgo());
    const auto reenvio = esperar(e.root->descargaGuiada().ejecutar(otra->valor(), {}));
    QVERIFY(reenvio && reenvio->esExito());
    QVERIFY(reenvio->valor().creadas.isEmpty());
    QCOMPARE(reenvio->valor().encoladas.size(), 2);
    QTRY_COMPARE(e.gateway.llamadas(Op::Crear), 2);
    QTRY_VERIFY(e.disponibilidad(*perfil)->emitidos.sinEnviar.isEmpty());
}

// T024.1 (D4): un mes guiado con archivos en ambas direcciones (flujo real
// hasta Descargado) produce exactamente 2 notificaciones "Facturas
// descargadas" (una por direccion, sin "Mostrar en Finder"); verificar de
// nuevo y reiniciar no duplican.
void TestDescargaGuiada::mesConArchivosEnAmbasDireccionesNotificaDosVeces()
{
    Entorno e;
    QVERIFY(e.ok);
    const auto perfil = e.perfil(true);
    QVERIFY(perfil);
    e.gateway.programar(Op::Crear, Paso::conCreacion(creacion("5000", QStringLiteral("aaaaaaaa-0000-4000-8000-000000000007"))));
    e.gateway.programar(Op::Crear, Paso::conCreacion(creacion("5000", QStringLiteral("aaaaaaaa-0000-4000-8000-000000000008"))));
    // Paquetes distintos por solicitud (2 y 1).
    for (const QStringList& ids : {QStringList{QStringLiteral("aaaaaaaa-0000-4000-8000-000000000007_01"),
                                               QStringLiteral("aaaaaaaa-0000-4000-8000-000000000007_02")},
                                   QStringList{QStringLiteral("aaaaaaaa-0000-4000-8000-000000000008_01")}}) {
        RespuestaVerificacion v = fakes::FakeSatGateway::verificacionTerminada();
        v.idsPaquete = ids;
        e.gateway.programar(Op::Verificar, Paso::conVerificacion(v));
    }
    const auto ej = e.descargar(*perfil);
    QVERIFY(ej);
    QCOMPARE(ej->creadas.size(), 2);

    const auto descargas = [&e] {
        QList<OSIntegration::NotificacionLocal> r;
        for (const auto& n : e.os.notificacionesPedidas) {
            if (n.tipo != QStringLiteral("credencial") && n.tipo != QStringLiteral("efirma_por_vencer")) {
                r.append(n);
            }
        }
        return r;
    };
    e.reloj.fijar(e.reloj.ahora().addSecs(11 * 60));
    QTRY_VERIFY_WITH_TIMEOUT(
        [&] {
            e.root->worker().ejecutarCiclo();
            return descargas().size() >= 2;
        }(),
        10000);
    QTest::qWait(100);
    const auto pedidas = descargas();
    QCOMPARE(pedidas.size(), 2);
    QStringList direcciones;
    for (const auto& n : pedidas) {
        QCOMPARE(n.tipo, QStringLiteral("descarga_completa"));
        QCOMPARE(n.titulo, QStringLiteral("Facturas descargadas"));
        QVERIFY(n.accionesExtra.isEmpty());
        QCOMPARE(n.destino.tipo, OSIntegration::DestinoNotificacion::Tipo::Solicitud);
        QVERIFY(ej->creadas.contains(SolicitudId::desdeTexto(n.destino.id).value()));
        QCOMPARE(n.id, n.destino.id + QStringLiteral(":descarga_completa"));
        direcciones << (n.cuerpo.contains(QStringLiteral("emitidas")) ? QStringLiteral("emitidas")
                                                                       : QStringLiteral("recibidas"));
        QVERIFY(n.cuerpo.contains(QStringLiteral("1–30 sep 2026")));
    }
    direcciones.sort();
    QCOMPARE(direcciones, (QStringList{QStringLiteral("emitidas"), QStringLiteral("recibidas")}));

    // Verificar de nuevo (reintento) no duplica.
    for (const SolicitudId& id : ej->creadas) {
        e.root->worker().verificarAhora(id);
    }
    e.root->worker().ejecutarCiclo();
    QTest::qWait(200);
    QCOMPARE(descargas().size(), 2);

    // Reinicio sobre la misma base: nada se vuelve a notificar.
    QVERIFY(e.crearRoot());
    e.reloj.fijar(e.reloj.ahora().addSecs(11 * 60));
    e.root->worker().ejecutarCiclo();
    QTest::qWait(200);
    QCOMPARE(descargas().size(), 2);
}

int ejecutarTestDescargaGuiada(int argc, char* argv[])
{
    TestDescargaGuiada prueba;
    return QTest::qExec(&prueba, argc, argv);
}

#include "TestDescargaGuiada.moc"
