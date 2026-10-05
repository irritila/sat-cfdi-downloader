#include "TestOperacionExecutor.h"

#include "EntornoOperaciones.h"
#include "fakes/FakeOperacionesSatConEventLoop.h"

#include "domain/operaciones/PoliticasOperacion.h"

#include <QSignalSpy>
#include <QTimer>

using namespace satcfdi;
using namespace pruebasT007;
using namespace Qt::StringLiterals;
using Op = fakes::FakeOperacionesSat::Op;
using D = ResultadoOperacion::Desenlace;

namespace {

using RE = fakes::FakeOperacionesSat::R<ResultadoEnvio>;
using RV = fakes::FakeOperacionesSat::R<ResultadoVerificacion>;
using RD = fakes::FakeOperacionesSat::R<ResultadoDescarga>;
using RA = fakes::FakeOperacionesSat::R<ResultadoArchivoFinal>;

RE envio(const char* cod, const char* id)
{
    ResultadoEnvio r;
    r.codEstatus = QString::fromLatin1(cod);
    r.mensaje = u"mensaje SAT"_s;
    if (id) {
        r.idSolicitudSat = QString::fromLatin1(id);
    }
    return RE::exito(r);
}

} // namespace

void TestOperacionExecutor::envioDesenlacesD7_data()
{
    QTest::addColumn<int>("caso");
    QTest::addColumn<int>("estadoFinal");
    QTest::addColumn<int>("log");
    const auto fila = [](const char* n, int caso, EstadoLocal e, TipoEventoLog l) {
        QTest::newRow(n) << caso << int(e) << int(l);
    };
    fila("5000 con IdSolicitud", 0, EstadoLocal::Enviada, TipoEventoLog::SolicitudEnviada);
    fila("Preparacion", 1, EstadoLocal::Creada, TipoEventoLog::EnvioNoIniciado);
    fila("Autenticacion", 2, EstadoLocal::Creada, TipoEventoLog::EnvioNoIniciado);
    fila("AntesDeEnvio", 3, EstadoLocal::Creada, TipoEventoLog::EnvioNoIniciado);
    fila("rechazo 5002", 4, EstadoLocal::EnvioFallido, TipoEventoLog::EnvioFallido);
    fila("DespuesDeEnvio", 5, EstadoLocal::EnvioIncierto, TipoEventoLog::EnvioIncierto);
    fila("5000 sin IdSolicitud", 6, EstadoLocal::EnvioIncierto, TipoEventoLog::EnvioIncierto);
    fila("5006", 7, EstadoLocal::EnvioIncierto, TipoEventoLog::EnvioIncierto);
    fila("no documentado", 8, EstadoLocal::EnvioIncierto, TipoEventoLog::EnvioIncierto);
}

void TestOperacionExecutor::envioDesenlacesD7()
{
    QFETCH(int, caso);
    QFETCH(int, estadoFinal);
    QFETCH(int, log);
    Entorno e;
    const SolicitudId id = e.sembrar(EstadoLocal::Creada).id;
    switch (caso) {
    case 0: e.sat.responderEnvio(envio("5000", "abc-123")); break;
    case 1: e.sat.responderEnvio(RE::fallo(falla(FaseOperacion::Preparacion))); break;
    case 2: e.sat.responderEnvio(RE::fallo(falla(FaseOperacion::Autenticacion))); break;
    case 3: e.sat.responderEnvio(RE::fallo(falla(FaseOperacion::AntesDeEnvio))); break;
    case 4: e.sat.responderEnvio(envio("5002", nullptr)); break;
    case 5: e.sat.responderEnvio(RE::fallo(falla(FaseOperacion::DespuesDeEnvio))); break;
    case 6: e.sat.responderEnvio(envio("5000", nullptr)); break;
    case 7: e.sat.responderEnvio(envio("5006", nullptr)); break;
    default: e.sat.responderEnvio(envio("9999", nullptr)); break;
    }
    QSignalSpy actualizada(e.ejecutor.get(), &OperacionExecutor::solicitudActualizada);
    const auto r = esperar(e.ejecutor->enviar(id));
    QVERIFY(r);
    QCOMPARE(r->desenlace, D::Aplicada);
    const SolicitudPersistida& s = *e.solicitud(id);
    QCOMPARE(int(s.estadoLocal), estadoFinal);
    QCOMPARE(e.logsDe(TipoEventoLog(log), id), 1);
    QCOMPARE(e.logsDe(TipoEventoLog::EnvioIniciado, id), 1);
    if (s.estadoLocal == EstadoLocal::Enviada) {
        QCOMPARE(s.idSolicitudSat, std::optional(u"abc-123"_s));
        QCOMPARE(s.siguienteVerificacionEn, std::optional(kInicio.addSecs(600)));
    }
    if (s.estadoLocal == EstadoLocal::Creada) {
        // ADR 0017: invariantes de Creada (CHECKs de T001).
        QVERIFY(!s.envioIniciadoEn && !s.codEstatusSolicitud && !s.mensajeSolicitudSat);
        QVERIFY(s.ultimoError && !s.ultimoError->isEmpty());
    }
    for (const auto& l : e.almacen.logs) {
        QCOMPARE(l.origen, OrigenLog::Usuario);
    }
    QVERIFY(QTest::qWaitFor([&] { return actualizada.count() >= 1; }, 30000));
}

void TestOperacionExecutor::envioNuncaSeReenviaNiSeSeleccionaCreada()
{
    Entorno e;
    const SolicitudId id = e.sembrar(EstadoLocal::Creada).id;
    e.sat.responderEnvio(RE::fallo(falla(FaseOperacion::DespuesDeEnvio)));
    QVERIFY(esperar(e.ejecutor->enviar(id)));
    // Un segundo intento ya no aplica: EnvioIncierto no se reenvia.
    const auto otro = esperar(e.ejecutor->enviar(id));
    QCOMPARE(otro->desenlace, D::Descartada);
    QCOMPARE(e.sat.llamadas().filter(u"enviar:"_s).size(), 1);
    // El repositorio de seleccion nunca devuelve Creada (D4).
    e.sembrar(EstadoLocal::Creada);
    QVERIFY(e.operaciones.listarVerificacionesDebidas({e.perfil}, kInicio.addYears(1), 100).valor().isEmpty());
}

void TestOperacionExecutor::eliminadaDuranteOperacionNoCambiaNada()
{
    Entorno e;
    const SolicitudId id = e.sembrar(EstadoLocal::Creada).id;
    e.sat.bloquearSiguiente(Op::Enviar);
    auto f = e.ejecutor->enviar(id);
    QVERIFY(e.sat.esperarBloqueo());
    // La UI elimina la solicitud mientras el puerto esta bloqueado.
    for (auto& s : e.almacen.solicitudes) {
        s.eliminadoEn = kInicio;
    }
    const auto logsAntes = e.almacen.logs.size();
    e.sat.liberar();
    const auto r = esperar(f);
    QCOMPARE(r->desenlace, D::Descartada);
    QCOMPARE(e.solicitud(id)->estadoLocal, EstadoLocal::Enviando); // sin resultado posterior
    QCOMPARE(e.almacen.logs.size(), logsAntes);

    // Igual para una descarga.
    Entorno d;
    const SolicitudId sid = d.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const QString pid = d.sembrarPaquete(sid, EstadoDescarga::Disponible).id;
    d.sat.bloquearSiguiente(Op::Descargar);
    auto fd = d.ejecutor->descargar(pid, OrigenLog::Worker);
    QVERIFY(d.sat.esperarBloqueo());
    for (auto& s : d.almacen.solicitudes) {
        s.eliminadoEn = kInicio;
    }
    for (auto& p : d.almacen.paquetes) {
        p.eliminadoEn = kInicio;
    }
    const auto logsD = d.almacen.logs.size();
    d.sat.liberar();
    QCOMPARE(esperar(fd)->desenlace, D::Descartada);
    QCOMPARE(d.paquete(pid)->estadoDescarga, EstadoDescarga::Descargando);
    QCOMPARE(d.almacen.logs.size(), logsD);
}

void TestOperacionExecutor::verificacionTerminadaRegistraPaquetesEnUnaTransaccion()
{
    Entorno e;
    const SolicitudId id = e.sembrar(EstadoLocal::Enviada).id;
    e.sat.responderVerificacion(RV::exito(verificacion(EstadoSolicitudSat::Terminada, {u"A_01"_s, u"A_02"_s}, 12)));
    const auto r = esperar(e.ejecutor->verificar(id, OrigenLog::Worker));
    QCOMPARE(r->desenlace, D::Aplicada);
    const SolicitudPersistida& s = *e.solicitud(id);
    QCOMPARE(s.estadoSolicitudSat, std::optional(EstadoSolicitudSat::Terminada));
    QVERIFY(!s.siguienteVerificacionEn); // Terminada: sin verificacion automatica
    QCOMPARE(e.almacen.paquetes.size(), 2);
    for (const auto& p : e.almacen.paquetes) {
        QCOMPARE(p.vencimientoEstimadoEn, std::optional(kInicio.addSecs(72 * 3600)));
        QCOMPARE(p.estadoDescarga, EstadoDescarga::Disponible);
    }
    // Una sola transaccion: begin..commit sin otro begin intermedio.
    const QStringList ev = e.almacen.eventos;
    const qsizetype i = ev.indexOf(u"aplicarVerificacion"_s);
    QVERIFY(i > 0);
    QCOMPARE(ev.lastIndexOf(u"begin"_s, i), ev.lastIndexOf(u"begin"_s));
    QCOMPARE(e.logsDe(TipoEventoLog::VerificacionRealizada, id), 1);
    QCOMPARE(e.logsDe(TipoEventoLog::PaquetesRegistrados, id), 1);

    // Una verificacion manual posterior con los mismos ids no los duplica ni
    // desplaza su vencimiento.
    e.reloj.fijar(kInicio.addSecs(3600));
    e.sat.responderVerificacion(RV::exito(verificacion(EstadoSolicitudSat::Terminada, {u"A_02"_s, u"A_01"_s}, 12)));
    QVERIFY(esperar(e.ejecutor->verificar(id, OrigenLog::Usuario)));
    QCOMPARE(e.almacen.paquetes.size(), 2);
    QCOMPARE(e.almacen.paquetes.first().vencimientoEstimadoEn, std::optional(kInicio.addSecs(72 * 3600)));
}

void TestOperacionExecutor::verificacionVencidaVencePaquetesNoDescargados()
{
    Entorno e;
    const SolicitudId id = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const QString disponible = e.sembrarPaquete(id, EstadoDescarga::Disponible).id;
    const QString descargado = e.sembrarPaquete(id, EstadoDescarga::Descargado).id;
    e.sat.responderVerificacion(RV::exito(verificacion(EstadoSolicitudSat::Vencida)));
    QVERIFY(esperar(e.ejecutor->verificar(id, OrigenLog::Usuario)));
    QCOMPARE(e.paquete(disponible)->estadoDescarga, EstadoDescarga::Vencido);
    QCOMPARE(e.paquete(disponible)->origenVencimiento, std::optional(OrigenVencimiento::Sat));
    QCOMPARE(e.paquete(disponible)->motivoVencimiento, std::optional(MotivoVencimiento::SolicitudExpirada));
    QCOMPARE(e.paquete(descargado)->estadoDescarga, EstadoDescarga::Descargado);
    QCOMPARE(e.logsDe(TipoEventoLog::PaqueteVencido, id), 1);
}

void TestOperacionExecutor::agendaSinCambioTresVecesYCambio()
{
    Entorno e;
    const SolicitudId id = e.sembrar(EstadoLocal::Enviada).id;
    // Primera observacion: cambia (de nulo a EnProceso).
    for (int i = 0; i < 4; ++i) {
        e.sat.responderVerificacion(RV::exito(verificacion(EstadoSolicitudSat::EnProceso)));
        QVERIFY(esperar(e.ejecutor->verificar(id, OrigenLog::Worker)));
    }
    // Tras la primera (cambio) hubo tres sin cambio consecutivas: 30 min.
    QCOMPARE(e.solicitud(id)->verificacionesSinCambio, 3);
    QCOMPARE(e.solicitud(id)->siguienteVerificacionEn, std::optional(kInicio.addSecs(1800)));
    QCOMPARE(e.logsDe(TipoEventoLog::VerificacionRealizada, id), 1); // repeticiones del worker sin log
    // Un cambio en uno de los cuatro campos: contador 0 y 10 min.
    e.sat.responderVerificacion(RV::exito(verificacion(EstadoSolicitudSat::EnProceso, {}, 5)));
    QVERIFY(esperar(e.ejecutor->verificar(id, OrigenLog::Worker)));
    QCOMPARE(e.solicitud(id)->verificacionesSinCambio, 0);
    QCOMPARE(e.solicitud(id)->siguienteVerificacionEn, std::optional(kInicio.addSecs(600)));
    // Cambio solo en el conjunto de ids (mismos escalares): tambien es cambio.
    for (int i = 0; i < 2; ++i) {
        e.sat.responderVerificacion(RV::exito(verificacion(EstadoSolicitudSat::EnProceso, {}, 5)));
        QVERIFY(esperar(e.ejecutor->verificar(id, OrigenLog::Worker)));
    }
    QCOMPARE(e.solicitud(id)->verificacionesSinCambio, 2);
    e.sat.responderVerificacion(RV::exito(verificacion(EstadoSolicitudSat::EnProceso, {u"N_01"_s}, 5)));
    QVERIFY(esperar(e.ejecutor->verificar(id, OrigenLog::Worker)));
    QCOMPARE(e.solicitud(id)->verificacionesSinCambio, 0);
    QCOMPARE(e.solicitud(id)->siguienteVerificacionEn, std::optional(kInicio.addSecs(600)));
}

void TestOperacionExecutor::fallaNoCuentaComoSinCambio()
{
    Entorno e;
    const SolicitudId id = e.sembrar(EstadoLocal::Enviada).id;
    for (int i = 0; i < 3; ++i) {
        e.sat.responderVerificacion(RV::exito(verificacion(EstadoSolicitudSat::EnProceso)));
        QVERIFY(esperar(e.ejecutor->verificar(id, OrigenLog::Worker)));
    }
    QCOMPARE(e.solicitud(id)->verificacionesSinCambio, 2);
    e.sat.responderVerificacion(RV::fallo(falla(FaseOperacion::DespuesDeEnvio)));
    const auto r = esperar(e.ejecutor->verificar(id, OrigenLog::Worker));
    QVERIFY(r && r->falla);
    QCOMPARE(e.solicitud(id)->verificacionesSinCambio, 2);
    QCOMPARE(e.solicitud(id)->siguienteVerificacionEn, std::optional(kInicio.addSecs(1800)));
    QCOMPARE(e.logsDe(TipoEventoLog::VerificacionFallida, id), 1);
}

void TestOperacionExecutor::tresFallasIgualesSuspendenYPersisten()
{
    Entorno e;
    const SolicitudId id = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso).id;
    for (int i = 0; i < 3; ++i) {
        e.sat.responderVerificacion(RV::fallo(falla(FaseOperacion::RespuestaExplicita, "5004")));
        QVERIFY(esperar(e.ejecutor->verificar(id, OrigenLog::Worker)));
        if (i < 2) {
            QVERIFY(e.solicitud(id)->siguienteVerificacionEn);
        }
    }
    QVERIFY(!e.solicitud(id)->siguienteVerificacionEn); // suspendida
    QCOMPARE(e.logsDe(TipoEventoLog::VerificacionFallida, id), 1); // misma clave: un log
    QCOMPARE(e.logsDe(TipoEventoLog::VerificacionSuspendida, id), 1);
    QCOMPARE(e.almacen.rachas.value(id).second, 3);

    // "Aun tras reiniciar": otro ejecutor sobre los mismos datos no la selecciona.
    e.ejecutor.reset();
    e.crearEjecutor();
    QVERIFY(e.operaciones.listarVerificacionesDebidas({e.perfil}, kInicio.addYears(1), 10).valor().isEmpty());
    // Una cuarta falla manual igual no crea logs nuevos.
    e.sat.responderVerificacion(RV::fallo(falla(FaseOperacion::RespuestaExplicita, "5004")));
    QVERIFY(esperar(e.ejecutor->verificar(id, OrigenLog::Usuario)));
    QCOMPARE(e.logsDe(TipoEventoLog::VerificacionFallida, id), 1);
    QCOMPARE(e.logsDe(TipoEventoLog::VerificacionSuspendida, id), 1);
    // "Verificar ahora" exitoso reanuda la agenda y reinicia la racha.
    e.sat.responderVerificacion(RV::exito(verificacion(EstadoSolicitudSat::EnProceso)));
    QVERIFY(esperar(e.ejecutor->verificar(id, OrigenLog::Usuario)));
    QVERIFY(e.solicitud(id)->siguienteVerificacionEn);
    QVERIFY(!e.almacen.rachas.contains(id));
}

void TestOperacionExecutor::fallasTransitoriasNoSuspenden()
{
    for (const FallaOperacion& t : {falla(FaseOperacion::DespuesDeEnvio), falla(FaseOperacion::RespuestaExplicita, "404"),
                                    falla(FaseOperacion::RespuestaExplicita, "5011"),
                                    falla(FaseOperacion::RespuestaExplicita, "a:InvalidSecurity")}) {
        Entorno e;
        const SolicitudId id = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso).id;
        for (int i = 0; i < 4; ++i) {
            e.sat.responderVerificacion(RV::fallo(t));
            QVERIFY(esperar(e.ejecutor->verificar(id, OrigenLog::Worker)));
        }
        QCOMPARE(e.solicitud(id)->siguienteVerificacionEn, std::optional(kInicio.addSecs(1800)));
        QCOMPARE(e.logsDe(TipoEventoLog::VerificacionSuspendida, id), 0);
    }
}

void TestOperacionExecutor::descargaDesenlaces_data()
{
    QTest::addColumn<int>("caso");
    QTest::addColumn<int>("estado");
    QTest::addColumn<int>("log");
    const auto fila = [](const char* n, int c, EstadoDescarga e, TipoEventoLog l) {
        QTest::newRow(n) << c << int(e) << int(l);
    };
    fila("exito", 0, EstadoDescarga::Descargado, TipoEventoLog::PaqueteDescargado);
    fila("exito con advertencia", 1, EstadoDescarga::Descargado, TipoEventoLog::PaqueteDescargado);
    fila("Preparacion", 2, EstadoDescarga::Error, TipoEventoLog::DescargaFallida);
    fila("Autenticacion", 3, EstadoDescarga::Error, TipoEventoLog::DescargaFallida);
    fila("5007", 4, EstadoDescarga::Vencido, TipoEventoLog::PaqueteVencido);
    fila("5008", 5, EstadoDescarga::Error, TipoEventoLog::DescargaFallida);
    fila("ColisionDestino", 6, EstadoDescarga::Error, TipoEventoLog::DescargaFallida);
    fila("DespuesDeEnvio", 7, EstadoDescarga::Error, TipoEventoLog::DescargaFallida);
}

void TestOperacionExecutor::descargaDesenlaces()
{
    QFETCH(int, caso);
    QFETCH(int, estado);
    QFETCH(int, log);
    Entorno e;
    const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const QString pid = e.sembrarPaquete(sid, EstadoDescarga::Disponible).id;
    ResultadoDescarga ok;
    ok.rutaFinal = u"paquetes/ok.zip"_s;
    FallaOperacion colision = falla(FaseOperacion::Almacenamiento);
    colision.causaAlmacenamiento = CausaAlmacenamiento::ColisionDestino;
    switch (caso) {
    case 0: e.sat.responderDescarga(RD::exito(ok)); break;
    case 1:
        ok.advertenciaDurabilidad = u"fsync del directorio fallo"_s;
        e.sat.responderDescarga(RD::exito(ok));
        break;
    case 2: e.sat.responderDescarga(RD::fallo(falla(FaseOperacion::Preparacion))); break;
    case 3: e.sat.responderDescarga(RD::fallo(falla(FaseOperacion::Autenticacion))); break;
    case 4: e.sat.responderDescarga(RD::fallo(falla(FaseOperacion::RespuestaExplicita, "5007"))); break;
    case 5: e.sat.responderDescarga(RD::fallo(falla(FaseOperacion::RespuestaExplicita, "5008"))); break;
    case 6: e.sat.responderDescarga(RD::fallo(colision)); break;
    default: e.sat.responderDescarga(RD::fallo(falla(FaseOperacion::DespuesDeEnvio))); break;
    }
    QVERIFY(esperar(e.ejecutor->descargar(pid, OrigenLog::Worker)));
    const PaquetePersistido& p = *e.paquete(pid);
    QCOMPARE(int(p.estadoDescarga), estado);
    QCOMPARE(e.logsDe(TipoEventoLog(log), sid), 1);
    QCOMPARE(e.logsDe(TipoEventoLog::DescargaIniciada, sid), 1);
    QCOMPARE(e.solicitud(sid)->estadoSolicitudSat, std::optional(EstadoSolicitudSat::Terminada)); // no cambia
    if (caso == 4) {
        QCOMPARE(p.origenVencimiento, std::optional(OrigenVencimiento::Sat));
        QCOMPARE(p.motivoVencimiento, std::optional(MotivoVencimiento::PaqueteExpirado));
    }
    const QString payload = e.almacen.logs.constLast().payloadResumenJson.value_or(QString());
    if (caso == 1) {
        QCOMPARE(p.rutaLocal, std::optional(u"paquetes/ok.zip"_s));
        QVERIFY(payload.contains(u"durabilidad"_s));
    }
    if (caso == 6) {
        QVERIFY(payload.contains(u"ColisionDestino"_s));
    }
}

void TestOperacionExecutor::vencimientoEstimadoLocal()
{
    Entorno e;
    const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    QStringList ids;
    for (EstadoDescarga d : {EstadoDescarga::Disponible, EstadoDescarga::Error, EstadoDescarga::Descargando}) {
        ids.append(e.sembrarPaquete(sid, d, kInicio.addSecs(-1)).id);
    }
    const QString futuro = e.sembrarPaquete(sid, EstadoDescarga::Disponible, kInicio.addSecs(60)).id;
    for (const QString& id : ids) {
        QCOMPARE(esperar(e.ejecutor->vencerEstimado(id))->desenlace, D::Aplicada);
        QCOMPARE(e.paquete(id)->estadoDescarga, EstadoDescarga::Vencido);
        QCOMPARE(e.paquete(id)->origenVencimiento, std::optional(OrigenVencimiento::EstimacionLocal));
    }
    QCOMPARE(esperar(e.ejecutor->vencerEstimado(futuro))->desenlace, D::Descartada);
    QCOMPARE(e.logsDe(TipoEventoLog::PaqueteVencido, sid), 3);
}

void TestOperacionExecutor::recuperacionAlArrancar()
{
    Entorno e;
    const SolicitudId enviando = e.sembrar(EstadoLocal::Enviando).id;
    const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const QString conArchivo = e.sembrarPaquete(sid, EstadoDescarga::Descargando).id;
    const QString sinArchivo = e.sembrarPaquete(sid, EstadoDescarga::Descargando).id;
    const QString conFalla = e.sembrarPaquete(sid, EstadoDescarga::Descargando).id;
    ResultadoArchivoFinal existe;
    existe.existe = true;
    existe.rutaFinal = u"paquetes/r.zip"_s;
    e.sat.responderArchivoFinal(fakes::FakeOperacionesSat::R<ResultadoArchivoFinal>::exito(existe));
    e.sat.responderArchivoFinal(fakes::FakeOperacionesSat::R<ResultadoArchivoFinal>::exito(ResultadoArchivoFinal{}));
    e.sat.responderArchivoFinal(fakes::FakeOperacionesSat::R<ResultadoArchivoFinal>::fallo(falla(FaseOperacion::Almacenamiento)));
    const auto r = esperar(e.ejecutor->recuperar());
    QCOMPARE(r->desenlace, D::Aplicada);
    QCOMPARE(e.solicitud(enviando)->estadoLocal, EstadoLocal::EnvioIncierto);
    QCOMPARE(e.logsDe(TipoEventoLog::EnvioIncierto, enviando), 1);
    QCOMPARE(e.paquete(conArchivo)->estadoDescarga, EstadoDescarga::Descargado);
    QVERIFY(e.paquete(conArchivo)->reconciliadoEn);
    QCOMPARE(e.paquete(sinArchivo)->estadoDescarga, EstadoDescarga::Disponible);
    QCOMPARE(e.paquete(conFalla)->estadoDescarga, EstadoDescarga::Descargando); // D14
    QCOMPARE(e.logsDe(TipoEventoLog::PaqueteReconciliado, sid), 1);
    QCOMPARE(e.logsDe(TipoEventoLog::DescargaInterrumpida, sid), 2); // sin archivo + falla de reconciliacion
    for (const auto& l : e.almacen.logs) {
        QCOMPARE(l.origen, OrigenLog::Recuperacion);
    }
}

void TestOperacionExecutor::colaSerialConPrioridad()
{
    Entorno e;
    const SolicitudId bloqueada = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso).id;
    const SolicitudId automatica = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso).id;
    const SolicitudId manual = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso).id;
    const SolicitudId terminada = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const QString pid = e.sembrarPaquete(terminada, EstadoDescarga::Disponible).id;
    e.sat.bloquearSiguiente(Op::Verificar);
    auto f0 = e.ejecutor->verificar(bloqueada, OrigenLog::Worker);
    QVERIFY(e.sat.esperarBloqueo());
    // Encolados a la vez mientras el puerto esta ocupado.
    auto fa = e.ejecutor->descargar(pid, OrigenLog::Worker);
    auto fb = e.ejecutor->verificar(automatica, OrigenLog::Worker);
    auto fc = e.ejecutor->verificar(manual, OrigenLog::Usuario);
    e.sat.liberar();
    QVERIFY(esperar(fa) && esperar(fb) && esperar(fc) && esperar(f0));
    QCOMPARE(e.sat.llamadas(),
             (QStringList{u"verificar:"_s + bloqueada.texto(), u"verificar:"_s + manual.texto(),
                          u"descargar:"_s + pid, u"verificar:"_s + automatica.texto()}));
    QCOMPARE(e.sat.concurrenciaMaxima(), 1);
    QCOMPARE(e.sat.hilos().size(), 1);
    QVERIFY(!e.sat.hilos().contains(QThread::currentThread()));
}

void TestOperacionExecutor::hiloGraficoSigueRespondiendo()
{
    Entorno e;
    const SolicitudId id = e.sembrar(EstadoLocal::Creada).id;
    e.sat.bloquearSiguiente(Op::Enviar);
    auto f = e.ejecutor->enviar(id);
    QVERIFY(e.sat.esperarBloqueo());
    bool procesado = false;
    QTimer::singleShot(0, [&] { procesado = true; });
    QVERIFY(QTest::qWaitFor([&] { return procesado; }, 30000));
    QCOMPARE(e.sat.activas(), 1); // el puerto sigue bloqueado
    e.sat.liberar();
    QVERIFY(esperar(f));
}

void TestOperacionExecutor::detenerEsperaHasta10sYCancela()
{
    Entorno e;
    const SolicitudId id = e.sembrar(EstadoLocal::Creada).id;
    const SolicitudId otra = e.sembrar(EstadoLocal::Creada).id;
    e.sat.bloquearSiguiente(Op::Enviar, FaseOperacion::DespuesDeEnvio);
    QSignalSpy detenido(e.ejecutor.get(), &OperacionExecutor::detenido);
    auto f = e.ejecutor->enviar(id);
    QVERIFY(e.sat.esperarBloqueo());
    auto fd = e.ejecutor->detener();
    QVERIFY(!e.ejecutor->aceptaOperaciones());
    // 9.999 s: sigue esperando y rechaza operaciones nuevas.
    e.programador.avanzar(std::chrono::milliseconds(9999));
    QCOMPARE(e.sat.activas(), 1);
    QVERIFY(!fd.isFinished());
    const auto rechazada = esperar(e.ejecutor->enviar(otra));
    QCOMPARE(rechazada->desenlace, D::Rechazada);
    QCOMPARE(e.solicitud(otra)->estadoLocal, EstadoLocal::Creada);
    // 10 s: cancelacion cooperativa; la falla DespuesDeEnvio lleva a EnvioIncierto.
    e.programador.avanzar(std::chrono::milliseconds(1));
    QVERIFY(esperarVoid(fd));
    const auto r = esperar(f);
    QVERIFY(r && r->falla && r->falla->cancelada);
    QCOMPARE(e.solicitud(id)->estadoLocal, EstadoLocal::EnvioIncierto);
    // La senal de cierre repetida no duplica logs ni cierres.
    auto otraVez = e.ejecutor->detener();
    QVERIFY(otraVez.isFinished());
    QCOMPARE(e.logsDe(TipoEventoLog::EnvioIncierto, id), 1);
    QCOMPARE(e.cierresConexion.load(), 1);
    QVERIFY(QTest::qWaitFor([&] { return detenido.count() == 1; }, 30000));
    QCOMPARE(e.sat.llamadas().size(), 1);
}

void TestOperacionExecutor::detenerSinOperacionesNoGeneraLogs()
{
    Entorno e;
    e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso);
    QVERIFY(esperarVoid(e.ejecutor->detener()));
    QVERIFY(e.almacen.logs.isEmpty());
    QCOMPARE(e.cierresConexion.load(), 1);
    QCOMPARE(e.programador.pendientes(), 0);
}

void TestOperacionExecutor::gateDeCredencialUnaVezPorPerfil()
{
    Entorno e;
    const PerfilId otro = PerfilId::generar();
    const PerfilId roto = PerfilId::generar();
    e.sat.fijarCredencial(otro, EstadoCredencial::Vencida);
    e.sat.fallarCredencial(roto, falla(FaseOperacion::Preparacion));
    QSignalSpy cambio(e.ejecutor.get(), &OperacionExecutor::estadoCredencialCambiado);
    const auto r = esperar(e.ejecutor->consultarCredenciales({e.perfil, otro, e.perfil, roto, otro}));
    QVERIFY(r);
    QCOMPARE(r->size(), 2); // el que falla no aparece (no Lista)
    QCOMPARE(e.sat.llamadas().size(), 3); // una consulta por perfil
    QVERIFY(QTest::qWaitFor([&] { return cambio.count() == 2; }, 30000));
    // Sin cambios: no se vuelve a emitir.
    QVERIFY(esperar(e.ejecutor->consultarCredenciales({e.perfil, otro})));
    e.sat.fijarCredencial(otro, EstadoCredencial::Lista);
    QVERIFY(esperar(e.ejecutor->consultarCredenciales({e.perfil, otro})));
    QVERIFY(QTest::qWaitFor([&] { return cambio.count() == 3; }, 30000));
    QCOMPARE(cambio.constLast().at(1).value<EstadoCredencial>(), EstadoCredencial::Lista);
}

void TestOperacionExecutor::intencionSeConsumeSoloSiNoCambio()
{
    Entorno e;
    SolicitudPersistida& s = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso);
    s.verificacionPendiente = true;
    s.accionPendienteEn = kInicio;
    const SolicitudId id = s.id;
    // Capturada distinta (otra intencion se registro despues): no se limpia.
    QVERIFY(esperar(e.ejecutor->verificar(id, OrigenLog::Usuario, kInicio.addSecs(-5))));
    QVERIFY(e.solicitud(id)->verificacionPendiente);
    QVERIFY(esperar(e.ejecutor->verificar(id, OrigenLog::Usuario, kInicio)));
    QVERIFY(!e.solicitud(id)->verificacionPendiente);
    QVERIFY(!e.solicitud(id)->accionPendienteEn);
    // Intencion de descarga sin paquetes: se descarta con log.
    QVERIFY(esperar(e.ejecutor->registrarIntencion(id, TipoIntencion::Descarga)));
    QCOMPARE(e.logsDe(TipoEventoLog::AccionPendienteRegistrada, id), 1);
    const QDateTime capturada = *e.solicitud(id)->accionPendienteEn;
    QCOMPARE(esperar(e.ejecutor->descartarIntencion(id, TipoIntencion::Descarga, capturada))->desenlace, D::Aplicada);
    QVERIFY(!e.solicitud(id)->descargaPendiente);
    QCOMPARE(e.logsDe(TipoEventoLog::AccionPendienteDescartada, id), 1);
}

void TestOperacionExecutor::destruirCierraConexionEnSuHiloSinAdvertencias()
{
    QTest::failOnWarning(QRegularExpression(u".*"_s)); // cualquier advertencia falla la prueba
    // Sin operacion en curso.
    {
        Entorno e;
        QVERIFY(e.drenar());
        e.ejecutor.reset();
        QCOMPARE(e.cierresConexion.load(), 1);
        QVERIFY(e.hiloCierre.load() != nullptr);
        QVERIFY(e.hiloCierre.load() != QThread::currentThread());
    }
    // Con una operacion bloqueada en el puerto: se cancela, se aplica su falla
    // y la conexion se cierra en el hilo del ejecutor antes de terminar.
    {
        Entorno e;
        const SolicitudId id = e.sembrar(EstadoLocal::Creada).id;
        e.sat.bloquearSiguiente(Op::Enviar, FaseOperacion::DespuesDeEnvio);
        auto f = e.ejecutor->enviar(id);
        QVERIFY(e.sat.esperarBloqueo());
        e.ejecutor.reset();
        QCOMPARE(e.cierresConexion.load(), 1);
        QVERIFY(e.hiloCierre.load() != QThread::currentThread());
        QVERIFY(f.isFinished());
        QCOMPARE(e.solicitud(id)->estadoLocal, EstadoLocal::EnvioIncierto);
        QCOMPARE(e.logsDe(TipoEventoLog::EnvioIncierto, id), 1);
    }
    // detener() previo con plazo aun sin vencer: el destructor cancela ya.
    {
        Entorno e;
        const SolicitudId id = e.sembrar(EstadoLocal::Creada).id;
        e.sat.bloquearSiguiente(Op::Enviar, FaseOperacion::DespuesDeEnvio);
        auto f = e.ejecutor->enviar(id);
        QVERIFY(e.sat.esperarBloqueo());
        auto fd = e.ejecutor->detener();
        e.ejecutor.reset();
        QCOMPARE(e.cierresConexion.load(), 1);
        QCOMPARE(e.programador.pendientes(), 0);
        QCOMPARE(e.logsDe(TipoEventoLog::EnvioIncierto, id), 1);
    }
}

void TestOperacionExecutor::recuperacionNoEscribeSiSeEliminaDuranteElPuerto()
{
    // existeArchivoFinal bloqueado; la UI elimina la solicitud; el puerto falla:
    // ni log ni cambio (D5). Se cubren falla, "no existe" y "existe".
    for (int caso = 0; caso < 3; ++caso) {
        Entorno e;
        const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
        const QString pid = e.sembrarPaquete(sid, EstadoDescarga::Descargando).id;
        ResultadoArchivoFinal existe;
        existe.existe = caso == 2;
        e.sat.responderArchivoFinal(caso == 0 ? RA::fallo(falla(FaseOperacion::Almacenamiento)) : RA::exito(existe));
        e.sat.bloquearSiguiente(Op::ExisteArchivoFinal, FaseOperacion::Almacenamiento);
        auto f = e.ejecutor->recuperar();
        QVERIFY(e.sat.esperarBloqueo());
        for (auto& s : e.almacen.solicitudes) {
            s.eliminadoEn = kInicio;
        }
        e.sat.liberar();
        QVERIFY(esperar(f));
        QVERIFY2(e.almacen.logs.isEmpty(), qPrintable(QString::number(caso)));
        QCOMPARE(e.paquete(pid)->estadoDescarga, EstadoDescarga::Descargando);
        QVERIFY(!e.paquete(pid)->reconciliadoEn);
    }
}

namespace {

std::unique_ptr<OperacionExecutor> ejecutorCon(Entorno& e, OperacionesSat& sat)
{
    return std::make_unique<OperacionExecutor>(
        PuertosEjecutor{e.solicitudes, e.logs, e.operaciones, e.uow, e.sanitizer, sat, [] {}, &e.paquetesRepo,
                        &e.storage},
        e.reloj.funcion(), e.programador);
}

} // namespace

// T009: un puerto que gira un QEventLoop local (como SatGatewayProductivo) no
// debe provocar que el ejecutor tome otro trabajo durante la operacion.
void TestOperacionExecutor::puertoConEventLoopNoReentra()
{
    Entorno e;
    fakes::FakeOperacionesSatConEventLoop sat;
    auto ejecutor = ejecutorCon(e, sat);
    const SolicitudId s1 = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso).id;
    const SolicitudId s2 = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso).id;
    const SolicitudId s3 = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso).id;

    sat.bloquearSiguiente();
    auto f1 = ejecutor->verificar(s1, OrigenLog::Worker);
    QVERIFY(QTest::qWaitFor([&] { return sat.girando(); }, 30000));
    // Encolados mientras el loop local gira: sus procesar() se despachan en
    // ese loop y deben regresar sin ejecutar nada.
    auto f2 = ejecutor->verificar(s2, OrigenLog::Worker);
    std::atomic<bool> leido{false};
    auto fl = ejecutor->leer([&leido](OperacionesSolicitudRepository&) { leido = true; });
    auto f3 = ejecutor->verificar(s3, OrigenLog::Usuario); // Manual: antes que s2
    sat.marcarDespacho();
    QVERIFY(QTest::qWaitFor([&] { return sat.despachado(); }, 30000));
    QCOMPARE(sat.llamadas().size(), 1);
    QVERIFY(!leido.load());
    QVERIFY(!f2.isFinished() && !f3.isFinished() && !fl.isFinished());

    sat.liberar();
    QVERIFY(esperar(f1));
    QVERIFY(esperar(f2));
    QVERIFY(esperar(f3));
    QVERIFY(esperarVoid(fl));
    QVERIFY(leido.load());
    QCOMPARE(sat.maximoActivas(), 1);
    QCOMPARE(sat.llamadas(), (QStringList{u"verificar:"_s + s1.texto(), u"verificar:"_s + s3.texto(),
                                          u"verificar:"_s + s2.texto()}));
    QCOMPARE(esperar(f2)->desenlace, D::Aplicada);
}

void TestOperacionExecutor::cierreCancelaOperacionConEventLoop()
{
    Entorno e;
    fakes::FakeOperacionesSatConEventLoop sat;
    auto ejecutor = ejecutorCon(e, sat);
    const SolicitudId s1 = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso).id;
    const SolicitudId s2 = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso).id;
    QSignalSpy detenido(ejecutor.get(), &OperacionExecutor::detenido);
    // El callback de cancelacion reentra al ejecutor (toma su mutex): la
    // cancelacion se solicita fuera del mutex, sin deadlock.
    std::optional<bool> aceptabaAlCancelar;
    sat.alCancelar = [&] { aceptabaAlCancelar = ejecutor->aceptaOperaciones(); };

    sat.bloquearSiguiente();
    auto f1 = ejecutor->verificar(s1, OrigenLog::Usuario);
    QVERIFY(QTest::qWaitFor([&] { return sat.girando(); }, 30000));
    auto f2 = ejecutor->verificar(s2, OrigenLog::Usuario);
    sat.marcarDespacho();
    QVERIFY(QTest::qWaitFor([&] { return sat.despachado(); }, 30000));

    auto fd = ejecutor->detener(); // plazo 10 s con el programador falso
    QCOMPARE(esperar(f2)->desenlace, D::Rechazada);
    e.programador.avanzar(std::chrono::milliseconds(9999));
    QVERIFY(sat.girando());
    QVERIFY(!fd.isFinished());
    // Al vencer el plazo, la cancelacion alcanza a la operacion ACTIVA.
    e.programador.avanzar(std::chrono::milliseconds(1));
    QVERIFY(esperarVoid(fd));
    const auto r = esperar(f1);
    QVERIFY(r && r->falla && r->falla->cancelada);
    QCOMPARE(aceptabaAlCancelar, std::optional<bool>(false));
    QVERIFY(QTest::qWaitFor([&] { return detenido.count() == 1; }, 30000));
    QCOMPARE(sat.maximoActivas(), 1);
    QCOMPARE(sat.llamadas(), QStringList{u"verificar:"_s + s1.texto()});
}
