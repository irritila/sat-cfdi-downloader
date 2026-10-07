#include "TestOperacionesSatProductivo.h"

#include "EntornoOperaciones.h"
#include "fakes/FakeAccesoCredencialSat.h"
#include "fakes/FakePackageStorage.h"
#include "fakes/FakeProgramador.h"
#include "fakes/FakeSatGateway.h"

#include "application/operaciones/MensajesOperacionSat.h"
#include "application/operaciones/OperacionesSatProductivo.h"
#include "application/operaciones/SaneamientoOperacion.h"
#include "domain/operaciones/PoliticasOperacion.h"

#include <QSignalSpy>
#include <QTest>

#include <functional>
#include <thread>
#include <vector>

using namespace satcfdi;
using fakes::FakeSatGateway;
using Op = fakes::FakeSatGateway::Op;
using Paso = fakes::FakeSatGateway::Paso;
namespace m = satcfdi::mensajessat;

namespace {

const QString kRfc = QStringLiteral("EKU9003173C9");
const QString kIdSolicitudSat = QStringLiteral("4e80345d-917f-40bb-a98f-4a73939343c5");
const QString kIdPaquete = QStringLiteral("4e80345d-917f-40bb-a98f-4a73939343c5_01");

// Banco de pruebas del adaptador: llamadas sincronas desde el hilo de la
// prueba (el adaptador no exige hilo; el ejecutor lo garantiza en la app).
struct Banco {
    fakes::FakeReloj reloj{pruebasT007::kInicio};
    FakeSatGateway gw;
    fakes::FakeAccesoCredencialSat cred;
    fakes::FakePackageStorage storage;
    PerfilId perfil = PerfilId::generar();
    OperacionesSatProductivo ops{gw, cred, storage, reloj.funcion()};

    Banco() { gw.ahora = reloj.funcion(); }

    SolicitudPersistida solicitud(TipoDescarga tipo = TipoDescarga::Emitidos) const
    {
        SolicitudPersistida s;
        s.id = SolicitudId::generar();
        s.perfilSatId = perfil;
        s.tipoCfdi = tipo;
        s.rfcSolicitante = kRfc;
        s.fechaInicialSat = QStringLiteral("2026-09-01T00:00:00");
        s.fechaFinalSat = QStringLiteral("2026-09-30T23:59:59");
        s.estadoLocal = EstadoLocal::Enviando;
        if (tipo == TipoDescarga::Emitidos) {
            s.rfcEmisor = kRfc;
            s.rfcReceptores = {QStringLiteral("AAA010101AAA"), QStringLiteral("BBB010101BBB")};
        } else {
            s.rfcReceptor = kRfc;
            s.rfcEmisor = QStringLiteral("CCC010101CCC");
        }
        s.tipoComprobante = QStringLiteral("I");
        return s;
    }

    ContextoVerificacion verificacion(SenalCancelacion senal = {}) const
    {
        return ContextoVerificacion{SolicitudId::generar(), perfil, kRfc, kIdSolicitudSat, std::move(senal)};
    }

    ContextoDescarga descarga(SenalCancelacion senal = {}) const
    {
        return ContextoDescarga{QStringLiteral("paq-local"), solicitudDescarga, perfil, kRfc, kIdPaquete,
                                std::move(senal), QStringLiteral("2026-09-01T00:00:00")};
    }

    QString rutaDe(const ContextoDescarga& c) const
    {
        return PackageStorage::derivarRutaRelativa(
                   UbicacionPaquete{c.rfcSolicitante, c.fechaInicialSat, c.solicitudId.texto(), c.idPaqueteSat})
            .valor();
    }

    SolicitudId solicitudDescarga = SolicitudId::generar();
};

ErrorSatGateway errorHttp(int http, std::optional<QString> codigo = std::nullopt)
{
    ErrorSatGateway e;
    e.fase = FaseSatGateway::RespuestaExplicita;
    e.estadoHttp = http;
    e.codigoSat = std::move(codigo);
    e.diagnosticoSanitizado = QStringLiteral("HTTP %1").arg(http);
    return e;
}

RespuestaCreacion creacion(const char* cod, bool conId = false)
{
    return {QString::fromLatin1(cod), QStringLiteral("texto SAT crudo"),
            conId ? std::optional<QString>(kIdSolicitudSat) : std::nullopt};
}

RespuestaVerificacion verif(const char* cod, std::optional<int> estado = std::nullopt)
{
    RespuestaVerificacion v;
    v.codEstatus = QString::fromLatin1(cod);
    v.estadoSolicitud = estado;
    v.mensaje = QStringLiteral("texto SAT crudo");
    return v;
}

// Ningun texto visible lleva RFC completo, token, material ni Mensaje SAT.
bool sinSecretos(const QString& texto)
{
    return !texto.contains(kRfc) && !texto.contains(QStringLiteral("fake-token"))
           && !texto.contains(QStringLiteral("ficticia")) && !texto.contains(QStringLiteral("cert-ficticio"))
           && !texto.contains(QStringLiteral("texto SAT crudo")) && !texto.contains(QStringLiteral("Mal Formado"))
           && !texto.contains(QStringLiteral("No existe el paquete"));
}

QByteArray nombre(const char* caso)
{
    return QByteArray("caso: ") + caso;
}

} // namespace

void TestOperacionesSatProductivo::mapeaSolicitudEmitidosYRecibidos()
{
    Banco b;
    const SolicitudPersistida emitidos = b.solicitud(TipoDescarga::Emitidos);
    const SolicitudPersistida recibidos = b.solicitud(TipoDescarga::Recibidos);
    QVERIFY(b.ops.enviar(ContextoEnvio{emitidos, {}}).esExito());
    QVERIFY(b.ops.enviar(ContextoEnvio{recibidos, {}}).esExito());

    const QList<SolicitudSat> enviadas = b.gw.solicitudes();
    QCOMPARE(enviadas.size(), 2);
    QCOMPARE(enviadas.at(0).tipo, TipoDescarga::Emitidos);
    QCOMPARE(enviadas.at(0).rfcSolicitante, kRfc);
    QCOMPARE(enviadas.at(0).contrapartes, emitidos.rfcReceptores);
    QCOMPARE(enviadas.at(0).fechaInicial, emitidos.fechaInicialSat);
    QCOMPARE(enviadas.at(0).fechaFinal, emitidos.fechaFinalSat);
    QCOMPARE(enviadas.at(0).tipoComprobante, std::optional<QString>(QStringLiteral("I")));
    QCOMPARE(enviadas.at(1).tipo, TipoDescarga::Recibidos);
    QCOMPARE(enviadas.at(1).rfcSolicitante, kRfc);
    QCOMPARE(enviadas.at(1).contrapartes, QStringList{QStringLiteral("CCC010101CCC")});
    // Una sola autenticacion para ambas (sesion del perfil).
    QCOMPARE(b.gw.autenticaciones(), 1);
    QCOMPARE(b.gw.maximoActivas(), 1);
}

void TestOperacionesSatProductivo::tablaEnvio()
{
    using C = ErrorSecretStore::Categoria;
    struct Caso {
        const char* nombre;
        std::function<void(Banco&)> preparar;
        std::optional<QString> codExito; // exito con este CodEstatus
        FaseOperacion fase;              // si no es exito
        std::optional<QString> codigo;
        EstadoLocal destino;
        QString mensaje;
        bool sinRed;
    };
    const auto fs = FaseOperacion::Preparacion;
    const std::vector<Caso> casos = {
        {"credencial vencida", [](Banco& b) { b.cred.fijarEstado(EstadoCredencial::Vencida); }, std::nullopt, fs,
         QStringLiteral("credencial:Vencida"), EstadoLocal::Creada, m::credencialVencida(), true},
        {"llavero bloqueado",
         [](Banco& b) { b.cred.fijarErrorEstado(ErrorCredencialSat::almacen(C::AlmacenBloqueado)); }, std::nullopt, fs,
         QStringLiteral("credencial:AlmacenBloqueado"), EstadoLocal::Creada, m::llaveroBloqueado(), true},
        {"material danado",
         [](Banco& b) { b.cred.fijarErrorMaterial(ErrorCredencialSat::almacen(C::CredencialDanada)); }, std::nullopt,
         fs, QStringLiteral("credencial:CredencialDanada"), EstadoLocal::Creada, m::credencialIlegible(), true},
        {"autentica fault", [](Banco& b) { b.gw.programar(Op::Autenticar, Paso::conError(FakeSatGateway::faultSintetico())); },
         std::nullopt, FaseOperacion::Autenticacion, QStringLiteral("InvalidSecurity"), EstadoLocal::Creada,
         m::autenticacionRechazada(), true},
        {"autentica sin red",
         [](Banco& b) {
             b.gw.programar(Op::Autenticar,
                            Paso::conError(FakeSatGateway::errorDeFase(FaseSatGateway::AntesDeEnvio, true)));
         },
         std::nullopt, FaseOperacion::Autenticacion, std::nullopt, EstadoLocal::Creada, m::autenticacionSinConexion(),
         true},
        {"gateway preparacion",
         [](Banco& b) {
             b.gw.programar(Op::Crear, Paso::conError(FakeSatGateway::errorDeFase(FaseSatGateway::Preparacion)));
         },
         std::nullopt, fs, std::nullopt, EstadoLocal::Creada, m::envioNoIniciado(), false},
        {"timeout antes de envio",
         [](Banco& b) {
             b.gw.programar(Op::Crear, Paso::conError(FakeSatGateway::errorDeFase(FaseSatGateway::AntesDeEnvio, true)));
         },
         std::nullopt, FaseOperacion::AntesDeEnvio, std::nullopt, EstadoLocal::Creada, m::envioNoIniciado(), false},
        {"corte despues de envio",
         [](Banco& b) {
             b.gw.programar(Op::Crear, Paso::conError(FakeSatGateway::errorDeFase(FaseSatGateway::DespuesDeEnvio)));
         },
         std::nullopt, FaseOperacion::DespuesDeEnvio, std::nullopt, EstadoLocal::EnvioIncierto, m::envioIncierto(),
         false},
        {"fault sintetico", [](Banco& b) { b.gw.programar(Op::Crear, Paso::conError(FakeSatGateway::faultSintetico())); },
         std::nullopt, FaseOperacion::RespuestaExplicita, QStringLiteral("InvalidSecurity"),
         EstadoLocal::EnvioIncierto, m::envioIncierto(), false},
        {"http no 200", [](Banco& b) { b.gw.programar(Op::Crear, Paso::conError(errorHttp(503))); }, std::nullopt,
         FaseOperacion::RespuestaExplicita, QStringLiteral("503"), EstadoLocal::EnvioIncierto, m::envioIncierto(),
         false},
        {"5000 aceptada", [](Banco&) {}, QStringLiteral("5000"), fs, std::nullopt, EstadoLocal::Enviada,
         m::solicitudAceptada(), false},
        {"5000 sin id", [](Banco& b) { b.gw.programar(Op::Crear, Paso::conCreacion(creacion("5000"))); },
         QStringLiteral("5000"), fs, std::nullopt, EstadoLocal::EnvioIncierto, m::envioIncierto(), false},
        {"5006", [](Banco& b) { b.gw.programar(Op::Crear, Paso::conCreacion(creacion("5006"))); },
         QStringLiteral("5006"), fs, std::nullopt, EstadoLocal::EnvioIncierto, m::envioIncierto(), false},
        {"no documentado", [](Banco& b) { b.gw.programar(Op::Crear, Paso::conCreacion(creacion("9999"))); },
         QStringLiteral("9999"), fs, std::nullopt, EstadoLocal::EnvioIncierto, m::envioIncierto(), false},
        {"301 fixture", [](Banco& b) { b.gw.programar(Op::Crear, Paso::conCreacion(FakeSatGateway::rechazo301())); },
         QStringLiteral("301"), fs, std::nullopt, EstadoLocal::EnvioFallido, m::solicitudRechazada(u"301"), false},
        {"305", [](Banco& b) { b.gw.programar(Op::Crear, Paso::conCreacion(creacion("305"))); },
         QStringLiteral("305"), fs, std::nullopt, EstadoLocal::EnvioFallido, m::solicitudRechazada(u"305"), false},
        {"5001", [](Banco& b) { b.gw.programar(Op::Crear, Paso::conCreacion(creacion("5001"))); },
         QStringLiteral("5001"), fs, std::nullopt, EstadoLocal::EnvioFallido, m::solicitudNoAutorizada(), false},
        {"5002", [](Banco& b) { b.gw.programar(Op::Crear, Paso::conCreacion(creacion("5002"))); },
         QStringLiteral("5002"), fs, std::nullopt, EstadoLocal::EnvioFallido, m::limiteCriterio5002(), false},
        {"5005", [](Banco& b) { b.gw.programar(Op::Crear, Paso::conCreacion(creacion("5005"))); },
         QStringLiteral("5005"), fs, std::nullopt, EstadoLocal::EnvioFallido, m::solicitudActiva5005(), false},
    };
    for (const Caso& c : casos) {
        Banco b;
        c.preparar(b);
        const auto r = b.ops.enviar(ContextoEnvio{b.solicitud(), {}});
        QVERIFY2(r.esExito() == c.codExito.has_value(), nombre(c.nombre));
        QString texto;
        if (r.esExito()) {
            QVERIFY2(r.valor().codEstatus == *c.codExito, nombre(c.nombre));
            QVERIFY2(politicas::destinoEnvio(r.valor()) == c.destino, nombre(c.nombre));
            texto = r.valor().mensaje;
            QVERIFY2(texto == c.mensaje, nombre(c.nombre));
        } else {
            const FallaOperacion& f = r.error();
            QVERIFY2(f.fase == c.fase, nombre(c.nombre));
            QVERIFY2(f.codigo == c.codigo, nombre(c.nombre));
            QVERIFY2(politicas::destinoEnvio(f) == c.destino, nombre(c.nombre));
            QVERIFY2(f.diagnosticoSanitizado.startsWith(c.mensaje), nombre(c.nombre));
            texto = textoUltimoError(f);
        }
        QVERIFY2(sinSecretos(texto), nombre(c.nombre));
        if (c.sinRed) {
            QVERIFY2(b.gw.llamadas(Op::Crear) == 0, nombre(c.nombre));
        }
    }
}

void TestOperacionesSatProductivo::tablaVerificacion()
{
    struct Caso {
        const char* nombre;
        std::function<void(Banco&)> preparar;
        std::optional<EstadoSolicitudSat> estado; // exito
        FaseOperacion fase;
        std::optional<QString> codigo;
        bool suspendible;
        QString mensaje;
    };
    auto conV = [](RespuestaVerificacion v) {
        return [v](Banco& b) { b.gw.programar(Op::Verificar, Paso::conVerificacion(v)); };
    };
    const auto rx = FaseOperacion::RespuestaExplicita;
    const std::vector<Caso> casos = {
        {"5000 terminada (fixture)", [](Banco&) {}, EstadoSolicitudSat::Terminada, rx, std::nullopt, false, {}},
        {"5000 en proceso", conV(verif("5000", 2)), EstadoSolicitudSat::EnProceso, rx, std::nullopt, false, {}},
        {"5000 vencida", conV(verif("5000", 6)), EstadoSolicitudSat::Vencida, rx, std::nullopt, false, {}},
        {"5000 rechazada", conV(verif("5000", 5)), EstadoSolicitudSat::Rechazada, rx, std::nullopt, false, {}},
        {"300", conV(verif("300")), std::nullopt, rx, QStringLiteral("300"), true, m::verificacionTransitoria()},
        {"302", conV(verif("302")), std::nullopt, rx, QStringLiteral("302"), true, m::verificacionTransitoria()},
        {"303", conV(verif("303")), std::nullopt, rx, QStringLiteral("303"), true, m::verificacionTransitoria()},
        {"5004", conV(verif("5004")), std::nullopt, rx, QStringLiteral("5004"), true, m::solicitudNoEncontrada()},
        {"304", conV(verif("304")), std::nullopt, FaseOperacion::Preparacion, QStringLiteral("304"), false,
         m::credencialVencida()},
        {"305", conV(verif("305")), std::nullopt, FaseOperacion::Preparacion, QStringLiteral("305"), false,
         m::credencialIlegible()},
        {"5011", conV(verif("5011")), std::nullopt, rx, QStringLiteral("5011"), false, m::limiteDiario5011()},
        {"404", [](Banco& b) { b.gw.programar(Op::Verificar, Paso::conError(errorHttp(404))); }, std::nullopt, rx,
         QStringLiteral("404"), false, m::verificacionTransitoria()},
        {"5000 sin estado", conV(verif("5000")), std::nullopt, rx, QStringLiteral("5000"), false,
         m::verificacionTransitoria()},
        {"despues de envio",
         [](Banco& b) {
             b.gw.programar(Op::Verificar, Paso::conError(FakeSatGateway::errorDeFase(FaseSatGateway::DespuesDeEnvio)));
         },
         std::nullopt, FaseOperacion::DespuesDeEnvio, std::nullopt, false, m::verificacionTransitoria()},
        {"credencial no lista", [](Banco& b) { b.cred.fijarEstado(EstadoCredencial::MaterialFaltante); }, std::nullopt,
         FaseOperacion::Preparacion, QStringLiteral("credencial:MaterialFaltante"), false, m::credencialIlegible()},
    };
    for (const Caso& c : casos) {
        Banco b;
        c.preparar(b);
        const auto r = b.ops.verificar(b.verificacion());
        QVERIFY2(r.esExito() == c.estado.has_value(), nombre(c.nombre));
        if (r.esExito()) {
            QVERIFY2(r.valor().estadoSolicitudSat == *c.estado, nombre(c.nombre));
            QVERIFY2(r.valor().codEstatus == QStringLiteral("5000"), nombre(c.nombre));
            QVERIFY2(sinSecretos(r.valor().mensaje), nombre(c.nombre));
            continue;
        }
        const FallaOperacion& f = r.error();
        QVERIFY2(f.fase == c.fase, nombre(c.nombre));
        QVERIFY2(f.codigo == c.codigo, nombre(c.nombre));
        QVERIFY2(politicas::esFallaSuspendible(f) == c.suspendible, nombre(c.nombre));
        QVERIFY2(f.diagnosticoSanitizado.startsWith(c.mensaje), nombre(c.nombre));
        QVERIFY2(sinSecretos(textoUltimoError(f)), nombre(c.nombre));
    }

    // Fixture completo: ids y numero de CFDI se conservan; consulta con el
    // IdSolicitud y el RFC del contexto.
    Banco b;
    const auto r = b.ops.verificar(b.verificacion());
    QVERIFY(r.esExito());
    QCOMPARE(r.valor().idsPaquetes, FakeSatGateway::verificacionTerminada().idsPaquete);
    QCOMPARE(r.valor().numeroCfdi, std::optional<qint64>(12));
    QCOMPARE(r.valor().codigoEstadoSolicitud, std::optional<QString>(QStringLiteral("5000")));
    QCOMPARE(b.gw.verificaciones().constFirst().idSolicitud, kIdSolicitudSat);
}

void TestOperacionesSatProductivo::tablaDescarga()
{
    using EA = ErrorAlmacenamiento;
    struct Caso {
        const char* nombre;
        std::function<void(Banco&, ContextoDescarga&)> preparar;
        bool exito;
        FaseOperacion fase;
        std::optional<QString> codigo;
        std::optional<CausaAlmacenamiento> causa;
        EstadoDescarga destino;
        QString mensaje;
        bool sinRed;
    };
    auto conD = [](RespuestaDescarga r) {
        return [r](Banco& b, ContextoDescarga&) { b.gw.programar(Op::Descargar, Paso::conDescarga(r)); };
    };
    const auto rx = FaseOperacion::RespuestaExplicita;
    const auto al = FaseOperacion::Almacenamiento;
    const std::vector<Caso> casos = {
        {"5000 (fixture)", [](Banco&, ContextoDescarga&) {}, true, rx, std::nullopt, std::nullopt,
         EstadoDescarga::Descargado, m::paqueteDescargado(), false},
        {"5007 (fixture)", conD(FakeSatGateway::descargaVencida5007()), false, rx, QStringLiteral("5007"),
         std::nullopt, EstadoDescarga::Vencido, m::paqueteVencido(), false},
        {"5008", conD({QStringLiteral("5008"), QStringLiteral("texto SAT crudo"), false, 0}), false, rx,
         QStringLiteral("5008"), std::nullopt, EstadoDescarga::Error, m::paqueteMaximoDescargas(), false},
        {"5004", conD({QStringLiteral("5004"), QStringLiteral("texto SAT crudo"), false, 0}), false, rx,
         QStringLiteral("5004"), std::nullopt, EstadoDescarga::Error, m::descargaFallida(), false},
        {"paquete invalido",
         [](Banco& b, ContextoDescarga&) {
             b.gw.programar(Op::Descargar, Paso::conError(errorHttp(200, QStringLiteral("5000"))));
         },
         false, rx, QStringLiteral("5000"), std::nullopt, EstadoDescarga::Error, m::descargaFallida(), false},
        {"despues de envio",
         [](Banco& b, ContextoDescarga&) {
             b.gw.programar(Op::Descargar,
                            Paso::conError(FakeSatGateway::errorDeFase(FaseSatGateway::DespuesDeEnvio, true)));
         },
         false, FaseOperacion::DespuesDeEnvio, std::nullopt, std::nullopt, EstadoDescarga::Error, m::descargaFallida(),
         false},
        {"sin espacio", [](Banco& b, ContextoDescarga&) { b.storage.programarFalla(EA::SinEspacio); }, false, al,
         std::nullopt, CausaAlmacenamiento::EspacioInsuficiente, EstadoDescarga::Error, m::sinEspacio(), false},
        {"permiso", [](Banco& b, ContextoDescarga&) { b.storage.programarFalla(EA::Permiso); }, false, al,
         std::nullopt, CausaAlmacenamiento::EscrituraFallida, EstadoDescarga::Error, m::sinPermisoEscritura(), false},
        {"colision",
         [](Banco& b, ContextoDescarga& c) { b.storage.agregarFinal(b.rutaDe(c), QByteArrayLiteral("previo")); },
         false, al, std::nullopt, CausaAlmacenamiento::ColisionDestino, EstadoDescarga::Error, m::colisionDestino(),
         false},
        {"sin fecha inicial", [](Banco&, ContextoDescarga& c) { c.fechaInicialSat.clear(); }, false,
         FaseOperacion::Preparacion, QStringLiteral("ubicacion_invalida"), std::nullopt, EstadoDescarga::Error,
         m::descargaFallida(), true},
        {"credencial no lista",
         [](Banco& b, ContextoDescarga&) { b.cred.fijarEstado(EstadoCredencial::NoVigenteAun); }, false,
         FaseOperacion::Preparacion, QStringLiteral("credencial:NoVigenteAun"), std::nullopt, EstadoDescarga::Error,
         m::credencialNoVigenteAun(), true},
        {"autenticacion",
         [](Banco& b, ContextoDescarga&) {
             b.gw.programar(Op::Autenticar, Paso::conError(FakeSatGateway::faultSintetico()));
         },
         false, FaseOperacion::Autenticacion, QStringLiteral("InvalidSecurity"), std::nullopt,
         EstadoDescarga::Error, m::autenticacionRechazada(), true},
    };
    for (const Caso& c : casos) {
        Banco b;
        ContextoDescarga ctx = b.descarga();
        c.preparar(b, ctx);
        const QString ruta = ctx.fechaInicialSat.isEmpty() ? QString() : b.rutaDe(ctx);
        const QByteArray previo = ruta.isEmpty() ? QByteArray() : b.storage.bytesFinal(ruta).value_or(QByteArray());
        const auto r = b.ops.descargar(ctx);
        QVERIFY2(r.esExito() == c.exito, nombre(c.nombre));
        if (r.esExito()) {
            QVERIFY2(r.valor().rutaFinal == ruta, nombre(c.nombre));
            QVERIFY2(r.valor().codEstatus == QStringLiteral("5000"), nombre(c.nombre));
            QVERIFY2(r.valor().mensaje == c.mensaje, nombre(c.nombre));
            QVERIFY2(b.storage.bytesFinal(ruta) == std::optional(FakeSatGateway::paqueteOk()), nombre(c.nombre));
            continue;
        }
        const FallaOperacion& f = r.error();
        QVERIFY2(f.fase == c.fase, nombre(c.nombre));
        QVERIFY2(f.codigo == c.codigo, nombre(c.nombre));
        QVERIFY2(f.causaAlmacenamiento == c.causa, nombre(c.nombre));
        QVERIFY2(politicas::desenlaceDescarga(f).destino == c.destino, nombre(c.nombre));
        QVERIFY2(f.diagnosticoSanitizado.startsWith(c.mensaje), nombre(c.nombre));
        QVERIFY2(sinSecretos(textoUltimoError(f)), nombre(c.nombre));
        // Ninguna falla deja un final nuevo (la colision conserva el previo).
        if (!ruta.isEmpty()) {
            QVERIFY2(b.storage.bytesFinal(ruta).value_or(QByteArray()) == previo, nombre(c.nombre));
        }
        if (c.sinRed) {
            QVERIFY2(b.gw.llamadas(Op::Descargar) == 0, nombre(c.nombre));
        }
    }
}

void TestOperacionesSatProductivo::sesionReutilizaYExpiraConReloj()
{
    Banco b;
    QVERIFY(b.ops.verificar(b.verificacion()).esExito());
    QVERIFY(b.ops.verificar(b.verificacion()).esExito());
    QVERIFY(b.ops.descargar(b.descarga()).esExito());
    QCOMPARE(b.gw.autenticaciones(), 1);
    QVERIFY(b.ops.tieneSesion(b.perfil));
    // Material: se lee en cada operacion (no se retiene).
    QCOMPARE(b.cred.lecturasMaterial(), 3);

    // Expires = inicio + 300 s; margen 30 s: a los 269 s sigue vigente.
    b.reloj.fijar(pruebasT007::kInicio.addSecs(269));
    QVERIFY(b.ops.verificar(b.verificacion()).esExito());
    QCOMPARE(b.gw.autenticaciones(), 1);
    // A los 271 s (271 + 30 >= 300) se autentica de nuevo.
    b.reloj.fijar(pruebasT007::kInicio.addSecs(271));
    QVERIFY(b.ops.verificar(b.verificacion()).esExito());
    QCOMPARE(b.gw.autenticaciones(), 2);
    QVERIFY(b.ops.verificar(b.verificacion()).esExito());
    QCOMPARE(b.gw.autenticaciones(), 2);

    // Otro perfil tiene su propia sesion.
    ContextoVerificacion otro = b.verificacion();
    otro.perfilSatId = PerfilId::generar();
    QVERIFY(b.ops.verificar(otro).esExito());
    QCOMPARE(b.gw.autenticaciones(), 3);
}

void TestOperacionesSatProductivo::sesionSeInvalidaPorRechazoFallaYCambio()
{
    Banco b;
    QVERIFY(b.ops.verificar(b.verificacion()).esExito());
    QCOMPARE(b.gw.autenticaciones(), 1);

    // Token rechazado (inferido; fixture sintetico): sin reintento automatico;
    // la siguiente operacion autentica de nuevo.
    b.gw.programar(Op::Verificar, Paso::conError(FakeSatGateway::faultSintetico()));
    const auto rechazo = b.ops.verificar(b.verificacion());
    QVERIFY(!rechazo.esExito());
    QCOMPARE(rechazo.error().fase, FaseOperacion::RespuestaExplicita);
    QVERIFY(!b.ops.tieneSesion(b.perfil));
    QCOMPARE(b.gw.llamadas(Op::Verificar), 2);
    QVERIFY(b.ops.verificar(b.verificacion()).esExito());
    QCOMPARE(b.gw.autenticaciones(), 2);

    // Cambio de credencial (credencialCambio -> invalidarSesion).
    b.ops.invalidarSesion(b.perfil);
    QVERIFY(!b.ops.tieneSesion(b.perfil));
    QVERIFY(b.ops.verificar(b.verificacion()).esExito());
    QCOMPARE(b.gw.autenticaciones(), 3);

    // Falla de Autentica: Autenticacion y sin sesion.
    b.ops.invalidarSesion(b.perfil);
    b.gw.programar(Op::Autenticar, Paso::conError(FakeSatGateway::errorDeFase(FaseSatGateway::DespuesDeEnvio)));
    const auto fallo = b.ops.verificar(b.verificacion());
    QVERIFY(!fallo.esExito());
    QCOMPARE(fallo.error().fase, FaseOperacion::Autenticacion);
    QVERIFY(fallo.error().diagnosticoSanitizado.startsWith(m::autenticacionSinConexion()));
    QVERIFY(!b.ops.tieneSesion(b.perfil));
    QCOMPARE(b.gw.llamadas(Op::Verificar), 4); // la quinta no llego a verificar

    // Una credencial que deja de estar Lista no usa la sesion (sin red).
    QVERIFY(b.ops.verificar(b.verificacion()).esExito());
    QVERIFY(b.ops.tieneSesion(b.perfil));
    b.cred.fijarEstado(EstadoCredencial::Vencida);
    const int verificaciones = b.gw.llamadas(Op::Verificar);
    QVERIFY(!b.ops.verificar(b.verificacion()).esExito());
    QCOMPARE(b.gw.llamadas(Op::Verificar), verificaciones);

    // Cierre: descarta todo.
    b.ops.cerrarSesiones();
    QVERIFY(!b.ops.tieneSesion(b.perfil));
}

void TestOperacionesSatProductivo::tokenObtenidoDuranteInvalidacionNoSeConserva()
{
    Banco b;
    b.gw.programar(Op::Autenticar, Paso::bloqueado(FaseSatGateway::AntesDeEnvio));
    std::optional<bool> exito;
    std::thread hilo([&] { exito = b.ops.verificar(b.verificacion()).esExito(); });
    QVERIFY(b.gw.esperarBloqueo());
    b.ops.invalidarSesion(b.perfil); // p. ej. credencial reemplazada en paralelo
    b.gw.liberar();
    hilo.join();
    QCOMPARE(exito, std::optional<bool>(true));
    QVERIFY(!b.ops.tieneSesion(b.perfil));
}

void TestOperacionesSatProductivo::cancelacionAntesYDuranteLaRed()
{
    {
        // Cancelada antes de empezar: Preparacion, sin credencial ni red.
        Banco b;
        SenalCancelacion senal;
        senal.solicitar();
        const auto r = b.ops.enviar(ContextoEnvio{b.solicitud(), senal});
        QVERIFY(!r.esExito());
        QCOMPARE(r.error().fase, FaseOperacion::Preparacion);
        QVERIFY(r.error().cancelada);
        QCOMPARE(politicas::destinoEnvio(r.error()), EstadoLocal::Creada);
        QCOMPARE(b.gw.autenticaciones(), 0);
        QCOMPARE(b.cred.lecturasMaterial(), 0);
    }
    {
        // Cancelada durante la verificacion: conserva la fase del gateway.
        Banco b;
        SenalCancelacion senal;
        b.gw.programar(Op::Verificar, Paso::bloqueado(FaseSatGateway::DespuesDeEnvio));
        std::optional<Resultado<ResultadoVerificacion, FallaOperacion>> r;
        std::thread hilo([&] { r.emplace(b.ops.verificar(b.verificacion(senal))); });
        QVERIFY(b.gw.esperarBloqueo());
        senal.solicitar();
        hilo.join();
        QVERIFY(r && !r->esExito());
        QCOMPARE(r->error().fase, FaseOperacion::DespuesDeEnvio);
        QVERIFY(r->error().cancelada);
    }
    {
        // Cancelada durante el envio antes de requestSent: Creada.
        Banco b;
        SenalCancelacion senal;
        b.gw.programar(Op::Crear, Paso::bloqueado(FaseSatGateway::AntesDeEnvio));
        std::optional<Resultado<ResultadoEnvio, FallaOperacion>> r;
        std::thread hilo([&] { r.emplace(b.ops.enviar(ContextoEnvio{b.solicitud(), senal})); });
        QVERIFY(b.gw.esperarBloqueo());
        senal.solicitar();
        hilo.join();
        QVERIFY(r && !r->esExito());
        QCOMPARE(r->error().fase, FaseOperacion::AntesDeEnvio);
        QVERIFY(r->error().cancelada);
        QCOMPARE(politicas::destinoEnvio(r->error()), EstadoLocal::Creada);
    }
}

namespace {

// PackageStorage que solicita la cancelacion al empezar a escribir.
class StorageQueCancela final : public PackageStorage {
public:
    StorageQueCancela(fakes::FakePackageStorage& base, SenalCancelacion senal) : m_base(base), m_senal(std::move(senal))
    {
    }
    Resultado<ArchivoFinal, ErrorGuardarZip> guardarAtomico(const UbicacionPaquete& u, FuenteZipPorChunks& f,
                                                            const Cancelacion& c) override
    {
        m_senal.solicitar();
        return m_base.guardarAtomico(u, f, c);
    }
    Resultado<bool, ErrorAlmacenamiento> existeArchivoFinal(const QString& r) override
    {
        return m_base.existeArchivoFinal(r);
    }
    Resultado<QList<HallazgoFilesystem>, ErrorAlmacenamiento> escanearRecuperacion() override
    {
        return m_base.escanearRecuperacion();
    }
    Resultado<Exito, ErrorAlmacenamiento> eliminarTemporal(const HallazgoFilesystem& t) override
    {
        return m_base.eliminarTemporal(t);
    }
    Resultado<RutaRevelable, ErrorAlmacenamiento> resolverArchivoRevelable(const QString& r) override
    {
        return m_base.resolverArchivoRevelable(r);
    }
    Resultado<RutaRevelable, ErrorAlmacenamiento> resolverCarpetaSolicitudRevelable(const QString& r) override
    {
        return m_base.resolverCarpetaSolicitudRevelable(r);
    }
    Resultado<RutaRevelable, ErrorAlmacenamiento> resolverRaizRevelable() override
    {
        return m_base.resolverRaizRevelable();
    }

private:
    fakes::FakePackageStorage& m_base;
    SenalCancelacion m_senal;
};

} // namespace

void TestOperacionesSatProductivo::cancelacionDuranteEscrituraSinFinal()
{
    Banco b;
    SenalCancelacion senal;
    StorageQueCancela storage(b.storage, senal);
    OperacionesSatProductivo ops(b.gw, b.cred, storage, b.reloj.funcion());
    const ContextoDescarga ctx = b.descarga(senal);
    const auto r = ops.descargar(ctx);
    QVERIFY(!r.esExito());
    QCOMPARE(r.error().fase, FaseOperacion::Almacenamiento);
    QVERIFY(r.error().cancelada);
    QCOMPARE(politicas::desenlaceDescarga(r.error()).destino, EstadoDescarga::Error);
    QVERIFY(!b.storage.bytesFinal(b.rutaDe(ctx)));
}

void TestOperacionesSatProductivo::existeArchivoFinalYEstadoCredencial()
{
    Banco b;
    const ContextoDescarga d = b.descarga();
    ContextoArchivoFinal ctx{d.paqueteId, d.solicitudId, d.perfilSatId, d.idPaqueteSat, {}, kRfc,
                             d.fechaInicialSat};
    auto r = b.ops.existeArchivoFinal(ctx);
    QVERIFY(r.esExito());
    QVERIFY(!r.valor().existe);
    QVERIFY(!r.valor().rutaFinal);

    b.storage.agregarFinal(b.rutaDe(d));
    r = b.ops.existeArchivoFinal(ctx);
    QVERIFY(r.esExito());
    QVERIFY(r.valor().existe);
    QCOMPARE(r.valor().rutaFinal, std::optional<QString>(b.rutaDe(d)));

    b.storage.programarFallaLectura(ErrorAlmacenamiento::LecturaRaiz);
    r = b.ops.existeArchivoFinal(ctx);
    QVERIFY(!r.esExito()); // una falla NO significa "no existe"
    QCOMPARE(r.error().fase, FaseOperacion::Almacenamiento);

    ctx.fechaInicialSat.clear();
    r = b.ops.existeArchivoFinal(ctx);
    QVERIFY(!r.esExito());
    QCOMPARE(r.error().fase, FaseOperacion::Preparacion);
    QCOMPARE(b.gw.autenticaciones(), 0); // sin red

    QCOMPARE(b.ops.obtenerEstadoCredencial(b.perfil).valor(), EstadoCredencial::Lista);
    b.cred.fijarEstado(EstadoCredencial::Vencida);
    QCOMPARE(b.ops.obtenerEstadoCredencial(b.perfil).valor(), EstadoCredencial::Vencida);
    b.cred.fijarErrorEstado(ErrorCredencialSat::almacen(ErrorSecretStore::Categoria::AlmacenBloqueado));
    const auto e = b.ops.obtenerEstadoCredencial(b.perfil);
    QVERIFY(!e.esExito());
    QCOMPARE(e.error().fase, FaseOperacion::Preparacion);
    QVERIFY(e.error().diagnosticoSanitizado.startsWith(m::llaveroBloqueado()));
    QCOMPARE(b.gw.autenticaciones(), 0);
}

void TestOperacionesSatProductivo::ultimoErrorSeDesglosa()
{
    Banco b;
    b.gw.programar(Op::Autenticar, Paso::conError(FakeSatGateway::faultSintetico()));
    const auto r = b.ops.verificar(b.verificacion());
    QVERIFY(!r.esExito());
    const QString texto = textoUltimoError(r.error());
    const auto d = desglosarUltimoError(texto);
    QVERIFY(d);
    QCOMPARE(d->fase, FaseOperacion::Autenticacion);
    QCOMPARE(d->codigo, std::optional<QString>(QStringLiteral("InvalidSecurity")));
    QCOMPARE(d->mensaje, m::autenticacionRechazada());
    QCOMPARE(d->detalleTecnico, QStringLiteral("VerificaSolicitudDescarga: HTTP 500; Fault InvalidSecurity"));
    QVERIFY(!d->cancelada);

    FallaOperacion almacen = FallaOperacion::de(FaseOperacion::Almacenamiento, std::nullopt, m::sinEspacio());
    almacen.causaAlmacenamiento = CausaAlmacenamiento::EspacioInsuficiente;
    almacen.cancelada = true;
    const auto da = desglosarUltimoError(textoUltimoError(almacen));
    QVERIFY(da);
    QCOMPARE(da->fase, FaseOperacion::Almacenamiento);
    QCOMPARE(da->causaAlmacenamiento, std::optional(CausaAlmacenamiento::EspacioInsuficiente));
    QVERIFY(!da->codigo);
    QVERIFY(da->cancelada);
    QCOMPARE(da->mensaje, m::sinEspacio());
    QVERIFY(da->detalleTecnico.isEmpty());

    const auto dp = desglosarUltimoError(u"Preparacion:credencial:Vencida: " + m::credencialVencida());
    QVERIFY(dp);
    QCOMPARE(dp->codigo, std::optional<QString>(QStringLiteral("credencial:Vencida")));
    QCOMPARE(dp->mensaje, m::credencialVencida());

    QVERIFY(!desglosarUltimoError(u"CodEstatus 301"));
    QVERIFY(!desglosarUltimoError(u"Envio interrumpido por cierre; resultado incierto"));
}

namespace {

// OperacionExecutor real sobre el adaptador productivo y los fakes de T007.
struct EntornoProductivo {
    pruebasT007::Entorno e; // su ejecutor (con FakeOperacionesSat) queda ocioso
    FakeSatGateway gw;
    fakes::FakeAccesoCredencialSat cred;
    std::unique_ptr<OperacionesSatProductivo> ops;
    std::unique_ptr<OperacionExecutor> ejecutor;

    EntornoProductivo()
    {
        gw.ahora = e.reloj.funcion();
        ops = std::make_unique<OperacionesSatProductivo>(gw, cred, e.storage, e.reloj.funcion());
        ejecutor = std::make_unique<OperacionExecutor>(
            PuertosEjecutor{e.solicitudes, e.logs, e.operaciones, e.uow, e.sanitizer, *ops, [] {}, &e.paquetesRepo,
                            &e.storage},
            e.reloj.funcion(), e.programador);
    }
    ~EntornoProductivo()
    {
        gw.liberar();
        ejecutor.reset();
        ops.reset();
    }
};

} // namespace

void TestOperacionesSatProductivo::ejecutorAplicaEstadosDeLaTabla()
{
    using pruebasT007::esperar;
    struct CasoEnvio {
        const char* nombre;
        std::function<void(EntornoProductivo&)> preparar;
        EstadoLocal destino;
        int creaciones;
    };
    const std::vector<CasoEnvio> envios = {
        {"credencial no lista", [](EntornoProductivo& p) { p.cred.fijarEstado(EstadoCredencial::Vencida); },
         EstadoLocal::Creada, 0},
        {"autenticacion", [](EntornoProductivo& p) { p.gw.programar(Op::Autenticar, Paso::conError(FakeSatGateway::faultSintetico())); },
         EstadoLocal::Creada, 0},
        {"antes de envio",
         [](EntornoProductivo& p) {
             p.gw.programar(Op::Crear, Paso::conError(FakeSatGateway::errorDeFase(FaseSatGateway::AntesDeEnvio, true)));
         },
         EstadoLocal::Creada, 1},
        {"despues de envio",
         [](EntornoProductivo& p) {
             p.gw.programar(Op::Crear, Paso::conError(FakeSatGateway::errorDeFase(FaseSatGateway::DespuesDeEnvio)));
         },
         EstadoLocal::EnvioIncierto, 1},
        {"301", [](EntornoProductivo& p) { p.gw.programar(Op::Crear, Paso::conCreacion(FakeSatGateway::rechazo301())); },
         EstadoLocal::EnvioFallido, 1},
        {"5000", [](EntornoProductivo&) {}, EstadoLocal::Enviada, 1},
    };
    for (const CasoEnvio& c : envios) {
        EntornoProductivo p;
        c.preparar(p);
        const SolicitudId id = p.e.sembrar(EstadoLocal::Creada).id;
        QVERIFY2(esperar(p.ejecutor->enviar(id)), nombre(c.nombre));
        QVERIFY2(p.e.solicitud(id)->estadoLocal == c.destino, nombre(c.nombre));
        QVERIFY2(p.gw.llamadas(Op::Crear) == c.creaciones, nombre(c.nombre));
        const std::optional<QString> error = p.e.solicitud(id)->ultimoError;
        QVERIFY2(!error || sinSecretos(*error), nombre(c.nombre));
        if (c.destino == EstadoLocal::Creada) {
            QVERIFY2(error && desglosarUltimoError(*error), nombre(c.nombre));
        }
        if (c.destino == EstadoLocal::Enviada) {
            QCOMPARE(p.e.solicitud(id)->idSolicitudSat, std::optional<QString>(kIdSolicitudSat));
            QCOMPARE(p.e.solicitud(id)->mensajeSolicitudSat, std::optional<QString>(m::solicitudAceptada()));
        }
    }

    struct CasoDescarga {
        const char* nombre;
        std::optional<RespuestaDescarga> respuesta;
        EstadoDescarga destino;
        std::optional<QString> codigo;
    };
    const std::vector<CasoDescarga> descargas = {
        {"5000", std::nullopt, EstadoDescarga::Descargado, QStringLiteral("5000")},
        {"5007", FakeSatGateway::descargaVencida5007(), EstadoDescarga::Vencido, QStringLiteral("5007")},
        {"5008", RespuestaDescarga{QStringLiteral("5008"), QStringLiteral("texto SAT crudo"), false, 0},
         EstadoDescarga::Error, QStringLiteral("5008")},
    };
    for (const CasoDescarga& c : descargas) {
        EntornoProductivo p;
        if (c.respuesta) {
            p.gw.programar(Op::Descargar, Paso::conDescarga(*c.respuesta));
        }
        const SolicitudId sid = p.e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Terminada).id;
        const QString pid = p.e.sembrarPaquete(sid, EstadoDescarga::Disponible).id;
        QVERIFY2(esperar(p.ejecutor->descargar(pid, OrigenLog::Usuario)), nombre(c.nombre));
        const PaquetePersistido* paq = p.e.paquete(pid);
        QVERIFY2(paq->estadoDescarga == c.destino, nombre(c.nombre));
        QVERIFY2(paq->codigoDescargaSat == c.codigo, nombre(c.nombre));
        const bool hayFinal = p.e.storage.bytesFinal(p.e.rutaFinal(*paq)).has_value();
        QVERIFY2(hayFinal == (c.destino == EstadoDescarga::Descargado), nombre(c.nombre));
        if (c.destino == EstadoDescarga::Descargado) {
            QVERIFY2(paq->rutaLocal == std::optional(p.e.rutaFinal(*paq)), nombre(c.nombre));
        }
    }
}

void TestOperacionesSatProductivo::ejecutorPublicaTransicionesUnaVez()
{
    using pruebasT007::esperar;
    EntornoProductivo p;
    QSignalSpy transiciones(p.ejecutor.get(), &OperacionExecutor::transicionConfirmada);
    const SolicitudId sid = p.e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::Aceptada).id;

    // EnProceso: no notifica; repetirlo tampoco.
    p.gw.programar(Op::Verificar, Paso::conVerificacion(verif("5000", 2)));
    p.gw.programar(Op::Verificar, Paso::conVerificacion(verif("5000", 2)));
    QVERIFY(esperar(p.ejecutor->verificar(sid, OrigenLog::Usuario)));
    QVERIFY(esperar(p.ejecutor->verificar(sid, OrigenLog::Usuario)));
    QVERIFY(p.e.drenar());
    QCOMPARE(transiciones.count(), 0);

    // Terminada con 2 paquetes (fixture): una transicion.
    QVERIFY(esperar(p.ejecutor->verificar(sid, OrigenLog::Usuario)));
    QVERIFY(QTest::qWaitFor([&] { return transiciones.count() == 1; }));
    auto t = transiciones.at(0).at(0).value<TransicionNotificable>();
    QCOMPARE(t.tipo, TipoTransicionNotificable::Terminada);
    QCOMPARE(t.solicitudId, sid);
    QCOMPARE(t.paquetes, 2);
    // Repetir la verificacion de una Terminada no publica otra.
    QVERIFY(esperar(p.ejecutor->verificar(sid, OrigenLog::Usuario)));
    QVERIFY(p.e.drenar());
    QCOMPARE(transiciones.count(), 1);

    // Descargas: ninguna por paquete; una "Descarga completa" al final.
    QStringList pids;
    for (const PaquetePersistido& paq : p.e.almacen.paquetes) {
        if (paq.solicitudMasivaId == sid) {
            pids.append(paq.id);
        }
    }
    QCOMPARE(pids.size(), 2);
    QVERIFY(esperar(p.ejecutor->descargar(pids.at(0), OrigenLog::Usuario)));
    QVERIFY(p.e.drenar());
    QCOMPARE(transiciones.count(), 1);
    QVERIFY(esperar(p.ejecutor->descargar(pids.at(1), OrigenLog::Usuario)));
    QVERIFY(QTest::qWaitFor([&] { return transiciones.count() == 2; }));
    t = transiciones.at(1).at(0).value<TransicionNotificable>();
    QCOMPARE(t.tipo, TipoTransicionNotificable::DescargaCompleta);
    QCOMPARE(t.descargados, 2);
    QCOMPARE(t.paquetes, 2);
    QCOMPARE(p.gw.autenticaciones(), 1); // una sesion para todo el flujo

    // Vencida desde EnProceso.
    const SolicitudId otra = p.e.sembrar(EstadoLocal::Enviada, EstadoSolicitudSat::EnProceso).id;
    p.gw.programar(Op::Verificar, Paso::conVerificacion(verif("5000", 6)));
    QVERIFY(esperar(p.ejecutor->verificar(otra, OrigenLog::Usuario)));
    QVERIFY(QTest::qWaitFor([&] { return transiciones.count() == 3; }));
    QCOMPARE(transiciones.at(2).at(0).value<TransicionNotificable>().tipo, TipoTransicionNotificable::Vencida);
}

namespace {

const QString kRfcHostil = QStringLiteral("XAXX010101000");
const QString kBase64Largo = QStringLiteral(
    "UEsDBBQAAAAIAAAAIQBkYXRvc2RlbHBhcXVldGVkZWxzYXRwYXJhcHJ1ZWJhc2RlZW5tYXNjYXJhZG8xMjM0NTY3ODk=");
const QString kTokenJwt = QStringLiteral(
    "eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiJwcnVlYmEtZGUtdG9rZW4tc2F0In0.dozjgNryP4J3jVmNHl0w5N_XgL0n3I9PlFUP0THsR8U");

ErrorSatGateway faultHostil()
{
    ErrorSatGateway e;
    e.fase = FaseSatGateway::RespuestaExplicita;
    e.estadoHttp = 500;
    e.codigoSat = QStringLiteral("a:") + kRfcHostil;
    e.diagnosticoSanitizado = QStringLiteral("Fault rfc=%1 paquete %2 auth %3").arg(kRfcHostil, kBase64Largo, kTokenJwt);
    return e;
}

bool contieneHostil(const QString& texto)
{
    return texto.contains(kRfcHostil) || texto.contains(kBase64Largo.left(40)) || texto.contains(kTokenJwt.left(30))
           || texto.contains(QStringLiteral("dozjgNryP4J3jVmNHl0w5N"));
}

// Todo lo persistido de la solicitud y sus logs, como texto.
QString persistido(const pruebasT007::Entorno& e, const SolicitudId& id)
{
    const SolicitudPersistida* s = e.solicitud(id);
    QStringList t{s->ultimoError.value_or(QString()), s->codEstatusSolicitud.value_or(QString()),
                  s->mensajeSolicitudSat.value_or(QString())};
    for (const LogPersistido& l : e.almacen.logs) {
        t << l.codigoSat.value_or(QString()) << l.mensajeSat.value_or(QString())
          << l.payloadResumenJson.value_or(QString());
    }
    return t.join(u'|');
}

} // namespace

void TestOperacionesSatProductivo::faultHostilNoLlegaAPersistencia()
{
    {
        // Adaptador: codigo sustituido y detalle saneado.
        Banco b;
        b.gw.programar(Op::Crear, Paso::conError(faultHostil()));
        const auto r = b.ops.enviar(ContextoEnvio{b.solicitud(), {}});
        QVERIFY(!r.esExito());
        QCOMPARE(r.error().codigo, std::optional<QString>(saneamiento::kCodigoNoSeguro));
        QVERIFY(!contieneHostil(r.error().diagnosticoSanitizado));
        QVERIFY(r.error().diagnosticoSanitizado.startsWith(m::envioIncierto()));
        QVERIFY(!contieneHostil(textoUltimoError(r.error())));
    }
    for (Op op : {Op::Autenticar, Op::Crear}) {
        // Ejecutor real: ni ultimo_error, ni codigos, ni logs.
        EntornoProductivo p;
        p.gw.programar(op, Paso::conError(faultHostil()));
        const SolicitudId id = p.e.sembrar(EstadoLocal::Creada).id;
        QVERIFY(pruebasT007::esperar(p.ejecutor->enviar(id)));
        QVERIFY(p.e.solicitud(id)->ultimoError);
        QVERIFY2(!contieneHostil(persistido(p.e, id)), qPrintable(persistido(p.e, id)));
    }
    {
        // Prefijo de faultcode desconocido (posible secreto corto): sustituido.
        for (const char* codigo : {"SecretoCorto1234:Client", "a:b:c", "a:", "x:InvalidSecurity"}) {
            QCOMPARE(saneamiento::codigoSeguro(QStringView(QString::fromLatin1(codigo))), saneamiento::kCodigoNoSeguro);
        }
        for (const char* codigo : {"s:Client", "soap:Server", "a:InvalidSecurity", "wsse:FailedAuthentication",
                                   "wsu:MessageExpired", "credencial:AlmacenBloqueado"}) {
            const QString c = QString::fromLatin1(codigo);
            QCOMPARE(saneamiento::codigoSeguro(QStringView(c)), c);
        }
        Banco b;
        ErrorSatGateway e = faultHostil();
        e.codigoSat = QStringLiteral("SecretoCorto1234:Client");
        e.diagnosticoSanitizado = QStringLiteral("HTTP 500; Fault");
        b.gw.programar(Op::Crear, Paso::conError(e));
        const auto r = b.ops.enviar(ContextoEnvio{b.solicitud(), {}});
        QVERIFY(!r.esExito());
        QCOMPARE(r.error().codigo, std::optional<QString>(saneamiento::kCodigoNoSeguro));
        EntornoProductivo p;
        p.gw.programar(Op::Crear, Paso::conError(e));
        const SolicitudId id = p.e.sembrar(EstadoLocal::Creada).id;
        QVERIFY(pruebasT007::esperar(p.ejecutor->enviar(id)));
        QVERIFY(!p.e.solicitud(id)->ultimoError.value_or(QString()).startsWith(
            QStringLiteral("RespuestaExplicita:SecretoCorto")));
        QVERIFY(!persistido(p.e, id).contains(QStringLiteral("RespuestaExplicita:SecretoCorto1234")));
    }
    {
        // CodEstatus hostil en una respuesta "explicita": no se persiste.
        EntornoProductivo p;
        p.gw.programar(Op::Crear, Paso::conCreacion(RespuestaCreacion{kTokenJwt, kRfcHostil, kBase64Largo}));
        const SolicitudId id = p.e.sembrar(EstadoLocal::Creada).id;
        QVERIFY(pruebasT007::esperar(p.ejecutor->enviar(id)));
        QCOMPARE(p.e.solicitud(id)->estadoLocal, EstadoLocal::EnvioIncierto);
        QVERIFY(!contieneHostil(persistido(p.e, id)));
    }
}

void TestOperacionesSatProductivo::puertoCualquieraSeSaneaEnElEjecutor()
{
    // Un puerto que devuelve texto crudo (no el adaptador productivo).
    pruebasT007::Entorno e;
    FallaOperacion cruda = FallaOperacion::de(FaseOperacion::RespuestaExplicita, kRfcHostil + QStringLiteral(" x"),
                                              QStringLiteral("detalle %1 %2 %3").arg(kRfcHostil, kBase64Largo, kTokenJwt)
                                                  + QStringLiteral(" palabra").repeated(300));
    e.sat.responderEnvio(fakes::FakeOperacionesSat::R<ResultadoEnvio>::fallo(cruda));
    const SolicitudId id = e.sembrar(EstadoLocal::Creada).id;
    QVERIFY(pruebasT007::esperar(e.ejecutor->enviar(id)));
    const QString error = e.solicitud(id)->ultimoError.value_or(QString());
    QVERIFY(error.startsWith(QStringLiteral("RespuestaExplicita:") + saneamiento::kCodigoNoSeguro));
    QVERIFY(error.size() < 400);
    QVERIFY2(!contieneHostil(persistido(e, id)), qPrintable(persistido(e, id)));

    QCOMPARE(saneamiento::codigoSeguro(u"5000"), QStringLiteral("5000"));
    QCOMPARE(saneamiento::codigoSeguro(u"a:InvalidSecurity"), QStringLiteral("a:InvalidSecurity"));
    QCOMPARE(saneamiento::codigoSeguro(u"credencial:AlmacenBloqueado"), QStringLiteral("credencial:AlmacenBloqueado"));
    QCOMPARE(saneamiento::codigoSeguro(u"300 <xml>"), saneamiento::kCodigoNoSeguro);
    QCOMPARE(saneamiento::codigoSeguro(QStringView(QString(41, u'a'))), saneamiento::kCodigoNoSeguro);
    QCOMPARE(saneamiento::textoSeguro(u"HTTP 500; Fault a:InvalidSecurity"),
             QStringLiteral("HTTP 500; Fault a:InvalidSecurity"));
}

void TestOperacionesSatProductivo::invalidacionConcurrenteConReutilizacion()
{
    Banco b;
    QVERIFY(b.ops.verificar(b.verificacion()).esExito());
    std::atomic<int> fallos{0};
    std::thread trabajador([&] {
        for (int i = 0; i < 200; ++i) {
            if (!b.ops.verificar(b.verificacion()).esExito()) {
                ++fallos;
            }
        }
    });
    for (int i = 0; i < 200; ++i) {
        b.ops.invalidarSesion(b.perfil);
        (void)b.ops.tieneSesion(b.perfil);
    }
    trabajador.join();
    QCOMPARE(fallos.load(), 0); // el token en uso sigue vivo aunque se invalide
    QCOMPARE(b.gw.maximoActivas(), 1);
    QVERIFY(b.gw.autenticaciones() >= 1 && b.gw.autenticaciones() <= 202);
    b.ops.invalidarSesion(b.perfil);
    const int antes = b.gw.autenticaciones();
    QVERIFY(b.ops.verificar(b.verificacion()).esExito());
    QCOMPARE(b.gw.autenticaciones(), antes + 1);
}

// Clave del saneado de frontera de core-infra (SaneadoRespuestaSat): codigo
// fuera de la lista documentada -> "codigo_no_reconocido" (D8: no documentado).
void TestOperacionesSatProductivo::codigoNoReconocidoEsNoDocumentado()
{
    {
        Banco b;
        b.gw.programar(Op::Crear, Paso::conCreacion(creacion("codigo_no_reconocido")));
        const auto r = b.ops.enviar(ContextoEnvio{b.solicitud(), {}});
        QVERIFY(r.esExito());
        QCOMPARE(r.valor().codEstatus, QStringLiteral("codigo_no_reconocido"));
        QCOMPARE(r.valor().mensaje, m::envioIncierto());
        QCOMPARE(politicas::destinoEnvio(r.valor()), EstadoLocal::EnvioIncierto);
    }
    {
        Banco b;
        b.gw.programar(Op::Verificar, Paso::conVerificacion(verif("codigo_no_reconocido")));
        const auto r = b.ops.verificar(b.verificacion());
        QVERIFY(!r.esExito());
        QCOMPARE(r.error().fase, FaseOperacion::RespuestaExplicita);
        QCOMPARE(r.error().codigo, std::optional<QString>(QStringLiteral("codigo_no_reconocido")));
        QVERIFY(!politicas::esFallaSuspendible(r.error())); // transitoria
        QVERIFY(r.error().diagnosticoSanitizado.startsWith(m::verificacionTransitoria()));
    }
    {
        // Con el ejecutor: Enviando -> EnvioIncierto.
        EntornoProductivo p;
        p.gw.programar(Op::Crear, Paso::conCreacion(creacion("codigo_no_reconocido")));
        const SolicitudId id = p.e.sembrar(EstadoLocal::Creada).id;
        QVERIFY(pruebasT007::esperar(p.ejecutor->enviar(id)));
        QCOMPARE(p.e.solicitud(id)->estadoLocal, EstadoLocal::EnvioIncierto);
    }
}
