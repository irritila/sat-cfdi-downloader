#include "TestPlanificadorExportacion.h"

#include "application/cfdi/PlanificadorExportacion.h"
#include "domain/solicitudes/OperacionSat.h"

#include <QSet>
#include <QTest>
#include <QTimeZone>

using namespace satcfdi;

namespace {

// Datos sinteticos: RFC generico del SAT y UUID inventados.
const QString kRfc = QStringLiteral("XAXX010101000");
const PerfilId kPerfil = *PerfilId::desdeTexto(u"0f000000-0000-4000-8000-000000000001");
const MesCalendario kAgosto = *MesCalendario::crear(2026, 8);
const MesCalendario kSeptiembre = *MesCalendario::crear(2026, 9);
const MesCalendario kDiciembre = *MesCalendario::crear(2026, 12); // futuro respecto de kAhora
const QDateTime kAhora = QDateTime(QDate(2026, 10, 20), QTime(18, 0), QTimeZone(QTimeZone::UTC));
const QString kUuidA = QStringLiteral("AAAAAAAA-0000-4000-8000-000000000001");
const QString kUuidB = QStringLiteral("BBBBBBBB-0000-4000-8000-000000000002");
const QString kUuidC = QStringLiteral("CCCCCCCC-0000-4000-8000-000000000003");
const QString kUuidD = QStringLiteral("DDDDDDDD-0000-4000-8000-000000000004");
const QString kUuidE = QStringLiteral("EEEEEEEE-0000-4000-8000-000000000005");
constexpr auto kE = TipoDescarga::Emitidos;
constexpr auto kR = TipoDescarga::Recibidos;

SolicitudId solicitudN(int n)
{
    return *SolicitudId::desdeTexto(QStringLiteral("5e000000-0000-4000-8000-%1").arg(n, 12, 10, QLatin1Char('0')));
}

EvidenciaPaqueteMensual paquete(const QString& id, EstadoDescarga estado = EstadoDescarga::Descargado,
                                std::optional<EstadoIndicePaquete> indice = EstadoIndicePaquete::Indexado)
{
    EvidenciaPaqueteMensual p;
    p.paqueteId = id;
    p.estadoDescarga = estado;
    if (estado == EstadoDescarga::Descargado) {
        p.indice = indice;
        p.indiceVigente = indice.has_value();
    }
    return p;
}

// Solicitud canonica del mes con sus paquetes; Terminada salvo que se pida otro estado.
EvidenciaSolicitudMensual solicitud(TipoDescarga d, MesCalendario mes, int n, QList<EvidenciaPaqueteMensual> paquetes,
                                    EstadoSolicitudSat estadoSat = EstadoSolicitudSat::Terminada)
{
    EvidenciaSolicitudMensual e;
    SolicitudPersistida& s = e.solicitud;
    s.id = solicitudN(n);
    s.perfilSatId = kPerfil;
    s.tipoCfdi = d;
    s.operacionSat = operacionPara(d);
    s.rfcSolicitante = kRfc;
    (d == kE ? s.rfcEmisor : s.rfcReceptor) = kRfc;
    s.fechaInicialSat = mes.inicioSat();
    s.fechaFinalSat = mes.finSat();
    s.estadoLocal = EstadoLocal::Enviada;
    s.estadoSolicitudSat = estadoSat;
    s.creadaEn = kAhora.addSecs(n);
    e.paquetes = std::move(paquetes);
    return e;
}

CfdiMensual cfdi(const QString& paq, int pos, const QString& uuid, TipoDescarga d, int sol = 1)
{
    CfdiMensual c;
    c.cfdi.paqueteId = paq;
    c.cfdi.posicion = pos;
    c.cfdi.resumen.estado = EstadoParseo::Completo;
    c.cfdi.resumen.uuid = uuid;
    c.cfdi.resumen.emisor.rfc = kRfc;
    c.direccion = d;
    c.solicitud = solicitudN(sol);
    return c;
}

// Fuente sintetica que el catalogo en memoria informa para cada paquete.
PaqueteFuenteExportacion fuenteDe(const QString& paq)
{
    return PaqueteFuenteExportacion{QStringLiteral("paquetes/") + paq + QStringLiteral(".zip"),
                                    HuellaZip{100, 1700000000000, QString(64, QLatin1Char('a'))}};
}

// `base`: nombre D18 esperado; vacio = el UUID.
FacturaPlaneada planeada(const QString& paq, int pos, MesCalendario mes, TipoDescarga d,
                         std::optional<QString> uuid, const QString& base = {})
{
    FacturaPlaneada f;
    f.origen = ReferenciaFactura{paq, pos};
    f.mes = mes;
    f.direccion = d;
    f.nombre = NombreEntradaFactura{QString(carpetaDireccion(d)) + QLatin1Char('/') + mes.texto(),
                                    base.isEmpty() ? uuid.value_or(QString()) : base};
    f.uuidCanonico = std::move(uuid);
    const PaqueteFuenteExportacion fuente = fuenteDe(paq);
    f.rutaPaquete = fuente.rutaLocal;
    f.huellaPaquete = *fuente.huella;
    return f;
}

ErrorPersistencia errorAlmacenamiento()
{
    return ErrorPersistencia::de(ErrorPersistencia::Tipo::Almacenamiento, QStringLiteral("disco"));
}

// Catalogo en memoria: solo la instantanea de exportacion.
class CatalogoExportacionMemoria final : public CatalogoMensualRepository {
public:
    void fijar(MesCalendario mes, QList<CfdiMensual> cfdis, QList<EvidenciaSolicitudMensual> evidencia)
    {
        m_meses.insert(mes.texto(), MesInstantaneaExportacion{mes, std::move(cfdis), std::move(evidencia)});
    }
    void fallar(bool f) { m_fallar = f; }
    // El paquete no tiene huella en el indice.
    void sinHuella(const QString& paq) { m_sinHuella.insert(paq); }

    int capturas = 0;
    int version = 0;
    QList<MesCalendario> pedidos;

    Resultado<CatalogoMensual, ErrorPersistencia> consultar(const ConsultaCatalogoMensual&) override
    {
        return Resultado<CatalogoMensual, ErrorPersistencia>::exito({});
    }
    Resultado<QList<EvidenciaSolicitudMensual>, ErrorPersistencia> leerEvidencia(const PerfilId&, MesCalendario,
                                                                               int) override
    {
        return Resultado<QList<EvidenciaSolicitudMensual>, ErrorPersistencia>::exito({});
    }
    Resultado<QHash<SolicitudId, int>, ErrorPersistencia> conteosPorSolicitud(const PerfilId&) override
    {
        return Resultado<QHash<SolicitudId, int>, ErrorPersistencia>::exito({});
    }
    Resultado<InstantaneaExportacion, ErrorPersistencia> capturarParaExportar(const PerfilId& perfil,
                                                                             const QList<MesCalendario>& meses,
                                                                             int versionParser) override
    {
        ++capturas;
        version = versionParser;
        pedidos = meses;
        if (m_fallar || perfil != kPerfil) {
            return Resultado<InstantaneaExportacion, ErrorPersistencia>::fallo(errorAlmacenamiento());
        }
        InstantaneaExportacion r;
        for (const MesCalendario mes : meses) {
            r.meses.append(m_meses.value(mes.texto(), MesInstantaneaExportacion{mes, {}, {}}));
            for (const CfdiMensual& c : r.meses.constLast().cfdis) {
                PaqueteFuenteExportacion fuente = fuenteDe(c.cfdi.paqueteId);
                if (m_sinHuella.contains(c.cfdi.paqueteId)) {
                    fuente.huella.reset();
                }
                r.paquetes.insert(c.cfdi.paqueteId, fuente);
            }
        }
        return Resultado<InstantaneaExportacion, ErrorPersistencia>::exito(r);
    }

private:
    QSet<QString> m_sinHuella;
    QHash<QString, MesInstantaneaExportacion> m_meses;
    bool m_fallar = false;
};

// Dos meses cerrados con ambas direcciones cubiertas. Agosto emitidas tiene
// un CFDI sin UUID; septiembre emitidas tiene el mismo UUID en dos paquetes
// de dos solicitudes.
void fijarDosMeses(CatalogoExportacionMemoria& catalogo)
{
    catalogo.fijar(kAgosto,
                   {cfdi(QStringLiteral("pa"), 0, kUuidC, kE), cfdi(QStringLiteral("pr"), 0, kUuidB, kR, 2),
                    cfdi(QStringLiteral("pa"), 1, QString(), kE), cfdi(QStringLiteral("pa"), 2, kUuidA, kE)},
                   {solicitud(kE, kAgosto, 1, {paquete(QStringLiteral("pa"))}),
                    solicitud(kR, kAgosto, 2, {paquete(QStringLiteral("pr"))})});
    catalogo.fijar(kSeptiembre,
                   {cfdi(QStringLiteral("ps"), 0, kUuidE, kR, 5), cfdi(QStringLiteral("pc"), 0, kUuidD, kE, 4),
                    cfdi(QStringLiteral("pb"), 0, kUuidD.toLower(), kE, 3)},
                   {solicitud(kE, kSeptiembre, 3, {paquete(QStringLiteral("pb"))}),
                    solicitud(kE, kSeptiembre, 4, {paquete(QStringLiteral("pc"))}),
                    solicitud(kR, kSeptiembre, 5, {paquete(QStringLiteral("ps"))})});
}

PlanExportacion planear(CatalogoExportacionMemoria& catalogo, const AlcanceExportacion& alcance,
                        PlanificadorExportacion::IndexacionProgramada indexacion = {})
{
    PlanificadorExportacion p(catalogo, [] { return kAhora; }, 1, std::move(indexacion));
    auto r = p.planear(alcance);
    if (!r) {
        qWarning("planear fallo");
        return {};
    }
    return std::move(r).valor();
}

} // namespace

void TestPlanificadorExportacion::rangoAmbasOrdenaYDeduplica()
{
    CatalogoExportacionMemoria catalogo;
    fijarDosMeses(catalogo);
    const PlanExportacion plan = planear(catalogo, AlcanceRangoExportacion{kPerfil, kAgosto, kSeptiembre, std::nullopt});

    // Orden: direccion, mes, UUID (sin UUID al final), paquete, posicion. El
    // UUID repetido en pb y pc se planea una vez (pb: menor paquete).
    const QList<FacturaPlaneada> esperadas = {
        planeada(QStringLiteral("pa"), 2, kAgosto, kE, kUuidA),
        planeada(QStringLiteral("pa"), 0, kAgosto, kE, kUuidC),
        planeada(QStringLiteral("pa"), 1, kAgosto, kE, std::nullopt, QStringLiteral("sin-uuid-1")),
        planeada(QStringLiteral("pb"), 0, kSeptiembre, kE, kUuidD),
        planeada(QStringLiteral("pr"), 0, kAgosto, kR, kUuidB),
        planeada(QStringLiteral("ps"), 0, kSeptiembre, kR, kUuidE),
    };
    QCOMPARE(plan.facturas, esperadas);
    QCOMPARE(plan.conteoPrevisto(), 6);
    QVERIFY(plan.omisiones.isEmpty()); // ambos meses cubiertos
    QCOMPARE(catalogo.pedidos, (QList<MesCalendario>{kAgosto, kSeptiembre}));

    // Determinista: el orden de la instantanea no cambia el plan.
    CatalogoExportacionMemoria otro;
    otro.fijar(kAgosto,
               {cfdi(QStringLiteral("pa"), 2, kUuidA, kE), cfdi(QStringLiteral("pa"), 1, QString(), kE),
                cfdi(QStringLiteral("pr"), 0, kUuidB, kR, 2), cfdi(QStringLiteral("pa"), 0, kUuidC, kE)},
               {solicitud(kR, kAgosto, 2, {paquete(QStringLiteral("pr"))}),
                solicitud(kE, kAgosto, 1, {paquete(QStringLiteral("pa"))})});
    otro.fijar(kSeptiembre,
               {cfdi(QStringLiteral("pb"), 0, kUuidD, kE, 3), cfdi(QStringLiteral("ps"), 0, kUuidE, kR, 5),
                cfdi(QStringLiteral("pc"), 0, kUuidD, kE, 4)},
               {solicitud(kE, kSeptiembre, 4, {paquete(QStringLiteral("pc"))}),
                solicitud(kR, kSeptiembre, 5, {paquete(QStringLiteral("ps"))}),
                solicitud(kE, kSeptiembre, 3, {paquete(QStringLiteral("pb"))})});
    QCOMPARE(planear(otro, AlcanceRangoExportacion{kPerfil, kAgosto, kSeptiembre, std::nullopt}).facturas, esperadas);
}

void TestPlanificadorExportacion::direccionEmitidasSoloEmitidas()
{
    CatalogoExportacionMemoria catalogo;
    fijarDosMeses(catalogo);
    const PlanExportacion emitidas = planear(catalogo, AlcanceRangoExportacion{kPerfil, kAgosto, kSeptiembre, kE});
    QCOMPARE(emitidas.conteoPrevisto(), 4);
    for (const FacturaPlaneada& f : emitidas.facturas) {
        QCOMPARE(f.direccion, kE);
    }
    const PlanExportacion recibidas = planear(catalogo, AlcanceMesExportacion{kPerfil, kSeptiembre, kR});
    QCOMPARE(recibidas.facturas,
             (QList<FacturaPlaneada>{planeada(QStringLiteral("ps"), 0, kSeptiembre, kR, kUuidE)}));
    QCOMPARE(catalogo.pedidos, QList<MesCalendario>{kSeptiembre});
}

void TestPlanificadorExportacion::seleccionPorIdentidadYDesaparecida()
{
    CatalogoExportacionMemoria catalogo;
    // Sin evidencia: con seleccion no hay lineas de mes.
    catalogo.fijar(kSeptiembre,
                   {cfdi(QStringLiteral("pb"), 0, kUuidD, kE), cfdi(QStringLiteral("ps"), 0, kUuidE, kR),
                    cfdi(QStringLiteral("ps"), 1, QString(), kR)},
                   {});
    AlcanceSeleccionExportacion s{kPerfil, {}};
    s.facturas = {
        FacturaSeleccionada{{QStringLiteral("pb"), 0}, kSeptiembre, kE, kUuidD},
        FacturaSeleccionada{{QStringLiteral("pb"), 0}, kSeptiembre, kE, kUuidD}, // repetida
        FacturaSeleccionada{{QStringLiteral("borrado"), 3}, kSeptiembre, kR, kUuidC},
        FacturaSeleccionada{{QStringLiteral("borrado"), 4}, kSeptiembre, kE, QStringLiteral("no-canonico")},
        // Otra fuente tomo el lugar de la elegida: se exporta por su UUID.
        FacturaSeleccionada{{QStringLiteral("anterior"), 7}, kSeptiembre, kR, kUuidE},
        FacturaSeleccionada{{QStringLiteral("ps"), 1}, kSeptiembre, kR, QString()},
        FacturaSeleccionada{{QStringLiteral("px"), 0}, kAgosto, kE, kUuidA},
    };
    const PlanExportacion plan = planear(catalogo, s);
    QCOMPARE(plan.facturas, (QList<FacturaPlaneada>{
                                planeada(QStringLiteral("pb"), 0, kSeptiembre, kE, kUuidD),
                                planeada(QStringLiteral("ps"), 0, kSeptiembre, kR, kUuidE),
                                planeada(QStringLiteral("ps"), 1, kSeptiembre, kR, std::nullopt, QStringLiteral("sin-uuid-1")),
                            }));
    QCOMPARE(plan.omisiones,
             (QList<OmisionExportacion>{
                 OmisionFacturaExportacion{kE, kAgosto, kUuidA, MotivoOmisionFactura::YaNoDisponible},
                 OmisionFacturaExportacion{kE, kSeptiembre, QStringLiteral("sin-uuid-1"),
                                           MotivoOmisionFactura::YaNoDisponible},
                 OmisionFacturaExportacion{kR, kSeptiembre, kUuidC, MotivoOmisionFactura::YaNoDisponible},
             }));
    QCOMPARE(catalogo.pedidos, (QList<MesCalendario>{kAgosto, kSeptiembre}));
    QCOMPARE(formatearOmitidas(plan.omisiones),
             std::optional<QByteArray>("emitidas/2026-08/AAAAAAAA-0000-4000-8000-000000000001: ya no disponible\n"
                                       "emitidas/2026-09/sin-uuid-1: ya no disponible\n"
                                       "recibidas/2026-09/CCCCCCCC-0000-4000-8000-000000000003: ya no disponible\n"));
}

void TestPlanificadorExportacion::mes5004SinFacturasConteoCero()
{
    CatalogoExportacionMemoria catalogo;
    EvidenciaSolicitudMensual e = solicitud(kE, kSeptiembre, 1, {}, EstadoSolicitudSat::Rechazada);
    e.solicitud.codigoEstadoSolicitud = QStringLiteral("5004");
    EvidenciaSolicitudMensual r = solicitud(kR, kSeptiembre, 2, {}, EstadoSolicitudSat::Rechazada);
    r.solicitud.codigoEstadoSolicitud = QStringLiteral("5004");
    catalogo.fijar(kSeptiembre, {}, {e, r});
    const PlanExportacion plan = planear(catalogo, AlcanceMesExportacion{kPerfil, kSeptiembre, std::nullopt});
    QCOMPARE(plan.conteoPrevisto(), 0);
    QVERIFY(plan.omisiones.isEmpty());
    QCOMPARE(formatearOmitidas(plan.omisiones), std::optional<QByteArray>());
}

void TestPlanificadorExportacion::lineasDeMesPorCobertura()
{
    CatalogoExportacionMemoria catalogo;
    // Agosto: emitidas en curso en el SAT; recibidas con un paquete indexado y
    // otro cuyo ZIP falta (OrigenNoDisponible, sin filas de CFDI: C1).
    catalogo.fijar(kAgosto, {cfdi(QStringLiteral("pr1"), 0, kUuidB, kR, 2)},
                   {solicitud(kE, kAgosto, 1, {}, EstadoSolicitudSat::EnProceso),
                    solicitud(kR, kAgosto, 2,
                              {paquete(QStringLiteral("pr1")),
                               paquete(QStringLiteral("pr2"), EstadoDescarga::Descargado,
                                       EstadoIndicePaquete::OrigenNoDisponible)})});
    // Septiembre: sin evidencia (nada descargado).
    const PlanExportacion plan = planear(catalogo, AlcanceRangoExportacion{kPerfil, kAgosto, kSeptiembre, std::nullopt});
    QCOMPARE(plan.facturas, (QList<FacturaPlaneada>{planeada(QStringLiteral("pr1"), 0, kAgosto, kR, kUuidB)}));
    QCOMPARE(plan.omisiones,
             (QList<OmisionExportacion>{
                 OmisionMesExportacion{kE, kAgosto, MotivoOmisionMes::DescargasPendientes},
                 OmisionMesExportacion{kR, kAgosto, MotivoOmisionMes::CoberturaIncompleta},
                 OmisionMesExportacion{kE, kSeptiembre, MotivoOmisionMes::CoberturaIncompleta},
                 OmisionMesExportacion{kR, kSeptiembre, MotivoOmisionMes::CoberturaIncompleta},
             }));
    QCOMPARE(formatearOmitidas(plan.omisiones), std::optional<QByteArray>("emitidas/2026-08: descargas pendientes\n"
                                                                          "emitidas/2026-09: cobertura incompleta\n"
                                                                          "recibidas/2026-08: cobertura incompleta\n"
                                                                          "recibidas/2026-09: cobertura incompleta\n"));

    // Solo la direccion pedida.
    QCOMPARE(planear(catalogo, AlcanceMesExportacion{kPerfil, kAgosto, kR}).omisiones,
             (QList<OmisionExportacion>{OmisionMesExportacion{kR, kAgosto, MotivoOmisionMes::CoberturaIncompleta}}));

    // Mes futuro: sin lineas ni facturas.
    const PlanExportacion futuro = planear(catalogo, AlcanceMesExportacion{kPerfil, kDiciembre, std::nullopt});
    QCOMPARE(futuro.conteoPrevisto(), 0);
    QVERIFY(futuro.omisiones.isEmpty());
}

void TestPlanificadorExportacion::indexacionProgramadaEsDescargaPendiente()
{
    CatalogoExportacionMemoria catalogo;
    catalogo.fijar(kSeptiembre, {},
                   {solicitud(kE, kSeptiembre, 1, {paquete(QStringLiteral("pe"), EstadoDescarga::Descargado, std::nullopt)}),
                    solicitud(kR, kSeptiembre, 2, {paquete(QStringLiteral("pr"))})});
    const AlcanceMesExportacion alcance{kPerfil, kSeptiembre, std::nullopt};
    QCOMPARE(planear(catalogo, alcance).omisiones,
             (QList<OmisionExportacion>{
                 OmisionMesExportacion{kE, kSeptiembre, MotivoOmisionMes::CoberturaIncompleta}}));
    QCOMPARE(planear(catalogo, alcance, [](const SolicitudId& s) { return s == solicitudN(1); }).omisiones,
             (QList<OmisionExportacion>{
                 OmisionMesExportacion{kE, kSeptiembre, MotivoOmisionMes::DescargasPendientes}}));
}

void TestPlanificadorExportacion::paqueteNoDescargadoSeOmite()
{
    CatalogoExportacionMemoria catalogo;
    // pd se esta volviendo a descargar: sus filas de CFDI no se exportan (D4).
    catalogo.fijar(kSeptiembre,
                   {cfdi(QStringLiteral("pd"), 0, kUuidA, kE), cfdi(QStringLiteral("pb"), 0, kUuidD, kE),
                    cfdi(QStringLiteral("pb"), 1, QString(), kE)},
                   {solicitud(kE, kSeptiembre, 1,
                              {paquete(QStringLiteral("pb")), paquete(QStringLiteral("pd"), EstadoDescarga::Descargando)}),
                    solicitud(kR, kSeptiembre, 2, {})});
    const PlanExportacion plan = planear(catalogo, AlcanceMesExportacion{kPerfil, kSeptiembre, kE});
    QCOMPARE(plan.facturas, (QList<FacturaPlaneada>{planeada(QStringLiteral("pb"), 0, kSeptiembre, kE, kUuidD),
                                                    planeada(QStringLiteral("pb"), 1, kSeptiembre, kE, std::nullopt,
                                                             QStringLiteral("sin-uuid-1"))}));
    QCOMPARE(plan.omisiones,
             (QList<OmisionExportacion>{
                 OmisionFacturaExportacion{kE, kSeptiembre, kUuidA, MotivoOmisionFactura::NoDisponibleEnEsteEquipo},
                 OmisionMesExportacion{kE, kSeptiembre, MotivoOmisionMes::DescargasPendientes},
             }));
}

void TestPlanificadorExportacion::planearCapturaUnaVezYValida()
{
    CatalogoExportacionMemoria catalogo;
    fijarDosMeses(catalogo);
    int lecturasReloj = 0;
    PlanificadorExportacion p(catalogo, [&lecturasReloj] { ++lecturasReloj; return kAhora; }, 3);

    // Rango invertido: error sin leer.
    auto invertido = p.planear(AlcanceRangoExportacion{kPerfil, kSeptiembre, kAgosto, std::nullopt});
    QVERIFY(!invertido);
    QCOMPARE(invertido.error().tipo, ErrorPersistencia::Tipo::Interno);
    // Perfil nulo o seleccion vacia: plan vacio sin leer.
    auto nulo = p.planear(AlcanceMesExportacion{PerfilId(), kSeptiembre, std::nullopt});
    QVERIFY(nulo);
    QCOMPARE(nulo.valor().conteoPrevisto(), 0);
    auto vacia = p.planear(AlcanceSeleccionExportacion{kPerfil, {}});
    QVERIFY(vacia);
    QCOMPARE(vacia.valor().conteoPrevisto(), 0);
    QCOMPARE(catalogo.capturas, 0);
    QCOMPARE(lecturasReloj, 0);

    // Una captura con la version vigente y un instante.
    auto r = p.planear(AlcanceRangoExportacion{kPerfil, kAgosto, kSeptiembre, std::nullopt});
    QVERIFY(r);
    QCOMPARE(r.valor().conteoPrevisto(), 6);
    QCOMPARE(catalogo.capturas, 1);
    QCOMPARE(catalogo.version, 3);
    QCOMPARE(lecturasReloj, 1);

    // Error del repositorio: se propaga.
    catalogo.fallar(true);
    auto fallo = p.planear(AlcanceMesExportacion{kPerfil, kSeptiembre, std::nullopt});
    QVERIFY(!fallo);
    QCOMPARE(fallo.error().tipo, ErrorPersistencia::Tipo::Almacenamiento);
}

void TestPlanificadorExportacion::planConAsignadorYOmitidas()
{
    CatalogoExportacionMemoria catalogo;
    fijarDosMeses(catalogo);
    const PlanExportacion plan = planear(catalogo, AlcanceRangoExportacion{kPerfil, kAgosto, kSeptiembre, std::nullopt});
    AsignadorNombresExportacion asignador;
    QStringList entradas;
    for (const FacturaPlaneada& f : plan.facturas) {
        // El nombre del plan es el del asignador en el orden del plan.
        QCOMPARE(f.nombre, asignador.siguiente(f.direccion, f.mes, f.uuidCanonico.value_or(QString())));
        entradas.append(f.nombre.xml());
    }
    QCOMPARE(entradas, (QStringList{
                           QStringLiteral("emitidas/2026-08/") + kUuidA + QStringLiteral(".xml"),
                           QStringLiteral("emitidas/2026-08/") + kUuidC + QStringLiteral(".xml"),
                           QStringLiteral("emitidas/2026-08/sin-uuid-1.xml"),
                           QStringLiteral("emitidas/2026-09/") + kUuidD + QStringLiteral(".xml"),
                           QStringLiteral("recibidas/2026-08/") + kUuidB + QStringLiteral(".xml"),
                           QStringLiteral("recibidas/2026-09/") + kUuidE + QStringLiteral(".xml"),
                       }));
    // Ningun nombre interno de paquete en las entradas.
    for (const QString& e : std::as_const(entradas)) {
        for (const QString& paq : {QStringLiteral("pa"), QStringLiteral("pb"), QStringLiteral("pc"),
                                   QStringLiteral("pr"), QStringLiteral("ps")}) {
            QVERIFY(!e.split(QLatin1Char('/')).last().startsWith(paq));
        }
    }
    QCOMPARE(formatearOmitidas(plan.omisiones), std::optional<QByteArray>());
}

void TestPlanificadorExportacion::omisionesSinUuidConNombrePropio()
{
    CatalogoExportacionMemoria catalogo;
    // Hallazgo 7: dos CFDI sin UUID de un paquete en descarga (D4) y uno sin
    // UUID exportado, en el mismo mes y direccion: cada uno con su nombre.
    catalogo.fijar(kSeptiembre,
                   {cfdi(QStringLiteral("pd"), 0, QString(), kE), cfdi(QStringLiteral("pd"), 1, QString(), kE),
                    cfdi(QStringLiteral("pb"), 0, QString(), kE), cfdi(QStringLiteral("pb"), 1, kUuidD, kE)},
                   {solicitud(kE, kSeptiembre, 1,
                              {paquete(QStringLiteral("pb")), paquete(QStringLiteral("pd"), EstadoDescarga::Descargando)})});
    const PlanExportacion plan = planear(catalogo, AlcanceMesExportacion{kPerfil, kSeptiembre, kE});
    QCOMPARE(plan.facturas,
             (QList<FacturaPlaneada>{planeada(QStringLiteral("pb"), 1, kSeptiembre, kE, kUuidD),
                                     planeada(QStringLiteral("pb"), 0, kSeptiembre, kE, std::nullopt,
                                              QStringLiteral("sin-uuid-1"))}));
    const auto m = MotivoOmisionFactura::NoDisponibleEnEsteEquipo;
    QCOMPARE(plan.omisiones,
             (QList<OmisionExportacion>{
                 OmisionFacturaExportacion{kE, kSeptiembre, QStringLiteral("sin-uuid-2"), m},
                 OmisionFacturaExportacion{kE, kSeptiembre, QStringLiteral("sin-uuid-3"), m},
                 OmisionMesExportacion{kE, kSeptiembre, MotivoOmisionMes::DescargasPendientes},
             }));
    // Conteo de omitidas = lineas de CFDI del archivo.
    const std::optional<QByteArray> texto = formatearOmitidas(plan.omisiones);
    QCOMPARE(texto, std::optional<QByteArray>("emitidas/2026-09: descargas pendientes\n"
                                              "emitidas/2026-09/sin-uuid-2: no disponible en este equipo\n"
                                              "emitidas/2026-09/sin-uuid-3: no disponible en este equipo\n"));
}

void TestPlanificadorExportacion::seleccionConIdentidadDeOtroUuid()
{
    // pb,0 ahora contiene otro CFDI (E) que el elegido (D); D sigue en pc,0.
    const auto fijar = [](CatalogoExportacionMemoria& c) {
        c.fijar(kSeptiembre, {cfdi(QStringLiteral("pb"), 0, kUuidE, kE), cfdi(QStringLiteral("pc"), 0, kUuidD, kE)},
                {});
    };
    const auto elegir = [](const QString& uuid) {
        return AlcanceSeleccionExportacion{
            kPerfil, {FacturaSeleccionada{{QStringLiteral("pb"), 0}, kSeptiembre, kE, uuid}}};
    };
    {
        // Se exporta la factura elegida desde su otra fuente, no la identidad.
        CatalogoExportacionMemoria c;
        fijar(c);
        const PlanExportacion plan = planear(c, elegir(kUuidD));
        QCOMPARE(plan.facturas,
                 (QList<FacturaPlaneada>{planeada(QStringLiteral("pc"), 0, kSeptiembre, kE, kUuidD)}));
        QVERIFY(plan.omisiones.isEmpty());
    }
    {
        // El UUID elegido ya no esta en ninguna fuente: ya no disponible.
        CatalogoExportacionMemoria c;
        fijar(c);
        const PlanExportacion plan = planear(c, elegir(kUuidA));
        QCOMPARE(plan.conteoPrevisto(), 0);
        QCOMPARE(plan.omisiones,
                 (QList<OmisionExportacion>{
                     OmisionFacturaExportacion{kE, kSeptiembre, kUuidA, MotivoOmisionFactura::YaNoDisponible}}));
    }
    {
        // Sin UUID canonico elegido basta la identidad.
        CatalogoExportacionMemoria c;
        fijar(c);
        const PlanExportacion plan = planear(c, elegir(QString()));
        QCOMPARE(plan.facturas,
                 (QList<FacturaPlaneada>{planeada(QStringLiteral("pb"), 0, kSeptiembre, kE, kUuidE)}));
    }
}

void TestPlanificadorExportacion::paqueteIlegibleEsCoberturaIncompleta()
{
    CatalogoExportacionMemoria catalogo;
    // Recibidas: un paquete Ilegible (sin filas de CFDI, decision C1).
    catalogo.fijar(kSeptiembre, {cfdi(QStringLiteral("pe"), 0, kUuidD, kE)},
                   {solicitud(kE, kSeptiembre, 1, {paquete(QStringLiteral("pe"))}),
                    solicitud(kR, kSeptiembre, 2,
                              {paquete(QStringLiteral("pi"), EstadoDescarga::Descargado,
                                       EstadoIndicePaquete::Ilegible)})});
    const PlanExportacion plan = planear(catalogo, AlcanceMesExportacion{kPerfil, kSeptiembre, std::nullopt});
    QCOMPARE(plan.facturas, (QList<FacturaPlaneada>{planeada(QStringLiteral("pe"), 0, kSeptiembre, kE, kUuidD)}));
    QCOMPARE(plan.omisiones,
             (QList<OmisionExportacion>{
                 OmisionMesExportacion{kR, kSeptiembre, MotivoOmisionMes::CoberturaIncompleta}}));
}

void TestPlanificadorExportacion::fuenteConRutaYHuella()
{
    CatalogoExportacionMemoria catalogo;
    catalogo.fijar(kSeptiembre, {cfdi(QStringLiteral("pb"), 0, kUuidD, kE), cfdi(QStringLiteral("ph"), 0, kUuidE, kE)},
                   {solicitud(kE, kSeptiembre, 1, {paquete(QStringLiteral("pb")), paquete(QStringLiteral("ph"))})});
    catalogo.sinHuella(QStringLiteral("ph"));
    const PlanExportacion plan = planear(catalogo, AlcanceMesExportacion{kPerfil, kSeptiembre, kE});
    QCOMPARE(plan.facturas.size(), 1);
    QCOMPARE(plan.facturas.at(0).rutaPaquete, QStringLiteral("paquetes/pb.zip"));
    QCOMPARE(plan.facturas.at(0).huellaPaquete, *fuenteDe(QStringLiteral("pb")).huella);
    // Sin huella no se puede revalidar la relectura: se omite.
    QCOMPARE(plan.omisiones,
             (QList<OmisionExportacion>{OmisionFacturaExportacion{kE, kSeptiembre, kUuidE,
                                                                  MotivoOmisionFactura::NoDisponibleEnEsteEquipo}}));
}
