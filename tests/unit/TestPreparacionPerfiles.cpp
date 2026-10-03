#include "TestPreparacionPerfiles.h"

#include "fakes/FakeCredencialesSatService.h"
#include "fakes/FakePerfilesSatService.h"

#include "application/profiles/ConsultaPreparacionPerfiles.h"

#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

using namespace satcfdi;
using namespace Qt::StringLiterals;
using fakes::FakeCredencialesSatService;
using fakes::FakePerfilesSatService;
using Cat = ErrorSecretStore::Categoria;

namespace {

const QDateTime kHasta = QDateTime(QDate(2029, 1, 1), QTime(0, 0), QTimeZone(QTimeZone::UTC));

struct Entorno {
    FakePerfilesSatService perfiles;
    FakeCredencialesSatService credenciales;
    ConsultaPreparacionPerfiles consulta{perfiles, credenciales};

    PerfilResumen activo = FakePerfilesSatService::perfil("AAA010101AAA", "Activo", true);
    PerfilResumen inactivo = FakePerfilesSatService::perfil("BBB020202BBB", "Inactivo", false);
    PerfilResumen otro = FakePerfilesSatService::perfil("CCC030303CCC", "Otro", true);

    void resolverLista(int i = 0)
    {
        perfiles.listas.resolver(i, PerfilesSatService::ResultadoLista::exito({activo, inactivo, otro}));
    }
};

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, 5000) || f.isCanceled()) {
        return std::nullopt;
    }
    return f.result();
}

} // namespace

void TestPreparacionPerfiles::catalogoYRegla()
{
    QSet<QString> claves;
    for (PreparacionPerfil p : kPreparacionesPerfil) {
        claves.insert(claveEstable(p));
        QCOMPARE(esListoParaSolicitudes(true, p), p == PreparacionPerfil::Lista);
        QVERIFY(!esListoParaSolicitudes(false, p));
    }
    QCOMPARE(claves.size(), 8);
    QCOMPARE(preparacionDesde(EstadoCredencial::Validando), PreparacionPerfil::Verificando);
    for (EstadoCredencial e : kEstadosCredencial) {
        QVERIFY(preparacionDesde(e) != PreparacionPerfil::EstadoNoDisponible);
    }
}

void TestPreparacionPerfiles::listarPersistidosPublicaVerificando()
{
    Entorno e;
    auto f = e.consulta.listarPersistidos();
    QVERIFY(!f.isFinished());
    e.resolverLista();
    const auto r = esperar(f);
    QVERIFY(r && r->esExito());
    QCOMPARE(r->valor().size(), 3);
    for (const PerfilConPreparacion& p : r->valor()) {
        QCOMPARE(p.preparacion, PreparacionPerfil::Verificando);
        QVERIFY(!p.listoParaSolicitudes);
        QVERIFY(!p.vigenteHasta);
    }
    QCOMPARE(r->valor().at(1).perfil, e.inactivo); // incluye inactivos
    QVERIFY(e.credenciales.historial.isEmpty());    // fase 1 no consulta credenciales
}

void TestPreparacionPerfiles::verificarMapeaCadaEstadoYVigencia()
{
    for (EstadoCredencial estado : kEstadosCredencial) {
        Entorno e;
        auto f = e.consulta.verificar(e.activo);
        QCOMPARE(e.credenciales.idsResumen, QList<PerfilId>{e.activo.id});
        e.credenciales.resolverResumen(0, estado, kHasta);
        const auto r = esperar(f);
        QVERIFY(r);
        QCOMPARE(r->perfil, e.activo);
        QCOMPARE(r->preparacion, preparacionDesde(estado));
        QCOMPARE(r->listoParaSolicitudes, estado == EstadoCredencial::Lista);
        if (estado == EstadoCredencial::SinCredencial) {
            QVERIFY(!r->vigenteHasta);
        } else {
            QCOMPARE(r->vigenteHasta, std::optional<QDateTime>(kHasta));
        }
    }
}

void TestPreparacionPerfiles::inactivoConListaNoEsElegible()
{
    Entorno e;
    auto f = e.consulta.verificar(e.inactivo);
    e.credenciales.resolverResumen(0, EstadoCredencial::Lista, kHasta);
    const auto r = esperar(f);
    QVERIFY(r);
    QCOMPARE(r->preparacion, PreparacionPerfil::Lista);
    QVERIFY(!r->listoParaSolicitudes);

    auto listos = e.consulta.listarListosParaSolicitudes();
    e.perfiles.listas.resolver(0, PerfilesSatService::ResultadoLista::exito({e.inactivo}));
    QVERIFY(QTest::qWaitFor([&] { return e.credenciales.resumenes.size() == 2; }, 5000));
    e.credenciales.resolverResumen(1, EstadoCredencial::Lista, kHasta);
    const auto lista = esperar(listos);
    QVERIFY(lista && lista->esExito());
    QVERIFY(lista->valor().isEmpty());
}

void TestPreparacionPerfiles::errorOCancelacionEsEstadoNoDisponible()
{
    const QList<ErrorCredencialSat> errores{
        FakeCredencialesSatService::errorAlmacen(Cat::AlmacenBloqueado),
        FakeCredencialesSatService::errorAlmacen(Cat::AccesoDenegado),
        FakeCredencialesSatService::perfilInvalido(),
        ErrorCredencialSat::persistencia(ErrorPersistencia::de(ErrorPersistencia::Tipo::Ocupado, u"x"_s)),
    };
    for (const ErrorCredencialSat& error : errores) {
        Entorno e;
        auto f = e.consulta.verificar(e.activo);
        e.credenciales.resumenes.resolver(0, CredencialesSatService::ResultadoResumen::fallo(error));
        const auto r = esperar(f);
        QVERIFY(r);
        QCOMPARE(r->preparacion, PreparacionPerfil::EstadoNoDisponible);
        QVERIFY(!r->listoParaSolicitudes);
        QVERIFY(!r->vigenteHasta);
    }
    // Cancelacion (dispatcher cerrado) tambien.
    Entorno e;
    auto f = e.consulta.verificar(e.activo);
    e.credenciales.resumenes.cancelar(0);
    const auto r = esperar(f);
    QVERIFY(r);
    QCOMPARE(r->preparacion, PreparacionPerfil::EstadoNoDisponible);

    // Reintento: una nueva verificacion consulta de nuevo.
    auto reintento = e.consulta.verificar(e.activo);
    e.credenciales.resolverResumen(1, EstadoCredencial::Lista);
    QCOMPARE(esperar(reintento)->preparacion, PreparacionPerfil::Lista);
}

void TestPreparacionPerfiles::listarVerificadosConRespuestasDesordenadas()
{
    Entorno e;
    auto f = e.consulta.listarVerificados();
    e.resolverLista();
    QVERIFY(QTest::qWaitFor([&] { return e.credenciales.resumenes.size() == 3; }, 5000));
    QCOMPARE(e.credenciales.idsResumen, (QList<PerfilId>{e.activo.id, e.inactivo.id, e.otro.id}));
    // Llegan en orden inverso; la tercera falla.
    e.credenciales.fallarResumen(2);
    QVERIFY(!f.isFinished());
    e.credenciales.resolverResumen(1, EstadoCredencial::Vencida, kHasta);
    e.credenciales.resolverResumen(0, EstadoCredencial::Lista, kHasta);
    const auto r = esperar(f);
    QVERIFY(r && r->esExito());
    QCOMPARE(r->valor().size(), 3);
    QCOMPARE(r->valor().at(0).perfil, e.activo); // orden de RFC conservado
    QCOMPARE(r->valor().at(0).preparacion, PreparacionPerfil::Lista);
    QVERIFY(r->valor().at(0).listoParaSolicitudes);
    QCOMPARE(r->valor().at(1).preparacion, PreparacionPerfil::Vencida);
    QCOMPARE(r->valor().at(2).preparacion, PreparacionPerfil::EstadoNoDisponible);
}

void TestPreparacionPerfiles::listarListosParaSolicitudesSoloListos()
{
    Entorno e;
    auto f = e.consulta.listarListosParaSolicitudes();
    e.resolverLista();
    QVERIFY(QTest::qWaitFor([&] { return e.credenciales.resumenes.size() == 3; }, 5000));
    e.credenciales.resolverResumen(0, EstadoCredencial::Lista, kHasta);    // activo: listo
    e.credenciales.resolverResumen(1, EstadoCredencial::Lista, kHasta);    // inactivo: no
    e.credenciales.resolverResumen(2, EstadoCredencial::SinCredencial);    // otro: no
    const auto r = esperar(f);
    QVERIFY(r && r->esExito());
    QCOMPARE(r->valor().size(), 1);
    QCOMPARE(r->valor().constFirst().perfil, e.activo);

    // Sin perfiles: lista vacia sin consultar credenciales.
    Entorno vacio;
    auto g = vacio.consulta.listarListosParaSolicitudes();
    vacio.perfiles.listas.resolver(0, PerfilesSatService::ResultadoLista::exito({}));
    const auto rv = esperar(g);
    QVERIFY(rv && rv->esExito() && rv->valor().isEmpty());
}

void TestPreparacionPerfiles::errorDePerfilesSePropaga()
{
    Entorno e;
    auto f = e.consulta.listarVerificados();
    auto g = e.consulta.listarPersistidos();
    const auto error = ErrorPersistencia::de(ErrorPersistencia::Tipo::Almacenamiento, u"x"_s);
    e.perfiles.listas.resolver(0, PerfilesSatService::ResultadoLista::fallo(error));
    e.perfiles.listas.resolver(1, PerfilesSatService::ResultadoLista::fallo(error));
    QVERIFY(!esperar(f)->esExito());
    QVERIFY(!esperar(g)->esExito());
    QVERIFY(e.credenciales.historial.isEmpty());
}

void TestPreparacionPerfiles::fakesRegistranSinSecretosYEmitenExplicitamente()
{
    FakePerfilesSatService perfiles;
    FakeCredencialesSatService credenciales;
    QSignalSpy cambioPerfiles(&perfiles, &PerfilesSatService::perfilesCambiaron);
    QSignalSpy cambioCredencial(&credenciales, &CredencialesSatService::credencialCambio);

    // Perfiles: RfcDuplicado y respuesta tardia.
    auto c1 = perfiles.crear(u"aaa010101aaa"_s, u"Uno"_s);
    auto c2 = perfiles.crear(u"AAA010101AAA"_s, u"Dos"_s);
    QCOMPARE(perfiles.llamadasCrear.size(), 2);
    perfiles.creaciones.resolver(1, PerfilesSatService::ResultadoCrear::fallo(FakePerfilesSatService::rfcDuplicado()));
    QCOMPARE(esperar(c2)->error().tipo, ErrorCrearPerfil::Tipo::RfcDuplicado);
    QVERIFY(!c1.isFinished());
    perfiles.creaciones.resolver(0, PerfilesSatService::ResultadoCrear::exito(
                                        FakePerfilesSatService::perfil("AAA010101AAA", "Uno")));
    QVERIFY(esperar(c1)->esExito());
    QCOMPARE(cambioPerfiles.count(), 0); // solo explicito
    perfiles.emitirCambio();
    QCOMPARE(cambioPerfiles.count(), 1);

    // Credenciales: registro sin secretos, reemplazo y origen de error.
    EntradaEFirma entrada;
    entrada.rutaCertificado = u"/privado/x.cer"_s;
    entrada.rutaLlavePrivada = u"/privado/x.key"_s;
    entrada.contrasena = BufferSecreto::desdeTexto(u"secreto");
    const PerfilId id = PerfilId::generar();
    auto r = credenciales.reemplazar(id, std::move(entrada));
    QVERIFY(entrada.contrasena.vacio()); // NOLINT(bugprone-use-after-move)
    QCOMPARE(credenciales.registros.size(), 1);
    QVERIFY(credenciales.registros.first().esReemplazo);
    QVERIFY(credenciales.registros.first().conCertificado && credenciales.registros.first().conLlave);
    QCOMPARE(credenciales.registros.first().largoContrasena, std::size_t(7));
    credenciales.reemplazos.resolver(0, CredencialesSatService::ResultadoImportacion::fallo(
                                            FakeCredencialesSatService::errorAlmacen(Cat::FormatoInvalido,
                                                                                     OrigenErrorEFirma::Llave)));
    const auto error = esperar(r);
    QCOMPARE(error->error().origen, OrigenErrorEFirma::Llave);
    QCOMPARE(cambioCredencial.count(), 0);
    credenciales.emitirCambio(id);
    QCOMPARE(cambioCredencial.count(), 1);
    QCOMPARE(cambioCredencial.at(0).at(0).toString(), id.texto());
    QCOMPARE(credenciales.historial, QStringList{u"reemplazar"_s});
}
