#include "TestCoberturaMensual.h"

#include "domain/cfdi/CoberturaMensual.h"
#include "domain/cfdi/MesCalendario.h"
#include "domain/solicitudes/OperacionSat.h"

#include <QTest>
#include <QTimeZone>

#include <algorithm>
#include <time.h>
#include <functional>

using namespace satcfdi;

namespace {

// Datos sinteticos: RFC generico del SAT e identificadores inventados.
const QString kRfc = QStringLiteral("XAXX010101000");
const PerfilId kPerfil = *PerfilId::desdeTexto(u"0f000000-0000-4000-8000-000000000001");
const PerfilId kOtroPerfil = *PerfilId::desdeTexto(u"0f000000-0000-4000-8000-000000000002");
const MesCalendario kSeptiembre = *MesCalendario::crear(2026, 9);
const MesCalendario kOctubre = *MesCalendario::crear(2026, 10);

// 2026-10-20 12:00:00 en Centro (UTC-6) = 18:00:00Z.
QDateTime utc(int anio, int mes, int dia, int h, int m, int s)
{
    return QDateTime(QDate(anio, mes, dia), QTime(h, m, s), QTimeZone(QTimeZone::UTC));
}
const QDateTime kAhora = utc(2026, 10, 20, 18, 0, 0);

using Ev = EvidenciaSolicitudMensual;
using D = TipoDescarga;

int g_orden = 0;

SolicitudId idSintetico(int n)
{
    return *SolicitudId::desdeTexto(QStringLiteral("5e000000-0000-4000-8000-%1").arg(n, 12, 10, QLatin1Char('0')));
}

EvidenciaPaqueteMensual paqueteIndexado()
{
    EvidenciaPaqueteMensual p;
    p.paqueteId = QStringLiteral("pa000000-0000-4000-8000-%1").arg(++g_orden, 12, 10, QLatin1Char('0'));
    p.estadoDescarga = EstadoDescarga::Descargado;
    p.indice = EstadoIndicePaquete::Indexado;
    p.indiceVigente = true;
    return p;
}

// Solicitud canonica Terminada con un paquete Descargado e indexado.
Ev canonica(D d, const QString& inicio, const QString& fin)
{
    const int n = ++g_orden;
    Ev e;
    SolicitudPersistida& s = e.solicitud;
    s.id = idSintetico(n);
    s.perfilSatId = kPerfil;
    s.tipoCfdi = d;
    s.operacionSat = operacionPara(d);
    s.rfcSolicitante = kRfc;
    if (d == D::Emitidos) {
        s.rfcEmisor = kRfc;
    } else {
        s.rfcReceptor = kRfc;
    }
    s.fechaInicialSat = inicio;
    s.fechaFinalSat = fin;
    s.estadoLocal = EstadoLocal::Enviada;
    s.idSolicitudSat = s.id.texto().toUpper();
    s.codEstatusSolicitud = QStringLiteral("5000");
    s.estadoSolicitudSat = EstadoSolicitudSat::Terminada;
    s.codigoEstadoSolicitud = QStringLiteral("5000");
    s.creadaEn = utc(2026, 10, 1, 0, 0, 0).addSecs(n);
    e.paquetes = {paqueteIndexado()};
    return e;
}

Ev completa(D d, MesCalendario mes = kSeptiembre)
{
    return canonica(d, mes.inicioSat(), mes.finSat());
}

Ev conEstado(D d, EstadoLocal local, std::optional<EstadoSolicitudSat> sat = std::nullopt)
{
    Ev e = completa(d);
    e.solicitud.estadoLocal = local;
    e.solicitud.estadoSolicitudSat = sat;
    e.paquetes.clear();
    if (local != EstadoLocal::Enviada) {
        e.solicitud.idSolicitudSat.reset();
        e.solicitud.codigoEstadoSolicitud.reset();
        e.solicitud.codEstatusSolicitud.reset();
    }
    return e;
}

Ev conPaquete(D d, std::function<void(EvidenciaPaqueteMensual&)> ajuste, bool indexacionProgramada = false)
{
    Ev e = completa(d);
    ajuste(e.paquetes.first());
    e.indexacionProgramada = indexacionProgramada;
    return e;
}

QString describir(const EvaluacionCoberturaMensual& r)
{
    return QStringLiteral("estado=%1 causa=%2")
        .arg(claveEstable(r.estado), r.causa ? QString(claveEstable(r.causa->causa)) : QStringLiteral("-"));
}

struct Caso {
    const char* nombre;
    QList<Ev> evidencia;
    EstadoMes estado;
    std::optional<CausaAtencion> causa;
};

void verificarCasos(const QList<Caso>& casos, MesCalendario mes = kSeptiembre, const QDateTime& ahora = kAhora)
{
    for (const Caso& c : casos) {
        const auto r = evaluarCoberturaMensual(kPerfil, mes, ahora, c.evidencia);
        const QByteArray detalle = QByteArray(c.nombre) + ": " + describir(r).toUtf8();
        QVERIFY2(r.estado == c.estado, detalle.constData());
        QVERIFY2((r.causa ? std::optional(r.causa->causa) : std::nullopt) == c.causa, detalle.constData());
        // La causa solo existe con RequiereAtencion.
        QVERIFY2(r.causa.has_value() == (r.estado == EstadoMes::RequiereAtencion), detalle.constData());
    }
}

} // namespace

void TestCoberturaMensual::mesCalendario()
{
    QVERIFY(!MesCalendario::crear(2026, 0));
    QVERIFY(!MesCalendario::crear(2026, 13));
    QVERIFY(!MesCalendario::desdeTexto(u"2026-1"));
    QVERIFY(!MesCalendario::desdeTexto(u"2026/01"));
    QVERIFY(!MesCalendario::desdeTexto(u"2026-13"));
    const MesCalendario enero = *MesCalendario::desdeTexto(u"2027-01");
    QCOMPARE(enero.texto(), QStringLiteral("2027-01"));
    QCOMPARE(enero.anterior()->texto(), QStringLiteral("2026-12"));
    QCOMPARE(enero.anterior()->siguiente(), std::optional(enero));
    QCOMPARE(enero.inicioSat(), QStringLiteral("2027-01-01T00:00:00"));
    QCOMPARE(enero.finSat(), QStringLiteral("2027-01-31T23:59:59"));
    QCOMPARE(MesCalendario::crear(2028, 2)->finSat(), QStringLiteral("2028-02-29T23:59:59")); // bisiesto
    QCOMPARE(MesCalendario::crear(2026, 2)->finSat(), QStringLiteral("2026-02-28T23:59:59"));
    QVERIFY(kSeptiembre < kOctubre);

    // Limites del dominio (0001-01 a 9999-12): sin meses invalidos.
    const MesCalendario primero = *MesCalendario::crear(1, 1);
    const MesCalendario ultimo = *MesCalendario::crear(9999, 12);
    QVERIFY(!primero.anterior());
    QCOMPARE(primero.siguiente()->texto(), QStringLiteral("0001-02"));
    QVERIFY(!ultimo.siguiente());
    QCOMPARE(ultimo.anterior()->texto(), QStringLiteral("9999-11"));
    QCOMPARE(MesCalendario::crear(9999, 11)->siguiente(), std::optional(ultimo));
    QCOMPARE(primero.inicioSat(), QStringLiteral("0001-01-01T00:00:00"));
    QCOMPARE(ultimo.finSat(), QStringLiteral("9999-12-31T23:59:59"));
    QVERIFY(!MesCalendario::crear(0, 12));
    QVERIFY(!MesCalendario::crear(10000, 1));
    QVERIFY(!MesCalendario::de(QDate()));
    QVERIFY(!MesCalendario::de(QDate(10000, 1, 1)));
    QVERIFY(!MesCalendario::de(QDate(-1, 12, 31)));
    QCOMPARE(MesCalendario::de(QDate(9999, 12, 31)), std::optional(ultimo));
    // Reloj fuera del dominio: sin mes en curso ni mes inicial.
    const QDateTime lejano(QDate(10000, 1, 2), QTime(12, 0), QTimeZone(QTimeZone::UTC));
    QVERIFY(!centro::mesEnCurso(lejano));
    QVERIFY(!centro::mesInicialPorOmision(lejano));
    const QDateTime enero1(QDate(1, 1, 15), QTime(12, 0), QTimeZone(QTimeZone::UTC));
    QCOMPARE(centro::mesEnCurso(enero1), std::optional(primero));
    QVERIFY(!centro::mesInicialPorOmision(enero1));
    // La regla sigue clasificando el mes en esos extremos.
    QCOMPARE(evaluarCoberturaMensual(kPerfil, ultimo, lejano, {}).tipoMes, TipoMes::Cerrado);
    QCOMPARE(evaluarCoberturaMensual(kPerfil, primero, enero1, {}).tipoMes, TipoMes::EnCurso);
    QCOMPARE(evaluarCoberturaMensual(kPerfil, ultimo, enero1, {}).tipoMes, TipoMes::Futuro);
    QVERIFY(*MesCalendario::crear(2026, 12) < enero);
}

void TestCoberturaMensual::fechasSat()
{
    const auto pared = centro::desdeFechaSat(u"2026-09-30T23:59:59");
    QVERIFY(pared);
    QCOMPARE(centro::fechaSat(*pared), QStringLiteral("2026-09-30T23:59:59"));
    QCOMPARE(centro::fechaSat(pared->addSecs(1)), QStringLiteral("2026-10-01T00:00:00"));
    QVERIFY(!centro::desdeFechaSat(u"2026-09-31T00:00:00"));
    QVERIFY(!centro::desdeFechaSat(u"2026-09-30 23:59:59"));
    QVERIFY(!centro::desdeFechaSat(u"2026-09-30T23:59:59.000"));
    QVERIFY(centro::zona().isValid());
    // 18:00Z = 12:00 Centro (sin horario de verano desde 2022).
    QCOMPARE(centro::fechaSat(centro::aParedCentro(kAhora)), QStringLiteral("2026-10-20T12:00:00"));
}

void TestCoberturaMensual::canonicidad()
{
    const Ev base = completa(D::Recibidos);
    QVERIFY(esSolicitudCanonicaDelMes(base.solicitud, kPerfil, kSeptiembre));
    QVERIFY(!esSolicitudCanonicaDelMes(base.solicitud, kOtroPerfil, kSeptiembre));
    QVERIFY(!esSolicitudCanonicaDelMes(base.solicitud, kPerfil, kOctubre));

    auto variante = [&](std::function<void(SolicitudPersistida&)> f) {
        SolicitudPersistida s = base.solicitud;
        f(s);
        return esSolicitudCanonicaDelMes(s, kPerfil, kSeptiembre);
    };
    QVERIFY(!variante([](SolicitudPersistida& s) { s.eliminadoEn = kAhora; }));
    QVERIFY(!variante([](SolicitudPersistida& s) { s.tipoComprobante = QStringLiteral("I"); }));
    QVERIFY(!variante([](SolicitudPersistida& s) { s.complemento = QStringLiteral("nomina12"); }));
    QVERIFY(!variante([](SolicitudPersistida& s) { s.rfcEmisor = QStringLiteral("XEXX010101000"); }));
    QVERIFY(!variante([](SolicitudPersistida& s) { s.fechaInicialSat = QStringLiteral("2026-08-31T23:59:59"); }));
    QVERIFY(!variante([](SolicitudPersistida& s) { s.fechaFinalSat = QStringLiteral("2026-10-01T00:00:00"); }));
    QVERIFY(!variante([](SolicitudPersistida& s) { s.fechaFinalSat = QStringLiteral("2026-09-30"); }));
    QVERIFY(!variante([](SolicitudPersistida& s) {
        s.fechaInicialSat = QStringLiteral("2026-09-10T00:00:00");
        s.fechaFinalSat = QStringLiteral("2026-09-09T00:00:00");
    }));
    // Tramo parcial dentro del mes: canonico.
    QVERIFY(variante([](SolicitudPersistida& s) { s.fechaInicialSat = QStringLiteral("2026-09-16T00:00:00"); }));

    Ev emitidos = completa(D::Emitidos);
    emitidos.solicitud.rfcReceptores = {QStringLiteral("XEXX010101000")};
    QVERIFY(!esSolicitudCanonicaDelMes(emitidos.solicitud, kPerfil, kSeptiembre));
}

void TestCoberturaMensual::matrizMesCerrado()
{
    Ev vaciaConfirmada = completa(D::Recibidos);
    vaciaConfirmada.paquetes.clear();
    vaciaConfirmada.solicitud.conjuntoPaquetesVacioConfirmadoEn = utc(2026, 10, 2, 0, 0, 0);
    Ev vaciaSinConfirmar = completa(D::Recibidos);
    vaciaSinConfirmar.paquetes.clear();

    Ev eliminadaConFalla = conEstado(D::Recibidos, EstadoLocal::EnvioFallido);
    eliminadaConFalla.solicitud.eliminadoEn = kAhora;
    Ev filtradaConFalla = conEstado(D::Recibidos, EstadoLocal::EnvioFallido);
    filtradaConFalla.solicitud.tipoComprobante = QStringLiteral("I");
    Ev noCanonicaCompleta = canonica(D::Recibidos, QStringLiteral("2026-08-15T00:00:00"), kSeptiembre.finSat());

    const QList<Caso> casos = {
        {"sin evidencia", {}, EstadoMes::SinDescargar, std::nullopt},
        {"una direccion en curso", {conEstado(D::Emitidos, EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso)},
         EstadoMes::Preparando, std::nullopt},
        {"una direccion creada", {conEstado(D::Emitidos, EstadoLocal::Creada)}, EstadoMes::Preparando, std::nullopt},
        {"enviada sin estado SAT", {conEstado(D::Emitidos, EstadoLocal::Enviada)}, EstadoMes::Preparando,
         std::nullopt},
        {"ambas completas", {completa(D::Emitidos), completa(D::Recibidos)}, EstadoMes::Disponibles, std::nullopt},
        {"una completa", {completa(D::Emitidos)}, EstadoMes::DisponiblesParcialmente, std::nullopt},
        {"vacio persistido cuenta", {completa(D::Emitidos), vaciaConfirmada}, EstadoMes::Disponibles, std::nullopt},
        {"sin paquetes registrados no cuenta", {completa(D::Emitidos), vaciaSinConfirmar},
         EstadoMes::DisponiblesParcialmente, std::nullopt},
        {"solo terminada sin paquetes registrados", {vaciaSinConfirmar}, EstadoMes::SinDescargar, std::nullopt},
        {"EnvioIncierto", {completa(D::Emitidos), conEstado(D::Recibidos, EstadoLocal::EnvioIncierto)},
         EstadoMes::RequiereAtencion, CausaAtencion::EnvioIncierto},
        {"EnvioFallido", {completa(D::Emitidos), completa(D::Recibidos), conEstado(D::Recibidos, EstadoLocal::EnvioFallido)},
         EstadoMes::RequiereAtencion, CausaAtencion::EnvioFallido},
        {"ErrorSat", {conEstado(D::Emitidos, EstadoLocal::Enviada, EstadoSolicitudSat::Error)},
         EstadoMes::RequiereAtencion, CausaAtencion::ErrorSat},
        {"Rechazada", {conEstado(D::Emitidos, EstadoLocal::Enviada, EstadoSolicitudSat::Rechazada)},
         EstadoMes::RequiereAtencion, CausaAtencion::Rechazada},
        {"Vencida", {conEstado(D::Emitidos, EstadoLocal::Enviada, EstadoSolicitudSat::Vencida)},
         EstadoMes::RequiereAtencion, CausaAtencion::Vencida},
        {"paquete Error",
         {completa(D::Recibidos), conPaquete(D::Emitidos, [](auto& p) { p.estadoDescarga = EstadoDescarga::Error; })},
         EstadoMes::RequiereAtencion, CausaAtencion::PaqueteError},
        {"paquete Vencido",
         {conPaquete(D::Emitidos, [](auto& p) { p.estadoDescarga = EstadoDescarga::Vencido; })},
         EstadoMes::RequiereAtencion, CausaAtencion::PaqueteVencido},
        {"paquete Disponible es trabajo",
         {completa(D::Recibidos), conPaquete(D::Emitidos, [](auto& p) { p.estadoDescarga = EstadoDescarga::Disponible; })},
         EstadoMes::Preparando, std::nullopt},
        {"paquete descargandose",
         {conPaquete(D::Emitidos, [](auto& p) { p.estadoDescarga = EstadoDescarga::Descargando; })},
         EstadoMes::Preparando, std::nullopt},
        {"ZIP faltante",
         {conPaquete(D::Emitidos, [](auto& p) { p.indice = EstadoIndicePaquete::OrigenNoDisponible; })},
         EstadoMes::RequiereAtencion, CausaAtencion::ZipNoDisponible},
        {"ZIP ilegible", {conPaquete(D::Emitidos, [](auto& p) { p.indice = EstadoIndicePaquete::Ilegible; })},
         EstadoMes::RequiereAtencion, CausaAtencion::ZipIlegible},
        {"indice ilegible", {conPaquete(D::Emitidos, [](auto& p) { p.indiceLegible = false; })},
         EstadoMes::RequiereAtencion, CausaAtencion::IndiceIlegible},
        {"indexacion pendiente sin trabajo", {conPaquete(D::Emitidos, [](auto& p) { p.indice.reset(); })},
         EstadoMes::RequiereAtencion, CausaAtencion::IndexacionSinProgramar},
        {"indexacion pendiente con trabajo", {conPaquete(D::Emitidos, [](auto& p) { p.indice.reset(); }, true)},
         EstadoMes::Preparando, std::nullopt},
        {"indice de otra version sin trabajo",
         {conPaquete(D::Emitidos, [](auto& p) { p.indiceVigente = false; })}, EstadoMes::RequiereAtencion,
         CausaAtencion::IndexacionSinProgramar},
        {"indice Pendiente con trabajo",
         {conPaquete(D::Emitidos, [](auto& p) { p.indice = EstadoIndicePaquete::Pendiente; }, true)},
         EstadoMes::Preparando, std::nullopt},
        {"solicitud eliminada no participa", {completa(D::Emitidos), completa(D::Recibidos), eliminadaConFalla},
         EstadoMes::Disponibles, std::nullopt},
        {"solicitud con filtros no participa", {completa(D::Emitidos), completa(D::Recibidos), filtradaConFalla},
         EstadoMes::Disponibles, std::nullopt},
        {"rango no canonico no acredita", {completa(D::Emitidos), noCanonicaCompleta},
         EstadoMes::DisponiblesParcialmente, std::nullopt},
    };
    verificarCasos(casos);

    // 5002 y 5005: el codigo precede al estado que lo porta.
    Ev c5002 = conEstado(D::Recibidos, EstadoLocal::EnvioFallido);
    c5002.solicitud.codEstatusSolicitud = QStringLiteral("5002");
    Ev c5005 = conEstado(D::Emitidos, EstadoLocal::EnvioFallido);
    c5005.solicitud.codEstatusSolicitud = QStringLiteral("5005");
    Ev v5005 = completa(D::Emitidos); // 5005 registrado en verificacion de una Terminada
    v5005.solicitud.codigoEstadoSolicitud = QStringLiteral("5005");
    verificarCasos({
        {"5002", {completa(D::Emitidos), c5002}, EstadoMes::RequiereAtencion, CausaAtencion::Codigo5002},
        {"5005", {c5005, completa(D::Recibidos)}, EstadoMes::RequiereAtencion, CausaAtencion::Codigo5005},
        {"5005 en verificacion", {v5005, completa(D::Recibidos)}, EstadoMes::RequiereAtencion,
         CausaAtencion::Codigo5005},
    });
    const auto r = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {completa(D::Emitidos), c5002});
    QCOMPARE(r.causas.size(), 2);
    QCOMPARE(r.causas.at(0), (CausaMensual{CausaAtencion::Codigo5002, D::Recibidos, c5002.solicitud.id}));
    QCOMPARE(r.causas.at(1), (CausaMensual{CausaAtencion::EnvioFallido, D::Recibidos, c5002.solicitud.id}));
    // La direccion libre conserva su cobertura.
    QVERIFY(r.emitidos.alcanzaFrontera);
    QVERIFY(!r.recibidos.hasta);
    // Una Terminada con 5005 no acredita.
    const auto r5005 = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {v5005});
    QVERIFY(!r5005.emitidos.hasta);

    // Conteo de no canonicas (solo del perfil y no eliminadas).
    const auto rf = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora,
                                            {filtradaConFalla, eliminadaConFalla, noCanonicaCompleta});
    QCOMPARE(rf.solicitudesNoCanonicas, 2);
    QCOMPARE(rf.estado, EstadoMes::SinDescargar);
}

void TestCoberturaMensual::precedenciaYCausas()
{
    // Atencion > Preparando > Disponibles.
    const Ev enCurso = conEstado(D::Emitidos, EstadoLocal::Enviada, EstadoSolicitudSat::Aceptada);
    const Ev incierta = conEstado(D::Recibidos, EstadoLocal::EnvioIncierto);
    const Ev errorPaq = conPaquete(D::Emitidos, [](auto& p) { p.estadoDescarga = EstadoDescarga::Error; });
    verificarCasos({
        {"atencion sobre trabajo", {enCurso, incierta}, EstadoMes::RequiereAtencion, CausaAtencion::EnvioIncierto},
        {"trabajo sobre disponibles", {completa(D::Emitidos), completa(D::Recibidos), enCurso}, EstadoMes::Preparando,
         std::nullopt},
        {"causa por prioridad, no por direccion", {errorPaq, incierta}, EstadoMes::RequiereAtencion,
         CausaAtencion::EnvioIncierto},
    });

    // Misma causa en ambas direcciones: Emitidos primero; repetida en la
    // misma solicitud (dos paquetes Error) se informa una vez.
    Ev dosErrores = conPaquete(D::Recibidos, [](auto& p) { p.estadoDescarga = EstadoDescarga::Error; });
    EvidenciaPaqueteMensual otro = paqueteIndexado();
    otro.estadoDescarga = EstadoDescarga::Error;
    dosErrores.paquetes.append(otro);
    const auto r = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {dosErrores, errorPaq});
    QCOMPARE(r.causas.size(), 2);
    QCOMPARE(r.causa->direccion, D::Emitidos);
    QCOMPARE(r.causas.at(1).direccion, D::Recibidos);

    // Solicitud en curso reutilizable por la planeacion.
    const auto rc = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {enCurso, completa(D::Recibidos)});
    QCOMPARE(rc.emitidos.solicitudesEnCurso, QList<SolicitudId>{enCurso.solicitud.id});
    QVERIFY(rc.recibidos.solicitudesEnCurso.isEmpty());
    QCOMPARE(rc.recibidos.solicitudesAcreditadas.size(), 1);
}

void TestCoberturaMensual::tramosYFronteras()
{
    const Ev recibidos = completa(D::Recibidos);
    // Contiguos: [1..15] + [16..30] cubren el mes.
    {
        const Ev a = canonica(D::Emitidos, QStringLiteral("2026-09-01T00:00:00"), QStringLiteral("2026-09-15T23:59:59"));
        const Ev b = canonica(D::Emitidos, QStringLiteral("2026-09-16T00:00:00"), QStringLiteral("2026-09-30T23:59:59"));
        const auto r = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {b, recibidos, a});
        QCOMPARE(r.estado, EstadoMes::Disponibles);
        QCOMPARE(centro::fechaSat(*r.emitidos.hasta), QStringLiteral("2026-09-30T23:59:59"));
        QCOMPARE(r.emitidos.tramosAcreditados, 2);
        QCOMPARE(r.emitidos.tramosTrasHueco, 0);
    }
    // Traslapados tambien se unen.
    {
        const Ev a = canonica(D::Emitidos, QStringLiteral("2026-09-01T00:00:00"), QStringLiteral("2026-09-20T00:00:00"));
        const Ev b = canonica(D::Emitidos, QStringLiteral("2026-09-10T00:00:00"), QStringLiteral("2026-09-30T23:59:59"));
        QCOMPARE(evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {a, b, recibidos}).estado, EstadoMes::Disponibles);
    }
    // Hueco de un segundo: el tramo posterior no extiende la cobertura.
    {
        const Ev a = canonica(D::Emitidos, QStringLiteral("2026-09-01T00:00:00"), QStringLiteral("2026-09-10T23:59:58"));
        const Ev b = canonica(D::Emitidos, QStringLiteral("2026-09-11T00:00:00"), QStringLiteral("2026-09-30T23:59:59"));
        const auto r = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {a, b, recibidos});
        QCOMPARE(r.estado, EstadoMes::DisponiblesParcialmente);
        QCOMPARE(centro::fechaSat(*r.emitidos.hasta), QStringLiteral("2026-09-10T23:59:58"));
        QVERIFY(!r.emitidos.alcanzaFrontera);
        QCOMPARE(r.emitidos.tramosTrasHueco, 1);
        QVERIFY(r.recibidos.alcanzaFrontera);
    }
    // Solo un tramo posterior a un hueco inicial (ADR 0020): parcial, sin
    // "disponibles hasta" y con el hueco inicial para planear.
    {
        const Ev b = canonica(D::Emitidos, QStringLiteral("2026-09-05T00:00:00"), QStringLiteral("2026-09-30T23:59:59"));
        const auto r = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {b});
        QCOMPARE(r.estado, EstadoMes::DisponiblesParcialmente);
        QVERIFY(!r.emitidos.hasta);
        QCOMPARE(r.emitidos.tramosTrasHueco, 1);
        QCOMPARE(r.emitidos.tramos.size(), 1);
        QCOMPARE(r.emitidos.huecos.size(), 1);
        QCOMPARE(centro::fechaSat(r.emitidos.huecos.at(0).tramo.fin), QStringLiteral("2026-09-04T23:59:59"));
    }
    // Frontera distinta entre direcciones y mes cerrado cubierto parcialmente.
    {
        const Ev a = completa(D::Emitidos);
        const Ev b = canonica(D::Recibidos, QStringLiteral("2026-09-01T00:00:00"), QStringLiteral("2026-09-20T23:59:59"));
        const auto r = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {a, b});
        QCOMPARE(r.tipoMes, TipoMes::Cerrado);
        QCOMPARE(centro::fechaSat(r.frontera), QStringLiteral("2026-09-30T23:59:59"));
        QCOMPARE(r.estado, EstadoMes::DisponiblesParcialmente);
        QVERIFY(r.emitidos.alcanzaFrontera);
        QCOMPARE(centro::fechaSat(*r.recibidos.hasta), QStringLiteral("2026-09-20T23:59:59"));
        QVERIFY(!r.recibidos.alcanzaFrontera);
    }
    // Mes futuro: nunca alcanza frontera.
    {
        const MesCalendario noviembre = *MesCalendario::crear(2026, 11);
        const auto r = evaluarCoberturaMensual(kPerfil, noviembre, kAhora, {});
        QCOMPARE(r.tipoMes, TipoMes::Futuro);
        QCOMPARE(r.estado, EstadoMes::SinDescargar);
    }
}

void TestCoberturaMensual::mesEnCursoHoyFrenteAAyer()
{
    // Hoy = 2026-10-20 (12:00 Centro).
    const Ev hoyE = canonica(D::Emitidos, kOctubre.inicioSat(), QStringLiteral("2026-10-20T10:00:00"));
    const Ev hoyR = canonica(D::Recibidos, kOctubre.inicioSat(), QStringLiteral("2026-10-20T00:00:00"));
    const auto r = evaluarCoberturaMensual(kPerfil, kOctubre, kAhora, {hoyE, hoyR});
    QCOMPARE(r.tipoMes, TipoMes::EnCurso);
    QCOMPARE(centro::fechaSat(r.frontera), QStringLiteral("2026-10-20T00:00:00"));
    QCOMPARE(r.estado, EstadoMes::Disponibles);

    const Ev ayerR = canonica(D::Recibidos, kOctubre.inicioSat(), QStringLiteral("2026-10-19T23:59:59"));
    const auto ra = evaluarCoberturaMensual(kPerfil, kOctubre, kAhora, {hoyE, ayerR});
    QCOMPARE(ra.estado, EstadoMes::DisponiblesParcialmente);
    QVERIFY(!ra.recibidos.alcanzaFrontera);
    QCOMPARE(centro::fechaSat(*ra.recibidos.hasta), QStringLiteral("2026-10-19T23:59:59"));
}

void TestCoberturaMensual::relojEneroYCambioDeAnio()
{
    // Enero: el mes inicial por omision es diciembre del anio previo.
    QCOMPARE(centro::mesInicialPorOmision(utc(2027, 1, 15, 18, 0, 0))->texto(), QStringLiteral("2026-12"));
    QCOMPARE(centro::mesInicialPorOmision(kAhora)->texto(), QStringLiteral("2026-09"));
    // Cambio de anio: 2027-01-01T05:59:59Z = 2026-12-31 23:59:59 Centro.
    const QDateTime antes = utc(2027, 1, 1, 5, 59, 59);
    const QDateTime despues = utc(2027, 1, 1, 6, 0, 0);
    QCOMPARE(centro::mesEnCurso(antes)->texto(), QStringLiteral("2026-12"));
    QCOMPARE(centro::mesInicialPorOmision(antes)->texto(), QStringLiteral("2026-11"));
    QCOMPARE(centro::mesEnCurso(despues)->texto(), QStringLiteral("2027-01"));
    QCOMPARE(centro::mesInicialPorOmision(despues)->texto(), QStringLiteral("2026-12"));
    // Diciembre deja de estar en curso justo al cambio de anio Centro.
    const MesCalendario diciembre = *MesCalendario::crear(2026, 12);
    QCOMPARE(evaluarCoberturaMensual(kPerfil, diciembre, antes, {}).tipoMes, TipoMes::EnCurso);
    QCOMPARE(evaluarCoberturaMensual(kPerfil, diciembre, despues, {}).tipoMes, TipoMes::Cerrado);
}

void TestCoberturaMensual::medianocheConZonaDelProcesoDistinta()
{
    const QByteArray tzAnterior = qgetenv("TZ");
    const bool habiaTz = qEnvironmentVariableIsSet("TZ");
    qputenv("TZ", "Asia/Tokyo");
    ::tzset();
    // La zona del proceso efectivamente cambio (UTC+9).
    const QDateTime local(QDate(2026, 10, 20), QTime(12, 0), QTimeZone::LocalTime);
    const bool zonaAplicada = local.offsetFromUtc() == 9 * 3600;

    // 2026-10-21T05:59:59Z = 20 oct 23:59:59 Centro (21 oct 14:59 en Tokio).
    const QDateTime antes = utc(2026, 10, 21, 5, 59, 59);
    const QDateTime despues = utc(2026, 10, 21, 6, 0, 0);
    const QString paredAntes = centro::fechaSat(centro::aParedCentro(antes));
    const QDate hoyAntes = centro::hoy(antes);
    const QDate hoyDespues = centro::hoy(despues);
    const Ev e = canonica(D::Emitidos, kOctubre.inicioSat(), QStringLiteral("2026-10-20T00:00:00"));
    const Ev r = canonica(D::Recibidos, kOctubre.inicioSat(), QStringLiteral("2026-10-20T23:59:59"));
    const EstadoMes estadoAntes = evaluarCoberturaMensual(kPerfil, kOctubre, antes, {e, r}).estado;
    const EstadoMes estadoDespues = evaluarCoberturaMensual(kPerfil, kOctubre, despues, {e, r}).estado;
    // Fin de mes cerca de medianoche: 2026-11-01T05:59:59Z sigue en octubre.
    const QString mesFin = centro::mesEnCurso(utc(2026, 11, 1, 5, 59, 59))->texto();

    if (habiaTz) {
        qputenv("TZ", tzAnterior);
    } else {
        qunsetenv("TZ");
    }
    ::tzset();

    QVERIFY2(zonaAplicada, "no se pudo cambiar la zona horaria del proceso");
    QCOMPARE(paredAntes, QStringLiteral("2026-10-20T23:59:59"));
    QCOMPARE(hoyAntes, QDate(2026, 10, 20));
    QCOMPARE(hoyDespues, QDate(2026, 10, 21));
    QCOMPARE(estadoAntes, EstadoMes::Disponibles);
    QCOMPARE(estadoDespues, EstadoMes::DisponiblesParcialmente); // emitidos termina ayer
    QCOMPARE(mesFin, QStringLiteral("2026-10"));
}

void TestCoberturaMensual::deterministaSinImportarOrden()
{
    const Ev a = conPaquete(D::Emitidos, [](auto& p) { p.estadoDescarga = EstadoDescarga::Vencido; });
    const Ev b = conEstado(D::Recibidos, EstadoLocal::Enviada, EstadoSolicitudSat::Rechazada);
    const Ev c = canonica(D::Recibidos, QStringLiteral("2026-09-01T00:00:00"), QStringLiteral("2026-09-12T00:00:00"));
    const Ev d = conEstado(D::Emitidos, EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso);
    QList<Ev> evidencia{a, b, c, d};
    const auto referencia = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, evidencia);
    std::reverse(evidencia.begin(), evidencia.end());
    const auto invertida = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, evidencia);
    QCOMPARE(invertida.causas, referencia.causas);
    QCOMPARE(invertida.estado, referencia.estado);
    QCOMPARE(invertida.recibidos.hasta, referencia.recibidos.hasta);
    QCOMPARE(invertida.emitidos.solicitudesEnCurso, referencia.emitidos.solicitudesEnCurso);
    QCOMPARE(referencia.causa->causa, CausaAtencion::Rechazada);
}

namespace {

using Tramos = QList<std::pair<QString, QString>>;

Tramos huecosTexto(const CoberturaDireccion& c)
{
    Tramos r;
    for (const HuecoCobertura& h : c.huecos) {
        r.append({centro::fechaSat(h.tramo.inicio), centro::fechaSat(h.tramo.fin)});
    }
    return r;
}

Ev tramo(D d, const char* inicio, const char* fin)
{
    return canonica(d, QString::fromLatin1(inicio), QString::fromLatin1(fin));
}

} // namespace

void TestCoberturaMensual::huecosInicialInternoYFinal()
{
    struct CasoHueco {
        const char* nombre;
        QList<Ev> emitidos;
        Tramos huecos;
        std::optional<QString> hasta;
    };
    const QList<CasoHueco> casos = {
        {"sin tramos: todo el mes", {}, {{"2026-09-01T00:00:00", "2026-09-30T23:59:59"}}, std::nullopt},
        {"completo: sin huecos", {completa(D::Emitidos)}, {}, QStringLiteral("2026-09-30T23:59:59")},
        {"inicial", {tramo(D::Emitidos, "2026-09-05T00:00:00", "2026-09-30T23:59:59")},
         {{"2026-09-01T00:00:00", "2026-09-04T23:59:59"}}, std::nullopt},
        {"interno",
         {tramo(D::Emitidos, "2026-09-01T00:00:00", "2026-09-10T23:59:59"),
          tramo(D::Emitidos, "2026-09-12T00:00:00", "2026-09-30T23:59:59")},
         {{"2026-09-11T00:00:00", "2026-09-11T23:59:59"}}, QStringLiteral("2026-09-10T23:59:59")},
        {"final", {tramo(D::Emitidos, "2026-09-01T00:00:00", "2026-09-20T23:59:59")},
         {{"2026-09-21T00:00:00", "2026-09-30T23:59:59"}}, QStringLiteral("2026-09-20T23:59:59")},
        {"inicial, interno y final con traslape",
         {tramo(D::Emitidos, "2026-09-03T00:00:00", "2026-09-08T00:00:00"),
          tramo(D::Emitidos, "2026-09-07T00:00:00", "2026-09-10T00:00:00"),
          tramo(D::Emitidos, "2026-09-15T00:00:00", "2026-09-25T23:59:59")},
         {{"2026-09-01T00:00:00", "2026-09-02T23:59:59"},
          {"2026-09-10T00:00:01", "2026-09-14T23:59:59"},
          {"2026-09-26T00:00:00", "2026-09-30T23:59:59"}},
         std::nullopt},
    };
    for (const CasoHueco& c : casos) {
        const auto r = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, c.emitidos);
        const QByteArray nombre(c.nombre);
        QVERIFY2(huecosTexto(r.emitidos) == c.huecos, nombre.constData());
        QVERIFY2((r.emitidos.hasta ? std::optional(centro::fechaSat(*r.emitidos.hasta)) : std::nullopt) == c.hasta,
                 nombre.constData());
        // Huecos y union son complementarios: nunca incluyen segundos acreditados.
        for (const HuecoCobertura& h : r.emitidos.huecos) {
            for (const TramoMensual& t : r.emitidos.tramos) {
                QVERIFY2(h.tramo.fin < t.inicio || h.tramo.inicio > t.fin, nombre.constData());
            }
        }
        const EstadoMes esperado = c.emitidos.isEmpty() ? EstadoMes::SinDescargar : EstadoMes::DisponiblesParcialmente;
        QVERIFY2(r.estado == esperado, nombre.constData());
    }
    // Union en componentes maximos.
    const auto r = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, casos.last().emitidos);
    QCOMPARE(r.emitidos.tramos.size(), 2);
    QCOMPARE(centro::fechaSat(r.emitidos.tramos.at(0).fin), QStringLiteral("2026-09-10T00:00:00"));
}

void TestCoberturaMensual::huecosMesEnCursoYReservaEnCurso()
{
    // Mes en curso (hoy 20 oct, 12:00): cubierto hasta ayer -> hueco final
    // hasta el instante; cubierto hoy -> sin huecos.
    const Ev ayer = canonica(D::Emitidos, kOctubre.inicioSat(), QStringLiteral("2026-10-19T23:59:59"));
    const auto r = evaluarCoberturaMensual(kPerfil, kOctubre, kAhora, {ayer});
    QCOMPARE(huecosTexto(r.emitidos), (Tramos{{"2026-10-20T00:00:00", "2026-10-20T12:00:00"}}));
    const Ev hoy = canonica(D::Emitidos, kOctubre.inicioSat(), QStringLiteral("2026-10-20T08:00:00"));
    QVERIFY(evaluarCoberturaMensual(kPerfil, kOctubre, kAhora, {hoy}).emitidos.huecos.isEmpty());
    // Recibidos sin nada: todo lo transcurrido del mes.
    QCOMPARE(huecosTexto(r.recibidos), (Tramos{{"2026-10-01T00:00:00", "2026-10-20T12:00:00"}}));

    // Mes cerrado: una solicitud en curso reserva el hueco que intersecta.
    Ev enCurso = canonica(D::Emitidos, QStringLiteral("2026-09-21T00:00:00"), QStringLiteral("2026-09-30T23:59:59"));
    enCurso.solicitud.estadoSolicitudSat = EstadoSolicitudSat::EnProceso;
    enCurso.paquetes.clear();
    const Ev parcial = tramo(D::Emitidos, "2026-09-05T00:00:00", "2026-09-20T23:59:59");
    const auto rc = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {parcial, enCurso});
    QCOMPARE(rc.estado, EstadoMes::Preparando);
    QCOMPARE(huecosTexto(rc.emitidos), (Tramos{{"2026-09-01T00:00:00", "2026-09-04T23:59:59"},
                                               {"2026-09-21T00:00:00", "2026-09-30T23:59:59"}}));
    QVERIFY(rc.emitidos.huecos.at(0).reservadoPor.isEmpty());
    QCOMPARE(rc.emitidos.huecos.at(1).reservadoPor, QList<SolicitudId>{enCurso.solicitud.id});
    QCOMPARE(rc.emitidos.enCurso.size(), 1);
    QCOMPARE(rc.emitidos.enCurso.first().solicitud, enCurso.solicitud.id);
}

void TestCoberturaMensual::causasSuperadasPorCoberturaPosterior()
{
    // Fallo antiguo + cobertura posterior completa de la misma direccion: sin
    // causa activa; la causa queda como superada.
    struct CasoSuperado {
        const char* nombre;
        Ev fallo;
    };
    Ev c5002 = conEstado(D::Emitidos, EstadoLocal::EnvioFallido);
    c5002.solicitud.codEstatusSolicitud = QStringLiteral("5002");
    Ev c5005 = conEstado(D::Emitidos, EstadoLocal::EnvioFallido);
    c5005.solicitud.codEstatusSolicitud = QStringLiteral("5005");
    const QList<CasoSuperado> casos = {
        {"EnvioFallido", conEstado(D::Emitidos, EstadoLocal::EnvioFallido)},
        {"Vencida", conEstado(D::Emitidos, EstadoLocal::Enviada, EstadoSolicitudSat::Vencida)},
        {"5002", c5002},
        {"5005", c5005},
        {"paquete Error", conPaquete(D::Emitidos, [](auto& p) { p.estadoDescarga = EstadoDescarga::Error; })},
        {"ZIP faltante", conPaquete(D::Emitidos, [](auto& p) { p.indice = EstadoIndicePaquete::OrigenNoDisponible; })},
    };
    for (const CasoSuperado& c : casos) {
        // Creadas despues del fallo (orden de construccion = creada_en).
        const Ev a = tramo(D::Emitidos, "2026-09-01T00:00:00", "2026-09-15T23:59:59");
        const Ev b = tramo(D::Emitidos, "2026-09-16T00:00:00", "2026-09-30T23:59:59");
        const QByteArray nombre(c.nombre);
        const auto parcial = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {c.fallo, a, b});
        QVERIFY2(parcial.estado == EstadoMes::DisponiblesParcialmente, nombre.constData());
        QVERIFY2(parcial.causas.isEmpty() && !parcial.causa, nombre.constData());
        QVERIFY2(!parcial.causasSuperadas.isEmpty(), nombre.constData());
        QVERIFY2(parcial.causasSuperadas.first().solicitud == c.fallo.solicitud.id, nombre.constData());
        // Con la otra direccion completa: Disponibles.
        const auto completo = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {c.fallo, a, b, completa(D::Recibidos)});
        QVERIFY2(completo.estado == EstadoMes::Disponibles, nombre.constData());
    }
    // 5002 superado: tambien se informan las causas que portaba.
    const Ev a = completa(D::Emitidos);
    const auto r = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {c5002, a});
    QCOMPARE(r.causasSuperadas.size(), 2);
    QCOMPARE(r.causasSuperadas.at(0).causa, CausaAtencion::Codigo5002);
    QCOMPARE(r.causasSuperadas.at(1).causa, CausaAtencion::EnvioFallido);
    // Un fallo de tramo parcial queda cubierto por una completa posterior.
    Ev falloParcial = tramo(D::Recibidos, "2026-09-10T00:00:00", "2026-09-12T00:00:00");
    falloParcial.solicitud.estadoSolicitudSat = EstadoSolicitudSat::Rechazada;
    falloParcial.paquetes.clear();
    const auto rp = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {falloParcial, completa(D::Recibidos), a});
    QCOMPARE(rp.estado, EstadoMes::Disponibles);
}

void TestCoberturaMensual::causasQueSiguenActivas()
{
    // Contrapuntos: la cobertura posterior parcial, de otra direccion o
    // anterior no supera la causa.
    Ev c5002 = conEstado(D::Emitidos, EstadoLocal::EnvioFallido);
    c5002.solicitud.codEstatusSolicitud = QStringLiteral("5002");
    const Ev posteriorParcial = tramo(D::Emitidos, "2026-09-01T00:00:00", "2026-09-29T23:59:59");
    {
        const auto r = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {c5002, posteriorParcial});
        QCOMPARE(r.estado, EstadoMes::RequiereAtencion);
        QCOMPARE(r.causa->causa, CausaAtencion::Codigo5002);
        QVERIFY(r.causasSuperadas.isEmpty());
    }
    {
        Ev c5005 = conEstado(D::Emitidos, EstadoLocal::EnvioFallido);
        c5005.solicitud.codEstatusSolicitud = QStringLiteral("5005");
        const auto r = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {c5005, completa(D::Recibidos)});
        QCOMPARE(r.estado, EstadoMes::RequiereAtencion); // otra direccion
        QCOMPARE(r.causa->causa, CausaAtencion::Codigo5005);
    }
    {
        const Ev anterior = completa(D::Emitidos);
        const Ev errorPaq = conPaquete(D::Emitidos, [](auto& p) { p.estadoDescarga = EstadoDescarga::Error; });
        const auto r = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {anterior, errorPaq});
        QCOMPARE(r.estado, EstadoMes::RequiereAtencion); // la cobertura es anterior
        QCOMPARE(r.causa->causa, CausaAtencion::PaqueteError);
        QVERIFY(r.emitidos.alcanzaFrontera);
    }
    {
        // Posterior completa pero de una solicitud no acreditada (en curso).
        Ev enCurso = completa(D::Emitidos);
        Ev fallo = conEstado(D::Emitidos, EstadoLocal::EnvioIncierto);
        fallo.solicitud.creadaEn = enCurso.solicitud.creadaEn.addSecs(-10);
        enCurso.solicitud.estadoSolicitudSat = EstadoSolicitudSat::EnProceso;
        enCurso.paquetes.clear();
        const auto r = evaluarCoberturaMensual(kPerfil, kSeptiembre, kAhora, {fallo, enCurso});
        QCOMPARE(r.estado, EstadoMes::RequiereAtencion);
        QCOMPARE(r.causa->causa, CausaAtencion::EnvioIncierto);
    }
}
