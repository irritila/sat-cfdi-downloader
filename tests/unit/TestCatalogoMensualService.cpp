#include "TestCatalogoMensualService.h"

#include "application/cfdi/CatalogoMensualCfdiService.h"
#include "application/cfdi/ProgresoIndexacionCfdi.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "domain/solicitudes/OperacionSat.h"

#include <QCoreApplication>
#include <QMutex>
#include <QPromise>
#include <QSet>
#include <QTest>
#include <QThread>
#include <QTimeZone>

#include <atomic>
#include <memory>

using namespace satcfdi;

namespace {

constexpr int kEspera = 10000;
const QString kRfc = QStringLiteral("XAXX010101000");
const PerfilId kPerfil = *PerfilId::desdeTexto(u"0f000000-0000-4000-8000-000000000001");
const MesCalendario kSeptiembre = *MesCalendario::crear(2026, 9);
const QDateTime kAhora = QDateTime(QDate(2026, 10, 20), QTime(18, 0), QTimeZone(QTimeZone::UTC));

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, kEspera) || f.isCanceled() || f.resultCount() == 0) {
        return std::nullopt;
    }
    return f.result();
}

ErrorPersistencia errorAlmacenamiento()
{
    return ErrorPersistencia::de(ErrorPersistencia::Tipo::Almacenamiento, QStringLiteral("disco"));
}

// Solicitud canonica Terminada del mes con un paquete Descargado.
EvidenciaSolicitudMensual canonica(TipoDescarga d, int n, std::optional<EstadoIndicePaquete> indice)
{
    EvidenciaSolicitudMensual e;
    SolicitudPersistida& s = e.solicitud;
    s.id = *SolicitudId::desdeTexto(QStringLiteral("5e000000-0000-4000-8000-%1").arg(n, 12, 10, QLatin1Char('0')));
    s.perfilSatId = kPerfil;
    s.tipoCfdi = d;
    s.operacionSat = operacionPara(d);
    s.rfcSolicitante = kRfc;
    (d == TipoDescarga::Emitidos ? s.rfcEmisor : s.rfcReceptor) = kRfc;
    s.fechaInicialSat = kSeptiembre.inicioSat();
    s.fechaFinalSat = kSeptiembre.finSat();
    s.estadoLocal = EstadoLocal::Enviada;
    s.estadoSolicitudSat = EstadoSolicitudSat::Terminada;
    s.creadaEn = kAhora.addSecs(n);
    EvidenciaPaqueteMensual p;
    p.paqueteId = QStringLiteral("p%1").arg(n);
    p.estadoDescarga = EstadoDescarga::Descargado;
    p.indice = indice;
    p.indiceVigente = indice.has_value();
    e.paquetes = {p};
    return e;
}

class CatalogoMensualMemoria final : public CatalogoMensualRepository {
public:
    void fijarEvidencia(QList<EvidenciaSolicitudMensual> e)
    {
        QMutexLocker b(&m_mutex);
        m_evidencia = std::move(e);
    }
    void fallar(bool f) { m_fallar = f; }
    int lecturasEvidencia() const { return m_lecturas; }
    int versionLeida() const { return m_version; }
    QThread* hilo() const
    {
        QMutexLocker b(&m_mutex);
        return m_hilo;
    }

    Resultado<CatalogoMensual, ErrorPersistencia> consultar(const ConsultaCatalogoMensual& c) override
    {
        registrarHilo();
        if (m_fallar) {
            return Resultado<CatalogoMensual, ErrorPersistencia>::fallo(errorAlmacenamiento());
        }
        CatalogoMensual r;
        r.total = c.mes.mes(); // eco para verificar el paso de la consulta
        return Resultado<CatalogoMensual, ErrorPersistencia>::exito(r);
    }

    Resultado<QList<EvidenciaSolicitudMensual>, ErrorPersistencia> leerEvidencia(const PerfilId&, MesCalendario,
                                                                               int versionParser) override
    {
        registrarHilo();
        ++m_lecturas;
        m_version = versionParser;
        if (m_fallar) {
            return Resultado<QList<EvidenciaSolicitudMensual>, ErrorPersistencia>::fallo(errorAlmacenamiento());
        }
        QMutexLocker b(&m_mutex);
        return Resultado<QList<EvidenciaSolicitudMensual>, ErrorPersistencia>::exito(m_evidencia);
    }

private:
    void registrarHilo()
    {
        QMutexLocker b(&m_mutex);
        m_hilo = QThread::currentThread();
    }

    mutable QMutex m_mutex;
    QList<EvidenciaSolicitudMensual> m_evidencia;
    QThread* m_hilo = nullptr;
    std::atomic<bool> m_fallar{false};
    std::atomic<int> m_lecturas{0};
    std::atomic<int> m_version{0};
};

class LotesMemoria final : public LoteDescargaGuiadaRepository {
public:
    Resultado<Exito, ErrorPersistencia> insertar(const NuevoLoteGuiado&) override
    {
        return Resultado<Exito, ErrorPersistencia>::exito({});
    }
    Resultado<QList<LoteGuiado>, ErrorPersistencia> listar(const PerfilId& perfil,
                                                           const std::optional<MesCalendario>& mes) override
    {
        hilo = QThread::currentThread();
        LoteGuiado l;
        l.perfil = perfil;
        l.mes = mes.value_or(kSeptiembre);
        return Resultado<QList<LoteGuiado>, ErrorPersistencia>::exito({l});
    }
    std::atomic<QThread*> hilo{nullptr};
};

// Indexador falso: revisarSolicitud devuelve un future que la prueba completa.
class IndexacionFalsa final : public ProgresoIndexacionCfdi {
public:
    bool cubre(const SolicitudId& s) const override
    {
        hiloCubre = QThread::currentThread();
        return programadas.contains(s.texto());
    }
    QFuture<void> revisarSolicitud(const SolicitudId& s) override
    {
        revisadas.append(s.texto());
        auto promesa = std::make_shared<QPromise<void>>();
        promesa->start();
        pendientes.append(promesa);
        return promesa->future();
    }
    int versionParser() const override { return 7; }

    void completarTodas()
    {
        for (const auto& p : std::as_const(pendientes)) {
            p->finish();
        }
        pendientes.clear();
    }

    QSet<QString> programadas;
    QStringList revisadas;
    QList<std::shared_ptr<QPromise<void>>> pendientes;
    mutable QThread* hiloCubre = nullptr;
};

struct Entorno {
    PersistenceDispatcher dispatcher;
    CatalogoMensualMemoria catalogo;
    LotesMemoria lotes;
    IndexacionFalsa indexacion;
    int lecturasReloj = 0;
    QDateTime ahora = kAhora;
    std::unique_ptr<CatalogoMensualCfdiService> servicio;

    Entorno()
    {
        servicio = std::make_unique<CatalogoMensualCfdiService>(dispatcher, catalogo, lotes, indexacion, [this] {
            ++lecturasReloj;
            return ahora;
        });
    }
    ~Entorno()
    {
        servicio.reset();
        dispatcher.cerrar();
    }
};

} // namespace

void TestCatalogoMensualService::consultarYLotesFueraDelHiloGrafico()
{
    Entorno e;
    ConsultaCatalogoMensual c;
    c.perfil = kPerfil;
    c.mes = kSeptiembre;
    const auto r = esperar(e.servicio->consultar(c));
    QVERIFY(r && *r);
    QCOMPARE(r->valor().total, 9);
    QVERIFY(e.catalogo.hilo() != nullptr);
    QVERIFY(e.catalogo.hilo() != QThread::currentThread());
    QCOMPARE(e.catalogo.hilo(), e.dispatcher.hilo());

    // Perfil nulo: vacio sin tocar el repositorio.
    ConsultaCatalogoMensual nulo;
    QCOMPARE(esperar(e.servicio->consultar(nulo))->valor().total, 0);

    const auto l = esperar(e.servicio->lotes(kPerfil, kSeptiembre));
    QVERIFY(l && *l);
    QCOMPARE(l->valor().size(), 1);
    QCOMPARE(l->valor().first().mes, kSeptiembre);
    QCOMPARE(e.lotes.hilo.load(), e.dispatcher.hilo());
}

void TestCatalogoMensualService::disponibilidadRevalidaYRelee()
{
    Entorno e;
    // Primera lectura: emitidos con indice pendiente; recibidos sin paquetes
    // Descargado (no se revalida).
    EvidenciaSolicitudMensual emitidos = canonica(TipoDescarga::Emitidos, 1, std::nullopt);
    EvidenciaSolicitudMensual recibidos = canonica(TipoDescarga::Recibidos, 2, EstadoIndicePaquete::Indexado);
    recibidos.paquetes.first().estadoDescarga = EstadoDescarga::Disponible;
    e.catalogo.fijarEvidencia({emitidos, recibidos});

    QFuture<CatalogoMensualCfdiService::ResultadoDisponibilidad> f = e.servicio->disponibilidad(kPerfil, kSeptiembre);
    QCOMPARE(e.lecturasReloj, 1); // capturado al iniciar
    QVERIFY(QTest::qWaitFor([&] { return !e.indexacion.revisadas.isEmpty(); }, kEspera));
    QCOMPARE(e.indexacion.revisadas, QStringList{emitidos.solicitud.id.texto()});
    QCOMPARE(e.catalogo.lecturasEvidencia(), 1);
    QCOMPARE(e.catalogo.versionLeida(), 7);
    QVERIFY(!f.isFinished()); // espera la revalidacion

    // El indexador termina: los ZIP quedaron indexados; la segunda lectura lo
    // refleja.
    emitidos.paquetes.first().indice = EstadoIndicePaquete::Indexado;
    emitidos.paquetes.first().indiceVigente = true;
    recibidos.paquetes.first().estadoDescarga = EstadoDescarga::Descargado;
    e.catalogo.fijarEvidencia({emitidos, recibidos});
    e.ahora = kAhora.addDays(40); // el reloj ya no se vuelve a leer
    e.indexacion.completarTodas();
    const auto r = esperar(f);
    QVERIFY(r && *r);
    QCOMPARE(e.catalogo.lecturasEvidencia(), 2);
    QCOMPARE(e.lecturasReloj, 1);
    const EvaluacionCoberturaMensual& ev = r->valor();
    QCOMPARE(ev.estado, EstadoMes::Disponibles);
    QCOMPARE(ev.tipoMes, TipoMes::Cerrado);
    QCOMPARE(centro::fechaSat(ev.instanteCentro), QStringLiteral("2026-10-20T12:00:00"));
    QCOMPARE(e.indexacion.hiloCubre, QThread::currentThread()); // estado del indexador en su hilo
}

void TestCatalogoMensualService::disponibilidadSinRevalidarUsaIndexacionProgramada()
{
    Entorno e;
    const EvidenciaSolicitudMensual emitidos = canonica(TipoDescarga::Emitidos, 1, std::nullopt);
    e.catalogo.fijarEvidencia({emitidos});
    using R = CatalogoMensualCfdiService::Revalidacion;

    auto sinTrabajo = esperar(e.servicio->disponibilidad(kPerfil, kSeptiembre, R::No));
    QVERIFY(sinTrabajo && *sinTrabajo);
    QCOMPARE(sinTrabajo->valor().estado, EstadoMes::RequiereAtencion);
    QCOMPARE(sinTrabajo->valor().causa->causa, CausaAtencion::IndexacionSinProgramar);

    e.indexacion.programadas.insert(emitidos.solicitud.id.texto());
    auto conTrabajo = esperar(e.servicio->disponibilidad(kPerfil, kSeptiembre, R::No));
    QVERIFY(conTrabajo && *conTrabajo);
    QCOMPARE(conTrabajo->valor().estado, EstadoMes::Preparando);
    QVERIFY(e.indexacion.revisadas.isEmpty());
    QCOMPARE(e.catalogo.lecturasEvidencia(), 2);

    // Sin solicitudes con Descargado no hay revalidacion ni segunda lectura.
    e.catalogo.fijarEvidencia({});
    auto vacio = esperar(e.servicio->disponibilidad(kPerfil, kSeptiembre));
    QVERIFY(vacio && *vacio);
    QCOMPARE(vacio->valor().estado, EstadoMes::SinDescargar);
    QCOMPARE(e.catalogo.lecturasEvidencia(), 3);
}

void TestCatalogoMensualService::erroresSePropagan()
{
    Entorno e;
    e.catalogo.fallar(true);
    ConsultaCatalogoMensual c;
    c.perfil = kPerfil;
    c.mes = kSeptiembre;
    const auto r = esperar(e.servicio->consultar(c));
    QVERIFY(r && !*r);
    QCOMPARE(r->error().tipo, ErrorPersistencia::Tipo::Almacenamiento);
    const auto d = esperar(e.servicio->disponibilidad(kPerfil, kSeptiembre));
    QVERIFY(d && !*d);
    QCOMPARE(d->error().tipo, ErrorPersistencia::Tipo::Almacenamiento);
    QVERIFY(e.indexacion.revisadas.isEmpty());

    // Error en la relectura tras revalidar.
    e.catalogo.fallar(false);
    e.catalogo.fijarEvidencia({canonica(TipoDescarga::Emitidos, 1, std::nullopt)});
    auto f = e.servicio->disponibilidad(kPerfil, kSeptiembre);
    QVERIFY(QTest::qWaitFor([&] { return !e.indexacion.pendientes.isEmpty(); }, kEspera));
    e.catalogo.fallar(true);
    e.indexacion.completarTodas();
    const auto segunda = esperar(f);
    QVERIFY(segunda && !*segunda);
}

void TestCatalogoMensualService::mesInicialPorOmisionSegunReloj()
{
    Entorno e;
    e.ahora = QDateTime(QDate(2027, 1, 15), QTime(18, 0), QTimeZone(QTimeZone::UTC));
    QCOMPARE(e.servicio->mesEnCurso()->texto(), QStringLiteral("2027-01"));
    QCOMPARE(e.servicio->mesInicialPorOmision()->texto(), QStringLiteral("2026-12"));
    // 1 ene 05:59:59Z sigue siendo 31 dic en Centro.
    e.ahora = QDateTime(QDate(2027, 1, 1), QTime(5, 59, 59), QTimeZone(QTimeZone::UTC));
    QCOMPARE(e.servicio->mesEnCurso()->texto(), QStringLiteral("2026-12"));
}
