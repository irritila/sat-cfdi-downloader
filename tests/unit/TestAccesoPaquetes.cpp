#include "TestAccesoPaquetes.h"

#include "EntornoOperaciones.h"

#include "application/paquetes/AccesoPaquetesService.h"

#include <QTest>

using namespace satcfdi;
using namespace pruebasT007;
using E = ResolucionRevelable::Estado;
using Revelable = fakes::FakePackageStorage::Revelable;

namespace {

// Volcado de la base en memoria (filas completas relevantes) para comparar
// antes y despues: solo lectura no debe cambiar nada.
QStringList volcado(const fakes::Almacen& a)
{
    QStringList v;
    for (const SolicitudPersistida& s : a.solicitudes) {
        v << QStringLiteral("s|%1|%2|%3|%4").arg(s.id.texto(), claveEstable(s.estadoLocal),
                                                 s.ultimoError.value_or(QString()),
                                                 s.eliminadoEn ? s.eliminadoEn->toString(Qt::ISODateWithMs) : QString());
    }
    for (const PaquetePersistido& p : a.paquetes) {
        v << QStringLiteral("p|%1|%2|%3|%4|%5")
                 .arg(p.id, claveEstable(p.estadoDescarga), p.rutaLocal.value_or(QString()),
                      p.ultimoError.value_or(QString()),
                      p.reconciliadoEn ? p.reconciliadoEn->toString(Qt::ISODateWithMs) : QString());
    }
    v << QStringLiteral("logs|%1").arg(a.logs.size());
    return v;
}

// Paquete Descargado con su final en el almacenamiento falso.
PaquetePersistido& descargado(Entorno& e, const SolicitudId& s, bool conArchivo = true)
{
    PaquetePersistido& p = e.sembrarPaquete(s, EstadoDescarga::Descargado);
    p.idPaqueteSat = QStringLiteral("4e80345d-917f-40bb-a98f-4a73939343c5_0%1").arg(e.almacen.paquetes.size());
    p.rutaLocal = e.rutaFinal(p);
    if (conArchivo) {
        e.storage.agregarFinal(*p.rutaLocal);
    }
    return p;
}

} // namespace

void TestAccesoPaquetes::archivoPresenteYAusente()
{
    Entorno e;
    AccesoPaquetesEjecutor acceso(*e.ejecutor);
    const SolicitudId s = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const PaquetePersistido p = descargado(e, s);

    const auto presente = esperar(acceso.resolverArchivoPaquete(p.id));
    QVERIFY(presente);
    QCOMPARE(presente->estado, E::Disponible);
    QCOMPARE(presente->rutaAbsoluta, e.storage.raizFicticia + QLatin1Char('/') + *p.rutaLocal);
    QVERIFY(presente->mensaje.isEmpty());

    // ZIP borrado entre la carga del detalle y el clic.
    e.storage.quitarFinal(*p.rutaLocal);
    const auto ausente = esperar(acceso.resolverArchivoPaquete(p.id));
    QCOMPARE(ausente->estado, E::NoEncontrado);
    QVERIFY(ausente->rutaAbsoluta.isEmpty());
    QCOMPARE(ausente->mensaje, QStringLiteral("Archivo local no encontrado"));
    QCOMPARE(e.paquete(p.id)->estadoDescarga, EstadoDescarga::Descargado); // no cambia
}

void TestAccesoPaquetes::archivoNoAplica()
{
    Entorno e;
    AccesoPaquetesEjecutor acceso(*e.ejecutor);
    const SolicitudId s = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    for (EstadoDescarga estado : {EstadoDescarga::Disponible, EstadoDescarga::Descargando, EstadoDescarga::Error,
                                  EstadoDescarga::Vencido}) {
        const QString id = e.sembrarPaquete(s, estado).id;
        const auto r = esperar(acceso.resolverArchivoPaquete(id));
        QVERIFY2(r && r->estado == E::NoAplica, qPrintable(claveEstable(estado)));
        QVERIFY(r->mensaje.isEmpty() && r->rutaAbsoluta.isEmpty());
    }
    QCOMPARE(esperar(acceso.resolverArchivoPaquete(QStringLiteral("inexistente")))->estado, E::NoAplica);

    // Solicitud eliminada: su paquete ya no es visible.
    const PaquetePersistido p = descargado(e, s);
    e.almacen.solicitudes.first().eliminadoEn = kInicio;
    QCOMPARE(esperar(acceso.resolverArchivoPaquete(p.id))->estado, E::NoAplica);
    QCOMPARE(e.storage.llamadasRevelables(Revelable::Archivo), 0); // sin filesystem
}

void TestAccesoPaquetes::carpetaDeSolicitud()
{
    Entorno e;
    AccesoPaquetesEjecutor acceso(*e.ejecutor);
    const SolicitudId s = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    e.sembrarPaquete(s, EstadoDescarga::Disponible);

    // Sin paquetes Descargado: no aplica (la accion no se ofrece).
    QCOMPARE(esperar(acceso.resolverCarpetaSolicitud(s))->estado, E::NoAplica);
    QCOMPARE(e.storage.llamadasRevelables(Revelable::Carpeta), 0);

    const PaquetePersistido p = descargado(e, s);
    const auto r = esperar(acceso.resolverCarpetaSolicitud(s));
    QCOMPARE(r->estado, E::Disponible);
    const QString carpeta = p.rutaLocal->section(QLatin1Char('/'), 0, -2);
    QCOMPARE(r->rutaAbsoluta, e.storage.raizFicticia + QLatin1Char('/') + carpeta);
    QVERIFY(e.storage.llamadas().contains(QStringLiteral("revelarCarpeta:") + *p.rutaLocal));

    // La carpeta ya no existe: mensaje D7 y no se crea.
    e.storage.programarRevelable(Revelable::Carpeta,
                                 fakes::FakePackageStorage::ResultadoRevelable::exito(RutaRevelable::noEncontrada()));
    const auto ausente = esperar(acceso.resolverCarpetaSolicitud(s));
    QCOMPARE(ausente->estado, E::NoEncontrado);
    QCOMPARE(ausente->mensaje, QStringLiteral("Carpeta de paquetes no encontrada"));

    // Solicitud eliminada o inexistente.
    e.almacen.solicitudes.first().eliminadoEn = kInicio;
    QCOMPARE(esperar(acceso.resolverCarpetaSolicitud(s))->estado, E::NoAplica);
    QCOMPARE(esperar(acceso.resolverCarpetaSolicitud(SolicitudId::generar()))->estado, E::NoAplica);
}

void TestAccesoPaquetes::raizDePaquetes()
{
    Entorno e;
    AccesoPaquetesEjecutor acceso(*e.ejecutor);
    const auto r = esperar(acceso.resolverRaizPaquetes());
    QCOMPARE(r->estado, E::Disponible);
    QCOMPARE(r->rutaAbsoluta, e.storage.raizFicticia);

    e.storage.establecerRaizAusente(true);
    const auto ausente = esperar(acceso.resolverRaizPaquetes());
    QCOMPARE(ausente->estado, E::NoEncontrado);
    QCOMPARE(ausente->mensaje, QStringLiteral("Carpeta de paquetes no encontrada"));

    e.storage.programarRevelable(Revelable::Raiz,
                                 fakes::FakePackageStorage::ResultadoRevelable::fallo(ErrorAlmacenamiento::LecturaRaiz));
    const auto error = esperar(acceso.resolverRaizPaquetes());
    QCOMPARE(error->estado, E::Error);
    QCOMPARE(error->mensaje, QStringLiteral("Carpeta de paquetes no encontrada"));
    QVERIFY(error->rutaAbsoluta.isEmpty());
}

void TestAccesoPaquetes::errorDeValidacionNoExponeRuta()
{
    Entorno e;
    AccesoPaquetesEjecutor acceso(*e.ejecutor);
    const SolicitudId s = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const QStringList hostiles = {QStringLiteral("/etc/passwd"), QStringLiteral("../../fuera.zip"),
                                  QStringLiteral("EKU9003173C9/2026-09/../../x.zip")};
    for (const QString& ruta : hostiles) {
        PaquetePersistido& p = e.sembrarPaquete(s, EstadoDescarga::Descargado);
        p.rutaLocal = ruta;
        const QString id = p.id;
        const auto r = esperar(acceso.resolverArchivoPaquete(id));
        QVERIFY2(r && r->estado == E::Error, qPrintable(ruta));
        QCOMPARE(r->mensaje, QStringLiteral("Archivo local no encontrado"));
        QVERIFY(r->rutaAbsoluta.isEmpty() && !r->mensaje.contains(ruta));
    }
    // Carpeta: el primer Descargado tiene ruta invalida -> Error, sin ruta.
    const auto c = esperar(acceso.resolverCarpetaSolicitud(s));
    QCOMPARE(c->estado, E::Error);
    QVERIFY(c->rutaAbsoluta.isEmpty());
    // Error de lectura de la base: Error con mensaje D7.
    e.almacen.fallos.insert(QStringLiteral("obtenerPaquete"), fakes::error(ErrorPersistencia::Tipo::Almacenamiento));
    QCOMPARE(esperar(acceso.resolverArchivoPaquete(QStringLiteral("x")))->estado, E::Error);
    QVERIFY(e.almacen.logs.isEmpty());
}

void TestAccesoPaquetes::soloLecturaYFueraDelHiloGrafico()
{
    Entorno e;
    AccesoPaquetesEjecutor acceso(*e.ejecutor);
    const SolicitudId s = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
    const PaquetePersistido p = descargado(e, s);
    const PaquetePersistido ausente = descargado(e, s, false);
    QVERIFY(e.drenar());
    const QStringList antes = volcado(e.almacen);
    e.almacen.eventos.clear();
    e.almacen.hilos.clear();

    QVERIFY(esperar(acceso.resolverArchivoPaquete(p.id)));
    QVERIFY(esperar(acceso.resolverArchivoPaquete(ausente.id)));
    QVERIFY(esperar(acceso.resolverCarpetaSolicitud(s)));
    QVERIFY(esperar(acceso.resolverRaizPaquetes()));

    QCOMPARE(volcado(e.almacen), antes);
    QVERIFY(!e.almacen.eventos.contains(QStringLiteral("begin")));
    QVERIFY(!e.almacen.eventos.contains(QStringLiteral("commit")));
    QVERIFY(!e.almacen.eventos.isEmpty());
    QVERIFY(!e.almacen.hilos.isEmpty());
    QVERIFY(!e.almacen.hilos.contains(QThread::currentThread())); // nunca el hilo grafico
    // Solo resoluciones revelables en el filesystem (nada de guardar/escanear).
    for (const QString& llamada : e.storage.llamadas()) {
        QVERIFY2(llamada.startsWith(QStringLiteral("revelar")), qPrintable(llamada));
    }
}

void TestAccesoPaquetes::ejecutorDetenidoDevuelveError()
{
    Entorno e;
    AccesoPaquetesEjecutor acceso(*e.ejecutor);
    QVERIFY(esperarVoid(e.ejecutor->detener()));
    const auto r = esperar(acceso.resolverRaizPaquetes());
    QCOMPARE(r->estado, E::Error);
    QCOMPARE(r->mensaje, QStringLiteral("Carpeta de paquetes no encontrada"));
    QCOMPARE(esperar(acceso.resolverArchivoPaquete(QStringLiteral("x")))->mensaje,
             QStringLiteral("Archivo local no encontrado"));
}
