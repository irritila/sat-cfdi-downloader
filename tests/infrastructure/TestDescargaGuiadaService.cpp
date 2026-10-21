#include "TestDescargaGuiadaService.h"

#include "SqlitePruebasComun.h"

#include "application/cfdi/CatalogoMensualCfdiService.h"
#include "application/cfdi/DescargaGuiadaService.h"
#include "application/cfdi/ProgresoIndexacionCfdi.h"
#include "application/logging/RegexLogSanitizer.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "domain/solicitudes/SolicitudCanonica.h"
#include "ports/repositories/CatalogoMensualRepository.h"
#include "ports/repositories/LogSolicitudRepository.h"
#include "ports/repositories/LoteDescargaGuiadaRepository.h"
#include "ports/repositories/PaqueteSolicitudRepository.h"
#include "ports/repositories/PerfilSatRepository.h"
#include "ports/repositories/SolicitudMasivaRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <QTest>
#include <QTimeZone>

#include <memory>

using namespace satcfdi;
using namespace satcfdi::pruebas;

namespace {

constexpr int kEspera = 10000;
// Datos sinteticos: RFC generico del SAT.
const QString kRfc = QStringLiteral("XAXX010101000");
const MesCalendario kSeptiembre = *MesCalendario::crear(2026, 9);
const MesCalendario kOctubre = *MesCalendario::crear(2026, 10);

QDateTime utc(int a, int m, int d, int h, int mi, int s = 0)
{
    return QDateTime(QDate(a, m, d), QTime(h, mi, s), QTimeZone(QTimeZone::UTC));
}

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, kEspera) || f.isCanceled() || f.resultCount() == 0) {
        return std::nullopt;
    }
    return f.result();
}

class IndexacionInactiva final : public ProgresoIndexacionCfdi {
public:
    bool cubre(const SolicitudId&) const override { return false; }
    QFuture<void> revisarSolicitud(const SolicitudId&) override { return QtFuture::makeReadyVoidFuture(); }
    int versionParser() const override { return 1; }
};

struct Entorno {
    QTemporaryDir dir;
    std::unique_ptr<SqlitePersistencia> p;
    std::unique_ptr<PersistenceDispatcher> dispatcher;
    RegexLogSanitizer sanitizer;
    IndexacionInactiva indexacion;
    std::unique_ptr<DescargaGuiadaService> servicio;
    QString perfil;
    QDateTime ahora = utc(2026, 10, 20, 18, 0); // 20 oct 12:00 Centro
    std::optional<EstadoCredencial> credencial = EstadoCredencial::Lista;
    QStringList encoladas;

    bool preparar()
    {
        if (!dir.isValid() || !inicializarBaseSqlite(rutaBase(dir))) {
            return false;
        }
        p = std::make_unique<SqlitePersistencia>(rutaBase(dir));
        dispatcher = std::make_unique<PersistenceDispatcher>();
        perfil = uuid::generarCanonico();
        PuertosDescargaGuiada puertos{
            *dispatcher,
            PuertosPersistencia{p->perfiles(), p->solicitudes(), p->paquetes(), p->logs(), p->unidadDeTrabajo(),
                                sanitizer},
            p->catalogoMensual(),
            p->lotesDescargaGuiada(),
            indexacion,
            [this](const PerfilId&) { return QtFuture::makeReadyValueFuture(credencial); },
            [this](const SolicitudId& id) { encoladas.append(id.texto()); },
            [this] { return ahora; }};
        servicio = std::make_unique<DescargaGuiadaService>(std::move(puertos));
        return insertarPerfilFixture(p->proveedor(), perfil, kRfc).isEmpty();
    }
    ~Entorno()
    {
        servicio.reset();
        if (dispatcher) {
            SqlitePersistencia* persistencia = p.get();
            dispatcher->despachar<void>(std::function<void()>([persistencia] { persistencia->cerrarConexionDelHiloActual(); }));
            dispatcher->cerrar();
        }
        if (p) {
            p->cerrarConexionDelHiloActual();
        }
    }

    PerfilId pid() const { return *PerfilId::desdeTexto(perfil); }

    DescargaGuiadaService::ResultadoPreplan planificar(MesCalendario mes, AccionGuiada a = AccionGuiada::DescargarFacturas)
    {
        auto r = esperar(servicio->planificar(pid(), mes, a));
        return r ? *r : DescargaGuiadaService::ResultadoPreplan::fallo({});
    }
    DescargaGuiadaService::ResultadoEjecucion ejecutar(const PreplanGuiado& pre, DecisionesGuiadas d = {})
    {
        auto r = esperar(servicio->ejecutar(pre, d));
        return r ? *r : DescargaGuiadaService::ResultadoEjecucion::fallo({});
    }

    // "tipo inicio fin estado_local" de todas las solicitudes.
    QStringList solicitudes()
    {
        return columna(p->proveedor(),
                       QStringLiteral("SELECT tipo_cfdi || ' ' || fecha_inicial_sat || ' ' || fecha_final_sat || ' ' || "
                                      "estado_local FROM solicitud_masiva ORDER BY tipo_cfdi, fecha_inicial_sat"));
    }
    int contar(const QString& sql) { return escalar(p->proveedor(), sql).toInt(); }

    // Solicitud canonica del perfil con la dedup_key real del tramo, en el
    // estado indicado. `acreditada`: Terminada con conjunto vacio confirmado.
    QString fixture(TipoDescarga d, const QString& inicio, const QString& fin, const QString& estado,
                    const QString& codigo = {})
    {
        const auto canonica = SolicitudCanonica::normalizarTramo(d, kRfc, inicio, fin);
        if (!canonica) {
            return {};
        }
        const QString id = uuid::generarCanonico();
        const bool emitidos = d == TipoDescarga::Emitidos;
        const bool terminada = estado == QStringLiteral("Terminada") || estado == QStringLiteral("EnProceso");
        const QString local = terminada ? QStringLiteral("Enviada") : estado;
        const bool enviada = local == QStringLiteral("Enviada");
        const QString error = ejecutarSql(
            p->proveedor(),
            QStringLiteral("INSERT INTO solicitud_masiva (id, perfil_sat_id, id_solicitud_sat, tipo_cfdi, operacion_sat, "
                           "rfc_solicitante, rfc_emisor, rfc_receptor, fecha_inicial_sat, fecha_final_sat, dedup_key, "
                           "estado_local, cod_estatus_solicitud, estado_solicitud_sat, creada_en, envio_iniciado_en, "
                           "enviada_en, ultima_verificacion_en, conjunto_paquetes_vacio_confirmado_en) VALUES "
                           "(%1, %2, %3, %4, %5, %6, %7, %8, %9, %10, %11, %12, %13, %14, %15, %16, %17, %18, %19)")
                .arg(citar(id), citar(perfil), enviada ? citar(id.toUpper()) : QStringLiteral("NULL"),
                     emitidos ? QStringLiteral("'emitidos'") : QStringLiteral("'recibidos'"),
                     emitidos ? QStringLiteral("'SolicitaDescargaEmitidos'") : QStringLiteral("'SolicitaDescargaRecibidos'"),
                     citar(kRfc), emitidos ? citar(kRfc) : QStringLiteral("NULL"),
                     emitidos ? QStringLiteral("NULL") : citar(kRfc), citar(inicio))
                .arg(citar(fin), citar(canonica.valor().dedupKey().texto()), citar(local),
                     enviada ? QStringLiteral("'5000'") : (codigo.isEmpty() ? QStringLiteral("NULL") : citar(codigo)),
                     terminada ? citar(estado) : QStringLiteral("NULL"), citar(kAhora),
                     local == QStringLiteral("Creada") || local == QStringLiteral("EnvioFallido") ? QStringLiteral("NULL")
                                                                                               : citar(kAhora),
                     enviada ? citar(kAhora) : QStringLiteral("NULL"))
                .arg(terminada ? citar(kAhora) : QStringLiteral("NULL"),
                     estado == QStringLiteral("Terminada") ? citar(kAhora) : QStringLiteral("NULL")));
        if (!error.isEmpty()) {
            qWarning("fixture: %s", qPrintable(error));
            return {};
        }
        return id;
    }
};

QString linea(const char* tipo, const char* inicio, const char* fin, const char* estado = "Creada")
{
    return QStringLiteral("%1 %2 %3 %4").arg(QLatin1StringView(tipo), QLatin1StringView(inicio),
                                             QLatin1StringView(fin), QLatin1StringView(estado));
}

} // namespace

void TestDescargaGuiadaService::mesCerradoSinEvidenciaCreaDosYLote()
{
    Entorno e;
    QVERIFY(e.preparar());
    auto pre = e.planificar(kSeptiembre);
    QVERIFY2(pre, pre ? "" : qPrintable(pre.error().mensaje));
    QCOMPARE(pre.valor().oferta.accion, std::optional(AccionGuiada::DescargarFacturas));
    QCOMPARE(pre.valor().candidatos.size(), 2);
    QVERIFY(pre.valor().direccionesPorConfirmar().isEmpty());
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM solicitud_masiva")), 0); // planificar no escribe

    auto ej = e.ejecutar(pre.valor());
    QVERIFY2(ej, ej ? "" : qPrintable(ej.error().mensaje));
    QCOMPARE(ej.valor().creadas.size(), 2);
    QVERIFY(ej.valor().loteId);
    QCOMPARE(e.solicitudes(), (QStringList{linea("emitidos", "2026-09-01T00:00:00", "2026-09-30T23:59:59"),
                                           linea("recibidos", "2026-09-01T00:00:00", "2026-09-30T23:59:59")}));
    // Un lote las vincula; se encolan ambas.
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM lote_descarga_guiada_solicitud WHERE lote_id = %1")
                          .arg(citar(*ej.valor().loteId))),
             2);
    QCOMPARE(e.encoladas.size(), 2);
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM log_solicitud WHERE tipo_evento = 'solicitud_creada'")), 2);
    // El mes queda Preparando.
    auto despues = e.planificar(kSeptiembre);
    QVERIFY(despues);
    QCOMPARE(despues.valor().evaluacion.estado, EstadoMes::Preparando);
    // Dedup keys distintas por direccion.
    QVERIFY(pre.valor().candidatos.at(0).dedupKey != pre.valor().candidatos.at(1).dedupKey);
}

void TestDescargaGuiadaService::reutilizaEnCursoYRepetirNoCreaNada()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString enCurso = e.fixture(TipoDescarga::Emitidos, kSeptiembre.inicioSat(), kSeptiembre.finSat(),
                                      QStringLiteral("EnProceso"));
    QVERIFY(!enCurso.isEmpty());
    auto pre = e.planificar(kSeptiembre);
    QVERIFY(pre);
    QCOMPARE(pre.valor().plan.emitidos.reutilizar.size(), 1);
    QCOMPARE(pre.valor().candidatos.size(), 1);
    QCOMPARE(pre.valor().candidatos.first().direccion, TipoDescarga::Recibidos);
    auto ej = e.ejecutar(pre.valor());
    QVERIFY(ej);
    QCOMPARE(ej.valor().creadas.size(), 1);
    QCOMPARE(ej.valor().reutilizadas.first().texto(), enCurso);
    QCOMPARE(e.encoladas.size(), 1); // la reutilizada ya se envio (no esta en Creada)

    // Repetir la accion: como la nueva sigue en Creada (el encolado de esta
    // prueba no envia), se ofrece para reenviarla; reutiliza ambas, no crea
    // nada y reencola la Creada.
    e.encoladas.clear();
    auto otra = e.planificar(kSeptiembre);
    QVERIFY(otra);
    QVERIFY(!otra.valor().creaAlgo());
    QCOMPARE(otra.valor().oferta.accion, std::optional(AccionGuiada::DescargarFacturas));
    auto ej2 = e.ejecutar(otra.valor());
    QVERIFY(ej2);
    QVERIFY(ej2.valor().creadas.isEmpty());
    QVERIFY(!ej2.valor().loteId);
    QCOMPARE(ej2.valor().reutilizadas.size(), 2);
    QCOMPARE(e.encoladas, QStringList{ej.valor().creadas.first().texto()});
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM solicitud_masiva")), 2);

    // Dos invocaciones seguidas con el mismo plan: la segunda no crea nada.
    Entorno f;
    QVERIFY(f.preparar());
    auto plan = f.planificar(kSeptiembre);
    QVERIFY(plan);
    QFuture<DescargaGuiadaService::ResultadoEjecucion> a = f.servicio->ejecutar(plan.valor(), {});
    QFuture<DescargaGuiadaService::ResultadoEjecucion> b = f.servicio->ejecutar(plan.valor(), {});
    const auto ra = esperar(a);
    const auto rb = esperar(b);
    QVERIFY(ra && rb);
    QVERIFY(*ra);
    QVERIFY(!*rb);
    QCOMPARE(rb->error().tipo, ErrorDescargaGuiada::Tipo::PlanDesactualizado);
    QVERIFY(rb->error().preplanActual && !rb->error().preplanActual->creaAlgo());
    QCOMPARE(f.contar(QStringLiteral("SELECT count(*) FROM solicitud_masiva")), 2);
    QCOMPARE(f.contar(QStringLiteral("SELECT count(*) FROM lote_descarga_guiada")), 1);
}

void TestDescargaGuiadaService::direccionCubiertaNoCreaNada()
{
    Entorno e;
    QVERIFY(e.preparar());
    QVERIFY(!e.fixture(TipoDescarga::Recibidos, kSeptiembre.inicioSat(), kSeptiembre.finSat(), QStringLiteral("Terminada"))
                 .isEmpty());
    auto pre = e.planificar(kSeptiembre, AccionGuiada::CompletarMes);
    QVERIFY(pre);
    QCOMPARE(pre.valor().oferta.accion, std::optional(AccionGuiada::CompletarMes));
    QCOMPARE(pre.valor().candidatos.size(), 1);
    QCOMPARE(pre.valor().candidatos.first().direccion, TipoDescarga::Emitidos);
    QVERIFY(e.ejecutar(pre.valor()));
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM solicitud_masiva WHERE tipo_cfdi = 'recibidos'")), 1);
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM solicitud_masiva WHERE tipo_cfdi = 'emitidos'")), 1);
}

void TestDescargaGuiadaService::confirmacionPorDireccion()
{
    // Emitidos: un EnvioFallido previo del mismo tramo -> RequiereConfirmacion.
    auto preparar = [](Entorno& e) {
        return !e.fixture(TipoDescarga::Emitidos, kSeptiembre.inicioSat(), kSeptiembre.finSat(),
                          QStringLiteral("EnvioFallido"), QStringLiteral("301"))
                    .isEmpty();
    };
    Entorno e;
    QVERIFY(e.preparar() && preparar(e));
    auto pre = e.planificar(kSeptiembre);
    QVERIFY(pre);
    QCOMPARE(pre.valor().direccionesPorConfirmar(), QList<TipoDescarga>{TipoDescarga::Emitidos});
    // Sin decision: no se crea nada.
    auto sin = e.ejecutar(pre.valor());
    QVERIFY(!sin);
    QCOMPARE(sin.error().tipo, ErrorDescargaGuiada::Tipo::ConfirmacionPendiente);
    QCOMPARE(sin.error().direcciones, QList<TipoDescarga>{TipoDescarga::Emitidos});
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM solicitud_masiva")), 1);
    // Cancelar emitidos: solo se crea recibidos.
    DecisionesGuiadas cancelar;
    cancelar.emitidos = DecisionConfirmacion::Cancelar;
    auto c = e.ejecutar(pre.valor(), cancelar);
    QVERIFY(c);
    QCOMPARE(c.valor().canceladas, QList<TipoDescarga>{TipoDescarga::Emitidos});
    QCOMPARE(c.valor().creadas.size(), 1);
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM solicitud_masiva WHERE estado_local = 'Creada'")), 1);
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM solicitud_masiva WHERE tipo_cfdi = 'recibidos'")), 1);

    // Confirmar: se crean ambas y queda el log duplicado_confirmado.
    Entorno f;
    QVERIFY(f.preparar() && preparar(f));
    auto pre2 = f.planificar(kSeptiembre);
    DecisionesGuiadas confirmar;
    confirmar.emitidos = DecisionConfirmacion::Confirmar;
    auto ok = f.ejecutar(pre2.valor(), confirmar);
    QVERIFY2(ok, ok ? "" : qPrintable(ok.error().mensaje));
    QCOMPARE(ok.valor().creadas.size(), 2);
    QCOMPARE(f.contar(QStringLiteral("SELECT count(*) FROM log_solicitud WHERE tipo_evento = 'duplicado_confirmado'")), 1);
}

void TestDescargaGuiadaService::mesEnCursoDiaDiezYDoce()
{
    Entorno e;
    QVERIFY(e.preparar());
    e.ahora = utc(2026, 10, 10, 20, 5); // 10 oct 14:05 Centro
    auto pre = e.planificar(kOctubre);
    QVERIFY(pre);
    QVERIFY(e.ejecutar(pre.valor()));
    QCOMPARE(e.solicitudes(), (QStringList{linea("emitidos", "2026-10-01T00:00:00", "2026-10-10T14:05:00"),
                                           linea("recibidos", "2026-10-01T00:00:00", "2026-10-10T14:05:00")}));
    // El SAT termina sin paquetes (simulado): acreditan.
    QCOMPARE(ejecutarSql(e.p->proveedor(),
                         QStringLiteral("UPDATE solicitud_masiva SET estado_local = 'Enviada', id_solicitud_sat = upper(id), "
                                        "cod_estatus_solicitud = '5000', envio_iniciado_en = %1, enviada_en = %1, "
                                        "estado_solicitud_sat = 'Terminada', ultima_verificacion_en = %1, "
                                        "conjunto_paquetes_vacio_confirmado_en = %1")
                             .arg(citar(kAhora))),
             QString());
    const QStringList primeras = columna(e.p->proveedor(), QStringLiteral("SELECT dedup_key FROM solicitud_masiva"));

    // Mismo dia: no se ofrece "Actualizar mes" ni hay nada que crear.
    e.ahora = utc(2026, 10, 11, 5, 0); // 10 oct 23:00 Centro
    auto mismoDia = e.planificar(kOctubre, AccionGuiada::ActualizarMes);
    QVERIFY(!mismoDia);
    QCOMPARE(mismoDia.error().tipo, ErrorDescargaGuiada::Tipo::AccionNoDisponible);
    QVERIFY(mismoDia.error().preplanActual);
    QCOMPARE(mismoDia.error().preplanActual->oferta.accion, std::nullopt);

    // Dia 12: de 14:05:01 del dia 10 al instante de la accion.
    e.ahora = utc(2026, 10, 12, 15, 30); // 12 oct 09:30 Centro
    auto dia12 = e.planificar(kOctubre, AccionGuiada::ActualizarMes);
    QVERIFY(dia12);
    QCOMPARE(dia12.valor().oferta.accion, std::optional(AccionGuiada::ActualizarMes));
    // La accion se ejecuta un poco despues: el tramo usa el instante capturado.
    e.ahora = utc(2026, 10, 12, 15, 31);
    auto ej = e.ejecutar(dia12.valor());
    QVERIFY2(ej, ej ? "" : qPrintable(ej.error().mensaje));
    QCOMPARE(ej.valor().creadas.size(), 2);
    QCOMPARE(columna(e.p->proveedor(),
                     QStringLiteral("SELECT fecha_inicial_sat || ' ' || fecha_final_sat FROM solicitud_masiva "
                                    "WHERE estado_local = 'Creada' ORDER BY tipo_cfdi")),
             (QStringList{QStringLiteral("2026-10-10T14:05:01 2026-10-12T09:30:00"),
                          QStringLiteral("2026-10-10T14:05:01 2026-10-12T09:30:00")}));
    const QStringList todas = columna(e.p->proveedor(), QStringLiteral("SELECT DISTINCT dedup_key FROM solicitud_masiva"));
    QCOMPARE(todas.size(), 4); // todas distintas
    for (const QString& k : primeras) {
        QVERIFY(todas.contains(k));
    }
}

void TestDescargaGuiadaService::completarMesUnaPorHueco()
{
    Entorno e;
    QVERIFY(e.preparar());
    // Emitidos: cobertura del 5 al 10 y del 12 al 20; recibidos: del 1 al 10 14:05.
    QVERIFY(!e.fixture(TipoDescarga::Emitidos, QStringLiteral("2026-09-05T00:00:00"), QStringLiteral("2026-09-10T23:59:59"),
                       QStringLiteral("Terminada"))
                 .isEmpty());
    QVERIFY(!e.fixture(TipoDescarga::Emitidos, QStringLiteral("2026-09-12T00:00:00"), QStringLiteral("2026-09-20T23:59:59"),
                       QStringLiteral("Terminada"))
                 .isEmpty());
    QVERIFY(!e.fixture(TipoDescarga::Recibidos, kSeptiembre.inicioSat(), QStringLiteral("2026-09-10T14:05:00"),
                       QStringLiteral("Terminada"))
                 .isEmpty());
    auto pre = e.planificar(kSeptiembre, AccionGuiada::CompletarMes);
    QVERIFY(pre);
    QCOMPARE(pre.valor().oferta.accion, std::optional(AccionGuiada::CompletarMes));
    auto ej = e.ejecutar(pre.valor());
    QVERIFY2(ej, ej ? "" : qPrintable(ej.error().mensaje));
    QCOMPARE(ej.valor().creadas.size(), 4);
    QCOMPARE(columna(e.p->proveedor(),
                     QStringLiteral("SELECT tipo_cfdi || ' ' || fecha_inicial_sat || ' ' || fecha_final_sat FROM "
                                    "solicitud_masiva WHERE estado_local = 'Creada' ORDER BY tipo_cfdi, fecha_inicial_sat")),
             (QStringList{QStringLiteral("emitidos 2026-09-01T00:00:00 2026-09-04T23:59:59"),
                          QStringLiteral("emitidos 2026-09-11T00:00:00 2026-09-11T23:59:59"),
                          QStringLiteral("emitidos 2026-09-21T00:00:00 2026-09-30T23:59:59"),
                          QStringLiteral("recibidos 2026-09-10T14:05:01 2026-09-30T23:59:59")}));
    // Un lote con las cuatro (tres de emitidos sin traslape).
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM lote_descarga_guiada_solicitud")), 4);
    // Una sola vez: repetir no crea mas (quedan en curso y se reutilizan).
    auto otra = e.planificar(kSeptiembre, AccionGuiada::CompletarMes);
    QVERIFY(otra && !otra.valor().creaAlgo());

    // Mes cerrado completo: no se ofrece descarga.
    Entorno f;
    QVERIFY(f.preparar());
    QVERIFY(!f.fixture(TipoDescarga::Emitidos, kSeptiembre.inicioSat(), kSeptiembre.finSat(), QStringLiteral("Terminada")).isEmpty());
    QVERIFY(!f.fixture(TipoDescarga::Recibidos, kSeptiembre.inicioSat(), kSeptiembre.finSat(), QStringLiteral("Terminada")).isEmpty());
    auto completo = f.planificar(kSeptiembre, AccionGuiada::CompletarMes);
    QVERIFY(!completo);
    QCOMPARE(completo.error().tipo, ErrorDescargaGuiada::Tipo::AccionNoDisponible);
    QCOMPARE(completo.error().preplanActual->evaluacion.estado, EstadoMes::Disponibles);
}

void TestDescargaGuiadaService::eFirmaNoListaYPerfilEliminado()
{
    Entorno e;
    QVERIFY(e.preparar());
    auto pre = e.planificar(kSeptiembre);
    QVERIFY(pre);
    e.credencial = EstadoCredencial::Vencida;
    auto r = e.planificar(kSeptiembre);
    QVERIFY(!r);
    QCOMPARE(r.error().tipo, ErrorDescargaGuiada::Tipo::EFirmaNoLista);
    QCOMPARE(r.error().estadoCredencial, std::optional(EstadoCredencial::Vencida));
    auto ej = e.ejecutar(pre.valor());
    QVERIFY(!ej);
    QCOMPARE(ej.error().tipo, ErrorDescargaGuiada::Tipo::EFirmaNoLista);
    e.credencial.reset(); // la consulta fallo: tampoco se crea
    QCOMPARE(e.ejecutar(pre.valor()).error().tipo, ErrorDescargaGuiada::Tipo::EFirmaNoLista);
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM solicitud_masiva")), 0);
    QVERIFY(e.encoladas.isEmpty());

    // Perfil eliminado entre planificar y ejecutar: se revalida en la transaccion.
    e.credencial = EstadoCredencial::Lista;
    QCOMPARE(ejecutarSql(e.p->proveedor(), QStringLiteral("UPDATE perfil_sat SET eliminado_en = %1 WHERE id = %2")
                                               .arg(citar(kAhora), citar(e.perfil))),
             QString());
    auto eliminado = e.ejecutar(pre.valor());
    QVERIFY(!eliminado);
    QCOMPARE(eliminado.error().tipo, ErrorDescargaGuiada::Tipo::PerfilNoDisponible);
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM solicitud_masiva")), 0);
    // Mes futuro.
    Entorno f;
    QVERIFY(f.preparar());
    auto futuro = f.planificar(*MesCalendario::crear(2026, 11));
    QVERIFY(!futuro);
    QCOMPARE(futuro.error().tipo, ErrorDescargaGuiada::Tipo::MesFuturo);
}

void TestDescargaGuiadaService::planDesactualizadoNoCrea()
{
    Entorno e;
    QVERIFY(e.preparar());
    auto pre = e.planificar(kSeptiembre);
    QVERIFY(pre);
    // Entre planificar y ejecutar aparece una solicitud en curso de emitidos.
    QVERIFY(!e.fixture(TipoDescarga::Emitidos, kSeptiembre.inicioSat(), kSeptiembre.finSat(), QStringLiteral("EnProceso"))
                 .isEmpty());
    auto ej = e.ejecutar(pre.valor());
    QVERIFY(!ej);
    QCOMPARE(ej.error().tipo, ErrorDescargaGuiada::Tipo::PlanDesactualizado);
    QVERIFY(ej.error().preplanActual);
    QCOMPARE(ej.error().preplanActual->candidatos.size(), 1);
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM solicitud_masiva")), 1);
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM lote_descarga_guiada")), 0);
    // Con el plan actualizado si se ejecuta.
    QVERIFY(e.ejecutar(*ej.error().preplanActual));
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM solicitud_masiva")), 2);
}

void TestDescargaGuiadaService::accionNoDisponibleSeRechaza()
{
    Entorno e;
    QVERIFY(e.preparar());
    // Sin evidencia solo se ofrece DescargarFacturas.
    for (AccionGuiada a : {AccionGuiada::CompletarMes, AccionGuiada::ActualizarMes}) {
        auto r = e.planificar(kSeptiembre, a);
        QVERIFY(!r);
        QCOMPARE(r.error().tipo, ErrorDescargaGuiada::Tipo::AccionNoDisponible);
        QCOMPARE(r.error().preplanActual->oferta.accion, std::optional(AccionGuiada::DescargarFacturas));
    }
    // La oferta cambia entre planificar y ejecutar (aparece cobertura de
    // recibidos): la accion planificada ya no es la ofrecida.
    auto pre = e.planificar(kSeptiembre);
    QVERIFY(pre);
    QVERIFY(!e.fixture(TipoDescarga::Recibidos, kSeptiembre.inicioSat(), kSeptiembre.finSat(), QStringLiteral("Terminada"))
                 .isEmpty());
    auto ej = e.ejecutar(pre.valor());
    QVERIFY(!ej);
    QCOMPARE(ej.error().tipo, ErrorDescargaGuiada::Tipo::AccionNoDisponible);
    QCOMPARE(ej.error().preplanActual->oferta.accion, std::optional(AccionGuiada::CompletarMes));
    QCOMPARE(e.contar(QStringLiteral("SELECT count(*) FROM solicitud_masiva")), 1);
}

void TestDescargaGuiadaService::reservaParcialCreaSoloLoLibre()
{
    Entorno e;
    QVERIFY(e.preparar());
    QVERIFY(!e.fixture(TipoDescarga::Recibidos, kSeptiembre.inicioSat(), kSeptiembre.finSat(), QStringLiteral("Terminada"))
                 .isEmpty());
    const QString reserva = e.fixture(TipoDescarga::Emitidos, QStringLiteral("2026-09-25T00:00:00"),
                                      kSeptiembre.finSat(), QStringLiteral("EnProceso"));
    QVERIFY(!e.fixture(TipoDescarga::Emitidos, kSeptiembre.inicioSat(), QStringLiteral("2026-09-20T23:59:59"),
                       QStringLiteral("Terminada"))
                 .isEmpty());
    auto pre = e.planificar(kSeptiembre, AccionGuiada::CompletarMes);
    QVERIFY2(pre, pre ? "" : qPrintable(pre.error().mensaje));
    auto ej = e.ejecutar(pre.valor());
    QVERIFY(ej);
    QCOMPARE(ej.valor().reutilizadas.first().texto(), reserva);
    QCOMPARE(columna(e.p->proveedor(), QStringLiteral("SELECT fecha_inicial_sat || ' ' || fecha_final_sat FROM "
                                                      "solicitud_masiva WHERE estado_local = 'Creada'")),
             QStringList{QStringLiteral("2026-09-21T00:00:00 2026-09-24T23:59:59")});
}

void TestDescargaGuiadaService::historialYUbicacionDeLotes()
{
    Entorno e;
    QVERIFY(e.preparar());
    CatalogoMensualCfdiService catalogo(*e.dispatcher, e.p->catalogoMensual(), e.p->lotesDescargaGuiada(),
                                        e.indexacion, [&e] { return e.ahora; });
    // Descarga guiada de septiembre (lote con emitidas y recibidas) y una
    // solicitud previa a T023 sin lote (agosto, Terminada).
    auto pre = e.planificar(kSeptiembre);
    QVERIFY(pre);
    auto ej = e.ejecutar(pre.valor());
    QVERIFY(ej && ej.valor().loteId);
    const SolicitudId emitida = ej.valor().creadas.at(0);
    const QString anterior = e.fixture(TipoDescarga::Recibidos, QStringLiteral("2026-08-01T00:00:00"),
                                       QStringLiteral("2026-08-31T23:59:59"), QStringLiteral("Terminada"));
    QVERIFY(!anterior.isEmpty());

    auto h = esperar(catalogo.historial(e.pid()));
    QVERIFY(h && *h);
    QCOMPARE(h->valor().size(), 2);
    const EntradaHistorial& lote = h->valor().at(0); // creado ahora (20 oct): mas reciente
    QCOMPARE(lote.tipo, EntradaHistorial::Tipo::Guiada);
    QCOMPARE(lote.loteId, *ej.valor().loteId);
    QCOMPARE(lote.mes, std::optional(kSeptiembre));
    QCOMPARE(lote.resultado, ResultadoHistorial::EnPreparacion);
    QVERIFY(lote.tieneEmitidas && lote.tieneRecibidas);
    QVERIFY(!lote.eliminacionParcial);
    QVERIFY(lote.diagnosticoEmitidas && lote.diagnosticoRecibidas);
    const EntradaHistorial& previa = h->valor().at(1);
    QCOMPARE(previa.tipo, EntradaHistorial::Tipo::Anterior);
    QCOMPARE(previa.resultado, ResultadoHistorial::Anterior);
    QCOMPARE(previa.mes, std::optional(*MesCalendario::crear(2026, 8)));
    QCOMPARE(previa.diagnosticoRecibidas, SolicitudId::desdeTexto(anterior));

    // Ubicacion para notificaciones.
    auto u = esperar(catalogo.ubicar(emitida));
    QVERIFY(u && *u && u->valor());
    QCOMPARE(u->valor()->perfil, e.pid());
    QCOMPARE(u->valor()->mesLote, std::optional(kSeptiembre));
    u = esperar(catalogo.ubicar(*SolicitudId::desdeTexto(anterior)));
    QVERIFY(u && *u && u->valor() && !u->valor()->mesLote);
    u = esperar(catalogo.ubicar(SolicitudId::generar()));
    QVERIFY(u && *u && !u->valor());

    // Eliminacion parcial: la fila conserva mes y fecha y lo indica.
    QCOMPARE(ejecutarSql(e.p->proveedor(), QStringLiteral("UPDATE solicitud_masiva SET eliminado_en = %1 WHERE id = %2")
                                               .arg(citar(kAhora), citar(emitida.texto()))),
             QString());
    h = esperar(catalogo.historial(e.pid()));
    QVERIFY(h && *h);
    QCOMPARE(h->valor().at(0).loteId, *ej.valor().loteId);
    QCOMPARE(h->valor().at(0).mes, std::optional(kSeptiembre));
    QVERIFY(h->valor().at(0).eliminacionParcial);
    QVERIFY(!h->valor().at(0).tieneEmitidas);
    QVERIFY(!h->valor().at(0).diagnosticoEmitidas);
    u = esperar(catalogo.ubicar(emitida));
    QVERIFY(u && *u && !u->valor()); // eliminada -> Facturas

    // Perfil eliminado y recreado con el mismo RFC: sus lotes no aparecen.
    QCOMPARE(ejecutarSql(e.p->proveedor(), QStringLiteral("UPDATE perfil_sat SET eliminado_en = %1 WHERE id = %2")
                                               .arg(citar(kAhora), citar(e.perfil))),
             QString());
    const QString recreado = uuid::generarCanonico();
    QCOMPARE(insertarPerfilFixture(e.p->proveedor(), recreado, kRfc), QString());
    h = esperar(catalogo.historial(e.pid()));
    QVERIFY(h && *h && h->valor().isEmpty());
    h = esperar(catalogo.historial(*PerfilId::desdeTexto(recreado)));
    QVERIFY(h && *h && h->valor().isEmpty());
    u = esperar(catalogo.ubicar(*SolicitudId::desdeTexto(anterior)));
    QVERIFY(u && *u && !u->valor());
}
