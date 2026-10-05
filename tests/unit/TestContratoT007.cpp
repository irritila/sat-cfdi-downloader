#include "TestContratoT007.h"

#include "FakesPersistencia.h"
#include "fakes/FakeOperacionesSat.h"
#include "fakes/FakeProgramador.h"

#include "application/operaciones/OperacionesSatNulo.h"
#include "domain/common/UuidCanonico.h"
#include "domain/operaciones/PoliticasOperacion.h"

#include <QSet>
#include <QTest>
#include <QThread>
#include <QTimeZone>

#include <atomic>
#include <memory>

using namespace satcfdi;
using namespace Qt::StringLiterals;
using fakes::Almacen;
using fakes::FakeOperacionesSat;

namespace {

const QDateTime kAhora = QDateTime(QDate(2026, 10, 5), QTime(12, 0), QTimeZone(QTimeZone::UTC));

FallaOperacion falla(FaseOperacion fase, const char* codigo = nullptr)
{
    return FallaOperacion::de(fase, codigo ? std::optional(QString::fromLatin1(codigo)) : std::nullopt);
}

ResultadoEnvio envio(const char* cod, const char* id = nullptr)
{
    ResultadoEnvio r;
    r.codEstatus = QString::fromLatin1(cod);
    if (id) {
        r.idSolicitudSat = QString::fromLatin1(id);
    }
    return r;
}

// Solicitud fixture en el Almacen.
SolicitudPersistida solicitud(EstadoLocal estado, const PerfilId& perfil)
{
    SolicitudPersistida s;
    s.id = SolicitudId::generar();
    s.perfilSatId = perfil;
    s.rfcSolicitante = u"EKU9003173C9"_s;
    s.estadoLocal = estado;
    s.creadaEn = kAhora;
    if (estado == EstadoLocal::Enviada) {
        s.idSolicitudSat = u"SAT-1"_s;
        s.enviadaEn = kAhora;
        s.envioIniciadoEn = kAhora;
        s.codEstatusSolicitud = u"5000"_s;
    }
    return s;
}

struct Entorno {
    Almacen almacen;
    fakes::FakeUnitOfWork uow{almacen};
    fakes::FakeOperacionesSolicitud repo{almacen};
    PerfilId perfil = PerfilId::generar();

    template <typename F>
    auto enTx(F f)
    {
        (void)uow.begin();
        auto r = f();
        (void)uow.commit();
        return r;
    }
};

} // namespace

void TestContratoT007::catalogosYClaveDeFalla()
{
    QSet<QString> claves;
    for (FaseOperacion f : kFasesOperacion) {
        claves.insert(claveEstable(f));
        QCOMPARE(faseOperacionDesdeClave(claveEstable(f)), std::optional(f));
    }
    QCOMPARE(claves.size(), 6);
    QCOMPARE(claveFalla(falla(FaseOperacion::RespuestaExplicita, "300")), u"RespuestaExplicita:300"_s);
    QCOMPARE(claveFalla(falla(FaseOperacion::DespuesDeEnvio)), u"DespuesDeEnvio"_s);
    QCOMPARE(claveEstable(TipoEventoLog::EnvioNoIniciado), u"envio_no_iniciado"_s);
    QCOMPARE(claveEstable(TipoEventoLog::VerificacionSuspendida), u"verificacion_suspendida"_s);
    QCOMPARE(kTiposEventoLog.size(), std::size_t(20));
}

void TestContratoT007::destinoDeEnvioD7()
{
    using politicas::destinoEnvio;
    QCOMPARE(destinoEnvio(envio("5000", "abc")), EstadoLocal::Enviada);
    QCOMPARE(destinoEnvio(envio("5000")), EstadoLocal::EnvioIncierto);
    QCOMPARE(destinoEnvio(envio("5006")), EstadoLocal::EnvioIncierto);
    QCOMPARE(destinoEnvio(envio("9999")), EstadoLocal::EnvioIncierto);
    for (const char* c : {"300", "301", "302", "303", "304", "305", "5001", "5002", "5005"}) {
        QCOMPARE(destinoEnvio(envio(c)), EstadoLocal::EnvioFallido);
    }
    for (FaseOperacion f : {FaseOperacion::Preparacion, FaseOperacion::Autenticacion, FaseOperacion::AntesDeEnvio}) {
        QCOMPARE(destinoEnvio(falla(f)), EstadoLocal::Creada);
    }
    for (FaseOperacion f : {FaseOperacion::DespuesDeEnvio, FaseOperacion::RespuestaExplicita}) {
        QCOMPARE(destinoEnvio(falla(f)), EstadoLocal::EnvioIncierto);
    }
    FallaOperacion cancelada = falla(FaseOperacion::DespuesDeEnvio);
    cancelada.cancelada = true; // D12: se aplica como su fase
    QCOMPARE(destinoEnvio(cancelada), EstadoLocal::EnvioIncierto);
}

void TestContratoT007::agendaTrasExitoD8()
{
    using namespace politicas;
    auto a = agendaTrasExito(0, false, EstadoSolicitudSat::EnProceso, kAhora);
    QCOMPARE(a.verificacionesSinCambio, 1);
    QCOMPARE(a.siguienteVerificacionEn, std::optional(kAhora.addSecs(600)));
    a = agendaTrasExito(2, false, EstadoSolicitudSat::EnProceso, kAhora); // tercera sin cambio
    QCOMPARE(a.verificacionesSinCambio, 3);
    QCOMPARE(a.siguienteVerificacionEn, std::optional(kAhora.addSecs(1800)));
    a = agendaTrasExito(5, true, EstadoSolicitudSat::EnProceso, kAhora); // cambio
    QCOMPARE(a.verificacionesSinCambio, 0);
    QCOMPARE(a.siguienteVerificacionEn, std::optional(kAhora.addSecs(600)));
    for (EstadoSolicitudSat e : {EstadoSolicitudSat::Terminada, EstadoSolicitudSat::Error, EstadoSolicitudSat::Rechazada,
                                 EstadoSolicitudSat::Vencida}) {
        QVERIFY(!agendaTrasExito(0, true, e, kAhora).siguienteVerificacionEn);
    }

    SolicitudPersistida s;
    s.estadoSolicitudSat = EstadoSolicitudSat::Terminada;
    s.codigoEstadoSolicitud = u"5000"_s;
    s.numeroCfdi = 3;
    ResultadoVerificacion r;
    r.estadoSolicitudSat = EstadoSolicitudSat::Terminada;
    r.codigoEstadoSolicitud = u"5000"_s;
    r.numeroCfdi = 3;
    r.idsPaquetes = {u"b"_s, u"a"_s};
    QVERIFY(sinCambio(s, {u"a"_s, u"b"_s}, r)); // conjunto, no orden
    r.idsPaquetes.append(u"c"_s);
    QVERIFY(!sinCambio(s, {u"a"_s, u"b"_s}, r));
    r.idsPaquetes = {u"a"_s, u"b"_s};
    r.numeroCfdi = 4;
    QVERIFY(!sinCambio(s, {u"a"_s, u"b"_s}, r));
}

void TestContratoT007::agendaTrasFallaYSuspensionD8()
{
    using namespace politicas;
    RachaVerificacion racha;
    racha.verificacionesSinCambio = 2;
    const FallaOperacion f300 = falla(FaseOperacion::RespuestaExplicita, "300");
    auto a = agendaTrasFalla(racha, f300, kAhora);
    QCOMPARE(a.fallasIguales, 1);
    QVERIFY(a.registrarLog);
    QVERIFY(!a.suspender);
    QCOMPARE(a.siguienteVerificacionEn, std::optional(kAhora.addSecs(1800)));
    racha.ultimaClaveFalla = a.claveFalla;
    racha.fallasIguales = 1;
    a = agendaTrasFalla(racha, f300, kAhora);
    QCOMPARE(a.fallasIguales, 2);
    QVERIFY(!a.registrarLog); // misma clave: sin log nuevo
    racha.fallasIguales = 2;
    a = agendaTrasFalla(racha, f300, kAhora);
    QCOMPARE(a.fallasIguales, 3);
    QVERIFY(a.suspender);
    QVERIFY(!a.siguienteVerificacionEn);
    // Otra clave reinicia; transitorias nunca suspenden.
    a = agendaTrasFalla(racha, falla(FaseOperacion::RespuestaExplicita, "302"), kAhora);
    QCOMPARE(a.fallasIguales, 1);
    QVERIFY(a.registrarLog);
    for (const FallaOperacion& t : {falla(FaseOperacion::RespuestaExplicita, "404"), falla(FaseOperacion::RespuestaExplicita, "5011"),
                                    falla(FaseOperacion::DespuesDeEnvio), falla(FaseOperacion::RespuestaExplicita, "a:InvalidSecurity")}) {
        RachaVerificacion larga;
        larga.ultimaClaveFalla = claveFalla(t);
        larga.fallasIguales = 10;
        QVERIFY(!agendaTrasFalla(larga, t, kAhora).suspender);
    }
    for (const char* c : {"300", "302", "303", "5004"}) {
        QVERIFY(esFallaSuspendible(falla(FaseOperacion::RespuestaExplicita, c)));
        QVERIFY(!esFallaSuspendible(falla(FaseOperacion::DespuesDeEnvio, c)));
    }
}

void TestContratoT007::desenlaceDescargaYVencimientoEstimado()
{
    using namespace politicas;
    const auto d5007 = desenlaceDescarga(falla(FaseOperacion::RespuestaExplicita, "5007"));
    QCOMPARE(d5007.destino, EstadoDescarga::Vencido);
    QCOMPARE(d5007.motivo, std::optional(MotivoVencimiento::PaqueteExpirado));
    QCOMPARE(d5007.origen, std::optional(OrigenVencimiento::Sat));
    FallaOperacion colision = falla(FaseOperacion::Almacenamiento);
    colision.causaAlmacenamiento = CausaAlmacenamiento::ColisionDestino;
    for (const FallaOperacion& f : {falla(FaseOperacion::RespuestaExplicita, "5008"), falla(FaseOperacion::Preparacion),
                                    falla(FaseOperacion::Autenticacion), colision}) {
        QCOMPARE(desenlaceDescarga(f).destino, EstadoDescarga::Error);
    }
    QCOMPARE(vencimientoEstimado(kAhora), kAhora.addSecs(72 * 3600));
    PaquetePersistido p;
    p.vencimientoEstimadoEn = kAhora;
    for (EstadoDescarga e : {EstadoDescarga::Disponible, EstadoDescarga::Descargando, EstadoDescarga::Error}) {
        p.estadoDescarga = e;
        QVERIFY(vencimientoEstimadoAlcanzado(p, kAhora));
        QVERIFY(!vencimientoEstimadoAlcanzado(p, kAhora.addMSecs(-1)));
    }
    for (EstadoDescarga e : {EstadoDescarga::Descargado, EstadoDescarga::Vencido}) {
        p.estadoDescarga = e;
        QVERIFY(!vencimientoEstimadoAlcanzado(p, kAhora.addDays(10)));
    }
}

void TestContratoT007::senalCancelacionEntreHilos()
{
    SenalCancelacion s;
    QVERIFY(!s.solicitada());
    std::atomic<bool> vista{false};
    std::unique_ptr<QThread> hilo(QThread::create([s, &vista]() { vista = s.esperar(std::chrono::seconds(30)); }));
    hilo->start();
    int notificaciones = 0;
    s.alSolicitar([&] { ++notificaciones; });
    const SenalCancelacion copia = s;
    copia.solicitar();
    copia.solicitar(); // idempotente
    QVERIFY(hilo->wait(10000));
    QVERIFY(vista);
    QVERIFY(s.solicitada());
    QCOMPARE(notificaciones, 1);
    s.alSolicitar([&] { ++notificaciones; }); // ya solicitada: inmediata
    QCOMPARE(notificaciones, 2);
    QVERIFY(!SenalCancelacion().esperar(std::chrono::milliseconds(1)));
}

void TestContratoT007::adaptadorNuloNuncaSimulaExito()
{
    OperacionesSatNulo nulo;
    QCOMPARE(nulo.enviar({}).error().fase, FaseOperacion::Preparacion);
    QCOMPARE(nulo.verificar({}).error().fase, FaseOperacion::Preparacion);
    QCOMPARE(nulo.descargar({}).error().fase, FaseOperacion::Preparacion);
    QCOMPARE(nulo.existeArchivoFinal({}).error().fase, FaseOperacion::Preparacion);
    QVERIFY(!nulo.obtenerEstadoCredencial(PerfilId::generar()).esExito());
    QCOMPARE(politicas::destinoEnvio(nulo.enviar({}).error()), EstadoLocal::Creada);
}

void TestContratoT007::fakeOperacionesSatGuionBarreraYCancelacion()
{
    FakeOperacionesSat sat;
    const PerfilId perfil = PerfilId::generar();
    ContextoEnvio ctx;
    ctx.solicitud.id = SolicitudId::generar();

    // Defecto y guion FIFO.
    QVERIFY(sat.enviar(ctx).esExito());
    sat.responderEnvio(FakeOperacionesSat::R<ResultadoEnvio>::exito(envio("5005")));
    sat.responderEnvio(FakeOperacionesSat::R<ResultadoEnvio>::fallo(falla(FaseOperacion::AntesDeEnvio)));
    QCOMPARE(sat.enviar(ctx).valor().codEstatus, u"5005"_s);
    QCOMPARE(sat.enviar(ctx).error().fase, FaseOperacion::AntesDeEnvio);
    sat.fijarCredencial(perfil, EstadoCredencial::Vencida);
    QCOMPARE(sat.obtenerEstadoCredencial(perfil).valor(), EstadoCredencial::Vencida);

    // Barrera desde otro hilo + liberacion.
    sat.bloquearSiguiente(FakeOperacionesSat::Op::Verificar);
    ContextoVerificacion cv;
    cv.solicitudId = SolicitudId::generar();
    bool ok = false;
    std::unique_ptr<QThread> h1(QThread::create([&] { ok = sat.verificar(cv).esExito(); }));
    h1->start();
    QVERIFY(sat.esperarBloqueo());
    QCOMPARE(sat.activas(), 1);
    sat.liberar();
    QVERIFY(h1->wait(10000));
    QVERIFY(ok);

    // Barrera + cancelacion cooperativa: falla cancelada con la fase indicada.
    sat.bloquearSiguiente(FakeOperacionesSat::Op::Descargar, FaseOperacion::Almacenamiento);
    ContextoDescarga cd;
    cd.paqueteId = u"p1"_s;
    std::optional<FallaOperacion> f;
    std::unique_ptr<QThread> h2(QThread::create([&] {
        auto r = sat.descargar(cd);
        if (!r.esExito()) {
            f = r.error();
        }
    }));
    h2->start();
    QVERIFY(sat.esperarBloqueo());
    cd.cancelacion.solicitar();
    QVERIFY(h2->wait(10000));
    QVERIFY(f && f->cancelada);
    QCOMPARE(f->fase, FaseOperacion::Almacenamiento);

    QCOMPARE(sat.concurrenciaMaxima(), 1);
    QCOMPARE(sat.llamadas().constLast(), u"descargar:p1"_s);
    QCOMPARE(sat.resultados().constLast(), u"descargar:falla:Almacenamiento:cancelada"_s);
    QVERIFY(sat.hilos().size() >= 2);
}

void TestContratoT007::fakeProgramadorAvanzaSinTiempoReal()
{
    fakes::FakeReloj reloj(kAhora);
    fakes::FakeProgramador p(reloj);
    QStringList orden;
    p.programar(kAhora.addSecs(600), [&] { orden.append(u"b"_s); });
    p.programar(kAhora.addSecs(60), [&] {
        orden.append(u"a"_s);
        p.programar(reloj.ahora().addSecs(60), [&] { orden.append(u"a2"_s); });
    });
    const auto cancelado = p.programar(kAhora.addSecs(300), [&] { orden.append(u"x"_s); });
    p.cancelar(cancelado);
    QCOMPARE(p.avanzar(std::chrono::minutes(5)), 2);
    QCOMPARE(orden, (QStringList{u"a"_s, u"a2"_s}));
    QCOMPARE(reloj.ahora(), kAhora.addSecs(300));
    QCOMPARE(p.proximo(), std::optional(kAhora.addSecs(600)));
    p.avanzar(std::chrono::minutes(30));
    QCOMPARE(orden, (QStringList{u"a"_s, u"a2"_s, u"b"_s}));
    QCOMPARE(p.pendientes(), 0);
}

void TestContratoT007::repositorioEnvioYEliminacion()
{
    Entorno e;
    SolicitudPersistida s = solicitud(EstadoLocal::Creada, e.perfil);
    e.almacen.solicitudes.append(s);
    QCOMPARE(e.repo.marcarEnviando(s.id, kAhora).error().tipo, ErrorPersistencia::Tipo::Transaccion);
    QVERIFY(e.enTx([&] { return e.repo.marcarEnviando(s.id, kAhora); }).valor());
    QVERIFY(!e.enTx([&] { return e.repo.marcarEnviando(s.id, kAhora); }).valor()); // ya no esta Creada

    AplicacionEnvio creada;
    creada.solicitudId = s.id;
    creada.destino = EstadoLocal::Creada;
    creada.ultimoError = u"llavero bloqueado"_s;
    QVERIFY(e.enTx([&] { return e.repo.aplicarEnvio(creada); }).valor());
    const SolicitudPersistida& r = e.almacen.solicitudes.first();
    QCOMPARE(r.estadoLocal, EstadoLocal::Creada);
    QVERIFY(!r.envioIniciadoEn && !r.codEstatusSolicitud && !r.mensajeSolicitudSat);

    // Eliminada mientras la operacion estaba en curso: nada se aplica.
    QVERIFY(e.enTx([&] { return e.repo.marcarEnviando(s.id, kAhora); }).valor());
    e.almacen.solicitudes.first().eliminadoEn = kAhora;
    AplicacionEnvio enviada;
    enviada.solicitudId = s.id;
    enviada.destino = EstadoLocal::Enviada;
    enviada.idSolicitudSat = u"SAT-9"_s;
    enviada.codEstatus = u"5000"_s;
    QVERIFY(!e.enTx([&] { return e.repo.aplicarEnvio(enviada); }).valor());
    QCOMPARE(e.almacen.solicitudes.first().estadoLocal, EstadoLocal::Enviando);
    QVERIFY(!e.repo.listarInterrumpidos().valor().solicitudesEnviando.contains(s.id));
}

void TestContratoT007::repositorioVerificacionYPaquetes()
{
    Entorno e;
    SolicitudPersistida s = solicitud(EstadoLocal::Enviada, e.perfil);
    s.siguienteVerificacionEn = kAhora;
    e.almacen.solicitudes.append(s);
    e.almacen.solicitudes.append(solicitud(EstadoLocal::Creada, e.perfil)); // nunca se selecciona (D4)
    QCOMPARE(e.repo.listarVerificacionesDebidas({e.perfil}, kAhora, 10).valor().size(), 1);
    QVERIFY(e.repo.listarVerificacionesDebidas({e.perfil}, kAhora.addMSecs(-1), 10).valor().isEmpty());
    QVERIFY(e.repo.listarVerificacionesDebidas({PerfilId::generar()}, kAhora, 10).valor().isEmpty());
    QCOMPARE(e.repo.listarPerfilesConTrabajo(kAhora).valor(), QList<PerfilId>{e.perfil});

    AplicacionVerificacion v;
    v.solicitudId = s.id;
    v.verificadaEn = kAhora;
    v.estadoSolicitudSat = EstadoSolicitudSat::Terminada;
    v.paquetesNuevos = {PaqueteNuevo{satcfdi::uuid::generarCanonico(), u"P_01"_s, kAhora, politicas::vencimientoEstimado(kAhora)}};
    auto r1 = e.enTx([&] { return e.repo.aplicarVerificacion(v); });
    QVERIFY(r1.valor().aplicada);
    QCOMPARE(r1.valor().paquetesInsertados.size(), 1);
    // Segunda observacion del mismo IdPaquete: no se duplica ni se desplaza (D10).
    v.verificadaEn = kAhora.addSecs(3600);
    v.paquetesNuevos = {PaqueteNuevo{satcfdi::uuid::generarCanonico(), u"P_01"_s, v.verificadaEn,
                                     politicas::vencimientoEstimado(v.verificadaEn)}};
    QVERIFY(e.enTx([&] { return e.repo.aplicarVerificacion(v); }).valor().paquetesInsertados.isEmpty());
    QCOMPARE(e.almacen.paquetes.size(), 1);
    QCOMPARE(e.almacen.paquetes.first().vencimientoEstimadoEn, std::optional(politicas::vencimientoEstimado(kAhora)));
    QCOMPARE(e.repo.listarDescargasAutomaticas({e.perfil}, 10).valor().size(), 1);

    // Falla y racha.
    AplicacionFallaVerificacion fv;
    fv.solicitudId = s.id;
    fv.falladaEn = kAhora;
    fv.ultimoError = u"rechazo"_s;
    fv.claveFalla = u"RespuestaExplicita:300"_s;
    fv.fallasIguales = 2; // siguienteVerificacionEn nullopt: suspendida
    QVERIFY(e.enTx([&] { return e.repo.aplicarFallaVerificacion(fv); }).valor());
    auto racha = e.repo.leerRachaVerificacion(s.id).valor();
    QVERIFY(racha);
    QCOMPARE(racha->ultimaClaveFalla, std::optional(u"RespuestaExplicita:300"_s));
    QCOMPARE(racha->fallasIguales, 2);
    QVERIFY(e.repo.listarVerificacionesDebidas({e.perfil}, kAhora.addDays(1), 10).valor().isEmpty()); // suspendida
    // Vencida: paquetes no descargados -> Vencido SAT.
    v.estadoSolicitudSat = EstadoSolicitudSat::Vencida;
    v.vencerNoDescargados = true;
    v.paquetesNuevos.clear();
    QCOMPARE(e.enTx([&] { return e.repo.aplicarVerificacion(v); }).valor().paquetesVencidos.size(), 1);
    QCOMPARE(e.almacen.paquetes.first().origenVencimiento, std::optional(OrigenVencimiento::Sat));
    QVERIFY(!e.repo.leerRachaVerificacion(s.id).valor()->ultimaClaveFalla); // exito reinicia la racha
}

void TestContratoT007::repositorioDescargaYVencimiento()
{
    Entorno e;
    SolicitudPersistida s = solicitud(EstadoLocal::Enviada, e.perfil);
    s.estadoSolicitudSat = EstadoSolicitudSat::Terminada;
    e.almacen.solicitudes.append(s);
    PaquetePersistido p;
    p.id = satcfdi::uuid::generarCanonico();
    p.solicitudMasivaId = s.id;
    p.idPaqueteSat = u"P_01"_s;
    p.estadoDescarga = EstadoDescarga::Error;
    p.disponibleEn = kAhora;
    p.vencimientoEstimadoEn = kAhora.addSecs(72 * 3600);
    e.almacen.paquetes.append(p);

    QVERIFY(!e.enTx([&] { return e.repo.marcarDescargando(p.id, kAhora, false); }).valor()); // Error: solo manual
    QVERIFY(e.enTx([&] { return e.repo.marcarDescargando(p.id, kAhora, true); }).valor());
    QCOMPARE(e.repo.listarInterrumpidos().valor().paquetesDescargando.size(), 1);
    AplicacionDescarga d;
    d.paqueteId = p.id;
    d.destino = EstadoDescarga::Descargado;
    d.aplicadaEn = kAhora;
    d.rutaFinal = u"paquetes/x.zip"_s;
    QVERIFY(e.enTx([&] { return e.repo.aplicarDescarga(d); }).valor());
    QCOMPARE(e.almacen.paquetes.first().estadoDescarga, EstadoDescarga::Descargado);
    QVERIFY(!e.enTx([&] { return e.repo.aplicarDescarga(d); }).valor()); // ya no esta Descargando

    PaquetePersistido q = p;
    q.id = satcfdi::uuid::generarCanonico();
    q.idPaqueteSat = u"P_02"_s;
    q.estadoDescarga = EstadoDescarga::Disponible;
    e.almacen.paquetes.append(q);
    const QDateTime vence = *q.vencimientoEstimadoEn;
    QVERIFY(e.repo.listarVencimientosEstimados(vence.addMSecs(-1), 10).valor().isEmpty());
    QCOMPARE(e.repo.listarVencimientosEstimados(vence, 10).valor().size(), 1);
    QVERIFY(e.enTx([&] { return e.repo.vencerPaqueteEstimado(q.id, vence); }).valor());
    QCOMPARE(e.almacen.paquetes.last().origenVencimiento, std::optional(OrigenVencimiento::EstimacionLocal));
}

void TestContratoT007::repositorioIntencionesCondicionales()
{
    Entorno e;
    SolicitudPersistida s = solicitud(EstadoLocal::Enviada, e.perfil);
    e.almacen.solicitudes.append(s);
    QVERIFY(e.enTx([&] { return e.repo.registrarIntencion(s.id, TipoIntencion::Verificacion, kAhora); }).valor());
    QVERIFY(e.enTx([&] { return e.repo.registrarIntencion(s.id, TipoIntencion::Descarga, kAhora); }).valor());
    auto intenciones = e.repo.listarIntencionesPendientes({e.perfil}, 10).valor();
    QCOMPARE(intenciones.size(), 1);
    QVERIFY(intenciones.first().verificacionPendiente && intenciones.first().descargaPendiente);
    const QDateTime capturada = intenciones.first().accionPendienteEn;

    // Se registra otra intencion mientras se procesa la primera: no se pierde (D13).
    const QDateTime despues = kAhora.addSecs(5);
    QVERIFY(e.enTx([&] { return e.repo.registrarIntencion(s.id, TipoIntencion::Verificacion, despues); }).valor());
    QVERIFY(!e.enTx([&] { return e.repo.consumirIntencion(s.id, TipoIntencion::Verificacion, capturada); }).valor());
    QVERIFY(e.almacen.solicitudes.first().verificacionPendiente);

    // Con el valor vigente: verificar y luego descargar.
    QVERIFY(e.enTx([&] { return e.repo.consumirIntencion(s.id, TipoIntencion::Verificacion, despues); }).valor());
    QVERIFY(e.almacen.solicitudes.first().accionPendienteEn); // queda la descarga
    QVERIFY(e.enTx([&] { return e.repo.consumirIntencion(s.id, TipoIntencion::Descarga, despues); }).valor());
    QVERIFY(!e.almacen.solicitudes.first().accionPendienteEn);
    QVERIFY(e.repo.listarIntencionesPendientes({e.perfil}, 10).valor().isEmpty());
}
