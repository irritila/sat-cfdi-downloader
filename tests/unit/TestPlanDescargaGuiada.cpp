#include "TestPlanDescargaGuiada.h"

#include "domain/cfdi/PlanDescargaGuiada.h"
#include "domain/solicitudes/OperacionSat.h"

#include <QTest>
#include <QTimeZone>

using namespace satcfdi;

namespace {

// Datos sinteticos.
const QString kRfc = QStringLiteral("XAXX010101000");
const PerfilId kPerfil = *PerfilId::desdeTexto(u"0f000000-0000-4000-8000-000000000001");
const MesCalendario kSeptiembre = *MesCalendario::crear(2026, 9);
const MesCalendario kOctubre = *MesCalendario::crear(2026, 10);
using D = TipoDescarga;
using Ev = EvidenciaSolicitudMensual;
using Tramos = QList<std::pair<QString, QString>>;

QDateTime utc(int a, int m, int d, int h, int mi, int s = 0)
{
    return QDateTime(QDate(a, m, d), QTime(h, mi, s), QTimeZone(QTimeZone::UTC));
}
// 2026-10-20 12:00 Centro.
const QDateTime kAhora = utc(2026, 10, 20, 18, 0);

int g_n = 0;

// Solicitud canonica Terminada, indexada (acredita su tramo).
Ev acreditada(D d, const QString& inicio, const QString& fin)
{
    Ev e;
    SolicitudPersistida& s = e.solicitud;
    const int n = ++g_n;
    s.id = *SolicitudId::desdeTexto(QStringLiteral("5e000000-0000-4000-8000-%1").arg(n, 12, 10, QLatin1Char('0')));
    s.perfilSatId = kPerfil;
    s.tipoCfdi = d;
    s.operacionSat = operacionPara(d);
    s.rfcSolicitante = kRfc;
    (d == D::Emitidos ? s.rfcEmisor : s.rfcReceptor) = kRfc;
    s.fechaInicialSat = inicio;
    s.fechaFinalSat = fin;
    s.estadoLocal = EstadoLocal::Enviada;
    s.estadoSolicitudSat = EstadoSolicitudSat::Terminada;
    s.creadaEn = utc(2026, 1, 1, 0, 0).addSecs(n);
    EvidenciaPaqueteMensual p;
    p.paqueteId = QStringLiteral("p%1").arg(n);
    p.estadoDescarga = EstadoDescarga::Descargado;
    p.indice = EstadoIndicePaquete::Indexado;
    p.indiceVigente = true;
    e.paquetes = {p};
    return e;
}

Ev enCurso(D d, const QString& inicio, const QString& fin)
{
    Ev e = acreditada(d, inicio, fin);
    e.solicitud.estadoSolicitudSat = EstadoSolicitudSat::EnProceso;
    e.paquetes.clear();
    return e;
}

Ev fallida(D d, const QString& codigo, MesCalendario mes = kSeptiembre)
{
    Ev e = acreditada(d, mes.inicioSat(), mes.finSat());
    e.solicitud.estadoLocal = EstadoLocal::EnvioFallido;
    e.solicitud.estadoSolicitudSat.reset();
    e.solicitud.codEstatusSolicitud = codigo;
    e.paquetes.clear();
    return e;
}

Tramos texto(const QList<TramoMensual>& tramos)
{
    Tramos r;
    for (const TramoMensual& t : tramos) {
        r.append({centro::fechaSat(t.inicio), centro::fechaSat(t.fin)});
    }
    return r;
}

// Plan de la accion pedida; si la oferta no la incluye, planificar debe
// rechazarla (AccionNoDisponible) y se devuelve un plan vacio.
PlanDescargaGuiada planDe(MesCalendario mes, const QDateTime& instante, const QList<Ev>& evidencia,
                          AccionGuiada accion = AccionGuiada::DescargarFacturas)
{
    const auto e = evaluarCoberturaMensual(kPerfil, mes, instante, evidencia);
    const auto r = planificarDescargaGuiada(e, accion);
    if (r) {
        return r.valor();
    }
    if (r.error() != ErrorPlanGuiado::AccionNoDisponible || ofertaDescargaGuiada(e).accion == accion) {
        qFatal("plan inesperado");
    }
    PlanDescargaGuiada vacio;
    vacio.accion = accion;
    vacio.recibidos.direccion = D::Recibidos;
    return vacio;
}

std::optional<AccionGuiada> ofertaDe(MesCalendario mes, const QDateTime& instante, const QList<Ev>& evidencia)
{
    return ofertaDescargaGuiada(evaluarCoberturaMensual(kPerfil, mes, instante, evidencia)).accion;
}

} // namespace

void TestPlanDescargaGuiada::mesCerradoSinEvidenciaPideMesCompletoEnAmbas()
{
    const PlanDescargaGuiada p = planDe(kSeptiembre, kAhora, {});
    const Tramos mes{{"2026-09-01T00:00:00", "2026-09-30T23:59:59"}};
    QCOMPARE(texto(p.emitidos.crear), mes);
    QCOMPARE(texto(p.recibidos.crear), mes);
    QVERIFY(p.emitidos.reutilizar.isEmpty() && p.recibidos.reutilizar.isEmpty());
    QCOMPARE(ofertaDe(kSeptiembre, kAhora, {}), std::optional(AccionGuiada::DescargarFacturas));
    // Determinista: repetir con la misma evidencia da el mismo plan.
    QCOMPARE(planDe(kSeptiembre, kAhora, {}), p);
}

void TestPlanDescargaGuiada::reutilizaEnCursoYNoPideDireccionCubierta()
{
    const Ev emitidosEnCurso = enCurso(D::Emitidos, kSeptiembre.inicioSat(), kSeptiembre.finSat());
    // Una direccion en curso se reutiliza; la otra se pide.
    const PlanDescargaGuiada p = planDe(kSeptiembre, kAhora, {emitidosEnCurso});
    QCOMPARE(p.emitidos.reutilizar, QList<SolicitudId>{emitidosEnCurso.solicitud.id});
    QVERIFY(p.emitidos.crear.isEmpty());
    QCOMPARE(p.recibidos.crear.size(), 1);
    QCOMPARE(ofertaDe(kSeptiembre, kAhora, {emitidosEnCurso}), std::optional(AccionGuiada::DescargarFacturas));
    // Direccion cubierta hasta la frontera: nada para ella.
    const Ev recibidosCompleta = acreditada(D::Recibidos, kSeptiembre.inicioSat(), kSeptiembre.finSat());
    const PlanDescargaGuiada q = planDe(kSeptiembre, kAhora, {emitidosEnCurso, recibidosCompleta});
    QCOMPARE(q.recibidos, (PlanDireccion{D::Recibidos, std::nullopt, {}, {}}));
    QVERIFY(!q.creaAlgo());
    // Solo trabajo reutilizable: no se ofrece boton.
    QCOMPARE(ofertaDe(kSeptiembre, kAhora, {emitidosEnCurso, recibidosCompleta}), std::nullopt);
    // Tras crear ambas (quedan en curso), repetir la accion no crea nada.
    const Ev r = enCurso(D::Recibidos, kSeptiembre.inicioSat(), kSeptiembre.finSat());
    QVERIFY(!planDe(kSeptiembre, kAhora, {emitidosEnCurso, r}).creaAlgo());
}

void TestPlanDescargaGuiada::mesEnCursoConRelojCentro()
{
    // Dia 10 a las 14:05 Centro (20:05Z): del dia 1 a las 14:05:00 del dia 10.
    const QDateTime dia10 = utc(2026, 10, 10, 20, 5);
    const PlanDescargaGuiada inicial = planDe(kOctubre, dia10, {});
    const Tramos primero{{"2026-10-01T00:00:00", "2026-10-10T14:05:00"}};
    QCOMPARE(texto(inicial.emitidos.crear), primero);
    QCOMPARE(texto(inicial.recibidos.crear), primero);
    QCOMPARE(ofertaDe(kOctubre, dia10, {}), std::optional(AccionGuiada::DescargarFacturas));

    // Ya descargado: el mismo dia no se ofrece "Actualizar mes".
    const QList<Ev> cubierto = {acreditada(D::Emitidos, kOctubre.inicioSat(), QStringLiteral("2026-10-10T14:05:00")),
                                acreditada(D::Recibidos, kOctubre.inicioSat(), QStringLiteral("2026-10-10T14:05:00"))};
    const QDateTime mismoDia = utc(2026, 10, 11, 5, 59, 59); // 10 oct 23:59:59 Centro
    QCOMPARE(ofertaDe(kOctubre, mismoDia, cubierto), std::nullopt);
    QVERIFY(!planDe(kOctubre, mismoDia, cubierto, AccionGuiada::ActualizarMes).creaAlgo());

    // Dia 12: de 14:05:01 del dia 10 al instante de la accion.
    const QDateTime dia12 = utc(2026, 10, 12, 15, 30); // 09:30 Centro
    QCOMPARE(ofertaDe(kOctubre, dia12, cubierto), std::optional(AccionGuiada::ActualizarMes));
    const PlanDescargaGuiada actualizar = planDe(kOctubre, dia12, cubierto, AccionGuiada::ActualizarMes);
    const Tramos segundo{{"2026-10-10T14:05:01", "2026-10-12T09:30:00"}};
    QCOMPARE(texto(actualizar.emitidos.crear), segundo);
    QCOMPARE(texto(actualizar.recibidos.crear), segundo);
    // Contiguo y sin solape con el primero.
    QCOMPARE(actualizar.emitidos.crear.first().inicio, inicial.emitidos.crear.first().fin.addSecs(1));
}

void TestPlanDescargaGuiada::completarMesPorHuecosYReserva()
{
    // Cobertura del 1 al 10 (14:05): una sola solicitud hasta el fin del mes.
    const Ev hasta10 = acreditada(D::Emitidos, kSeptiembre.inicioSat(), QStringLiteral("2026-09-10T14:05:00"));
    const Ev recibidos = acreditada(D::Recibidos, kSeptiembre.inicioSat(), kSeptiembre.finSat());
    QCOMPARE(ofertaDe(kSeptiembre, kAhora, {hasta10, recibidos}), std::optional(AccionGuiada::CompletarMes));
    const PlanDescargaGuiada p = planDe(kSeptiembre, kAhora, {hasta10, recibidos}, AccionGuiada::CompletarMes);
    QCOMPARE(texto(p.emitidos.crear), (Tramos{{"2026-09-10T14:05:01", "2026-09-30T23:59:59"}}));
    QVERIFY(p.recibidos.crear.isEmpty());

    // Huecos inicial, interno y final: una solicitud por hueco maximo.
    const QList<Ev> conHuecos = {acreditada(D::Emitidos, QStringLiteral("2026-09-05T00:00:00"), QStringLiteral("2026-09-10T23:59:59")),
                                 acreditada(D::Emitidos, QStringLiteral("2026-09-12T00:00:00"), QStringLiteral("2026-09-20T23:59:59")),
                                 recibidos};
    const auto evaluacion = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, conHuecos);
    const PlanDescargaGuiada h = planificarDescargaGuiada(evaluacion, AccionGuiada::CompletarMes).valor();
    QCOMPARE(texto(h.emitidos.crear), (Tramos{{"2026-09-01T00:00:00", "2026-09-04T23:59:59"},
                                              {"2026-09-11T00:00:00", "2026-09-11T23:59:59"},
                                              {"2026-09-21T00:00:00", "2026-09-30T23:59:59"}}));
    for (const TramoMensual& c : h.emitidos.crear) {
        for (const TramoMensual& t : evaluacion.emitidos.tramos) {
            QVERIFY(c.fin < t.inicio || c.inicio > t.fin); // ningun segundo acreditado
        }
    }

    // Una solicitud en curso que intersecta un hueco lo reserva: se reutiliza
    // y solo se piden los demas huecos.
    const Ev reserva = enCurso(D::Emitidos, QStringLiteral("2026-09-25T00:00:00"), QStringLiteral("2026-09-30T23:59:59"));
    QList<Ev> conReserva = conHuecos;
    conReserva.append(reserva);
    const PlanDescargaGuiada r = planDe(kSeptiembre, kAhora, conReserva, AccionGuiada::CompletarMes);
    QCOMPARE(r.emitidos.reutilizar, QList<SolicitudId>{reserva.solicitud.id});
    // El hueco final 21-30 se reutiliza en 25-30 y se pide su parte libre.
    QCOMPARE(texto(r.emitidos.crear), (Tramos{{"2026-09-01T00:00:00", "2026-09-04T23:59:59"},
                                              {"2026-09-11T00:00:00", "2026-09-11T23:59:59"},
                                              {"2026-09-21T00:00:00", "2026-09-24T23:59:59"}}));

    // Mes cerrado completo: no se ofrece descarga.
    const Ev emitidos = acreditada(D::Emitidos, kSeptiembre.inicioSat(), kSeptiembre.finSat());
    QCOMPARE(ofertaDe(kSeptiembre, kAhora, {emitidos, recibidos}), std::nullopt);
    QVERIFY(!planDe(kSeptiembre, kAhora, {emitidos, recibidos}).creaAlgo());
}

void TestPlanDescargaGuiada::inhibicion5002y5005()
{
    const Ev f5002 = fallida(D::Emitidos, QStringLiteral("5002"));
    const auto e = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {f5002});
    const PlanDescargaGuiada p = planificarDescargaGuiada(e, AccionGuiada::DescargarFacturas).valor();
    QCOMPARE(p.emitidos.inhibidaPor, std::optional(CausaAtencion::Codigo5002));
    QVERIFY(p.emitidos.crear.isEmpty());
    QCOMPARE(p.recibidos.crear.size(), 1); // la otra se crea si esta libre
    const OfertaDescargaGuiada o = ofertaDescargaGuiada(e);
    QCOMPARE(o.direccionesInhibidas, QList<TipoDescarga>{D::Emitidos});
    QCOMPARE(o.accion, std::optional(AccionGuiada::DescargarFacturas));
    QVERIFY(o.diagnostico5005.isEmpty());
    QCOMPARE(e.estado, EstadoMes::RequiereAtencion);

    const Ev f5005 = fallida(D::Recibidos, QStringLiteral("5005"));
    const auto e2 = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {f5002, f5005});
    const OfertaDescargaGuiada o2 = ofertaDescargaGuiada(e2);
    QCOMPARE(o2.direccionesInhibidas, (QList<TipoDescarga>{D::Emitidos, D::Recibidos}));
    QCOMPARE(o2.diagnostico5005.size(), 1);
    QCOMPARE(o2.diagnostico5005.first().solicitud, f5005.solicitud.id);
    QCOMPARE(o2.accion, std::nullopt); // ninguna direccion libre
    QCOMPARE(planificarDescargaGuiada(e2, AccionGuiada::DescargarFacturas).error(), ErrorPlanGuiado::AccionNoDisponible);

    // 5002 superado por cobertura posterior (ADR 0020): deja de inhibir.
    const Ev posterior = acreditada(D::Emitidos, kSeptiembre.inicioSat(), kSeptiembre.finSat());
    const auto e3 = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {f5002, posterior});
    QVERIFY(!planificarDescargaGuiada(e3, AccionGuiada::CompletarMes).valor().emitidos.inhibidaPor);
    QVERIFY(ofertaDescargaGuiada(e3).direccionesInhibidas.isEmpty());
}

void TestPlanDescargaGuiada::mesFuturo()
{
    const auto e = evaluarCoberturaMensual(kPerfil, *MesCalendario::crear(2026, 11), kAhora, {});
    const auto p = planificarDescargaGuiada(e, AccionGuiada::DescargarFacturas);
    QVERIFY(!p);
    QCOMPARE(p.error(), ErrorPlanGuiado::MesFuturo);
    QCOMPARE(ofertaDescargaGuiada(e).accion, std::nullopt);
}

void TestPlanDescargaGuiada::reservaParcialCreaSegmentosLibres()
{
    // Mes cerrado; recibidos completo. Emitidos sin acreditar (hueco = mes).
    const Ev recibidos = acreditada(D::Recibidos, kSeptiembre.inicioSat(), kSeptiembre.finSat());
    struct Caso {
        const char* nombre;
        QList<std::pair<const char*, const char*>> reservas;
        Tramos libres;
    };
    const QList<Caso> casos = {
        {"reserva al inicio", {{"2026-09-01T00:00:00", "2026-09-10T00:00:00"}},
         {{"2026-09-10T00:00:01", "2026-09-30T23:59:59"}}},
        {"reserva al final", {{"2026-09-25T00:00:00", "2026-09-30T23:59:59"}},
         {{"2026-09-01T00:00:00", "2026-09-24T23:59:59"}}},
        {"reserva en medio", {{"2026-09-10T00:00:00", "2026-09-20T23:59:59"}},
         {{"2026-09-01T00:00:00", "2026-09-09T23:59:59"}, {"2026-09-21T00:00:00", "2026-09-30T23:59:59"}}},
        {"varias reservas, una traslapada",
         {{"2026-09-05T00:00:00", "2026-09-08T00:00:00"},
          {"2026-09-07T00:00:00", "2026-09-10T23:59:59"},
          {"2026-09-20T00:00:00", "2026-09-22T00:00:00"}},
         {{"2026-09-01T00:00:00", "2026-09-04T23:59:59"},
          {"2026-09-11T00:00:00", "2026-09-19T23:59:59"},
          {"2026-09-22T00:00:01", "2026-09-30T23:59:59"}}},
    };
    for (const Caso& c : casos) {
        QList<Ev> evidencia{recibidos};
        QList<SolicitudId> ids;
        for (const auto& [inicio, fin] : c.reservas) {
            evidencia.append(enCurso(D::Emitidos, QString::fromLatin1(inicio), QString::fromLatin1(fin)));
            ids.append(evidencia.last().solicitud.id);
        }
        const auto e = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, evidencia);
        const QByteArray nombre(c.nombre);
        // Recibidos ya tiene tramos: la accion es CompletarMes.
        QVERIFY2(ofertaDescargaGuiada(e).accion == AccionGuiada::CompletarMes, nombre.constData());
        const PlanDescargaGuiada p = planificarDescargaGuiada(e, AccionGuiada::CompletarMes).valor();
        QVERIFY2(texto(p.emitidos.crear) == c.libres, nombre.constData());
        QVERIFY2(p.emitidos.reutilizar == ids, nombre.constData());
        // Ningun tramo creado se solapa con acreditados ni reservados.
        for (const TramoMensual& t : p.emitidos.crear) {
            for (const TramoEnCurso& r : e.emitidos.enCurso) {
                QVERIFY2(t.fin < r.tramo.inicio || t.inicio > r.tramo.fin, nombre.constData());
            }
            for (const TramoMensual& a : e.emitidos.tramos) {
                QVERIFY2(t.fin < a.inicio || t.inicio > a.fin, nombre.constData());
            }
        }
    }
    // Reserva y acreditado juntos en un mismo hueco interno.
    const QList<Ev> mixto = {recibidos,
                             acreditada(D::Emitidos, kSeptiembre.inicioSat(), QStringLiteral("2026-09-10T23:59:59")),
                             acreditada(D::Emitidos, QStringLiteral("2026-09-21T00:00:00"), kSeptiembre.finSat()),
                             enCurso(D::Emitidos, QStringLiteral("2026-09-15T00:00:00"), QStringLiteral("2026-09-16T23:59:59"))};
    const PlanDescargaGuiada m = planDe(kSeptiembre, kAhora, mixto, AccionGuiada::CompletarMes);
    QCOMPARE(texto(m.emitidos.crear), (Tramos{{"2026-09-11T00:00:00", "2026-09-14T23:59:59"},
                                              {"2026-09-17T00:00:00", "2026-09-20T23:59:59"}}));

    // Mes en curso: tras la primera descarga (en curso hasta 14:05 del dia
    // 10), repetir minutos despues no pide el tramo transcurrido.
    const QDateTime dia10 = utc(2026, 10, 10, 20, 5);
    const QList<Ev> pedidas = {enCurso(D::Emitidos, kOctubre.inicioSat(), QStringLiteral("2026-10-10T14:05:00")),
                               enCurso(D::Recibidos, kOctubre.inicioSat(), QStringLiteral("2026-10-10T14:05:00"))};
    const auto e = evaluarCoberturaMensual(kPerfil, kOctubre, dia10.addSecs(300), pedidas);
    QVERIFY(e.emitidos.alcanzaFronteraConReservas);
    QCOMPARE(ofertaDescargaGuiada(e).accion, std::nullopt);
    QVERIFY(!planDe(kOctubre, dia10.addSecs(300), pedidas).creaAlgo());
}

void TestPlanDescargaGuiada::accionDistintaDeLaOfrecida()
{
    // Sin evidencia se ofrece DescargarFacturas: Completar/Actualizar se rechazan.
    const auto vacio = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {});
    QVERIFY(planificarDescargaGuiada(vacio, AccionGuiada::DescargarFacturas));
    for (AccionGuiada a : {AccionGuiada::CompletarMes, AccionGuiada::ActualizarMes}) {
        const auto r = planificarDescargaGuiada(vacio, a);
        QVERIFY(!r);
        QCOMPARE(r.error(), ErrorPlanGuiado::AccionNoDisponible);
    }
    // Mes cerrado parcial: solo CompletarMes.
    const auto parcial = evaluarCoberturaMensual(
        kPerfil, kSeptiembre, kAhora, {acreditada(D::Emitidos, kSeptiembre.inicioSat(), QStringLiteral("2026-09-10T00:00:00"))});
    QVERIFY(planificarDescargaGuiada(parcial, AccionGuiada::CompletarMes));
    QCOMPARE(planificarDescargaGuiada(parcial, AccionGuiada::ActualizarMes).error(), ErrorPlanGuiado::AccionNoDisponible);
    QCOMPARE(planificarDescargaGuiada(parcial, AccionGuiada::DescargarFacturas).error(), ErrorPlanGuiado::AccionNoDisponible);
    // Mes completo: ninguna accion.
    const auto completo = evaluarCoberturaMensual(
        kPerfil, kSeptiembre, kAhora,
        {acreditada(D::Emitidos, kSeptiembre.inicioSat(), kSeptiembre.finSat()),
         acreditada(D::Recibidos, kSeptiembre.inicioSat(), kSeptiembre.finSat())});
    for (AccionGuiada a : {AccionGuiada::DescargarFacturas, AccionGuiada::CompletarMes, AccionGuiada::ActualizarMes}) {
        QCOMPARE(planificarDescargaGuiada(completo, a).error(), ErrorPlanGuiado::AccionNoDisponible);
    }
}

void TestPlanDescargaGuiada::creadasSinEnviarSeOfrecenParaReenviar()
{
    // Cierre entre el commit y el encolado: las solicitudes guiadas quedan en
    // Creada. El mes esta Preparando, pero la accion se sigue ofreciendo para
    // que el usuario las reenvie; no crea nada nuevo.
    Ev e1 = enCurso(D::Emitidos, kSeptiembre.inicioSat(), kSeptiembre.finSat());
    Ev r1 = enCurso(D::Recibidos, kSeptiembre.inicioSat(), kSeptiembre.finSat());
    for (Ev* e : {&e1, &r1}) {
        e->solicitud.estadoLocal = EstadoLocal::Creada;
        e->solicitud.estadoSolicitudSat.reset();
    }
    const auto ev = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {e1, r1});
    QCOMPARE(ev.estado, EstadoMes::Preparando);
    QCOMPARE(ev.emitidos.sinEnviar, QList<SolicitudId>{e1.solicitud.id});
    QCOMPARE(ofertaDescargaGuiada(ev).accion, std::optional(AccionGuiada::DescargarFacturas));
    const PlanDescargaGuiada p = planificarDescargaGuiada(ev, AccionGuiada::DescargarFacturas).valor();
    QVERIFY(!p.creaAlgo());
    QCOMPARE(p.emitidos.reutilizar, QList<SolicitudId>{e1.solicitud.id});
    QCOMPARE(p.recibidos.reutilizar, QList<SolicitudId>{r1.solicitud.id});
    // Ya enviadas (EnProceso): sin oferta.
    const Ev e2 = enCurso(D::Emitidos, kSeptiembre.inicioSat(), kSeptiembre.finSat());
    const Ev r2 = enCurso(D::Recibidos, kSeptiembre.inicioSat(), kSeptiembre.finSat());
    QCOMPARE(ofertaDescargaGuiada(evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {e2, r2})).accion, std::nullopt);
}
