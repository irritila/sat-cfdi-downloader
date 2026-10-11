#include "TestEstadoAgregado.h"

#include "FakesPersistencia.h"

#include "application/estadoagregado/MonitorEstadoAgregado.h"
#include "application/notificaciones/ServicioNotificaciones.h"
#include "application/operaciones/WorkerLocal.h"
#include "application/persistence/PersistenceDispatcher.h"

#include <QPromise>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace satcfdi;
using fakes::Almacen;

namespace {

using E = EstadoAgregado;

template <typename T>
bool esperar(const QFuture<T>& f)
{
    return QTest::qWaitFor([&] { return f.isFinished(); }, 5000);
}

// Consulta controlable: cada llamada crea una promesa pendiente.
struct ConsultaManual {
    int llamadas = 0;
    QList<std::shared_ptr<QPromise<std::optional<bool>>>> pendientes;

    MonitorEstadoAgregado::Consulta funcion()
    {
        return [this] {
            ++llamadas;
            auto p = std::make_shared<QPromise<std::optional<bool>>>();
            p->start();
            pendientes.append(p);
            return p->future();
        };
    }
    void responder(std::optional<bool> valor)
    {
        auto p = pendientes.takeFirst();
        if (valor) {
            p->addResult(valor);
        } else {
            p->addResult(std::optional<bool>{});
        }
        p->finish();
        QCoreApplication::processEvents(); // continuacion con contexto
    }
    void cancelar()
    {
        auto p = pendientes.takeFirst();
        p->future().cancel();
        p->finish();
        QCoreApplication::processEvents();
    }
};

InstantaneaWorker instantanea(EstadoWorker estado)
{
    InstantaneaWorker i;
    i.estado = estado;
    return i;
}

SolicitudPersistida solicitud(EstadoLocal local, std::optional<EstadoSolicitudSat> sat = std::nullopt)
{
    SolicitudPersistida s;
    s.id = SolicitudId::generar();
    s.perfilSatId = PerfilId::generar();
    s.estadoLocal = local;
    s.estadoSolicitudSat = sat;
    s.creadaEn = QDateTime::currentDateTimeUtc();
    return s;
}

PaquetePersistido paquete(const SolicitudId& id, EstadoDescarga estado)
{
    PaquetePersistido p;
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.solicitudMasivaId = id;
    p.idPaqueteSat = p.id;
    p.estadoDescarga = estado;
    return p;
}

std::optional<bool> consultar(Almacen& a)
{
    fakes::FakeSolicitudes s{a};
    fakes::FakePaquetes p{a};
    PersistenceDispatcher d;
    auto f = consultarAtencionSolicitudes(d, s, p);
    if (!esperar(f) || f.isCanceled()) {
        d.cerrar();
        return std::optional<bool>{};
    }
    auto r = f.result();
    d.cerrar();
    return r;
}

} // namespace

void TestEstadoAgregado::prioridad_data()
{
    QTest::addColumn<bool>("solicitudes");
    QTest::addColumn<bool>("perfiles");
    QTest::addColumn<bool>("trabajando");
    QTest::addColumn<bool>("pausado");
    QTest::addColumn<EstadoAgregado>("esperado");

    QTest::newRow("normal") << false << false << false << false << E::Normal;
    QTest::newRow("pausado") << false << false << false << true << E::Pausado;
    QTest::newRow("trabajando") << false << false << true << false << E::Trabajando;
    QTest::newRow("trabajando>pausado") << false << false << true << true << E::Trabajando;
    QTest::newRow("atencion solicitudes") << true << false << false << false << E::Atencion;
    QTest::newRow("atencion perfiles") << false << true << false << false << E::Atencion;
    QTest::newRow("atencion>trabajando") << true << false << true << false << E::Atencion;
    QTest::newRow("atencion>pausado") << false << true << false << true << E::Atencion;
    QTest::newRow("todo") << true << true << true << true << E::Atencion;
}

void TestEstadoAgregado::prioridad()
{
    QFETCH(bool, solicitudes);
    QFETCH(bool, perfiles);
    QFETCH(bool, trabajando);
    QFETCH(bool, pausado);
    QFETCH(EstadoAgregado, esperado);
    QCOMPARE(calcularEstadoAgregado({solicitudes, perfiles, trabajando, pausado}), esperado);
}

void TestEstadoAgregado::solicitudRequiereAtencion()
{
    using R = EstadoResumen;
    const QList<R> si = {R::ErrorSat, R::Rechazada, R::EnvioFallido, R::EnvioIncierto};
    const QList<R> no = {R::Creada,    R::Enviando,  R::Enviada, R::Aceptada,
                         R::EnProceso, R::Terminada, R::Vencida};
    for (R r : si) {
        QVERIFY2(satcfdi::solicitudRequiereAtencion(r), qPrintable(claveEstable(r)));
    }
    for (R r : no) {
        QVERIFY2(!satcfdi::solicitudRequiereAtencion(r), qPrintable(claveEstable(r)));
    }
    QVERIFY(paqueteRequiereAtencion(EstadoDescarga::Error));
    for (EstadoDescarga d : {EstadoDescarga::Disponible, EstadoDescarga::Descargando, EstadoDescarga::Descargado,
                             EstadoDescarga::Vencido}) {
        QVERIFY(!paqueteRequiereAtencion(d));
    }
}

void TestEstadoAgregado::perfilRequiereAtencion()
{
    PerfilResumen activo{PerfilId::generar(), QStringLiteral("EKU9003173C9"), QStringLiteral("A"), true};
    PerfilResumen inactivo = activo;
    inactivo.activo = false;
    for (PreparacionPerfil p : kPreparacionesPerfil) {
        const bool esperado = p != PreparacionPerfil::Lista && p != PreparacionPerfil::Verificando;
        QCOMPARE(satcfdi::perfilRequiereAtencion(PerfilConPreparacion::componer(activo, p)), esperado);
        QVERIFY(!satcfdi::perfilRequiereAtencion(PerfilConPreparacion::componer(inactivo, p)));
    }
}

void TestEstadoAgregado::consultaSolicitudes()
{
    {
        Almacen a;
        QCOMPARE(consultar(a), std::optional<bool>(false));
    }
    {
        Almacen a;
        a.solicitudes.append(solicitud(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso));
        a.solicitudes.append(solicitud(EstadoLocal::Enviada, EstadoSolicitudSat::Vencida));
        const SolicitudPersistida t = solicitud(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada);
        a.solicitudes.append(t);
        a.paquetes.append(paquete(t.id, EstadoDescarga::Descargado));
        a.paquetes.append(paquete(t.id, EstadoDescarga::Disponible));
        QCOMPARE(consultar(a), std::optional<bool>(false));
        // Un paquete en Error de una solicitud terminada -> atencion.
        a.paquetes.append(paquete(t.id, EstadoDescarga::Error));
        QCOMPARE(consultar(a), std::optional<bool>(true));
        // Eliminado: no cuenta.
        a.paquetes.last().eliminadoEn = QDateTime::currentDateTimeUtc();
        QCOMPARE(consultar(a), std::optional<bool>(false));
    }
    for (auto [local, sat] : {std::pair{EstadoLocal::Enviada, std::optional(EstadoSolicitudSat::Error)},
                              std::pair{EstadoLocal::Enviada, std::optional(EstadoSolicitudSat::Rechazada)},
                              std::pair{EstadoLocal::EnvioFallido, std::optional<EstadoSolicitudSat>{}},
                              std::pair{EstadoLocal::EnvioIncierto, std::optional<EstadoSolicitudSat>{}}}) {
        Almacen a;
        a.solicitudes.append(solicitud(local, sat));
        QCOMPARE(consultar(a), std::optional<bool>(true));
        a.solicitudes.last().eliminadoEn = QDateTime::currentDateTimeUtc();
        QCOMPARE(consultar(a), std::optional<bool>(false));
    }
}

void TestEstadoAgregado::consultaSolicitudesFallidaEsNula()
{
    Almacen a;
    a.fallos.insert(QStringLiteral("listarSolicitudes"), fakes::error(ErrorPersistencia::Tipo::Almacenamiento));
    QCOMPARE(consultar(a), std::optional<bool>{});
    Almacen b;
    b.fallos.insert(QStringLiteral("contarPaquetesPorEstado"),
                    fakes::error(ErrorPersistencia::Tipo::Almacenamiento));
    QCOMPARE(consultar(b), std::optional<bool>{});
}

void TestEstadoAgregado::monitorEmiteSoloAlCambiar()
{
    ConsultaManual sol;
    ConsultaManual per;
    MonitorEstadoAgregado m(sol.funcion(), per.funcion());
    QSignalSpy spy(&m, &MonitorEstadoAgregado::estadoCambiado);

    m.alCambiarWorker(instantanea(EstadoWorker::ActivoEnEspera)); // antes de iniciar: sin emitir
    QCOMPARE(spy.count(), 0);
    m.iniciar();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.last().at(0).value<EstadoAgregado>(), E::Normal);
    QCOMPARE(sol.llamadas, 1);
    QCOMPARE(per.llamadas, 1);

    sol.responder(false);
    per.responder(false);
    QCOMPARE(spy.count(), 1); // sigue Normal

    m.alCambiarWorker(instantanea(EstadoWorker::Ejecutando));
    m.alCambiarWorker(instantanea(EstadoWorker::Ejecutando));
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.last().at(0).value<EstadoAgregado>(), E::Trabajando);

    m.alCambiarWorker(instantanea(EstadoWorker::Pausado));
    QCOMPARE(spy.last().at(0).value<EstadoAgregado>(), E::Pausado);

    m.refrescarPerfiles();
    per.responder(true);
    QCOMPARE(spy.last().at(0).value<EstadoAgregado>(), E::Atencion);
    m.alCambiarWorker(instantanea(EstadoWorker::Ejecutando)); // Atencion manda
    QCOMPARE(spy.count(), 4);

    m.refrescarPerfiles();
    per.responder(false);
    QCOMPARE(spy.count(), 5);
    QCOMPARE(spy.last().at(0).value<EstadoAgregado>(), E::Trabajando);
    m.alCambiarWorker(instantanea(EstadoWorker::ActivoEnEspera));
    QCOMPARE(spy.last().at(0).value<EstadoAgregado>(), E::Normal);
    QCOMPARE(spy.count(), 6);
}

void TestEstadoAgregado::monitorCoalesceConsultas()
{
    ConsultaManual sol;
    ConsultaManual per;
    MonitorEstadoAgregado m(sol.funcion(), per.funcion());
    m.refrescarSolicitudes(); // antes de iniciar: no consulta
    QCOMPARE(sol.llamadas, 0);
    m.iniciar();
    m.iniciar(); // idempotente
    QCOMPARE(sol.llamadas, 1);
    m.refrescarSolicitudes();
    m.refrescarSolicitudes();
    m.refrescarSolicitudes();
    QCOMPARE(sol.llamadas, 1); // en curso: solo marca sucia
    sol.responder(false);
    QCOMPARE(sol.llamadas, 2); // un unico relanzamiento
    sol.responder(true);
    QCOMPARE(sol.llamadas, 2);
    QCOMPARE(m.estado(), E::Atencion);
}

void TestEstadoAgregado::monitorConservaValorAnteFallo()
{
    ConsultaManual sol;
    ConsultaManual per;
    MonitorEstadoAgregado m(sol.funcion(), per.funcion());
    QSignalSpy spy(&m, &MonitorEstadoAgregado::estadoCambiado);
    m.iniciar();
    sol.responder(true);
    per.responder(false);
    QCOMPARE(m.estado(), E::Atencion);
    m.refrescarSolicitudes();
    sol.responder(std::nullopt); // lectura fallida: conserva Atencion
    QCOMPARE(m.estado(), E::Atencion);
    m.refrescarSolicitudes();
    sol.cancelar(); // cancelada: conserva y libera la fuente
    QCOMPARE(m.estado(), E::Atencion);
    QVERIFY(!m.consultando());
    m.refrescarSolicitudes();
    QCOMPARE(sol.llamadas, 4);
    sol.responder(false);
    QCOMPARE(m.estado(), E::Normal);
    QCOMPARE(spy.count(), 3); // Normal, Atencion, Normal
}

void TestEstadoAgregado::monitorDetenidoNoEmite()
{
    ConsultaManual sol;
    ConsultaManual per;
    MonitorEstadoAgregado m(sol.funcion(), per.funcion());
    QSignalSpy spy(&m, &MonitorEstadoAgregado::estadoCambiado);
    m.iniciar();
    m.detener();
    sol.responder(true);
    per.responder(true);
    m.alCambiarWorker(instantanea(EstadoWorker::Ejecutando));
    m.refrescarSolicitudes();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(sol.llamadas, 1);
}

void TestEstadoAgregado::destinoDeNotificaciones()
{
    using T = DestinoNotificacion::Tipo;
    TransicionNotificable t;
    t.solicitudId = SolicitudId::generar();
    t.rfcSolicitante = QStringLiteral("EKU9003173C9");
    for (TipoTransicionNotificable tipo :
         {TipoTransicionNotificable::Terminada, TipoTransicionNotificable::DescargaCompleta,
          TipoTransicionNotificable::ErrorSat, TipoTransicionNotificable::Rechazada,
          TipoTransicionNotificable::Vencida}) {
        t.tipo = tipo;
        const Notificacion n = ServicioNotificaciones::componer(t);
        QCOMPARE(n.destino, (DestinoNotificacion{T::Solicitud, t.solicitudId.texto()}));
    }
    const PerfilId perfil = PerfilId::generar();
    const auto c = ServicioNotificaciones::componerCredencial(perfil, EstadoCredencial::Vencida);
    QVERIFY(c);
    QCOMPARE(c->destino, (DestinoNotificacion{T::Perfil, perfil.texto()}));
    const Notificacion v = ServicioNotificaciones::componerVencimiento(
        perfil, QStringLiteral("EKU9003173C9"), QDateTime::currentDateTimeUtc().addDays(5), 7, 5);
    QCOMPARE(v.destino, (DestinoNotificacion{T::Perfil, perfil.texto()}));
    // El destino nunca lleva RFC: solo el UUID interno.
    QVERIFY(!v.destino.id.contains(QStringLiteral("EKU")));
}
