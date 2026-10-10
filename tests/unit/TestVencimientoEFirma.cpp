#include "TestVencimientoEFirma.h"

#include "FakesPersistencia.h"
#include "fakes/FakeSecretStore.h"
#include "fakes/FakeAvisoVencimientoRepository.h"
#include "fakes/FakeCredencialesSatService.h"
#include "fakes/FakePerfilesSatService.h"
#include "fakes/FakeProgramador.h"

#include "application/notificaciones/ServicioNotificaciones.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "application/primeruso/ConsultaPrimerUsoPersistida.h"
#include "application/vencimiento/AvisoVencimientoEFirma.h"
#include "domain/common/UuidCanonico.h"
#include "domain/credenciales/VencimientoEFirma.h"

#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

using namespace satcfdi;
using namespace Qt::StringLiterals;

namespace {

const QDateTime kHoy = QDateTime(QDate(2026, 10, 10), QTime(9, 0), QTimeZone(QTimeZone::UTC));
const QString kRfc = u"EKU9003173C9"_s;

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, 30000) || f.isCanceled() || f.resultCount() == 0) {
        return std::nullopt;
    }
    return f.result();
}

class NotificadorFalso final : public Notificador {
public:
    void notificar(const Notificacion& n) override { pedidas.append(n); }
    QList<Notificacion> pedidas;
};

// Banco: perfiles con su preparacion y vigencia, controlados por la prueba.
struct Banco {
    fakes::FakeReloj reloj{kHoy};
    fakes::FakeProgramador programador{reloj};
    fakes::Almacen almacen;
    fakes::FakeUnitOfWork uow{almacen};
    fakes::FakeAvisoVencimientoRepository avisos; // persiste entre "reinicios"
    PersistenceDispatcher dispatcher;
    NotificadorFalso notificador;
    std::unique_ptr<ServicioNotificaciones> servicio;
    std::unique_ptr<AvisoVencimientoEFirma> aviso;
    QList<PerfilConPreparacion> perfiles;
    PerfilId perfil = PerfilId::generar();

    Banco() { arrancar(); }
    ~Banco()
    {
        aviso.reset();
        dispatcher.cerrar();
    }

    // Instancias nuevas (simula reiniciar la app); el dedupe persiste.
    void arrancar()
    {
        aviso.reset();
        servicio = std::make_unique<ServicioNotificaciones>(notificador);
        aviso = std::make_unique<AvisoVencimientoEFirma>(
            [this] {
                return QtFuture::makeReadyValueFuture(ConsultaPreparacionPerfiles::ResultadoLista::exito(perfiles));
            },
            dispatcher, avisos, uow, *servicio, programador, reloj.funcion());
    }

    void fijar(PreparacionPerfil preparacion, std::optional<QDateTime> vigenteHasta, const PerfilId& id)
    {
        PerfilResumen r{id, kRfc, u"Perfil"_s, true};
        PerfilConPreparacion c = PerfilConPreparacion::componer(r, preparacion, vigenteHasta);
        for (PerfilConPreparacion& p : perfiles) {
            if (p.perfil.id == id) {
                p = c;
                return;
            }
        }
        perfiles.append(c);
    }
    void fijar(PreparacionPerfil preparacion, std::optional<QDateTime> vigenteHasta)
    {
        fijar(preparacion, vigenteHasta, perfil);
    }

    int evaluar() { return esperar(aviso->evaluar()).value_or(-1); }

    // Avanza el reloj `dias` dias completos disparando la revision diaria y
    // espera a que cada evaluacion termine (senal, sin tiempo real).
    void avanzarDias(int dias)
    {
        for (int i = 0; i < dias; ++i) {
            QSignalSpy fin(aviso.get(), &AvisoVencimientoEFirma::evaluacionTerminada);
            programador.avanzar(std::chrono::hours(24));
            QVERIFY(QTest::qWaitFor([&] { return fin.count() == 1; }, 30000));
        }
    }
    // iniciar() evalua al arrancar; espera a que termine.
    bool iniciar()
    {
        QSignalSpy fin(aviso.get(), &AvisoVencimientoEFirma::evaluacionTerminada);
        aviso->iniciar();
        return QTest::qWaitFor([&] { return fin.count() == 1; }, 30000);
    }
};

} // namespace

void TestVencimientoEFirma::reglasPuras()
{
    using namespace vencimientoefirma;
    QCOMPARE(diasParaVencer(kHoy.addDays(31), kHoy), std::nullopt);
    QCOMPARE(diasParaVencer(kHoy.addDays(30), kHoy), std::optional(30));
    QCOMPARE(diasParaVencer(kHoy.addDays(30).addSecs(-1), kHoy), std::optional(29));
    QCOMPARE(diasParaVencer(kHoy.addSecs(3600), kHoy), std::optional(0));
    QCOMPARE(diasParaVencer(kHoy, kHoy), std::nullopt);              // vencida
    QCOMPARE(diasParaVencer(kHoy.addDays(-1), kHoy), std::nullopt);
    QCOMPARE(diasParaVencer(QDateTime(), kHoy), std::nullopt);
    QCOMPARE(umbralPara(30), 30);
    QCOMPARE(umbralPara(8), 30);
    QCOMPARE(umbralPara(7), 7);
    QCOMPARE(umbralPara(0), 7);
}

void TestVencimientoEFirma::avisa30UnaVezYNoRepiteTrasReinicio()
{
    Banco b;
    b.fijar(PreparacionPerfil::Lista, kHoy.addDays(31));
    QVERIFY(b.iniciar()); // evalua al arrancar
    QCOMPARE(b.notificador.pedidas.size(), 0); // 31 dias: aun no

    b.avanzarDias(1); // 30 dias
    QCOMPARE(b.notificador.pedidas.size(), 1);
    QCOMPARE(b.notificador.pedidas.at(0).tipo, u"efirma_por_vencer"_s);
    QCOMPARE(b.notificador.pedidas.at(0).id, u"vencimiento:%1:%2:30"_s.arg(
                                                b.perfil.texto(), kHoy.addDays(31).toString(Qt::ISODate)));
    b.avanzarDias(1); // 29 dias: mismo umbral, sin repetir
    QCOMPARE(b.notificador.pedidas.size(), 1);

    // Reinicio: instancias nuevas, dedupe persistido.
    b.arrancar();
    QCOMPARE(b.evaluar(), 0);
    QCOMPARE(b.notificador.pedidas.size(), 1);
    QCOMPARE(b.avisos.registrados(), 1);
}

void TestVencimientoEFirma::avisa7YRevisionDiaria()
{
    Banco b;
    b.fijar(PreparacionPerfil::Lista, kHoy.addDays(9));
    QVERIFY(b.iniciar());
    QCOMPARE(b.notificador.pedidas.size(), 1); // 9 dias: umbral 30
    QVERIFY(b.notificador.pedidas.at(0).id.endsWith(u":30"_s));
    QCOMPARE(b.programador.pendientes(), 1); // revision diaria programada
    b.avanzarDias(1); // 8
    QCOMPARE(b.notificador.pedidas.size(), 1);
    b.avanzarDias(1); // 7
    QCOMPARE(b.notificador.pedidas.size(), 2);
    QVERIFY(b.notificador.pedidas.at(1).id.endsWith(u":7"_s));
    QVERIFY(b.notificador.pedidas.at(1).cuerpo.startsWith(u"La e.firma vence en 7 días"_s));
    b.avanzarDias(3); // 4: sin mas avisos
    QCOMPARE(b.notificador.pedidas.size(), 2);
    b.aviso->detener();
    QCOMPARE(b.programador.pendientes(), 0);
}

void TestVencimientoEFirma::reemplazoVuelveAAvisar()
{
    Banco b;
    b.fijar(PreparacionPerfil::Lista, kHoy.addDays(20));
    QVERIFY(b.iniciar());
    QCOMPARE(b.notificador.pedidas.size(), 1);
    // Reemplazo por otra e.firma que tambien vence pronto: vigencia nueva.
    b.fijar(PreparacionPerfil::Lista, kHoy.addDays(25));
    QSignalSpy fin(b.aviso.get(), &AvisoVencimientoEFirma::evaluacionTerminada);
    b.aviso->alCambiarCredencial();
    QVERIFY(QTest::qWaitFor([&] { return fin.count() == 1; }, 30000));
    QCOMPARE(b.notificador.pedidas.size(), 2);
    QVERIFY(b.notificador.pedidas.at(1).id.contains(kHoy.addDays(25).toString(Qt::ISODate)));
    // Reemplazo por una que vence lejos: sin aviso.
    b.fijar(PreparacionPerfil::Lista, kHoy.addDays(400));
    QCOMPARE(b.evaluar(), 0);
}

void TestVencimientoEFirma::vencidaONoListaNoAvisa()
{
    Banco b;
    b.fijar(PreparacionPerfil::Vencida, kHoy.addDays(-1));
    b.fijar(PreparacionPerfil::Vencida, kHoy.addDays(5), PerfilId::generar()); // estado manda
    b.fijar(PreparacionPerfil::NoVigenteAun, kHoy.addDays(5), PerfilId::generar());
    b.fijar(PreparacionPerfil::EstadoNoDisponible, kHoy.addDays(5), PerfilId::generar());
    b.fijar(PreparacionPerfil::Lista, std::nullopt, PerfilId::generar()); // sin metadata
    QCOMPARE(b.evaluar(), 0);
    QCOMPARE(b.notificador.pedidas.size(), 0);
    QCOMPARE(b.avisos.registrados(), 0);
}

void TestVencimientoEFirma::permisoDenegadoNoCambiaNada()
{
    // El Notificador es "dispara y olvida": con el permiso denegado la
    // entrega no ocurre y nada vuelve a la app. El dedupe queda igual que con
    // permiso (no se reintenta en cada revision).
    Banco b;
    b.fijar(PreparacionPerfil::Lista, kHoy.addDays(10));
    QCOMPARE(b.evaluar(), 1);
    QCOMPARE(b.evaluar(), 0);
    QCOMPARE(b.notificador.pedidas.size(), 1);
    QCOMPARE(b.avisos.registrados(), 1);
}

void TestVencimientoEFirma::textosSinRfcCompleto()
{
    const PerfilId id = PerfilId::generar();
    const QDateTime vigencia(QDate(2026, 11, 3), QTime(12, 0), QTimeZone(QTimeZone::UTC));
    Notificacion n = ServicioNotificaciones::componerVencimiento(id, kRfc, vigencia, 30, 24);
    QCOMPARE(n.tipo, u"efirma_por_vencer"_s);
    QCOMPARE(n.titulo, u"e.firma por vencer"_s);
    const QString fecha = n.cuerpo.section(u'(', 1).section(u')', 0, 0);
    QVERIFY(fecha.endsWith(u" nov 2026"_s));
    QCOMPARE(n.cuerpo, u"La e.firma vence en 24 días (%1).\nRFC ***3C9 · Reemplázala en Perfiles SAT."_s.arg(fecha));
    QVERIFY(!n.cuerpo.contains(kRfc) && !n.titulo.contains(kRfc) && !n.cuerpo.contains(id.texto()));
    QVERIFY(ServicioNotificaciones::componerVencimiento(id, kRfc, vigencia, 7, 1)
                .cuerpo.startsWith(u"La e.firma vence en 1 día ("_s));
    QVERIFY(ServicioNotificaciones::componerVencimiento(id, kRfc, vigencia, 7, 0)
                .cuerpo.startsWith(u"La e.firma vence hoy ("_s));
}

void TestVencimientoEFirma::diasParaVencerEnLaLista()
{
    fakes::FakePerfilesSatService perfiles;
    fakes::FakeCredencialesSatService credenciales;
    fakes::FakeReloj reloj{kHoy};
    ConsultaPreparacionPerfiles consulta(perfiles, credenciales, nullptr, reloj.funcion());
    const PerfilResumen p{PerfilId::generar(), kRfc, u"P"_s, true};

    struct Caso {
        EstadoCredencial estado;
        std::optional<QDateTime> vigencia;
        std::optional<int> dias;
    };
    const QList<Caso> casos = {
        {EstadoCredencial::Lista, kHoy.addDays(12), 12},
        {EstadoCredencial::Lista, kHoy.addDays(30), 30},
        {EstadoCredencial::Lista, kHoy.addDays(45), std::nullopt},
        {EstadoCredencial::Vencida, kHoy.addDays(-2), std::nullopt},
        {EstadoCredencial::NoVigenteAun, kHoy.addDays(3), std::nullopt},
    };
    for (qsizetype i = 0; i < casos.size(); ++i) {
        auto f = consulta.verificar(p);
        credenciales.resolverResumen(int(i), casos.at(i).estado, casos.at(i).vigencia);
        const auto r = esperar(f);
        QVERIFY(r);
        QCOMPARE(r->diasParaVencer, casos.at(i).dias);
    }
}

void TestVencimientoEFirma::primerUso()
{
    fakes::Almacen almacen;
    fakes::FakePerfiles perfiles{almacen};
    fakes::FakeSolicitudes solicitudes{almacen};
    fakes::FakeCredenciales credenciales{almacen};
    fakes::FakeSecretStore store;
    fakes::FakeReloj reloj{QDateTime(QDate(2026, 10, 10), QTime(9, 0), QTimeZone(QTimeZone::UTC))};
    PersistenceDispatcher dispatcher;
    ConsultaPrimerUsoPersistida consulta(dispatcher, perfiles, solicitudes, credenciales, store, reloj.funcion());
    auto estado = [&] {
        const auto r = esperar(consulta.consultar());
        return r && r->esExito() ? std::optional(r->valor()) : std::nullopt;
    };

    // Sin datos.
    QCOMPARE(estado(), std::optional(EstadoPrimerUso{false, false, false}));
    // Con perfil (sin e.firma).
    const PerfilId perfil = PerfilId::generar();
    almacen.perfiles.append(PerfilSat{perfil, kRfc, u"P"_s, true, kHoy, kHoy, std::nullopt});
    QCOMPARE(estado(), std::optional(EstadoPrimerUso{true, false, false}));
    // Con e.firma Lista (fila + generacion vigente en el almacen seguro).
    const CredencialRef ref = store.sembrar();
    CredencialSat c;
    c.id = uuid::generarCanonico();
    c.perfilSatId = perfil;
    c.certificadoRef = ref.referenciaCertificado();
    c.llavePrivadaRef = ref.referenciaContenedor();
    c.contrasenaRef = ref.referenciaContrasena();
    almacen.credenciales.append(c);
    QCOMPARE(estado(), std::optional(EstadoPrimerUso{true, true, false}));
    // Con solicitudes.
    SolicitudPersistida s;
    s.id = SolicitudId::generar();
    s.perfilSatId = perfil;
    s.creadaEn = kHoy;
    almacen.solicitudes.append(s);
    QCOMPARE(estado(), std::optional(EstadoPrimerUso{true, true, true}));
    // e.firma vencida (reloj despues de vigenteHasta): no cuenta como Lista.
    reloj.fijar(store.vigenteHasta.addDays(1));
    QCOMPARE(estado(), std::optional(EstadoPrimerUso{true, false, true}));
    // Llavero bloqueado: cuenta como no Lista (no falla).
    reloj.fijar(kHoy);
    store.fallos.insert(fakes::FakeSecretStore::Operacion::Estado, ErrorSecretStore::Categoria::AlmacenBloqueado);
    QCOMPARE(estado(), std::optional(EstadoPrimerUso{true, false, true}));
    // Error de persistencia: la consulta falla (la UI no muestra la guia).
    almacen.fallos.insert(u"listarPerfilesVisibles"_s, fakes::error(ErrorPersistencia::Tipo::Almacenamiento));
    const auto fallo = esperar(consulta.consultar());
    QVERIFY(fallo && !fallo->esExito());
    QVERIFY(!almacen.hilos.contains(QThread::currentThread())); // fuera del hilo grafico
    dispatcher.cerrar();
}
