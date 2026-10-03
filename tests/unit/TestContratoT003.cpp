#include "TestContratoT003.h"

#include "application/persistence/PersistenceDispatcher.h"
#include "application/profiles/DemoPerfilesSatService.h"
#include "application/requests/DemoSolicitudesService.h"
#include "domain/common/TimestampUtc.h"
#include "domain/logs/LogSolicitud.h"
#include "domain/paquetes/PaquetePersistido.h"
#include "domain/solicitudes/Duplicados.h"
#include "domain/solicitudes/SolicitudCanonica.h"

#include <QSignalSpy>
#include <QTest>
#include <QThread>

using namespace satcfdi;
using namespace Qt::StringLiterals;

namespace {

EntradaSolicitudCanonica entrada(TipoDescarga tipo, QDate inicial, QDate final_,
                                 QStringList contrapartes = {})
{
    EntradaSolicitudCanonica e;
    e.tipoDescarga = tipo;
    e.rfcPerfil = QStringLiteral("EKU9003173C9");
    e.fechaInicial = inicial;
    e.fechaFinal = final_;
    e.rfcContrapartes = std::move(contrapartes);
    return e;
}

} // namespace

void TestContratoT003::dedupKeyGoldenV1_data()
{
    QTest::addColumn<int>("caso");
    QTest::addColumn<QByteArray>("canonico");
    QTest::addColumn<QString>("clave");

    // SHA-256 calculado fuera de la implementacion con `printf ... | shasum -a 256`.
    QTest::newRow("emitidos sin contraparte")
        << 1
        << QByteArray("operacion_sat=SolicitaDescargaEmitidos\nrfc_solicitante=EKU9003173C9\n"
                      "rfc_emisor=EKU9003173C9\nfecha_inicial_sat=2026-08-01T00:00:00\n"
                      "fecha_final_sat=2026-08-31T23:59:59\ntipo_solicitud=CFDI\n"
                      "estado_comprobante=Vigente")
        << QStringLiteral("v1:dddbc6b90c7d11b456a6f289ca0c4cc413553ddc1185c5f2874ca534712853ed");
    QTest::newRow("recibidos con emisor, tipo y complemento")
        << 2
        << QByteArray("operacion_sat=SolicitaDescargaRecibidos\nrfc_solicitante=EKU9003173C9\n"
                      "rfc_emisor=XAXX010101000\nrfc_receptor=EKU9003173C9\n"
                      "fecha_inicial_sat=2026-09-01T00:00:00\nfecha_final_sat=2026-09-30T23:59:59\n"
                      "tipo_solicitud=CFDI\nestado_comprobante=Vigente\ntipo_comprobante=I\n"
                      "complemento=nomina12")
        << QStringLiteral("v1:e14269e3bae11d540ec7fe1e6b6ac41403ea7914550df750a85dc7fa24aa5145");
    QTest::newRow("emitidos con receptores y Ñ")
        << 3
        << QByteArray("operacion_sat=SolicitaDescargaEmitidos\nrfc_solicitante=EKU9003173C9\n"
                      "rfc_emisor=EKU9003173C9\nrfc_receptores=AAA010101AAA,\xC3\x91" "A\xC3\x91"
                      "010101AB1\nfecha_inicial_sat=2026-01-01T00:00:00\n"
                      "fecha_final_sat=2026-01-01T23:59:59\ntipo_solicitud=CFDI\n"
                      "estado_comprobante=Vigente")
        << QStringLiteral("v1:348da0edd220703fd164bd6808e1070ce54c4302342c45e273ccf136a6eab67d");
}

void TestContratoT003::dedupKeyGoldenV1()
{
    QFETCH(int, caso);
    QFETCH(QByteArray, canonico);
    QFETCH(QString, clave);

    EntradaSolicitudCanonica e;
    switch (caso) {
    case 1:
        e = entrada(TipoDescarga::Emitidos, QDate(2026, 8, 1), QDate(2026, 8, 31));
        break;
    case 2:
        e = entrada(TipoDescarga::Recibidos, QDate(2026, 9, 1), QDate(2026, 9, 30),
                    {QStringLiteral(" xaxx 010101000 ")});
        e.tipoComprobante = QStringLiteral(" i ");
        e.complemento = QStringLiteral(" nomina12 ");
        break;
    default:
        e = entrada(TipoDescarga::Emitidos, QDate(2026, 1, 1), QDate(2026, 1, 1),
                    {QString::fromUtf8("ñañ010101ab1"), QStringLiteral("AAA010101AAA"),
                     QStringLiteral("aaa010101aaa"), QStringLiteral("  ")});
        break;
    }
    const auto r = SolicitudCanonica::normalizar(e);
    QVERIFY(r.esExito());
    QCOMPARE(r.valor().serializacionCanonicaV1(), canonico);
    QCOMPARE(r.valor().dedupKey().texto(), clave);
    QCOMPARE(r.valor().dedupKey().version(), 1);
}

void TestContratoT003::canonicaMapeoDc2()
{
    const auto emitidos = SolicitudCanonica::normalizar(
        entrada(TipoDescarga::Emitidos, QDate(2026, 8, 1), QDate(2026, 8, 31),
                {QStringLiteral("XAXX010101000")})).valor();
    QCOMPARE(emitidos.tipoCfdi(), QStringLiteral("emitidos"));
    QCOMPARE(emitidos.operacionSat(), OperacionSat::SolicitaDescargaEmitidos);
    QCOMPARE(emitidos.rfcEmisor(), std::optional<QString>(QStringLiteral("EKU9003173C9")));
    QVERIFY(!emitidos.rfcReceptor().has_value());
    QCOMPARE(emitidos.rfcReceptoresJson(),
             std::optional<QString>(QStringLiteral("[\"XAXX010101000\"]")));
    QCOMPARE(emitidos.fechaInicialSat(), QStringLiteral("2026-08-01T00:00:00"));
    QCOMPARE(emitidos.fechaFinalSat(), QStringLiteral("2026-08-31T23:59:59"));

    const auto recibidos = SolicitudCanonica::normalizar(
        entrada(TipoDescarga::Recibidos, QDate(2026, 8, 1), QDate(2026, 8, 31),
                {QStringLiteral("XAXX010101000")})).valor();
    QCOMPARE(recibidos.tipoCfdi(), QStringLiteral("recibidos"));
    QCOMPARE(recibidos.rfcReceptor(), std::optional<QString>(QStringLiteral("EKU9003173C9")));
    QCOMPARE(recibidos.rfcEmisor(), std::optional<QString>(QStringLiteral("XAXX010101000")));
    QVERIFY(!recibidos.rfcReceptoresJson().has_value());
    QVERIFY(emitidos.dedupKey() != recibidos.dedupKey());

    QCOMPARE(parsearRfcReceptoresJson(*emitidos.rfcReceptoresJson()),
             std::optional<QStringList>(QStringList{QStringLiteral("XAXX010101000")}));
}

void TestContratoT003::canonicaInvarianteAOrdenYFormato()
{
    auto a = entrada(TipoDescarga::Emitidos, QDate(2026, 8, 1), QDate(2026, 8, 31),
                     {QStringLiteral("BBB010101BB1"), QStringLiteral("AAA010101AA1")});
    auto b = entrada(TipoDescarga::Emitidos, QDate(2026, 8, 1), QDate(2026, 8, 31),
                     {QStringLiteral(" aaa010101aa1"), QStringLiteral("bbb 010101 bb1"),
                      QStringLiteral("AAA010101AA1")});
    b.rfcPerfil = QStringLiteral(" eku9003173c9 ");
    b.complemento = QStringLiteral("   ");
    b.tipoComprobante = QString();
    QCOMPARE(SolicitudCanonica::normalizar(a).valor().dedupKey(),
             SolicitudCanonica::normalizar(b).valor().dedupKey());

    // Sensibilidad: cambiar un dia cambia la clave.
    auto c = a;
    c.fechaFinal = QDate(2026, 8, 30);
    QVERIFY(SolicitudCanonica::normalizar(a).valor().dedupKey()
            != SolicitudCanonica::normalizar(c).valor().dedupKey());
}

void TestContratoT003::canonicaRechazaEntradasInvalidas()
{
    using C = ErrorSolicitudCanonica::Codigo;
    auto e = entrada(TipoDescarga::Recibidos, QDate(2026, 8, 31), QDate(2026, 8, 1),
                     {QStringLiteral("XAXX010101000"), QStringLiteral("AAA010101AA1")});
    e.rfcPerfil = QStringLiteral("NO-ES-RFC");
    e.tipoComprobante = QStringLiteral("X");
    e.complemento = QStringLiteral("a=b");
    const auto r = SolicitudCanonica::normalizar(e);
    QVERIFY(!r.esExito());
    QList<C> codigos;
    for (const auto& err : r.error()) {
        codigos.append(err.codigo);
    }
    QCOMPARE(codigos, (QList<C>{C::RfcSolicitanteInvalido, C::ContraparteMultipleNoPermitida,
                                C::RangoFechasInvalido, C::TipoComprobanteInvalido,
                                C::CaracterReservado}));
}

void TestContratoT003::dedupKeyDesdeTexto()
{
    const QString v1 = QStringLiteral("v1:") + QString(64, u'a');
    QVERIFY(DedupKey::desdeTexto(v1).has_value());
    QCOMPARE(DedupKey::desdeTexto(QStringLiteral("v2:otra"))->version(), 2);
    for (const QString& malo : {QStringLiteral(""), QStringLiteral("v1:abc"), QStringLiteral("v0:x"),
                                QStringLiteral("x1:aa"), QStringLiteral("v1:"),
                                QStringLiteral("v1:") + QString(64, u'A')}) {
        QVERIFY2(!DedupKey::desdeTexto(malo).has_value(), qPrintable(malo));
    }
}

void TestContratoT003::matrizDuplicados()
{
    using L = EstadoLocal;
    using S = EstadoSolicitudSat;
    using D = EstadoDescarga;
    using K = ClasificacionDuplicado;
    using M = MotivoDuplicado;

    auto c = [](L local, std::optional<S> sat, QList<D> paquetes = {}, bool eliminada = false) {
        CoincidenciaDuplicado x;
        x.id = SolicitudId::generar();
        x.estadoLocal = local;
        x.estadoSat = sat;
        x.paquetesNoEliminados = std::move(paquetes);
        x.eliminada = eliminada;
        return x;
    };
    struct Caso {
        CoincidenciaDuplicado coincidencia;
        K esperado;
        M motivo;
    };
    const QList<Caso> casos = {
        {c(L::Creada, {}), K::Bloqueado, M::SolicitudEnCurso},
        {c(L::Enviando, {}), K::Bloqueado, M::SolicitudEnCurso},
        {c(L::Enviada, {}), K::Bloqueado, M::SolicitudEnCurso},
        {c(L::Enviada, S::Aceptada), K::Bloqueado, M::SolicitudEnCurso},
        {c(L::Enviada, S::EnProceso), K::Bloqueado, M::SolicitudEnCurso},
        {c(L::Enviada, S::Terminada, {D::Descargado, D::Error}), K::Bloqueado,
         M::TerminadaConPaquetesPendientes},
        {c(L::Enviada, S::Terminada, {D::Descargado}), K::Bloqueado, M::TerminadaDescargada},
        {c(L::Enviada, S::Terminada, {D::Descargado, D::Vencido}), K::RequiereConfirmacion,
         M::TerminadaConPaquetesVencidos},
        {c(L::Enviada, S::Terminada, {}), K::RequiereConfirmacion, M::TerminadaSinPaquetes},
        {c(L::EnvioIncierto, {}), K::RequiereConfirmacion, M::EnvioIncierto},
        {c(L::EnvioFallido, {}), K::RequiereConfirmacion, M::SolicitudSinExito},
        {c(L::Enviada, S::Error), K::RequiereConfirmacion, M::SolicitudSinExito},
        {c(L::Enviada, S::Rechazada), K::RequiereConfirmacion, M::SolicitudSinExito},
        {c(L::Enviada, S::Vencida), K::RequiereConfirmacion, M::SolicitudSinExito},
        {c(L::Creada, {}, {}, true), K::RequiereConfirmacion, M::SolicitudEliminada},
    };
    const DedupKey clave = *DedupKey::desdeTexto(QStringLiteral("v1:") + QString(64, u'0'));
    for (const Caso& caso : casos) {
        const auto r = clasificarCoincidencias(clave, {caso.coincidencia});
        QCOMPARE(r.clasificacion, caso.esperado);
        QCOMPARE(r.motivo, caso.motivo);
        QCOMPARE(r.solicitudReferencia, std::optional<SolicitudId>(caso.coincidencia.id));
        QCOMPARE(r.dedupKey, clave);
    }

    const auto libre = clasificarCoincidencias(clave, {});
    QCOMPARE(libre.clasificacion, K::Libre);
    QVERIFY(!libre.solicitudReferencia.has_value());

    // Precedencia: Bloqueado > RequiereConfirmacion.
    const auto confirmable = c(L::EnvioIncierto, {});
    const auto bloqueante = c(L::Creada, {});
    const auto r = clasificarCoincidencias(clave, {confirmable, bloqueante});
    QCOMPARE(r.clasificacion, K::Bloqueado);
    QCOMPARE(r.solicitudReferencia, std::optional<SolicitudId>(bloqueante.id));
}

void TestContratoT003::catalogosIgualesAlEsquema()
{
    // Literales copiados de 001_initial_schema.sql (CHECK de log_solicitud y
    // paquete_solicitud). Un cambio en cualquiera de los lados rompe la prueba.
    const QStringList tipos = {
        u"solicitud_creada"_s, u"duplicado_confirmado"_s, u"envio_iniciado"_s,
        u"solicitud_enviada"_s, u"envio_fallido"_s, u"envio_incierto"_s,
        u"verificacion_realizada"_s, u"verificacion_fallida"_s, u"paquetes_registrados"_s,
        u"descarga_iniciada"_s, u"paquete_descargado"_s, u"descarga_fallida"_s,
        u"descarga_interrumpida"_s, u"paquete_reconciliado"_s, u"paquete_vencido"_s,
        u"archivo_huerfano"_s, u"accion_pendiente_registrada"_s,
        u"accion_pendiente_descartada"_s,
    };
    QCOMPARE(qsizetype(kTiposEventoLog.size()), tipos.size());
    for (qsizetype i = 0; i < tipos.size(); ++i) {
        QCOMPARE(claveEstable(kTiposEventoLog.at(std::size_t(i))), tipos.at(i));
        QCOMPARE(tipoEventoLogDesdeClave(tipos.at(i)), std::optional(kTiposEventoLog.at(std::size_t(i))));
    }
    QCOMPARE(claveEstable(OrigenLog::Worker), u"worker"_s);
    QCOMPARE(claveEstable(OrigenLog::Usuario), u"usuario"_s);
    QCOMPARE(claveEstable(OrigenLog::Recuperacion), u"recuperacion"_s);
    QCOMPARE(claveEstable(OrigenCodigoSat::Creacion), u"creacion"_s);
    QCOMPARE(claveEstable(OrigenCodigoSat::Verificacion), u"verificacion"_s);
    QCOMPARE(claveEstable(OrigenCodigoSat::Descarga), u"descarga"_s);
    QCOMPARE(claveEstable(MotivoVencimiento::SolicitudExpirada), u"solicitud_expirada"_s);
    QCOMPARE(claveEstable(OrigenVencimiento::Sat), u"SAT"_s);
    QCOMPARE(claveEstable(OrigenVencimiento::EstimacionLocal), u"estimacion_local"_s);
    QCOMPARE(claveEstable(EstadoSolicitudSat::Error), u"Error"_s);
    QCOMPARE(estadoLocalDesdeClave(u"EnvioIncierto"), std::optional(EstadoLocal::EnvioIncierto));
    QCOMPARE(estadoDescargaDesdeClave(u"Vencido"), std::optional(EstadoDescarga::Vencido));
}

void TestContratoT003::timestampUtcIdaYVuelta()
{
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(1790000000123, QTimeZone(QTimeZone::UTC));
    const QString texto = timestamp::aTexto(t);
    QCOMPARE(texto.size(), 24);
    QVERIFY(texto.endsWith(u".123Z"));
    QCOMPARE(timestamp::desdeTexto(texto), std::optional<QDateTime>(t));
    QVERIFY(!timestamp::desdeTexto(u"2026-10-03 12:00:00").has_value());
}

void TestContratoT003::dispatcherEjecutaFueraDelHiloGrafico()
{
    PersistenceDispatcher dispatcher;
    QThread* grafico = QThread::currentThread();
    QFuture<bool> f = dispatcher.despachar<bool>([grafico]() {
        return QThread::currentThread() != grafico;
    });
    QTRY_VERIFY(f.isFinished());
    QVERIFY(f.result());

    dispatcher.cerrar();
    QVERIFY(dispatcher.cerrado());
    QFuture<int> tarde = dispatcher.despachar<int>([]() { return 1; });
    QVERIFY(tarde.isCanceled());
}

void TestContratoT003::demoEvaluaCreaYEliminaConSenales()
{
    DemoSolicitudesService servicio(DemoPerfilesSatService::perfilesDemo(),
                                    DemoSolicitudesService::Datos::Vacio);
    NuevaSolicitudRequest req;
    req.perfilId = DemoPerfilesSatService::perfilesDemo().constFirst().id;
    req.tipoDescarga = TipoDescarga::Emitidos;
    req.fechaInicial = QDate(2026, 8, 1);
    req.fechaFinal = QDate(2026, 8, 31);

    QCOMPARE(servicio.evaluarDuplicado(req).result().valor().clasificacion,
             ClasificacionDuplicado::Libre);
    QSignalSpy lista(&servicio, &SolicitudesService::listaCambiada);
    QSignalSpy eliminada(&servicio, &SolicitudesService::solicitudEliminada);
    const SolicitudId id = servicio.crear(req).result().valor();
    QCOMPARE(lista.count(), 1);

    const auto bloqueado = servicio.crear(req).result();
    QCOMPARE(bloqueado.error().tipo, ErrorCrear::Tipo::DedupBloqueado);
    QCOMPARE(bloqueado.error().duplicado->solicitudReferencia, std::optional(id));

    QVERIFY(servicio.eliminar(id).result().valor().cambio);
    QCOMPARE(eliminada.count(), 1);
    QCOMPARE(servicio.obtener(id).result().error().tipo, ErrorObtener::Tipo::NoEncontrada);
    QVERIFY(!servicio.eliminar(id).result().valor().cambio);
    QCOMPARE(eliminada.count(), 1);

    const auto confirmar = servicio.crear(req).result();
    QCOMPARE(confirmar.error().tipo, ErrorCrear::Tipo::RequiereConfirmacion);
    QCOMPARE(confirmar.error().duplicado->motivo, MotivoDuplicado::SolicitudEliminada);
    const auto creada = servicio.crearLocal(req, ConfirmacionDuplicado::Confirmada).result();
    QVERIFY(creada.esExito());
    QCOMPARE(servicio.obtener(creada.valor()).result().valor().logs.size(), 2);

    NuevaSolicitudRequest malo = req;
    malo.rfcContraparte = QStringLiteral("NO-RFC");
    QCOMPARE(servicio.evaluarDuplicado(malo).result().error().tipo, ErrorCrear::Tipo::FiltroInvalido);
}

void TestContratoT003::demoPerfilCrearYSembrar()
{
    DemoPerfilesSatService servicio({});
    QSignalSpy cambio(&servicio, &PerfilesSatService::perfilesCambiaron);
    const auto r = servicio.crear(QStringLiteral(" eku9003173c9 "), QStringLiteral(" Demo ")).result();
    QVERIFY(r.esExito());
    QCOMPARE(r.valor().rfc, u"EKU9003173C9"_s);
    QCOMPARE(r.valor().nombre, u"Demo"_s);
    QCOMPARE(cambio.count(), 1);
    QCOMPARE(servicio.listarNoEliminados().result().valor().constFirst().rfc, u"EKU9003173C9"_s);

    const auto dup = servicio.crear(QStringLiteral("EKU9003173C9"), QStringLiteral("Otro")).result();
    QCOMPARE(dup.error().tipo, ErrorCrearPerfil::Tipo::RfcDuplicado);
    const auto invalido = servicio.crear(QStringLiteral("x"), QString()).result();
    QCOMPARE(invalido.error().validaciones.size(), 2);
    QCOMPARE(cambio.count(), 1);

    // Siembra fuera del contrato: respeta `activo`.
    const auto sembrado = servicio.sembrar(QStringLiteral("CACX7605101P8"), QStringLiteral("Inactivo"), false);
    QVERIFY(sembrado.esExito() && !sembrado.valor().activo);
    QCOMPARE(servicio.obtener(sembrado.valor().id).result().valor()->nombre, u"Inactivo"_s);
    const auto renombrado = servicio.actualizarNombre(sembrado.valor().id, QStringLiteral("Renombrado")).result();
    QVERIFY(renombrado.esExito());
    QCOMPARE(renombrado.valor().rfc, u"CACX7605101P8"_s);
    QCOMPARE(cambio.count(), 3);
}
