#include "TestWorkerLocal.h"

#include "EntornoOperaciones.h"

#include "domain/operaciones/PoliticasOperacion.h"

#include <QSignalSpy>

#include <algorithm>

using namespace satcfdi;
using namespace pruebasT007;
using namespace Qt::StringLiterals;
using Op = fakes::FakeOperacionesSat::Op;

namespace {

struct EntornoWorker : Entorno {
    fakes::FakeConfiguracionAppService configuracion;
    std::unique_ptr<WorkerLocal> worker;

    explicit EntornoWorker(bool pausado = false)
    {
        configuracion.autoConfirmar = true;
        configuracion.confirmada.monitoreoPausado = pausado;
        worker = std::make_unique<WorkerLocal>(*ejecutor, configuracion, programador, reloj.funcion());
    }

    ~EntornoWorker()
    {
        worker.reset();
    }

    // Un ciclo termino cuando el worker dejo programado el siguiente (al
    // empezar un ciclo cancela el programado; al terminar programa uno).
    bool esperarFinCiclo()
    {
        return QTest::qWaitFor([&] { return programador.pendientes() == 1; }, 30000) && drenar();
    }

    // Ejecuta los ciclos inmediatos (trabajo hecho -> reseleccion) hasta que
    // el siguiente quede en el futuro. Sin tiempo real.
    bool estabilizar()
    {
        for (int i = 0; i < 20; ++i) {
            if (!esperarFinCiclo()) {
                return false;
            }
            const auto proximo = programador.proximo();
            if (!proximo || *proximo > reloj.ahora()) {
                return drenar();
            }
            programador.avanzarHasta(reloj.ahora());
        }
        return false;
    }

    bool iniciar()
    {
        worker->iniciar();
        return estabilizar();
    }

    void fijarPausa(bool pausado) { configuracion.actualizarMonitoreoPausado(pausado); }

    QStringList llamadasSat(const QString& prefijo) const { return sat.llamadas().filter(prefijo); }
};

} // namespace

void TestWorkerLocal::recuperacionAntesDeCualquierCiclo()
{
    EntornoWorker e;
    const SolicitudId enviando = e.sembrar(EstadoLocal::Enviando).id;
    SolicitudPersistida& debida = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso);
    debida.siguienteVerificacionEn = kInicio;
    QVERIFY(e.iniciar());
    const QStringList ev = e.almacen.eventos;
    QVERIFY(ev.indexOf(u"listarInterrumpidos"_s) >= 0);
    QVERIFY(ev.indexOf(u"listarInterrumpidos"_s) < ev.indexOf(u"listarVencimientosEstimados"_s));
    QCOMPARE(e.solicitud(enviando)->estadoLocal, EstadoLocal::EnvioIncierto);
    QCOMPARE(e.logsDe(TipoEventoLog::EnvioIncierto, enviando), 1);
    QCOMPARE(e.llamadasSat(u"verificar:"_s).size(), 1);
}

void TestWorkerLocal::verificacionDebidaSeEncolaUnaSolaVez()
{
    EntornoWorker e;
    SolicitudPersistida& s = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso);
    s.siguienteVerificacionEn = kInicio.addSecs(600);
    const SolicitudId id = s.id;
    QVERIFY(e.iniciar());
    QVERIFY(e.llamadasSat(u"verificar:"_s).isEmpty());
    QCOMPARE(e.programador.proximo(), std::optional(kInicio.addSecs(600))); // agenda por solicitud
    // 1 ms antes: nada.
    e.programador.avanzarHasta(kInicio.addSecs(600).addMSecs(-1));
    QVERIFY(e.llamadasSat(u"verificar:"_s).isEmpty());
    // En el instante debido: una sola verificacion.
    e.programador.avanzarHasta(kInicio.addSecs(600));
    QVERIFY(e.estabilizar());
    QCOMPARE(e.llamadasSat(u"verificar:"_s), QStringList{u"verificar:"_s + id.texto()});
    // Sin cambio (EnProceso -> Aceptada es cambio): la siguiente queda a 10 min.
    QCOMPARE(e.solicitud(id)->siguienteVerificacionEn, std::optional(kInicio.addSecs(1200)));
    QCOMPARE(e.programador.proximo(), std::optional(kInicio.addSecs(1200)));
}

void TestWorkerLocal::workerNoSeleccionaCreadaYDescargaAutomatica()
{
    EntornoWorker e;
    e.sembrar(EstadoLocal::Creada);
    const SolicitudId terminada = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const QString disponible = e.sembrarPaquete(terminada, EstadoDescarga::Disponible).id;
    const QString enError = e.sembrarPaquete(terminada, EstadoDescarga::Error).id;
    QVERIFY(e.iniciar());
    QVERIFY(e.llamadasSat(u"enviar:"_s).isEmpty());
    QCOMPARE(e.llamadasSat(u"descargar:"_s), QStringList{u"descargar:"_s + disponible}); // Error: solo manual
    QCOMPARE(e.paquete(disponible)->estadoDescarga, EstadoDescarga::Descargado);
    QCOMPARE(e.paquete(enError)->estadoDescarga, EstadoDescarga::Error);
}

void TestWorkerLocal::vencimientoEstimadoAunEnPausa()
{
    EntornoWorker e(true);
    const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const QString pid = e.sembrarPaquete(sid, EstadoDescarga::Disponible, kInicio.addSecs(3600)).id;
    QVERIFY(e.iniciar());
    QVERIFY(e.programador.proximo() <= std::optional(kInicio.addSecs(3600))); // latido o vencimiento
    e.programador.avanzarHasta(kInicio.addSecs(3600));
    QVERIFY(e.estabilizar());
    QCOMPARE(e.paquete(pid)->estadoDescarga, EstadoDescarga::Vencido);
    QCOMPARE(e.paquete(pid)->origenVencimiento, std::optional(OrigenVencimiento::EstimacionLocal));
    QVERIFY(e.sat.llamadas().isEmpty());
}

void TestWorkerLocal::pausaNoLlamaAlPuerto()
{
    EntornoWorker e(true);
    SolicitudPersistida& s = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso);
    s.siguienteVerificacionEn = kInicio;
    const SolicitudId terminada = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    e.sembrarPaquete(terminada, EstadoDescarga::Disponible);
    QVERIFY(e.iniciar());
    e.programador.avanzar(std::chrono::hours(5));
    QVERIFY(e.estabilizar());
    QVERIFY(e.sat.llamadas().isEmpty()); // ni credenciales, ni verificar, ni descargar
    QCOMPARE(e.worker->instantanea().estado, EstadoWorker::Pausado);
}

void TestWorkerLocal::pausaRegistraIntencionesYReanudaEnOrden()
{
    EntornoWorker e(true);
    const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const QString pid = e.sembrarPaquete(sid, EstadoDescarga::Error).id;
    QVERIFY(e.iniciar());
    e.worker->verificarAhora(sid);
    e.worker->reintentarDescarga(sid);
    QVERIFY(e.drenar());
    const SolicitudPersistida* s = e.solicitud(sid);
    QVERIFY(s->verificacionPendiente && s->descargaPendiente && s->accionPendienteEn);
    QVERIFY(e.sat.llamadas().isEmpty());
    QVERIFY(QTest::qWaitFor([&] { return e.worker->instantanea().pendientes == 1; }, 30000));

    e.sat.responderVerificacion(fakes::FakeOperacionesSat::R<ResultadoVerificacion>::exito(
        verificacion(EstadoSolicitudSat::Terminada, {e.paquete(pid)->idPaqueteSat})));
    e.fijarPausa(false);
    QVERIFY(e.estabilizar());
    QCOMPARE(e.sat.llamadas().filter(QRegularExpression(u"^(verificar|descargar):"_s)),
             (QStringList{u"verificar:"_s + sid.texto(), u"descargar:"_s + pid}));
    QVERIFY(!e.solicitud(sid)->verificacionPendiente);
    QVERIFY(!e.solicitud(sid)->descargaPendiente);
    QVERIFY(!e.solicitud(sid)->accionPendienteEn);
    QCOMPARE(e.paquete(pid)->estadoDescarga, EstadoDescarga::Descargado);
    QCOMPARE(e.logsDe(TipoEventoLog::AccionPendienteRegistrada, sid), 2);
    QVERIFY(QTest::qWaitFor([&] { return e.worker->instantanea().pendientes == 0; }, 30000));
}

void TestWorkerLocal::intencionQueYaNoAplicaSeDescartaConLog()
{
    EntornoWorker e(true);
    const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    e.sembrarPaquete(sid, EstadoDescarga::Descargado);
    QVERIFY(e.iniciar());
    e.worker->reintentarDescarga(sid);
    QVERIFY(e.drenar());
    QVERIFY(e.solicitud(sid)->descargaPendiente);
    e.fijarPausa(false);
    QVERIFY(e.estabilizar());
    QVERIFY(e.llamadasSat(u"descargar:"_s).isEmpty());
    QVERIFY(!e.solicitud(sid)->descargaPendiente);
    QCOMPARE(e.logsDe(TipoEventoLog::AccionPendienteDescartada, sid), 1);
}

void TestWorkerLocal::intencionNuevaDuranteOtraNoSePierde()
{
    EntornoWorker e(true);
    const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso).id;
    QVERIFY(e.iniciar());
    e.worker->verificarAhora(sid);
    QVERIFY(e.drenar());
    e.sat.bloquearSiguiente(Op::Verificar);
    e.fijarPausa(false);
    // El ciclo necesita el event loop grafico: se espera procesando eventos
    // (esperarBloqueo() bloquearia el hilo grafico).
    QVERIFY(QTest::qWaitFor([&] { return e.sat.activas() == 1; }, 30000));
    // Mientras se procesa la primera, el usuario pausa y pide otra.
    e.fijarPausa(true);
    e.reloj.fijar(kInicio.addSecs(5));
    e.worker->verificarAhora(sid);
    e.sat.liberar();
    QVERIFY(e.estabilizar());
    QVERIFY(e.solicitud(sid)->verificacionPendiente); // la nueva intencion sigue
    QCOMPARE(e.solicitud(sid)->accionPendienteEn, std::optional(kInicio.addSecs(5)));
    QCOMPARE(e.llamadasSat(u"verificar:"_s).size(), 1);
}

void TestWorkerLocal::gateDeCredencialUnaVezPorPerfil()
{
    EntornoWorker e;
    e.sat.fijarCredencial(e.perfil, EstadoCredencial::Vencida);
    for (int i = 0; i < 3; ++i) {
        e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso).siguienteVerificacionEn = kInicio;
    }
    QVERIFY(e.iniciar());
    QCOMPARE(e.llamadasSat(u"credencial:"_s), QStringList{u"credencial:"_s + e.perfil.texto()});
    QVERIFY(e.llamadasSat(u"verificar:"_s).isEmpty());
}

void TestWorkerLocal::enviarSeEjecutaAunEnPausa()
{
    EntornoWorker e(true);
    const SolicitudId id = e.sembrar(EstadoLocal::Creada).id;
    QVERIFY(e.iniciar());
    e.worker->enviar(id);
    QVERIFY(QTest::qWaitFor([&] { return e.solicitud(id)->estadoLocal == EstadoLocal::Enviada; }, 30000));
    QVERIFY(e.estabilizar());
    QCOMPARE(e.llamadasSat(u"enviar:"_s).size(), 1);
    // La verificacion espera a que se reanude.
    e.programador.avanzar(std::chrono::minutes(15));
    QVERIFY(e.estabilizar());
    QVERIFY(e.llamadasSat(u"verificar:"_s).isEmpty());
}

void TestWorkerLocal::manualAntesQueAutomaticoEnUnCiclo()
{
    EntornoWorker e;
    const SolicitudId automatica = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso).id;
    e.almacen.solicitudes.last().siguienteVerificacionEn = kInicio.addSecs(600);
    const SolicitudId manual = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso).id;
    QVERIFY(e.iniciar());
    // Un envio manual bloqueado ocupa el ejecutor mientras llegan el ciclo y
    // una verificacion manual.
    const SolicitudId creada = e.sembrar(EstadoLocal::Creada).id;
    e.sat.bloquearSiguiente(Op::Enviar);
    e.worker->enviar(creada);
    QVERIFY(e.sat.esperarBloqueo());
    e.programador.avanzarHasta(kInicio.addSecs(600)); // ciclo: verificacion automatica
    e.worker->verificarAhora(manual);
    e.sat.liberar();
    QVERIFY(e.estabilizar());
    const QStringList verifs = e.llamadasSat(u"verificar:"_s);
    QCOMPARE(verifs.size(), 2);
    QCOMPARE(verifs.first(), u"verificar:"_s + manual.texto());
    QCOMPARE(verifs.last(), u"verificar:"_s + automatica.texto());
    QCOMPARE(e.sat.concurrenciaMaxima(), 1);
}

void TestWorkerLocal::instantaneasDeEstadoYPendientes()
{
    EntornoWorker e;
    const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso).id;
    QSignalSpy spy(e.worker.get(), &WorkerLocal::instantaneaCambiada);
    QCOMPARE(e.worker->instantanea().estado, EstadoWorker::Detenido);
    QVERIFY(e.iniciar());
    QCOMPARE(e.worker->instantanea().estado, EstadoWorker::ActivoEnEspera);
    // Ejecutando(tipo) mientras una operacion esta en curso.
    e.sat.bloquearSiguiente(Op::Verificar);
    e.worker->verificarAhora(sid);
    QVERIFY(e.sat.esperarBloqueo());
    QVERIFY(QTest::qWaitFor([&] { return e.worker->instantanea().estado == EstadoWorker::Ejecutando; }, 30000));
    QCOMPARE(e.worker->instantanea().tipo, std::optional(TipoOperacion::Verificacion));
    e.sat.liberar();
    QVERIFY(QTest::qWaitFor([&] { return e.worker->instantanea().estado == EstadoWorker::ActivoEnEspera; }, 30000));
    // Pausado con pendientes.
    e.fijarPausa(true);
    QCOMPARE(e.worker->instantanea().estado, EstadoWorker::Pausado);
    e.worker->verificarAhora(sid);
    QVERIFY(QTest::qWaitFor([&] { return e.worker->instantanea().pendientes == 1; }, 30000));
    // Deteniendo y Detenido.
    e.worker->detener();
    QCOMPARE(e.worker->instantanea().estado, EstadoWorker::Deteniendo);
    QVERIFY(esperarVoid(e.ejecutor->detener()));
    QVERIFY(QTest::qWaitFor([&] { return e.worker->instantanea().estado == EstadoWorker::Detenido; }, 30000));
    QSet<int> vistos;
    for (const auto& args : spy) {
        vistos.insert(int(args.at(0).value<InstantaneaWorker>().estado));
    }
    for (EstadoWorker esperado : {EstadoWorker::ActivoEnEspera, EstadoWorker::Ejecutando, EstadoWorker::Pausado,
                                  EstadoWorker::Deteniendo, EstadoWorker::Detenido}) {
        QVERIFY2(vistos.contains(int(esperado)), qPrintable(claveEstable(esperado)));
    }
}

// --- T014.2: reintento por paquete ---------------------------------------------

void TestWorkerLocal::reintentoPorPaqueteActivoSoloEsePaquete()
{
    EntornoWorker e;
    const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const QString p1 = e.sembrarPaquete(sid, EstadoDescarga::Error).id;
    const QString p2 = e.sembrarPaquete(sid, EstadoDescarga::Error).id;
    const QString p3 = e.sembrarPaquete(sid, EstadoDescarga::Disponible).id;
    Q_UNUSED(p3); // Disponible + Terminada: se descarga automatico en iniciar()
    QVERIFY(e.iniciar());
    const qsizetype antes = e.llamadasSat(u"descargar:"_s).size();
    e.worker->reintentarDescargaPaquete(sid, e.paquete(p1)->idPaqueteSat);
    QVERIFY(e.estabilizar());
    QCOMPARE(e.llamadasSat(u"descargar:"_s).mid(antes), QStringList{u"descargar:"_s + p1});
    QCOMPARE(e.paquete(p1)->estadoDescarga, EstadoDescarga::Descargado);
    QCOMPARE(e.paquete(p2)->estadoDescarga, EstadoDescarga::Error); // los demas no cambian
    QVERIFY(!e.paquete(p2)->reintentoPendienteEn);

    // Prioridad Manual y origen Usuario en el ejecutor.
    const QString p4 = e.sembrarPaquete(sid, EstadoDescarga::Error).id;
    const auto r = esperar(e.ejecutor->reintentarDescargaPaquete(sid, e.paquete(p4)->idPaqueteSat));
    QVERIFY(r);
    QCOMPARE(r->origen, OrigenLog::Usuario);
    QCOMPARE(r->desenlace, ResultadoOperacion::Desenlace::Aplicada);
    QCOMPARE(r->paqueteId, std::optional<QString>(p4));
}

void TestWorkerLocal::reintentoPorPaquete5008SinTrafico()
{
    for (bool pausado : {false, true}) {
        EntornoWorker e(pausado);
        const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
        PaquetePersistido& p = e.sembrarPaquete(sid, EstadoDescarga::Error);
        p.codigoDescargaSat = u"5008"_s;
        const QString pid = p.id;
        const QString descargado = e.sembrarPaquete(sid, EstadoDescarga::Descargado).id;
        QVERIFY(e.iniciar());
        e.worker->reintentarDescargaPaquete(sid, e.paquete(pid)->idPaqueteSat);
        e.worker->reintentarDescargaPaquete(sid, e.paquete(descargado)->idPaqueteSat);
        e.worker->reintentarDescargaPaquete(sid, u"NO_EXISTE"_s);
        QVERIFY(e.estabilizar());
        QVERIFY(e.llamadasSat(u"descargar:"_s).isEmpty());
        QCOMPARE(e.paquete(pid)->estadoDescarga, EstadoDescarga::Error);
        QVERIFY(!e.paquete(pid)->reintentoPendienteEn);
        QCOMPARE(e.logsDe(TipoEventoLog::AccionPendienteRegistrada, sid), 0);
        QCOMPARE(e.logsDe(TipoEventoLog::DescargaIniciada, sid), 0);
    }
}

void TestWorkerLocal::reintentoPorPaqueteEnPausaSobreviveReinicio()
{
    EntornoWorker e(true);
    const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const QString p1 = e.sembrarPaquete(sid, EstadoDescarga::Error).id;
    const QString p2 = e.sembrarPaquete(sid, EstadoDescarga::Error).id;
    QVERIFY(e.iniciar());
    e.worker->reintentarDescargaPaquete(sid, e.paquete(p1)->idPaqueteSat);
    QVERIFY(e.drenar());
    QCOMPARE(e.paquete(p1)->reintentoPendienteEn, std::optional(kInicio)); // solo ese paquete
    QVERIFY(!e.paquete(p2)->reintentoPendienteEn);
    QVERIFY(!e.solicitud(sid)->descargaPendiente);
    QVERIFY(e.sat.llamadas().isEmpty());
    QCOMPARE(e.logsDe(TipoEventoLog::AccionPendienteRegistrada, sid), 1);
    QVERIFY(QTest::qWaitFor([&] { return e.worker->instantanea().pendientes == 1; }, 30000));

    // "Reinicio": nuevo ejecutor y worker sobre la misma base (la intencion
    // esta persistida en el paquete).
    e.worker.reset();
    e.ejecutor.reset();
    e.crearEjecutor();
    e.worker = std::make_unique<WorkerLocal>(*e.ejecutor, e.configuracion, e.programador, e.reloj.funcion());
    QVERIFY(e.iniciar());
    QVERIFY(e.sat.llamadas().filter(u"descargar:"_s).isEmpty()); // sigue en pausa
    QCOMPARE(e.paquete(p1)->reintentoPendienteEn, std::optional(kInicio));

    e.fijarPausa(false);
    QVERIFY(e.estabilizar());
    QCOMPARE(e.llamadasSat(u"descargar:"_s), QStringList{u"descargar:"_s + p1});
    QCOMPARE(e.paquete(p1)->estadoDescarga, EstadoDescarga::Descargado);
    QVERIFY(!e.paquete(p1)->reintentoPendienteEn);
    QCOMPARE(e.paquete(p2)->estadoDescarga, EstadoDescarga::Error);
    QVERIFY(QTest::qWaitFor([&] { return e.worker->instantanea().pendientes == 0; }, 30000));
}

void TestWorkerLocal::intencionPaqueteObsoletaSeDescartaConLog()
{
    EntornoWorker e(true);
    const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const QString pid = e.sembrarPaquete(sid, EstadoDescarga::Error).id;
    QVERIFY(e.iniciar());
    e.worker->reintentarDescargaPaquete(sid, e.paquete(pid)->idPaqueteSat);
    QVERIFY(e.drenar());
    QVERIFY(e.paquete(pid)->reintentoPendienteEn);
    // Ya no aplica al reanudar (p. ej. vencio).
    for (PaquetePersistido& p : e.almacen.paquetes) {
        if (p.id == pid) {
            p.estadoDescarga = EstadoDescarga::Vencido;
        }
    }
    e.fijarPausa(false);
    QVERIFY(e.estabilizar());
    QVERIFY(e.llamadasSat(u"descargar:"_s).isEmpty());
    QVERIFY(!e.paquete(pid)->reintentoPendienteEn);
    QCOMPARE(e.logsDe(TipoEventoLog::AccionPendienteDescartada, sid), 1);
    bool conPaquete = false;
    for (const LogPersistido& l : e.almacen.logs) {
        conPaquete = conPaquete
                     || (l.tipoEvento == TipoEventoLog::AccionPendienteDescartada
                         && l.payloadResumenJson.value_or(QString()).contains(e.paquete(pid)->idPaqueteSat));
    }
    QVERIFY(conPaquete);
}

void TestWorkerLocal::intencionesSolicitudYPaqueteEnOrdenSinDuplicar()
{
    EntornoWorker e(true);
    // A: intencion de verificacion (t0); B: reintento de la solicitud con dos
    // paquetes (t1); B/p1: reintento por paquete (t2) que coincide con el de
    // la solicitud; C: verificacion despues (t3). Orden global por instante.
    const SolicitudId a = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso).id;
    const SolicitudId b = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const SolicitudId c = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso).id;
    const QString p1 = e.sembrarPaquete(b, EstadoDescarga::Error).id;
    const QString p2 = e.sembrarPaquete(b, EstadoDescarga::Error).id;
    const QString otro = e.sembrarPaquete(c, EstadoDescarga::Error).id;
    QVERIFY(e.iniciar());
    e.worker->verificarAhora(a);
    QVERIFY(e.drenar());
    e.reloj.fijar(kInicio.addSecs(1));
    e.worker->reintentarDescarga(b);
    QVERIFY(e.drenar());
    e.reloj.fijar(kInicio.addSecs(2));
    e.worker->reintentarDescargaPaquete(b, e.paquete(p1)->idPaqueteSat);
    e.worker->reintentarDescargaPaquete(c, e.paquete(otro)->idPaqueteSat);
    QVERIFY(e.drenar());
    e.reloj.fijar(kInicio.addSecs(3));
    e.worker->verificarAhora(c);
    QVERIFY(e.drenar());
    QVERIFY(QTest::qWaitFor([&] { return e.worker->instantanea().pendientes == 5; }, 30000));

    e.fijarPausa(false);
    QVERIFY(e.estabilizar());
    QCOMPARE(e.sat.llamadas().filter(QRegularExpression(u"^(verificar|descargar):"_s)),
             (QStringList{u"verificar:"_s + a.texto(), u"descargar:"_s + p1, u"descargar:"_s + p2,
                          u"descargar:"_s + otro, u"verificar:"_s + c.texto()}));
    // p1 no se descargo dos veces: la intencion por paquete se descarto con log.
    QCOMPARE(e.paquete(p1)->estadoDescarga, EstadoDescarga::Descargado);
    QVERIFY(!e.paquete(p1)->reintentoPendienteEn);
    QCOMPARE(e.logsDe(TipoEventoLog::AccionPendienteDescartada, b), 1);
    QVERIFY(!e.solicitud(b)->descargaPendiente);
    QVERIFY(QTest::qWaitFor([&] { return e.worker->instantanea().pendientes == 0; }, 30000));
}

// D3: la recuperacion al arrancar resuelve el Descargando interrumpido y
// conserva la intencion por paquete, que se ejecuta al reanudar.
void TestWorkerLocal::recuperacionConservaIntencionPorPaquete()
{
    EntornoWorker e(true);
    const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    PaquetePersistido& p = e.sembrarPaquete(sid, EstadoDescarga::Descargando);
    p.reintentoPendienteEn = kInicio;
    const QString pid = p.id;
    QVERIFY(e.iniciar()); // recuperar(): sin archivo final -> Disponible
    QCOMPARE(e.paquete(pid)->estadoDescarga, EstadoDescarga::Disponible);
    QCOMPARE(e.paquete(pid)->reintentoPendienteEn, std::optional(kInicio));
    e.fijarPausa(false);
    QVERIFY(e.estabilizar());
    QCOMPARE(e.llamadasSat(u"descargar:"_s), QStringList{u"descargar:"_s + pid}); // una sola vez
    QCOMPARE(e.paquete(pid)->estadoDescarga, EstadoDescarga::Descargado);
    QVERIFY(!e.paquete(pid)->reintentoPendienteEn);
}

// T007 D5 / ADR 0007 (enmienda T014.2): sin solicitud o paquete visibles no se
// escriben logs; la intencion por paquete se limpia sin log y sin trafico.
void TestWorkerLocal::intencionPaqueteDeSolicitudEliminadaSeLimpiaSinLog()
{
    EntornoWorker e(true);
    const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const QString pid = e.sembrarPaquete(sid, EstadoDescarga::Error).id;
    QVERIFY(e.iniciar());
    e.worker->reintentarDescargaPaquete(sid, e.paquete(pid)->idPaqueteSat);
    QVERIFY(e.drenar());
    QVERIFY(e.paquete(pid)->reintentoPendienteEn);
    const qsizetype logsAntes = e.almacen.logs.size();

    // Eliminacion de la solicitud (marca la solicitud y sus paquetes).
    for (SolicitudPersistida& s : e.almacen.solicitudes) {
        if (s.id == sid) {
            s.eliminadoEn = kInicio;
        }
    }
    QVERIFY(e.paquetesRepo.marcarEliminadosPorSolicitud(sid, kInicio));
    e.fijarPausa(false);
    QVERIFY(e.estabilizar());
    const auto& paq = *std::find_if(e.almacen.paquetes.cbegin(), e.almacen.paquetes.cend(),
                                    [&](const PaquetePersistido& p) { return p.id == pid; });
    QVERIFY(!paq.reintentoPendienteEn);
    QCOMPARE(e.almacen.logs.size(), logsAntes);
    QVERIFY(e.sat.llamadas().isEmpty());

    // Carrera: la intencion ya seleccionada y la solicitud eliminada despues
    // (sin pasar por marcarEliminadosPorSolicitud): el ejecutor la limpia sin
    // log al no encontrar el paquete visible.
    const SolicitudId otra = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    PaquetePersistido& q = e.sembrarPaquete(otra, EstadoDescarga::Error);
    q.reintentoPendienteEn = kInicio.addSecs(7);
    const QString qid = q.id;
    for (SolicitudPersistida& s : e.almacen.solicitudes) {
        if (s.id == otra) {
            s.eliminadoEn = kInicio;
        }
    }
    const qsizetype logsCarrera = e.almacen.logs.size();
    const auto r = esperar(e.ejecutor->descargarIntencionPaquete(qid, kInicio.addSecs(7)));
    QVERIFY(r);
    QCOMPARE(r->desenlace, ResultadoOperacion::Desenlace::Descartada);
    QVERIFY(!e.paquete(qid)->reintentoPendienteEn);
    QCOMPARE(e.almacen.logs.size(), logsCarrera);
    QVERIFY(e.sat.llamadas().isEmpty());
}

void TestWorkerLocal::reintentoPorPaqueteCon5008QuedaErrorSaneado()
{
    EntornoWorker e;
    const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const QString pid = e.sembrarPaquete(sid, EstadoDescarga::Error).id;
    QVERIFY(e.iniciar());
    const QString crudo = u"Maximo alcanzado para XAXX010101000 token=abcdefghijklmnopqrstuvwxyz0123456789ABCDEFG"_s;
    e.sat.responderDescarga(fakes::FakeOperacionesSat::R<ResultadoDescarga>::fallo(
        FallaOperacion::de(FaseOperacion::RespuestaExplicita, u"5008"_s, crudo)));
    e.worker->reintentarDescargaPaquete(sid, e.paquete(pid)->idPaqueteSat);
    QVERIFY(e.estabilizar());
    QCOMPARE(e.llamadasSat(u"descargar:"_s), QStringList{u"descargar:"_s + pid});
    const PaquetePersistido* p = e.paquete(pid);
    QCOMPARE(p->estadoDescarga, EstadoDescarga::Error);
    QCOMPARE(p->codigoDescargaSat, std::optional<QString>(u"5008"_s));
    QVERIFY(!politicas::puedeReintentarManual(*p));
    const QString error = p->ultimoError.value_or(QString());
    QVERIFY(error.startsWith(u"RespuestaExplicita:5008"_s));
    QVERIFY(!error.contains(u"XAXX010101000"_s));
    QVERIFY(!error.contains(u"abcdefghijklmnopqrstuvwxyz0123456789"_s));
    // Un nuevo reintento de ese paquete ya no genera trafico.
    e.worker->reintentarDescargaPaquete(sid, p->idPaqueteSat);
    QVERIFY(e.estabilizar());
    QCOMPARE(e.llamadasSat(u"descargar:"_s).size(), 1);
}

void TestWorkerLocal::intencionPaqueteRespetaGateDeCredencial()
{
    EntornoWorker e(true);
    // El unico trabajo del perfil es una intencion por paquete (solicitud
    // EnProceso sin verificacion debida ni descargas automaticas).
    SolicitudPersistida& s = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada);
    const SolicitudId sid = s.id;
    const QString pid = e.sembrarPaquete(sid, EstadoDescarga::Error).id;
    e.sat.fijarCredencial(e.perfil, EstadoCredencial::Vencida);
    QVERIFY(e.iniciar());
    e.worker->reintentarDescargaPaquete(sid, e.paquete(pid)->idPaqueteSat);
    QVERIFY(e.drenar());
    e.fijarPausa(false);
    QVERIFY(e.estabilizar());
    QVERIFY(e.sat.llamadas().filter(u"credencial:"_s).size() >= 1); // el gate se consulto
    QVERIFY(e.llamadasSat(u"descargar:"_s).isEmpty());
    QCOMPARE(e.paquete(pid)->reintentoPendienteEn, std::optional(kInicio));

    // Con la credencial Lista se ejecuta.
    e.sat.fijarCredencial(e.perfil, EstadoCredencial::Lista);
    e.worker->ejecutarCiclo();
    QVERIFY(e.estabilizar());
    QCOMPARE(e.llamadasSat(u"descargar:"_s), QStringList{u"descargar:"_s + pid});
    QVERIFY(!e.paquete(pid)->reintentoPendienteEn);
}
