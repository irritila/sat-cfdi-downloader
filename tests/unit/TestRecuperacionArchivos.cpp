#include "TestRecuperacionArchivos.h"

#include "EntornoOperaciones.h"

#include <QRegularExpression>
#include <QSignalSpy>

using namespace satcfdi;
using namespace pruebasT007;
using namespace Qt::StringLiterals;
using D = ResultadoOperacion::Desenlace;

namespace {

const QByteArray kCentinela = QByteArrayLiteral("PK\x03\x04-BYTES-ZIP-CENTINELA");

QString temporalDe(const QString& rutaFinal, const char* hex = "0123456789abcdef")
{
    const qsizetype barra = rutaFinal.lastIndexOf(u'/');
    return rutaFinal.left(barra + 1) + u'.' + rutaFinal.mid(barra + 1) + u'.' + QString::fromLatin1(hex) + u".part"_s;
}

QString rutaAjena(const SolicitudId& id, const char* archivo)
{
    return u"EKU9003173C9/2026-09/"_s + id.texto() + u'/' + QString::fromLatin1(archivo);
}

QRegularExpression mensaje(const QString& texto)
{
    return QRegularExpression(QRegularExpression::escape(texto));
}

} // namespace

void TestRecuperacionArchivos::temporalAsociadoADescargandoSeEliminaTrasLaRegla()
{
    Entorno e;
    const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const PaquetePersistido paq = e.sembrarPaquete(sid, EstadoDescarga::Descargando);
    const QString temporal = temporalDe(e.rutaFinal(paq));
    e.storage.agregarTemporal(temporal);
    QVERIFY(esperar(e.ejecutor->recuperar()));
    // Regla de T007 (sin archivo final -> Disponible) y despues eliminarTemporal.
    QCOMPARE(e.paquete(paq.id)->estadoDescarga, EstadoDescarga::Disponible);
    QCOMPARE(e.logsDe(TipoEventoLog::DescargaInterrumpida, sid), 1);
    QVERIFY(e.storage.temporales().isEmpty());
    QCOMPARE(e.storage.llamadas(), (QStringList{u"escanear"_s, u"eliminarTemporal:"_s + temporal}));
}

void TestRecuperacionArchivos::temporalNoAsociableSeEliminaConDiagnostico()
{
    Entorno e;
    const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const PaquetePersistido disponible = e.sembrarPaquete(sid, EstadoDescarga::Disponible);
    const QString deDisponible = temporalDe(e.rutaFinal(disponible));      // paquete no Descargando
    const QString sinSolicitud = rutaAjena(SolicitudId::generar(), ".x.zip.0123456789abcdef.part"); // UUID desconocido
    e.storage.agregarTemporal(deDisponible);
    e.storage.agregarTemporal(sinSolicitud);
    QTest::ignoreMessage(QtInfoMsg, mensaje(u"temporal no asociable eliminado: "_s + deDisponible));
    QTest::ignoreMessage(QtInfoMsg, mensaje(u"temporal no asociable eliminado: "_s + sinSolicitud));
    QVERIFY(esperar(e.ejecutor->recuperar()));
    QVERIFY(e.storage.temporales().isEmpty());
    QVERIFY(e.almacen.logs.isEmpty()); // solo diagnostico de app
    QCOMPARE(e.paquete(disponible.id)->estadoDescarga, EstadoDescarga::Disponible);
}

void TestRecuperacionArchivos::finalHuerfanoConSolicitudSeConservaYRegistraUnaVez()
{
    Entorno e;
    const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const QString huerfano = rutaAjena(sid, "SIN_PAQUETE_01.zip");
    e.storage.agregarFinal(huerfano, kCentinela);
    QVERIFY(esperar(e.ejecutor->recuperar()));
    QCOMPARE(e.storage.bytesFinal(huerfano), std::optional(kCentinela)); // se conserva
    QCOMPARE(e.logsDe(TipoEventoLog::ArchivoHuerfano, sid), 1);
    QCOMPARE(e.almacen.logs.constLast().origen, OrigenLog::Recuperacion);
    // Otro arranque: no se repite el log.
    e.ejecutor.reset();
    e.crearEjecutor();
    QVERIFY(esperar(e.ejecutor->recuperar()));
    QCOMPARE(e.logsDe(TipoEventoLog::ArchivoHuerfano, sid), 1);
    QVERIFY(e.storage.bytesFinal(huerfano));
}

void TestRecuperacionArchivos::finalDeUnPaqueteRegistradoNoEsHuerfano()
{
    Entorno e;
    const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const PaquetePersistido descargado = e.sembrarPaquete(sid, EstadoDescarga::Descargado);
    e.storage.agregarFinal(e.rutaFinal(descargado));
    QVERIFY(esperar(e.ejecutor->recuperar()));
    QVERIFY(e.almacen.logs.isEmpty());
    QCOMPARE(e.storage.finales(), QStringList{e.rutaFinal(descargado)});
    QVERIFY(e.storage.llamadas().filter(u"eliminarTemporal"_s).isEmpty());
}

void TestRecuperacionArchivos::finalSinSolicitudSoloDiagnostico()
{
    Entorno e;
    const SolicitudId desconocida = SolicitudId::generar();
    const QString final = rutaAjena(desconocida, "PAQ_01.zip");
    e.storage.agregarFinal(final, kCentinela);
    // Tambien una solicitud eliminada cuenta como no asociable.
    SolicitudPersistida& eliminada = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada);
    eliminada.eliminadoEn = kInicio;
    const QString deEliminada = rutaAjena(eliminada.id, "PAQ_02.zip");
    e.storage.agregarFinal(deEliminada);
    QTest::ignoreMessage(QtWarningMsg, mensaje(u"archivo final sin solicitud asociable (se conserva): "_s + final));
    QTest::ignoreMessage(QtWarningMsg, mensaje(u"archivo final sin solicitud asociable (se conserva): "_s + deEliminada));
    QVERIFY(esperar(e.ejecutor->recuperar()));
    QCOMPARE(e.storage.bytesFinal(final), std::optional(kCentinela));
    QVERIFY(e.storage.bytesFinal(deEliminada));
    QVERIFY(e.almacen.logs.isEmpty()); // D3: sin log_solicitud
}

void TestRecuperacionArchivos::escaneoFallidoNoBorraYSigueLaRecuperacionSqlite()
{
    Entorno e;
    const SolicitudId enviando = e.sembrar(EstadoLocal::Enviando).id;
    const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const PaquetePersistido paq = e.sembrarPaquete(sid, EstadoDescarga::Descargando);
    const QString temporal = temporalDe(e.rutaFinal(paq));
    e.storage.agregarTemporal(temporal);
    e.storage.programarFallaLectura(ErrorAlmacenamiento::LecturaRaiz);
    QTest::ignoreMessage(QtWarningMsg, mensaje(u"escaneo de recuperacion fallido (LecturaRaiz): no se borra nada"_s));
    QCOMPARE(esperar(e.ejecutor->recuperar())->desenlace, D::Aplicada);
    QCOMPARE(e.storage.temporales(), QStringList{temporal});
    QVERIFY(e.storage.llamadas().filter(u"eliminarTemporal"_s).isEmpty());
    // La recuperacion SQLite siguio.
    QCOMPARE(e.solicitud(enviando)->estadoLocal, EstadoLocal::EnvioIncierto);
    QCOMPARE(e.paquete(paq.id)->estadoDescarga, EstadoDescarga::Disponible);
}

void TestRecuperacionArchivos::existenciaNoCambiaElEstado()
{
    Entorno e;
    const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const PaquetePersistido descargado = e.sembrarPaquete(sid, EstadoDescarga::Descargado);
    const PaquetePersistido ausente = e.sembrarPaquete(sid, EstadoDescarga::Descargado);
    e.storage.agregarFinal(e.rutaFinal(descargado));
    QSignalSpy spy(e.ejecutor.get(), &OperacionExecutor::existenciaConsultada);
    const auto antes = e.almacen.paquetes;
    const auto eventosAntes = e.almacen.eventos.size();

    QCOMPARE(esperar(e.ejecutor->consultarExistencia(e.rutaFinal(descargado))), std::optional(ExistenciaArchivo::Presente));
    QCOMPARE(esperar(e.ejecutor->consultarExistencia(e.rutaFinal(ausente))), std::optional(ExistenciaArchivo::NoEncontrado));
    QCOMPARE(esperar(e.ejecutor->consultarExistencia(u"/Users/x/a.zip"_s)), std::optional(ExistenciaArchivo::ErrorComprobacion));
    e.storage.programarFallaLectura(ErrorAlmacenamiento::LecturaRaiz);
    QCOMPARE(esperar(e.ejecutor->consultarExistencia(e.rutaFinal(descargado))),
             std::optional(ExistenciaArchivo::ErrorComprobacion));

    // La senal llega al hilo grafico con el mismo resultado.
    QVERIFY(QTest::qWaitFor([&] { return spy.count() == 4; }, 30000));
    QCOMPARE(spy.at(0).at(0).toString(), e.rutaFinal(descargado));
    QCOMPARE(spy.at(0).at(1).value<ExistenciaArchivo>(), ExistenciaArchivo::Presente);
    QCOMPARE(spy.at(1).at(1).value<ExistenciaArchivo>(), ExistenciaArchivo::NoEncontrado);
    // Sin estado ni log: ni paquetes, ni logs, ni operaciones de persistencia.
    QCOMPARE(e.almacen.paquetes.size(), antes.size());
    for (qsizetype i = 0; i < antes.size(); ++i) {
        QCOMPARE(e.almacen.paquetes.at(i).estadoDescarga, antes.at(i).estadoDescarga);
    }
    QVERIFY(e.almacen.logs.isEmpty());
    QCOMPARE(e.almacen.eventos.size(), eventosAntes);
    // Detenido: ErrorComprobacion sin tocar el filesystem.
    QVERIFY(esperarVoid(e.ejecutor->detener()));
    QCOMPARE(esperar(e.ejecutor->consultarExistencia(e.rutaFinal(descargado))),
             std::optional(ExistenciaArchivo::ErrorComprobacion));
}

void TestRecuperacionArchivos::logsSinBytesNiRutasAbsolutas()
{
    Entorno e;
    const SolicitudId sid = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const PaquetePersistido paq = e.sembrarPaquete(sid, EstadoDescarga::Descargando);
    e.storage.agregarTemporal(temporalDe(e.rutaFinal(paq)));
    e.storage.agregarFinal(rutaAjena(sid, "HUERFANO_01.zip"), kCentinela);
    QVERIFY(esperar(e.ejecutor->recuperar()));
    QVERIFY(!e.almacen.logs.isEmpty());
    for (const auto& l : e.almacen.logs) {
        const QString texto = l.payloadResumenJson.value_or(QString()) + l.mensajeSat.value_or(QString());
        QVERIFY2(!texto.contains(QString::fromLatin1(kCentinela.mid(4))), qPrintable(texto));
        QVERIFY2(!texto.contains(u"/Users"_s) && !texto.contains(u"\"/"_s), qPrintable(texto));
    }
}
