#include "TestServiciosPersistidos.h"

#include "FakesPersistencia.h"

#include "application/logging/RegexLogSanitizer.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "application/profiles/PerfilesSatServicePersistido.h"
#include "application/requests/SolicitudesServicePersistido.h"
#include "domain/common/UuidCanonico.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>
#include <QTimer>

using namespace satcfdi;
using namespace Qt::StringLiterals;
using fakes::Almacen;

namespace {

const QDateTime kAhora = QDateTime(QDate(2026, 10, 3), QTime(12, 0, 0, 250), QTimeZone(QTimeZone::UTC));

// Grafo de prueba. Orden de miembros = orden de construccion: fakes, despues
// dispatcher, despues servicios. Al destruir: servicios, dispatcher (cerrar)
// y al final los fakes.
struct Entorno {
    Almacen almacen;
    fakes::FakePerfiles perfiles{almacen};
    fakes::FakeSolicitudes solicitudes{almacen};
    fakes::FakePaquetes paquetes{almacen};
    fakes::FakeLogs logs{almacen};
    fakes::FakeUnitOfWork uow{almacen};
    RegexLogSanitizer sanitizer;
    PersistenceDispatcher dispatcher;
    SolicitudesServicePersistido servicio{
        dispatcher, PuertosPersistencia{perfiles, solicitudes, paquetes, logs, uow, sanitizer},
        [] { return kAhora; }};
    QDateTime ahora = kAhora; // reloj de perfilesServicio (T005.1)
    PerfilesSatServicePersistido perfilesServicio{dispatcher, perfiles, uow, [this] { return ahora; }};

    PerfilId perfilActivo = PerfilId::generar();

    Entorno()
    {
        almacen.perfiles.append(PerfilSat{perfilActivo, QStringLiteral("EKU9003173C9"),
                                          QStringLiteral("Activo"), true, kAhora, kAhora, std::nullopt});
    }

    ~Entorno() { almacen.liberar(); }

    NuevaSolicitudRequest request() const
    {
        NuevaSolicitudRequest r;
        r.perfilId = perfilActivo;
        r.tipoDescarga = TipoDescarga::Recibidos;
        r.fechaInicial = QDate(2026, 9, 1);
        r.fechaFinal = QDate(2026, 9, 30);
        r.rfcContraparte = QStringLiteral("XAXX010101000");
        return r;
    }

    DedupKey claveDe(const NuevaSolicitudRequest& r) const
    {
        EntradaSolicitudCanonica e;
        e.tipoDescarga = r.tipoDescarga;
        e.rfcPerfil = QStringLiteral("EKU9003173C9");
        e.fechaInicial = r.fechaInicial;
        e.fechaFinal = r.fechaFinal;
        e.rfcContrapartes = {*r.rfcContraparte};
        return SolicitudCanonica::normalizar(e).valor().dedupKey();
    }

    // Fila existente con la misma clave en el estado indicado.
    SolicitudId sembrar(EstadoLocal local, std::optional<EstadoSolicitudSat> sat,
                        QList<EstadoDescarga> estadosPaquetes = {}, bool eliminada = false)
    {
        SolicitudPersistida s;
        s.id = SolicitudId::generar();
        s.perfilSatId = perfilActivo;
        s.tipoCfdi = TipoDescarga::Recibidos;
        s.operacionSat = OperacionSat::SolicitaDescargaRecibidos;
        s.rfcSolicitante = QStringLiteral("EKU9003173C9");
        s.fechaInicialSat = QStringLiteral("2026-09-01T00:00:00");
        s.fechaFinalSat = QStringLiteral("2026-09-30T23:59:59");
        s.dedupKey = claveDe(request());
        s.estadoLocal = local;
        s.estadoSolicitudSat = sat;
        s.creadaEn = kAhora.addDays(-1);
        if (eliminada) {
            s.eliminadoEn = kAhora.addSecs(-60);
        }
        almacen.solicitudes.append(s);
        int n = 0;
        for (EstadoDescarga e : estadosPaquetes) {
            PaquetePersistido p;
            p.id = uuid::generarCanonico();
            p.solicitudMasivaId = s.id;
            p.idPaqueteSat = QStringLiteral("PAQ_%1").arg(++n);
            p.estadoDescarga = e;
            p.disponibleEn = kAhora.addDays(-1);
            almacen.paquetes.append(p);
        }
        return s.id;
    }
};

// Espera sin bloquear el hilo grafico (procesa eventos) y devuelve el resultado.
template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, 30000) || f.isCanceled()) {
        return std::nullopt;
    }
    return f.result();
}

qsizetype contar(const QStringList& eventos, const char* op)
{
    return eventos.count(QString::fromLatin1(op));
}

} // namespace

void TestServiciosPersistidos::crearInsertaCreadaConLogSaneado()
{
    Entorno e;
    NuevaSolicitudRequest req = e.request();
    req.complemento = QStringLiteral("Bearer hunter2"); // complemento va a filtros del log
    const auto r = esperar(e.servicio.crear(req));
    QVERIFY(r && r->esExito());

    QCOMPARE(e.almacen.solicitudes.size(), 1);
    const SolicitudPersistida& s = e.almacen.solicitudes.constFirst();
    QCOMPARE(s.id, r->valor());
    QCOMPARE(s.estadoLocal, EstadoLocal::Creada);
    QVERIFY(!s.estadoSolicitudSat && !s.idSolicitudSat && !s.codEstatusSolicitud);
    QCOMPARE(s.rfcSolicitante, QStringLiteral("EKU9003173C9"));
    QCOMPARE(s.rfcReceptor, std::optional<QString>(QStringLiteral("EKU9003173C9")));
    QCOMPARE(s.rfcEmisor, std::optional<QString>(QStringLiteral("XAXX010101000")));
    QCOMPARE(s.creadaEn, kAhora);
    QVERIFY(s.dedupKey.texto().startsWith(u"v1:"));

    QCOMPARE(e.almacen.logs.size(), 1);
    const LogPersistido& l = e.almacen.logs.constFirst();
    QCOMPARE(l.tipoEvento, TipoEventoLog::SolicitudCreada);
    QCOMPARE(l.origen, OrigenLog::Usuario);
    QCOMPARE(l.solicitudMasivaId, s.id);
    QVERIFY(uuid::esCanonico(l.id));
    QVERIFY(l.payloadResumenJson);
    const QJsonObject payload = QJsonDocument::fromJson(l.payloadResumenJson->toUtf8()).object();
    QCOMPARE(payload.value(u"dedup_key").toString(), s.dedupKey.texto());
    QCOMPARE(payload.value(u"operacion_sat").toString(), QStringLiteral("SolicitaDescargaRecibidos"));
    QVERIFY(!l.payloadResumenJson->contains(u"hunter2"));

    QCOMPARE(e.almacen.eventos.filter(QRegularExpression(u"^(begin|commit|rollback)$"_s)),
             (QStringList{u"begin"_s, u"commit"_s}));
    QVERIFY(!e.almacen.hilos.contains(QThread::currentThread()));
}

void TestServiciosPersistidos::senalesTrasCommitYAntesDeCompletarFuture()
{
    Entorno e;
    // Barrera: la tarea no termina antes de que crear() encadene su .then().
    e.almacen.bloquearEn(u"commit"_s);
    QFuture<SolicitudesService::ResultadoCrear> f = e.servicio.crear(e.request());

    QStringList orden;
    bool futureTerminadoEnSenal = true;
    bool enHiloGrafico = false;
    QThread* grafico = QThread::currentThread();
    connect(&e.servicio, &SolicitudesService::listaCambiada, this, [&] {
        orden.append(u"listaCambiada"_s);
        futureTerminadoEnSenal = f.isFinished();
        enHiloGrafico = QThread::currentThread() == grafico;
        QVERIFY(e.almacen.eventos.contains(u"commit"_s));
    });
    connect(&e.servicio, &SolicitudesService::solicitudActualizada, this,
            [&](const SolicitudId&) { orden.append(u"solicitudActualizada"_s); });
    QVERIFY(!f.isFinished());
    e.almacen.soltar();

    const auto r = esperar(f);
    QVERIFY(r && r->esExito());
    QCOMPARE(orden, (QStringList{u"listaCambiada"_s, u"solicitudActualizada"_s}));
    QVERIFY(!futureTerminadoEnSenal);
    QVERIFY(enHiloGrafico);
}

void TestServiciosPersistidos::senalesDeEliminarYPerfilAntesDeCompletarFuture()
{
    Entorno e;
    const SolicitudId id = e.sembrar(EstadoLocal::Creada, {});
    QThread* grafico = QThread::currentThread();

    e.almacen.bloquearEn(u"commit"_s); // ver senalesTrasCommitYAntesDeCompletarFuture
    QFuture<SolicitudesService::ResultadoEliminar> fe = e.servicio.eliminar(id);
    QStringList orden;
    bool eliminarTerminado = true;
    connect(&e.servicio, &SolicitudesService::listaCambiada, this, [&] { orden.append(u"listaCambiada"_s); });
    connect(&e.servicio, &SolicitudesService::solicitudEliminada, this, [&](const SolicitudId& emitido) {
        orden.append(u"solicitudEliminada"_s);
        eliminarTerminado = fe.isFinished();
        QCOMPARE(emitido, id);
        QCOMPARE(QThread::currentThread(), grafico);
        QVERIFY(e.almacen.eventos.contains(u"commit"_s));
    });
    QVERIFY(!fe.isFinished());
    e.almacen.soltar();
    const auto r = esperar(fe);
    QVERIFY(r && r->esExito() && r->valor().cambio);
    QCOMPARE(orden, (QStringList{u"listaCambiada"_s, u"solicitudEliminada"_s}));
    QVERIFY(!eliminarTerminado);

    e.almacen.bloquearEn(u"commit"_s);
    QFuture<PerfilesSatService::ResultadoCrear> fp =
        e.perfilesServicio.crear(u"CACX7605101P8"_s, u"Nuevo"_s);
    bool perfilTerminado = true;
    bool perfilEnGrafico = false;
    connect(&e.perfilesServicio, &PerfilesSatService::perfilesCambiaron, this, [&] {
        perfilTerminado = fp.isFinished();
        perfilEnGrafico = QThread::currentThread() == grafico;
    });
    QVERIFY(!fp.isFinished());
    e.almacen.soltar();
    const auto rp = esperar(fp);
    QVERIFY(rp && rp->esExito());
    QVERIFY(!perfilTerminado);
    QVERIFY(perfilEnGrafico);
}

void TestServiciosPersistidos::listarYObtenerMapeanFilas()
{
    Entorno e;
    const SolicitudId terminada = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada,
                                            {EstadoDescarga::Descargado, EstadoDescarga::Vencido});
    e.almacen.solicitudes.last().idSolicitudSat = QStringLiteral("ID-SAT-1");
    e.almacen.solicitudes.last().numeroCfdi = 42;
    e.almacen.paquetes.last().eliminadoEn = kAhora; // no visible

    const auto lista = esperar(e.servicio.listar());
    QVERIFY(lista && lista->esExito());
    QCOMPARE(lista->valor().size(), 1);
    const SolicitudResumen& fila = lista->valor().constFirst();
    QCOMPARE(fila.id, terminada);
    QCOMPARE(fila.totalPaquetes, 1);
    QCOMPARE(fila.fechaInicial, QDate(2026, 9, 1));
    QCOMPARE(fila.fechaFinal, QDate(2026, 9, 30));
    QCOMPARE(fila.estadoSat, std::optional(EstadoSolicitudSat::Terminada));

    const auto detalle = esperar(e.servicio.obtener(terminada));
    QVERIFY(detalle && detalle->esExito());
    QCOMPARE(detalle->valor().paquetes.size(), 1);
    QCOMPARE(detalle->valor().resumen.totalPaquetes, 1);
    QCOMPARE(detalle->valor().idSolicitudSat, std::optional<QString>(u"ID-SAT-1"_s));
    QCOMPARE(detalle->valor().numeroCfdi, std::optional<qint64>(42));
    QCOMPARE(detalle->valor().fechaFinalSat, u"2026-09-30T23:59:59"_s);
    QVERIFY(!e.almacen.hilos.contains(QThread::currentThread()));
}

// T013 D8 (UX-09): nombre del perfil en la fila; orden creada_en DESC intacto.
void TestServiciosPersistidos::listarIncluyeNombreDelPerfil()
{
    Entorno e;
    const PerfilId sinNombre = PerfilId::generar();
    const PerfilId eliminado = PerfilId::generar();
    e.almacen.perfiles.append(PerfilSat{sinNombre, u"AAA010101AAA"_s, u"  "_s, false, kAhora, kAhora, std::nullopt});
    e.almacen.perfiles.append(PerfilSat{eliminado, u"ZZZ010101ZZZ"_s, u"Borrado"_s, true, kAhora, kAhora, kAhora});
    const SolicitudId a = e.sembrar(EstadoLocal::Creada, {}, {});
    const SolicitudId b = e.sembrar(EstadoLocal::Creada, {}, {});
    const SolicitudId c = e.sembrar(EstadoLocal::Creada, {}, {});
    const QList<SolicitudId> ids{a, b, c};
    const QList<PerfilId> perfiles{e.perfilActivo, sinNombre, eliminado};
    for (int i = 0; i < 3; ++i) {
        for (SolicitudPersistida& s : e.almacen.solicitudes) {
            if (s.id == ids.at(i)) {
                s.perfilSatId = perfiles.at(i);
                s.creadaEn = kAhora.addSecs(-3600 * (3 - i)); // c la mas reciente
            }
        }
    }
    const auto lista = esperar(e.servicio.listar());
    QVERIFY(lista && lista->esExito());
    QCOMPARE(lista->valor().size(), 3);
    QList<SolicitudId> orden;
    QHash<SolicitudId, QString> nombres;
    for (const SolicitudResumen& r : lista->valor()) {
        orden.append(r.id);
        nombres.insert(r.id, r.perfilNombre);
    }
    QCOMPARE(orden, (QList<SolicitudId>{c, b, a}));
    QCOMPARE(nombres.value(a), e.almacen.perfiles.constFirst().nombre);
    QVERIFY(!nombres.value(a).isEmpty());
    QCOMPARE(nombres.value(b), QString()); // perfil sin nombre
    QCOMPARE(nombres.value(c), QString()); // perfil eliminado
    QVERIFY(!e.almacen.hilos.contains(QThread::currentThread()));
}

// T014.1 D2: descargados y pendientes (Disponible + Error) por solicitud, en
// una sola consulta agrupada; eliminados excluidos; Vencido no es pendiente.
void TestServiciosPersistidos::listarConteaPaquetesPorEstado()
{
    using ED = EstadoDescarga;
    Entorno e;
    const SolicitudId ninguno = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada,
                                          {ED::Disponible, ED::Disponible, ED::Error});
    const SolicitudId dosDeTres = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada,
                                            {ED::Descargado, ED::Descargado, ED::Disponible});
    const SolicitudId todos = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada,
                                        {ED::Descargado, ED::Descargado, ED::Descargado});
    const SolicitudId vencidos = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada,
                                           {ED::Vencido, ED::Descargando, ED::Descargado, ED::Error});
    const SolicitudId sinPaquetes = e.sembrar(EstadoLocal::Creada, {}, {});
    // El ultimo paquete de `vencidos` (Error) esta eliminado: no cuenta.
    e.almacen.paquetes.last().eliminadoEn = kAhora;
    const qsizetype eventosAntes = e.almacen.eventos.size();

    const auto lista = esperar(e.servicio.listar());
    QVERIFY(lista && lista->esExito());
    QHash<SolicitudId, SolicitudResumen> filas;
    for (const SolicitudResumen& r : lista->valor()) {
        filas.insert(r.id, r);
    }
    auto conteo = [&](const SolicitudId& id) {
        const SolicitudResumen& r = filas[id];
        return QList<int>{r.totalPaquetes, r.paquetesDescargados, r.paquetesPendientesDescarga};
    };
    QCOMPARE(conteo(ninguno), (QList<int>{3, 0, 3}));
    QCOMPARE(conteo(dosDeTres), (QList<int>{3, 2, 1}));
    QCOMPARE(conteo(todos), (QList<int>{3, 3, 0}));
    QCOMPARE(conteo(vencidos), (QList<int>{3, 1, 0})); // Error eliminado; Vencido/Descargando no pendientes
    QCOMPARE(conteo(sinPaquetes), (QList<int>{0, 0, 0}));
    // Sin N+1: una sola lectura agregada de paquetes.
    const QStringList eventos = e.almacen.eventos.mid(eventosAntes);
    QCOMPARE(eventos.count(QStringLiteral("contarPaquetesPorEstado")), 1);
    QCOMPARE(eventos.count(QStringLiteral("listarPaquetes")), 0);

    // El detalle trae los mismos conteos.
    const auto detalle = esperar(e.servicio.obtener(dosDeTres));
    QVERIFY(detalle && detalle->esExito());
    QCOMPARE(detalle->valor().resumen.paquetesDescargados, 2);
    QCOMPARE(detalle->valor().resumen.paquetesPendientesDescarga, 1);
}

// T014.2: puedeReintentar (Error sin 5008) y reintentoPendiente por paquete.
void TestServiciosPersistidos::detalleExponeReintentoPorPaquete()
{
    using ED = EstadoDescarga;
    Entorno e;
    const SolicitudId id = e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada,
                                     {ED::Error, ED::Error, ED::Disponible, ED::Descargado});
    QList<PaquetePersistido*> paquetes;
    for (PaquetePersistido& p : e.almacen.paquetes) {
        if (p.solicitudMasivaId == id) {
            paquetes.append(&p);
        }
    }
    paquetes.at(1)->codigoDescargaSat = u"5008"_s;
    paquetes.at(0)->reintentoPendienteEn = kAhora;
    const auto detalle = esperar(e.servicio.obtener(id));
    QVERIFY(detalle && detalle->esExito());
    QHash<QString, PaqueteResumen> porId;
    for (const PaqueteResumen& r : detalle->valor().paquetes) {
        porId.insert(r.idPaqueteSat, r);
    }
    QVERIFY(porId[u"PAQ_1"_s].puedeReintentar);
    QVERIFY(porId[u"PAQ_1"_s].reintentoPendiente);
    QVERIFY(!porId[u"PAQ_2"_s].puedeReintentar); // 5008
    QVERIFY(!porId[u"PAQ_3"_s].puedeReintentar); // Disponible: se descarga automatico
    QVERIFY(!porId[u"PAQ_4"_s].puedeReintentar);
    QVERIFY(!porId[u"PAQ_2"_s].reintentoPendiente);
}

void TestServiciosPersistidos::obtenerInexistenteOEliminadaEsNoEncontrada()
{
    Entorno e;
    const SolicitudId eliminada = e.sembrar(EstadoLocal::Creada, {}, {}, true);
    for (const SolicitudId& id : {SolicitudId(), SolicitudId::generar(), eliminada}) {
        const auto r = esperar(e.servicio.obtener(id));
        QVERIFY(r && !r->esExito());
        QCOMPARE(r->error().tipo, ErrorObtener::Tipo::NoEncontrada);
    }
}

void TestServiciosPersistidos::matrizDuplicados_data()
{
    QTest::addColumn<int>("local");
    QTest::addColumn<int>("sat"); // -1 = sin estado SAT
    QTest::addColumn<QList<int>>("paquetes");
    QTest::addColumn<bool>("eliminada");
    QTest::addColumn<int>("esperado");

    using L = EstadoLocal;
    using S = EstadoSolicitudSat;
    using D = EstadoDescarga;
    using K = ClasificacionDuplicado;
    auto fila = [](const char* n, L l, int s, QList<int> p, bool el, K k) {
        QTest::newRow(n) << int(l) << s << p << el << int(k);
    };
    fila("sin coincidencias", L::Creada, -2, {}, false, K::Libre);
    fila("Creada", L::Creada, -1, {}, false, K::Bloqueado);
    fila("Enviando", L::Enviando, -1, {}, false, K::Bloqueado);
    fila("Enviada", L::Enviada, -1, {}, false, K::Bloqueado);
    fila("Aceptada", L::Enviada, int(S::Aceptada), {}, false, K::Bloqueado);
    fila("EnProceso", L::Enviada, int(S::EnProceso), {}, false, K::Bloqueado);
    fila("Terminada con Disponible", L::Enviada, int(S::Terminada), {int(D::Descargado), int(D::Disponible)}, false, K::Bloqueado);
    fila("Terminada con Descargando", L::Enviada, int(S::Terminada), {int(D::Descargando)}, false, K::Bloqueado);
    fila("Terminada con Error", L::Enviada, int(S::Terminada), {int(D::Vencido), int(D::Error)}, false, K::Bloqueado);
    fila("Terminada descargada", L::Enviada, int(S::Terminada), {int(D::Descargado), int(D::Descargado)}, false, K::Bloqueado);
    fila("Terminada con Vencido", L::Enviada, int(S::Terminada), {int(D::Descargado), int(D::Vencido)}, false, K::RequiereConfirmacion);
    fila("Terminada sin paquetes", L::Enviada, int(S::Terminada), {}, false, K::RequiereConfirmacion);
    fila("EnvioIncierto", L::EnvioIncierto, -1, {}, false, K::RequiereConfirmacion);
    fila("EnvioFallido", L::EnvioFallido, -1, {}, false, K::RequiereConfirmacion);
    fila("ErrorSat", L::Enviada, int(S::Error), {}, false, K::RequiereConfirmacion);
    fila("Rechazada", L::Enviada, int(S::Rechazada), {}, false, K::RequiereConfirmacion);
    fila("Vencida", L::Enviada, int(S::Vencida), {}, false, K::RequiereConfirmacion);
    fila("eliminada", L::Creada, -1, {}, true, K::RequiereConfirmacion);
}

void TestServiciosPersistidos::matrizDuplicados()
{
    QFETCH(int, local);
    QFETCH(int, sat);
    QFETCH(QList<int>, paquetes);
    QFETCH(bool, eliminada);
    QFETCH(int, esperado);
    const auto clasificacion = ClasificacionDuplicado(esperado);

    Entorno e;
    std::optional<SolicitudId> existente;
    if (sat != -2) {
        QList<EstadoDescarga> estados;
        for (int p : paquetes) {
            estados.append(EstadoDescarga(p));
        }
        existente = e.sembrar(EstadoLocal(local),
                              sat < 0 ? std::nullopt : std::optional(EstadoSolicitudSat(sat)),
                              estados, eliminada);
    }
    const qsizetype filasAntes = e.almacen.solicitudes.size();
    QSignalSpy lista(&e.servicio, &SolicitudesService::listaCambiada);

    const auto evaluacion = esperar(e.servicio.evaluarDuplicado(e.request()));
    QVERIFY(evaluacion && evaluacion->esExito());
    QCOMPARE(evaluacion->valor().clasificacion, clasificacion);
    QCOMPARE(evaluacion->valor().solicitudReferencia, existente);
    QCOMPARE(contar(e.almacen.eventos, "begin"), 0); // evaluar no escribe

    const auto sinConfirmar = esperar(e.servicio.crear(e.request()));
    QVERIFY(sinConfirmar);
    switch (clasificacion) {
    case ClasificacionDuplicado::Libre:
        QVERIFY(sinConfirmar->esExito());
        QCOMPARE(lista.count(), 1);
        QCOMPARE(e.almacen.logs.size(), 1);
        return;
    case ClasificacionDuplicado::Bloqueado:
        QCOMPARE(sinConfirmar->error().tipo, ErrorCrear::Tipo::DedupBloqueado);
        break;
    case ClasificacionDuplicado::RequiereConfirmacion:
        QCOMPARE(sinConfirmar->error().tipo, ErrorCrear::Tipo::RequiereConfirmacion);
        break;
    }
    QCOMPARE(sinConfirmar->error().duplicado->solicitudReferencia, existente);
    QCOMPARE(contar(e.almacen.eventos, "rollback"), 1);
    QCOMPARE(contar(e.almacen.eventos, "insertarCreada"), 0);
    QCOMPARE(e.almacen.solicitudes.size(), filasAntes);
    QCOMPARE(lista.count(), 0);

    const auto confirmada = esperar(e.servicio.crearLocal(e.request(), ConfirmacionDuplicado::Confirmada));
    QVERIFY(confirmada);
    if (clasificacion == ClasificacionDuplicado::Bloqueado) {
        // La confirmacion nunca habilita un bloqueado.
        QCOMPARE(confirmada->error().tipo, ErrorCrear::Tipo::DedupBloqueado);
        QCOMPARE(lista.count(), 0);
        QCOMPARE(e.almacen.solicitudes.size(), filasAntes);
        return;
    }
    QVERIFY(confirmada->esExito());
    QCOMPARE(lista.count(), 1);
    QCOMPARE(e.almacen.solicitudes.size(), filasAntes + 1);
    QList<TipoEventoLog> tipos;
    for (const LogPersistido& l : e.almacen.logs) {
        tipos.append(l.tipoEvento);
    }
    QCOMPARE(tipos, (QList<TipoEventoLog>{TipoEventoLog::SolicitudCreada,
                                          TipoEventoLog::DuplicadoConfirmado}));
}

void TestServiciosPersistidos::perfilInvalidoOInactivo_data()
{
    QTest::addColumn<int>("caso");
    QTest::addColumn<int>("codigo");
    using C = ErrorValidacion::Codigo;
    QTest::newRow("sin perfil") << 0 << int(C::PerfilRequerido);
    QTest::newRow("desconocido") << 1 << int(C::PerfilInexistente);
    QTest::newRow("inactivo") << 2 << int(C::PerfilInexistente);
    QTest::newRow("eliminado") << 3 << int(C::PerfilInexistente);
}

void TestServiciosPersistidos::perfilInvalidoOInactivo()
{
    QFETCH(int, caso);
    QFETCH(int, codigo);

    Entorno e;
    NuevaSolicitudRequest req = e.request();
    if (caso == 0) {
        req.perfilId = PerfilId();
    } else if (caso == 1) {
        req.perfilId = PerfilId::generar();
    } else {
        PerfilSat p{PerfilId::generar(), QStringLiteral("CACX7605101P8"), QStringLiteral("Otro"),
                    caso != 2, kAhora, kAhora, std::nullopt};
        if (caso == 3) {
            p.eliminadoEn = kAhora;
        }
        e.almacen.perfiles.append(p);
        req.perfilId = p.id;
    }
    QSignalSpy lista(&e.servicio, &SolicitudesService::listaCambiada);

    const auto r = esperar(e.servicio.crear(req));
    QVERIFY(r && !r->esExito());
    QCOMPARE(r->error().tipo, ErrorCrear::Tipo::Validacion);
    QCOMPARE(r->error().validaciones.size(), 1);
    QCOMPARE(int(r->error().validaciones.constFirst().codigo), codigo);
    const auto ev = esperar(e.servicio.evaluarDuplicado(req));
    QVERIFY(ev && !ev->esExito());
    QCOMPARE(ev->error().tipo, ErrorCrear::Tipo::Validacion);
    QCOMPARE(contar(e.almacen.eventos, "begin"), 0);
    QVERIFY(e.almacen.solicitudes.isEmpty());
    QCOMPARE(lista.count(), 0);
}

void TestServiciosPersistidos::filtroInvalidoNoAbreTransaccion()
{
    Entorno e;
    NuevaSolicitudRequest req = e.request();
    req.tipoComprobante = QStringLiteral("Z");
    const auto r = esperar(e.servicio.crear(req));
    QVERIFY(r && !r->esExito());
    QCOMPARE(r->error().tipo, ErrorCrear::Tipo::FiltroInvalido);
    QCOMPARE(r->error().filtros.constFirst().codigo, ErrorSolicitudCanonica::Codigo::TipoComprobanteInvalido);
    QCOMPARE(contar(e.almacen.eventos, "begin"), 0);
}

void TestServiciosPersistidos::violacionIndiceDedupEsBloqueado()
{
    Entorno e;
    e.almacen.fallos.insert(u"insertarCreada"_s,
                            fakes::error(ErrorPersistencia::Tipo::DedupBloqueado,
                                         "ux_solicitud_masiva_dedup_bloqueante"));
    QSignalSpy lista(&e.servicio, &SolicitudesService::listaCambiada);
    const auto r = esperar(e.servicio.crear(e.request()));
    QVERIFY(r && !r->esExito());
    QCOMPARE(r->error().tipo, ErrorCrear::Tipo::DedupBloqueado);
    QCOMPARE(r->error().duplicado->clasificacion, ClasificacionDuplicado::Bloqueado);
    QCOMPARE(contar(e.almacen.eventos, "rollback"), 1);
    QCOMPARE(lista.count(), 0);
}

void TestServiciosPersistidos::falloTrasInsertarHaceRollbackSinSenales_data()
{
    QTest::addColumn<QString>("operacion");
    QTest::addColumn<int>("tipoError");
    QTest::addColumn<int>("esperado");
    using T = ErrorPersistencia::Tipo;
    using C = ErrorCrear::Tipo;
    QTest::newRow("begin ocupado") << u"begin"_s << int(T::Ocupado) << int(C::Persistencia);
    QTest::newRow("clasificar") << u"clasificarDuplicado"_s << int(T::Almacenamiento) << int(C::Persistencia);
    QTest::newRow("insert integridad") << u"insertarCreada"_s << int(T::Integridad) << int(C::Integridad);
    QTest::newRow("log integridad") << u"agregarLog"_s << int(T::Integridad) << int(C::Integridad);
    QTest::newRow("commit") << u"commit"_s << int(T::Almacenamiento) << int(C::Persistencia);
}

void TestServiciosPersistidos::falloTrasInsertarHaceRollbackSinSenales()
{
    QFETCH(QString, operacion);
    QFETCH(int, tipoError);
    QFETCH(int, esperado);

    Entorno e;
    e.almacen.fallos.insert(operacion, fakes::error(ErrorPersistencia::Tipo(tipoError)));
    QSignalSpy lista(&e.servicio, &SolicitudesService::listaCambiada);
    QSignalSpy actualizada(&e.servicio, &SolicitudesService::solicitudActualizada);

    const auto r = esperar(e.servicio.crear(e.request()));
    QVERIFY(r && !r->esExito());
    QCOMPARE(int(r->error().tipo), esperado);
    QVERIFY(r->error().causa.has_value());
    if (operacion != u"begin") {
        QCOMPARE(contar(e.almacen.eventos, "rollback"), 1);
    }
    QVERIFY(e.almacen.solicitudes.isEmpty());
    QVERIFY(e.almacen.logs.isEmpty());
    QCOMPARE(lista.count(), 0);
    QCOMPARE(actualizada.count(), 0);
}

void TestServiciosPersistidos::eliminarEsTransaccionalEIdempotente()
{
    Entorno e;
    const auto creada = esperar(e.servicio.crear(e.request()));
    QVERIFY(creada && creada->esExito());
    const SolicitudId id = creada->valor();
    PaquetePersistido p;
    p.id = uuid::generarCanonico();
    p.solicitudMasivaId = id;
    p.idPaqueteSat = u"PAQ"_s;
    p.disponibleEn = kAhora;
    e.almacen.paquetes.append(p);

    QSignalSpy lista(&e.servicio, &SolicitudesService::listaCambiada);
    QSignalSpy eliminada(&e.servicio, &SolicitudesService::solicitudEliminada);

    const auto r = esperar(e.servicio.eliminar(id));
    QVERIFY(r && r->esExito());
    QVERIFY(r->valor().cambio);
    QCOMPARE(r->valor().eliminadoEn, std::optional(kAhora));
    QCOMPARE(e.almacen.solicitudes.constFirst().eliminadoEn, std::optional(kAhora));
    QCOMPARE(e.almacen.paquetes.constFirst().eliminadoEn, std::optional(kAhora));
    QCOMPARE(e.almacen.logs.constFirst().eliminadoEn, std::optional(kAhora));
    QCOMPARE(lista.count(), 1);
    QCOMPARE(eliminada.count(), 1);
    QCOMPARE(eliminada.constFirst().constFirst().value<SolicitudId>(), id);

    const auto detalle = esperar(e.servicio.obtener(id));
    QCOMPARE(detalle->error().tipo, ErrorObtener::Tipo::NoEncontrada);

    for (const SolicitudId& otro : {id, SolicitudId::generar(), SolicitudId()}) {
        const auto again = esperar(e.servicio.eliminar(otro));
        QVERIFY(again && again->esExito());
        QVERIFY(!again->valor().cambio);
        QVERIFY(!again->valor().eliminadoEn);
    }
    QCOMPARE(lista.count(), 1);
    QCOMPARE(eliminada.count(), 1);
    QCOMPARE(contar(e.almacen.eventos, "marcarPaquetes"), 1);
}

void TestServiciosPersistidos::eliminarConFalloHaceRollbackSinSenales()
{
    Entorno e;
    const SolicitudId id = e.sembrar(EstadoLocal::Creada, {}, {EstadoDescarga::Disponible});
    e.almacen.fallos.insert(u"marcarLogs"_s, fakes::error(ErrorPersistencia::Tipo::Ocupado));
    QSignalSpy eliminada(&e.servicio, &SolicitudesService::solicitudEliminada);

    const auto r = esperar(e.servicio.eliminar(id));
    QVERIFY(r && !r->esExito());
    QCOMPARE(r->error().tipo, ErrorPersistencia::Tipo::Ocupado);
    QVERIFY(!e.almacen.solicitudes.constFirst().eliminadoEn); // rollback restauro
    QVERIFY(!e.almacen.paquetes.constFirst().eliminadoEn);
    QCOMPARE(eliminada.count(), 0);
}

void TestServiciosPersistidos::crearPerfilNormalizaYEmite()
{
    Entorno e;
    QSignalSpy cambio(&e.perfilesServicio, &PerfilesSatService::perfilesCambiaron);
    const auto r = esperar(e.perfilesServicio.crear(u" cacx 7605101p8 "_s, u"  Persona Fisica "_s));
    QVERIFY(r && r->esExito());
    QCOMPARE(cambio.count(), 1);
    const PerfilSat& p = e.almacen.perfiles.last();
    QCOMPARE(r->valor(), (PerfilResumen{p.id, u"CACX7605101P8"_s, u"Persona Fisica"_s, true}));
    QCOMPARE(p.rfc, u"CACX7605101P8"_s);
    QCOMPARE(p.nombre, u"Persona Fisica"_s);
    QVERIFY(p.activo);
    QCOMPARE(p.creadoEn, kAhora);
    QCOMPARE(contar(e.almacen.eventos, "commit"), 1);

    const auto lista = esperar(e.perfilesServicio.listarNoEliminados());
    QVERIFY(lista && lista->esExito());
    QCOMPARE(lista->valor().size(), 2);
    QCOMPARE(lista->valor().constFirst().rfc, u"CACX7605101P8"_s); // orden por RFC
}

void TestServiciosPersistidos::crearPerfilDuplicadoEsRfcDuplicado()
{
    Entorno e;
    // El RFC queda reservado aunque el perfil este inactivo.
    e.almacen.perfiles.first().activo = false;
    QSignalSpy cambio(&e.perfilesServicio, &PerfilesSatService::perfilesCambiaron);
    const auto r = esperar(e.perfilesServicio.crear(u"EKU9003173C9"_s, u"Dup"_s));
    QVERIFY(r && !r->esExito());
    QCOMPARE(r->error().tipo, ErrorCrearPerfil::Tipo::RfcDuplicado);
    QCOMPARE(r->error().causa->restriccion, u"ux_perfil_sat_rfc_vigente"_s);
    QVERIFY(!r->error().mensaje.contains(u"EKU9003173C9"_s));
    QCOMPARE(contar(e.almacen.eventos, "rollback"), 1);
    QCOMPARE(e.almacen.perfiles.size(), 1);
    QCOMPARE(cambio.count(), 0);

    // Otra restriccion o fallo de almacenamiento NO es RfcDuplicado.
    e.almacen.fallos.insert(u"insertarPerfil"_s, fakes::error(ErrorPersistencia::Tipo::Integridad, "ck_x"));
    const auto otra = esperar(e.perfilesServicio.crear(u"CACX7605101P8"_s, u"Otro"_s));
    QCOMPARE(otra->error().tipo, ErrorCrearPerfil::Tipo::Persistencia);
    e.almacen.fallos.insert(u"insertarPerfil"_s, fakes::error(ErrorPersistencia::Tipo::Unicidad, "pk_perfil_sat"));
    const auto pk = esperar(e.perfilesServicio.crear(u"CACX7605101P8"_s, u"Otro"_s));
    QCOMPARE(pk->error().tipo, ErrorCrearPerfil::Tipo::Persistencia);
}

void TestServiciosPersistidos::crearPerfilInvalidoNoTocaPersistencia()
{
    Entorno e;
    const auto r = esperar(e.perfilesServicio.crear(u"NO-RFC"_s, u"  "_s));
    QVERIFY(r && !r->esExito());
    QCOMPARE(r->error().tipo, ErrorCrearPerfil::Tipo::Validacion);
    QCOMPARE(r->error().validaciones,
             (QList<CodigoValidacionPerfil>{CodigoValidacionPerfil::RfcInvalido,
                                            CodigoValidacionPerfil::NombreRequerido}));
    QVERIFY(r->error().tieneErrorEn(CampoPerfil::Rfc));
    QVERIFY(r->error().tieneErrorEn(CampoPerfil::Nombre));
    const auto soloNombre = esperar(e.perfilesServicio.crear(u"CACX7605101P8"_s, u""_s));
    QVERIFY(!soloNombre->error().tieneErrorEn(CampoPerfil::Rfc));
    QVERIFY(soloNombre->error().tieneErrorEn(CampoPerfil::Nombre));
    QVERIFY(e.almacen.eventos.isEmpty());
}

void TestServiciosPersistidos::listarNoEliminadosYObtenerIncluyenInactivos()
{
    Entorno e;
    const PerfilId inactivo = PerfilId::generar();
    const PerfilId eliminado = PerfilId::generar();
    e.almacen.perfiles.append(PerfilSat{inactivo, u"AAA010101AAA"_s, u"Inactivo"_s, false, kAhora, kAhora, std::nullopt});
    e.almacen.perfiles.append(PerfilSat{eliminado, u"ZZZ010101ZZZ"_s, u"Eliminado"_s, true, kAhora, kAhora, kAhora});

    const auto lista = esperar(e.perfilesServicio.listarNoEliminados());
    QVERIFY(lista && lista->esExito());
    QCOMPARE(lista->valor().size(), 2);
    QCOMPARE(lista->valor().at(0), (PerfilResumen{inactivo, u"AAA010101AAA"_s, u"Inactivo"_s, false}));
    QCOMPARE(lista->valor().at(1).id, e.perfilActivo);

    const auto activo = esperar(e.perfilesServicio.obtener(inactivo));
    QVERIFY(activo && activo->esExito() && activo->valor());
    QVERIFY(!activo->valor()->activo);
    QVERIFY(!esperar(e.perfilesServicio.obtener(eliminado))->valor());
    QVERIFY(!esperar(e.perfilesServicio.obtener(PerfilId()))->valor());

    e.almacen.fallos.insert(u"listarPerfilesVisibles"_s, fakes::error(ErrorPersistencia::Tipo::Almacenamiento));
    const auto fallo = esperar(e.perfilesServicio.listarNoEliminados());
    QVERIFY(fallo && !fallo->esExito());
}

void TestServiciosPersistidos::actualizarNombreSoloCambiaNombre()
{
    Entorno e;
    e.almacen.perfiles.first().activo = false; // tambien inactivos
    const QDateTime despues = kAhora.addSecs(60);
    e.ahora = despues;
    QSignalSpy cambio(&e.perfilesServicio, &PerfilesSatService::perfilesCambiaron);
    const auto r = esperar(e.perfilesServicio.actualizarNombre(e.perfilActivo, u"  Nuevo nombre "_s));
    QVERIFY(r && r->esExito());
    QCOMPARE(r->valor(), (PerfilResumen{e.perfilActivo, u"EKU9003173C9"_s, u"Nuevo nombre"_s, false}));
    QCOMPARE(e.almacen.perfiles.first().rfc, u"EKU9003173C9"_s);
    QCOMPARE(e.almacen.perfiles.first().actualizadoEn, despues);
    QCOMPARE(cambio.count(), 1);

    const auto vacio = esperar(e.perfilesServicio.actualizarNombre(e.perfilActivo, u"   "_s));
    QCOMPARE(vacio->error().tipo, ErrorActualizarPerfil::Tipo::Validacion);
    QVERIFY(vacio->error().tieneErrorEn(CampoPerfil::Nombre));
    QVERIFY(!vacio->error().tieneErrorEn(CampoPerfil::Rfc));

    const auto inexistente = esperar(e.perfilesServicio.actualizarNombre(PerfilId::generar(), u"X"_s));
    QCOMPARE(inexistente->error().tipo, ErrorActualizarPerfil::Tipo::PerfilInexistente);
    QVERIFY(contar(e.almacen.eventos, "rollback") >= 1);

    e.almacen.fallos.insert(u"commit"_s, fakes::error(ErrorPersistencia::Tipo::Ocupado));
    const auto commit = esperar(e.perfilesServicio.actualizarNombre(e.perfilActivo, u"Otro"_s));
    QCOMPARE(commit->error().tipo, ErrorActualizarPerfil::Tipo::Persistencia);
    QCOMPARE(e.almacen.perfiles.first().nombre, u"Nuevo nombre"_s); // rollback
    QCOMPARE(cambio.count(), 1);
}

void TestServiciosPersistidos::hiloGraficoNoSeBloquea()
{
    Entorno e;
    e.almacen.bloquearListar = true;

    QFuture<SolicitudesService::ResultadoLista> f = e.servicio.listar();
    int ticks = 0;
    QTimer timer;
    timer.setInterval(5);
    connect(&timer, &QTimer::timeout, this, [&] { ++ticks; });
    timer.start();

    QTRY_VERIFY(e.almacen.listarBloqueado.load());
    QTRY_VERIFY(ticks >= 5); // el event loop grafico avanza con el repositorio bloqueado
    QVERIFY(!f.isFinished());

    e.almacen.liberar();
    QTRY_VERIFY(f.isFinished());
    QVERIFY(f.result().esExito());
    QVERIFY(!e.almacen.hilos.contains(QThread::currentThread()));
}
