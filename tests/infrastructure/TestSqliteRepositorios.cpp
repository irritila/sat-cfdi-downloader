#include "TestSqliteRepositorios.h"

#include "SqlitePruebasComun.h"

#include "domain/logs/LogSolicitud.h"
#include "domain/perfiles/PerfilSat.h"
#include "domain/solicitudes/SolicitudCanonica.h"
#include "domain/solicitudes/SolicitudPersistida.h"
#include "infrastructure/persistence/sqlite/SqliteSoporte.h"
#include "ports/repositories/ConfiguracionAppRepository.h"
#include "ports/repositories/LogSolicitudRepository.h"
#include "ports/repositories/PaqueteSolicitudRepository.h"
#include "ports/repositories/PerfilSatRepository.h"
#include "ports/repositories/SolicitudMasivaRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <QTest>

#include <memory>

using namespace satcfdi;
using namespace satcfdi::pruebas;

namespace {

// Base migrada + grafo de persistencia, cerrado al destruir.
struct Entorno {
    QTemporaryDir dir;
    std::unique_ptr<SqlitePersistencia> p;

    bool preparar()
    {
        if (!dir.isValid() || !inicializarBaseSqlite(rutaBase(dir))) {
            return false;
        }
        p = std::make_unique<SqlitePersistencia>(rutaBase(dir));
        return true;
    }
    ~Entorno()
    {
        if (p) {
            p->cerrarConexionDelHiloActual();
        }
    }
    SqliteConnectionProvider& proveedor() { return p->proveedor(); }
};

NuevoPerfilSat nuevoPerfil(const QString& rfc, const QString& nombre = QStringLiteral("Contribuyente"))
{
    NuevoPerfilSat n;
    n.id = PerfilId::generar();
    n.rfc = rfc;
    n.nombre = nombre;
    n.creadoEn = timestamp::ahoraUtc();
    n.actualizadoEn = n.creadoEn;
    return n;
}

PerfilId crearPerfil(SqlitePersistencia& p, const QString& rfc)
{
    const NuevoPerfilSat n = nuevoPerfil(rfc);
    if (!p.unidadDeTrabajo().begin() || !p.perfiles().insertar(n) || !p.unidadDeTrabajo().commit()) {
        return {};
    }
    return n.id;
}

SolicitudCanonica canonica(TipoDescarga tipo = TipoDescarga::Emitidos,
                           QStringList contrapartes = {kRfcOtro}, int dia = 1)
{
    EntradaSolicitudCanonica e;
    e.tipoDescarga = tipo;
    e.rfcPerfil = kRfcPerfil;
    e.fechaInicial = QDate(2026, 1, dia);
    e.fechaFinal = QDate(2026, 1, 31);
    e.rfcContrapartes = std::move(contrapartes);
    auto r = SolicitudCanonica::normalizar(e);
    Q_ASSERT(r.esExito());
    return r.valor();
}

SolicitudNuevaPersistida nuevaSolicitud(const PerfilId& perfil, const SolicitudCanonica& c,
                                        const QDateTime& creadaEn = timestamp::ahoraUtc())
{
    return SolicitudNuevaPersistida{SolicitudId::generar(), perfil, c, creadaEn};
}

Resultado<Exito, ErrorPersistencia> insertarEnTx(SqlitePersistencia& p,
                                                 const SolicitudNuevaPersistida& s)
{
    if (auto b = p.unidadDeTrabajo().begin(); !b) {
        return b;
    }
    auto r = p.solicitudes().insertarCreada(s);
    if (!r) {
        (void)p.unidadDeTrabajo().rollback();
        return r;
    }
    return p.unidadDeTrabajo().commit();
}

LogEntradaSaneada logSaneado(const SolicitudId& solicitud, const QDateTime& creadoEn)
{
    LogEntradaSaneada l;
    l.id = uuid::generarCanonico();
    l.solicitudMasivaId = solicitud;
    l.tipoEvento = TipoEventoLog::SolicitudCreada;
    l.origen = OrigenLog::Usuario;
    l.payloadResumenJson = QStringLiteral("{\"mensaje\":\"creada\"}");
    l.creadoEn = creadoEn;
    return l;
}

QString columnaDe(SqliteConnectionProvider& prov, const QString& col, const SolicitudId& id)
{
    return columna(prov, QStringLiteral("SELECT %1 FROM solicitud_masiva WHERE id = %2")
                             .arg(col, citar(id.texto())))
        .value(0);
}

} // namespace

void TestSqliteRepositorios::perfilInsertarYConsultar()
{
    Entorno e;
    QVERIFY(e.preparar());
    PerfilSatRepository& repo = e.p->perfiles();
    const NuevoPerfilSat n = nuevoPerfil(kRfcPerfil, QStringLiteral("Contador Ñandú & Cía"));
    QVERIFY(e.p->unidadDeTrabajo().begin());
    auto insertado = repo.insertar(n);
    QVERIFY(insertado);
    QVERIFY(e.p->unidadDeTrabajo().commit());
    QCOMPARE(insertado.valor().id, n.id);
    QVERIFY(!insertado.valor().eliminadoEn.has_value());

    auto porId = repo.obtener(n.id);
    QVERIFY(porId && porId.valor().has_value());
    QCOMPARE(porId.valor()->rfc, kRfcPerfil);
    QCOMPARE(porId.valor()->nombre, n.nombre);
    QCOMPARE(porId.valor()->creadoEn, n.creadoEn);
    QVERIFY(porId.valor()->activo);

    auto porRfc = repo.obtenerVigentePorRfc(kRfcPerfil);
    QVERIFY(porRfc && porRfc.valor().has_value());
    QCOMPARE(porRfc.valor()->id, n.id);

    auto ausente = repo.obtener(PerfilId::generar());
    QVERIFY(ausente && !ausente.valor().has_value());

    // Inactivo: visible por id/RFC pero fuera de listarActivosVisibles.
    NuevoPerfilSat inactivo = nuevoPerfil(kRfcOtro);
    inactivo.activo = false;
    QVERIFY(e.p->unidadDeTrabajo().begin());
    QVERIFY(repo.insertar(inactivo));
    QVERIFY(e.p->unidadDeTrabajo().commit());
    auto activos = repo.listarActivosVisibles();
    QVERIFY(activos);
    QCOMPARE(activos.valor().size(), 1);
    QCOMPARE(activos.valor().first().id, n.id);
    auto inactivoPorRfc = repo.obtenerVigentePorRfc(kRfcOtro);
    QVERIFY(inactivoPorRfc && inactivoPorRfc.valor().has_value());
    QVERIFY(!inactivoPorRfc.valor()->activo);

    // Eliminado: invisible.
    QCOMPARE(ejecutarSql(e.proveedor(), QStringLiteral("UPDATE perfil_sat SET eliminado_en = %1 "
                                                       "WHERE id = %2")
                                            .arg(citar(kAhora), citar(n.id.texto()))),
             QString());
    auto eliminado = repo.obtener(n.id);
    QVERIFY(eliminado && !eliminado.valor().has_value());
    QVERIFY(repo.listarActivosVisibles().valor().isEmpty());
}

void TestSqliteRepositorios::perfilRfcVigenteDuplicadoEsUnicidad()
{
    Entorno e;
    QVERIFY(e.preparar());
    QVERIFY(!crearPerfil(*e.p, kRfcPerfil).esNulo());
    QVERIFY(e.p->unidadDeTrabajo().begin());
    auto r = e.p->perfiles().insertar(nuevoPerfil(kRfcPerfil));
    QVERIFY(e.p->unidadDeTrabajo().rollback());
    QVERIFY(!r);
    QCOMPARE(r.error().tipo, ErrorPersistencia::Tipo::Unicidad);
    QCOMPARE(r.error().restriccion, QStringLiteral("ux_perfil_sat_rfc_vigente"));
    QVERIFY(r.error().codigoNativo.has_value());
    QVERIFY(!r.error().mensaje.contains(kRfcPerfil));
    QVERIFY(!r.error().mensaje.contains(QStringLiteral("INSERT")));
}

void TestSqliteRepositorios::perfilCheckEsIntegridad()
{
    Entorno e;
    QVERIFY(e.preparar());
    QVERIFY(e.p->unidadDeTrabajo().begin());
    auto nombreVacio = e.p->perfiles().insertar(nuevoPerfil(kRfcPerfil, QStringLiteral("   ")));
    auto rfcInvalido = e.p->perfiles().insertar(nuevoPerfil(QStringLiteral("aaa010101aaa")));
    QVERIFY(e.p->unidadDeTrabajo().rollback());
    QVERIFY(!nombreVacio);
    QCOMPARE(nombreVacio.error().tipo, ErrorPersistencia::Tipo::Integridad);
    QVERIFY(!rfcInvalido);
    QCOMPARE(rfcInvalido.error().tipo, ErrorPersistencia::Tipo::Integridad);
    QVERIFY(!rfcInvalido.error().mensaje.contains(QStringLiteral("aaa010101aaa")));
}

void TestSqliteRepositorios::solicitudCreadaConNulos()
{
    Entorno e;
    QVERIFY(e.preparar());
    const PerfilId perfil = crearPerfil(*e.p, kRfcPerfil);
    QVERIFY(!perfil.esNulo());
    const SolicitudCanonica c = canonica();
    const SolicitudNuevaPersistida s = nuevaSolicitud(perfil, c);
    QVERIFY(insertarEnTx(*e.p, s));

    // Columnas SAT y de seguimiento en NULL o DEFAULT.
    SqliteConnectionProvider& prov = e.proveedor();
    for (const char* col : {"id_solicitud_sat", "cod_estatus_solicitud", "mensaje_solicitud_sat",
                            "estado_solicitud_sat", "codigo_estado_solicitud",
                            "mensaje_verificacion_sat", "numero_cfdi", "envio_iniciado_en",
                            "enviada_en", "ultima_verificacion_en", "siguiente_verificacion_en",
                            "ultimo_error", "accion_pendiente_en", "eliminado_en", "rfc_receptor",
                            "tipo_comprobante", "complemento"}) {
        QCOMPARE(columnaDe(prov, QString::fromLatin1(col), s.id), QStringLiteral("<NULL>"));
    }
    QCOMPARE(columnaDe(prov, QStringLiteral("estado_local"), s.id), QStringLiteral("Creada"));
    QCOMPARE(columnaDe(prov, QStringLiteral("verificaciones_sin_cambio"), s.id), QStringLiteral("0"));
    QCOMPARE(columnaDe(prov, QStringLiteral("verificacion_pendiente"), s.id), QStringLiteral("0"));
    QCOMPARE(columnaDe(prov, QStringLiteral("descarga_pendiente"), s.id), QStringLiteral("0"));
    QCOMPARE(columnaDe(prov, QStringLiteral("rfc_solicitante"), s.id), kRfcPerfil);
    QCOMPARE(columnaDe(prov, QStringLiteral("dedup_key"), s.id), c.dedupKey().texto());
    QCOMPARE(columnaDe(prov, QStringLiteral("rfc_receptores_json"), s.id),
             QStringLiteral("[\"%1\"]").arg(kRfcOtro));
    QCOMPARE(columnaDe(prov, QStringLiteral("creada_en"), s.id), timestamp::aTexto(s.creadaEn));
    QVERIFY(columnaDe(prov, QStringLiteral("creada_en"), s.id).endsWith(QLatin1Char('Z')));

    // Ida y vuelta por el puerto.
    auto leida = e.p->solicitudes().obtenerVisible(s.id);
    QVERIFY(leida && leida.valor().has_value());
    const SolicitudPersistida& f = *leida.valor();
    QCOMPARE(f.id, s.id);
    QCOMPARE(f.perfilSatId, perfil);
    QCOMPARE(f.tipoCfdi, TipoDescarga::Emitidos);
    QCOMPARE(f.operacionSat, OperacionSat::SolicitaDescargaEmitidos);
    QCOMPARE(f.rfcEmisor, std::optional<QString>(kRfcPerfil));
    QVERIFY(!f.rfcReceptor.has_value());
    QCOMPARE(f.rfcReceptores, QStringList{kRfcOtro});
    QCOMPARE(f.fechaInicialSat, c.fechaInicialSat());
    QCOMPARE(f.fechaFinalSat, c.fechaFinalSat());
    QCOMPARE(f.dedupKey, c.dedupKey());
    QCOMPARE(f.estadoLocal, EstadoLocal::Creada);
    QVERIFY(!f.estadoSolicitudSat.has_value());
    QVERIFY(!f.idSolicitudSat.has_value());
    QCOMPARE(f.creadaEn, s.creadaEn);
    QVERIFY(!f.eliminadoEn.has_value());

    // Recibidos: contraparte en rfc_emisor, receptores NULL.
    const SolicitudCanonica cr = canonica(TipoDescarga::Recibidos, {kRfcOtro});
    const SolicitudNuevaPersistida sr = nuevaSolicitud(perfil, cr);
    QVERIFY(insertarEnTx(*e.p, sr));
    auto rec = e.p->solicitudes().obtenerVisible(sr.id);
    QVERIFY(rec && rec.valor().has_value());
    QCOMPARE(rec.valor()->rfcEmisor, std::optional<QString>(kRfcOtro));
    QCOMPARE(rec.valor()->rfcReceptor, std::optional<QString>(kRfcPerfil));
    QVERIFY(rec.valor()->rfcReceptores.isEmpty());
    QCOMPARE(columnaDe(prov, QStringLiteral("rfc_receptores_json"), sr.id), QStringLiteral("<NULL>"));
}

void TestSqliteRepositorios::solicitudListaOrdenadaYVisible()
{
    Entorno e;
    QVERIFY(e.preparar());
    const PerfilId perfil = crearPerfil(*e.p, kRfcPerfil);
    const QDateTime base = timestamp::ahoraUtc();
    const auto antigua = nuevaSolicitud(perfil, canonica(TipoDescarga::Emitidos, {}, 1), base);
    const auto reciente =
        nuevaSolicitud(perfil, canonica(TipoDescarga::Emitidos, {}, 2), base.addSecs(60));
    const auto eliminada =
        nuevaSolicitud(perfil, canonica(TipoDescarga::Emitidos, {}, 3), base.addSecs(120));
    QVERIFY(insertarEnTx(*e.p, antigua));
    QVERIFY(insertarEnTx(*e.p, reciente));
    QVERIFY(insertarEnTx(*e.p, eliminada));
    QVERIFY(e.p->unidadDeTrabajo().begin());
    QVERIFY(e.p->solicitudes().marcarEliminadaVisible(eliminada.id, base.addSecs(180)).valor());
    QVERIFY(e.p->unidadDeTrabajo().commit());

    auto lista = e.p->solicitudes().listarVisibles();
    QVERIFY(lista);
    QCOMPARE(lista.valor().size(), 2);
    QCOMPARE(lista.valor().at(0).id, reciente.id);
    QCOMPARE(lista.valor().at(1).id, antigua.id);
}

void TestSqliteRepositorios::solicitudDedupBloqueanteTraducido()
{
    Entorno e;
    QVERIFY(e.preparar());
    const PerfilId perfil = crearPerfil(*e.p, kRfcPerfil);
    const SolicitudCanonica c = canonica();
    QVERIFY(insertarEnTx(*e.p, nuevaSolicitud(perfil, c)));
    auto r = insertarEnTx(*e.p, nuevaSolicitud(perfil, c));
    QVERIFY(!r);
    QCOMPARE(r.error().tipo, ErrorPersistencia::Tipo::DedupBloqueado);
    QCOMPARE(r.error().restriccion, QStringLiteral("ux_solicitud_masiva_dedup_bloqueante"));
    QVERIFY(!r.error().mensaje.contains(c.dedupKey().texto()));
    QVERIFY(!e.proveedor().transaccionActiva());
    QCOMPARE(e.p->solicitudes().listarVisibles().valor().size(), 1);
}

void TestSqliteRepositorios::solicitudPerfilInexistenteEsIntegridadFk()
{
    Entorno e;
    QVERIFY(e.preparar());
    auto r = insertarEnTx(*e.p, nuevaSolicitud(PerfilId::generar(), canonica()));
    QVERIFY(!r);
    QCOMPARE(r.error().tipo, ErrorPersistencia::Tipo::Integridad);
    QCOMPARE(r.error().restriccion, QStringLiteral("foreign_key"));
    QVERIFY(e.p->solicitudes().listarVisibles().valor().isEmpty());
}

void TestSqliteRepositorios::solicitudIdDuplicadoEsUnicidad()
{
    Entorno e;
    QVERIFY(e.preparar());
    const PerfilId perfil = crearPerfil(*e.p, kRfcPerfil);
    const auto primera = nuevaSolicitud(perfil, canonica(TipoDescarga::Emitidos, {}, 1));
    QVERIFY(insertarEnTx(*e.p, primera));
    auto segunda = nuevaSolicitud(perfil, canonica(TipoDescarga::Emitidos, {}, 2));
    segunda.id = primera.id;
    auto r = insertarEnTx(*e.p, segunda);
    QVERIFY(!r);
    QCOMPARE(r.error().tipo, ErrorPersistencia::Tipo::Unicidad);
    QCOMPARE(r.error().restriccion, QStringLiteral("pk_solicitud_masiva"));
}

void TestSqliteRepositorios::restriccionesSinApiDeEscrituraTraducidas()
{
    // credencial unica, paquete unico y configuracion unica no tienen API de
    // escritura en T003: se verifica la traduccion del mismo camino SQL que
    // usan los repositorios.
    Entorno e;
    QVERIFY(e.preparar());
    const PerfilId perfil = crearPerfil(*e.p, kRfcPerfil);
    SqliteConnectionProvider& prov = e.proveedor();
    auto conexion = prov.conexion();
    QVERIFY(conexion);
    QSqlDatabase db = conexion.valor();

    auto ejecutarTraducido = [&](const QString& sql) {
        QSqlQuery q(db);
        return sqlite::ejecutarDirecto(q, sql, u"prueba.restriccion");
    };

    const QString credencial = QStringLiteral(
        "INSERT INTO credencial_sat (id, perfil_sat_id, certificado_ref, llave_privada_ref, "
        "contrasena_ref, registrada_en, actualizada_en) "
        "VALUES (%1, %2, 'cert-ref', 'key-ref', 'pwd-ref', %3, %3)");
    QVERIFY(ejecutarTraducido(credencial.arg(citar(uuid::generarCanonico()), citar(perfil.texto()),
                                             citar(kAhora))));
    auto credDuplicada = ejecutarTraducido(
        credencial.arg(citar(uuid::generarCanonico()), citar(perfil.texto()), citar(kAhora)));
    QVERIFY(!credDuplicada);
    QCOMPARE(credDuplicada.error().tipo, ErrorPersistencia::Tipo::Unicidad);
    QCOMPARE(credDuplicada.error().restriccion, QStringLiteral("ux_credencial_sat_perfil"));

    const SolicitudFixture s = fixture(perfil.texto(), DedupKey::calcularV1("paquete-unico"),
                                       QStringLiteral("Enviada"), QStringLiteral("Terminada"));
    QCOMPARE(insertarSolicitudFixture(prov, s), QString());
    QCOMPARE(insertarPaqueteFixture(prov, uuid::generarCanonico(), s.id, QStringLiteral("P1"),
                                    QStringLiteral("Disponible")),
             QString());
    auto paqueteDuplicado = ejecutarTraducido(
        QStringLiteral("INSERT INTO paquete_solicitud (id, solicitud_masiva_id, id_paquete_sat, "
                       "disponible_en) VALUES (%1, %2, 'P1', %3)")
            .arg(citar(uuid::generarCanonico()), citar(s.id), citar(kAhora)));
    QVERIFY(!paqueteDuplicado);
    QCOMPARE(paqueteDuplicado.error().tipo, ErrorPersistencia::Tipo::Unicidad);
    QCOMPARE(paqueteDuplicado.error().restriccion,
             QStringLiteral("ux_paquete_solicitud_id_paquete_sat"));

    auto configuracionDuplicada = ejecutarTraducido(QStringLiteral(
        "INSERT INTO configuracion_app (id, actualizada_en) VALUES (1, '2026-01-01T00:00:00.000Z')"));
    QVERIFY(!configuracionDuplicada);
    QCOMPARE(configuracionDuplicada.error().tipo, ErrorPersistencia::Tipo::Unicidad);
    QCOMPARE(configuracionDuplicada.error().restriccion, QStringLiteral("pk_configuracion_app"));
    auto configuracionSegunda = ejecutarTraducido(QStringLiteral(
        "INSERT INTO configuracion_app (id, actualizada_en) VALUES (2, '2026-01-01T00:00:00.000Z')"));
    QVERIFY(!configuracionSegunda);
    QCOMPARE(configuracionSegunda.error().tipo, ErrorPersistencia::Tipo::Integridad);

    // CHECK con nombre conserva el nombre de la restriccion.
    auto checkNombrado = ejecutarTraducido(
        QStringLiteral("UPDATE paquete_solicitud SET estado_descarga = 'Descargado' WHERE id_paquete_sat = 'P1'"));
    QVERIFY(!checkNombrado);
    QCOMPARE(checkNombrado.error().tipo, ErrorPersistencia::Tipo::Integridad);
    QCOMPARE(checkNombrado.error().restriccion, QStringLiteral("ck_paquete_descargado"));

    // NOT NULL.
    auto notNull = ejecutarTraducido(QStringLiteral(
        "INSERT INTO perfil_sat (id, rfc, nombre, activo, creado_en, actualizado_en) VALUES "
        "('00000000-0000-4000-8000-000000000000', 'CCC030303CCC', 'x', 1, NULL, 'x')"));
    QVERIFY(!notNull);
    QCOMPARE(notNull.error().tipo, ErrorPersistencia::Tipo::Integridad);
    QCOMPARE(notNull.error().restriccion, QStringLiteral("not_null:perfil_sat.creado_en"));

    // FK en paquete.
    auto fk = ejecutarTraducido(
        QStringLiteral("INSERT INTO paquete_solicitud (id, solicitud_masiva_id, id_paquete_sat, "
                       "disponible_en) VALUES (%1, %2, 'P9', %3)")
            .arg(citar(uuid::generarCanonico()), citar(uuid::generarCanonico()), citar(kAhora)));
    QVERIFY(!fk);
    QCOMPARE(fk.error().tipo, ErrorPersistencia::Tipo::Integridad);
    QCOMPARE(fk.error().restriccion, QStringLiteral("foreign_key"));
}

void TestSqliteRepositorios::matrizDuplicados_data()
{
    QTest::addColumn<QString>("estadoLocal");
    QTest::addColumn<QString>("estadoSat");
    QTest::addColumn<bool>("eliminada");
    // "Estado" o "Estado!" (paquete eliminado).
    QTest::addColumn<QStringList>("paquetes");
    QTest::addColumn<int>("clasificacion");
    QTest::addColumn<int>("motivo");

    using C = ClasificacionDuplicado;
    using M = MotivoDuplicado;
    const auto fila = [](const char* nombre, const char* local, const char* sat, bool eliminada,
                         QStringList paquetes, C c, M m) {
        QTest::newRow(nombre) << QString::fromLatin1(local) << QString::fromLatin1(sat) << eliminada
                              << paquetes << static_cast<int>(c) << static_cast<int>(m);
    };
    const QString disp = QStringLiteral("Disponible");
    const QString desc = QStringLiteral("Descargado");
    const QString venc = QStringLiteral("Vencido");

    fila("Creada", "Creada", "", false, {}, C::Bloqueado, M::SolicitudEnCurso);
    fila("Enviando", "Enviando", "", false, {}, C::Bloqueado, M::SolicitudEnCurso);
    fila("Enviada sin estado SAT", "Enviada", "", false, {}, C::Bloqueado, M::SolicitudEnCurso);
    fila("Aceptada", "Enviada", "Aceptada", false, {}, C::Bloqueado, M::SolicitudEnCurso);
    fila("EnProceso", "Enviada", "EnProceso", false, {}, C::Bloqueado, M::SolicitudEnCurso);
    fila("Terminada Disponible", "Enviada", "Terminada", false, {desc, disp}, C::Bloqueado,
         M::TerminadaConPaquetesPendientes);
    fila("Terminada Descargando", "Enviada", "Terminada", false, {QStringLiteral("Descargando")},
         C::Bloqueado, M::TerminadaConPaquetesPendientes);
    fila("Terminada Error", "Enviada", "Terminada", false, {venc, QStringLiteral("Error")},
         C::Bloqueado, M::TerminadaConPaquetesPendientes);
    fila("Terminada todo Descargado", "Enviada", "Terminada", false, {desc, desc}, C::Bloqueado,
         M::TerminadaDescargada);
    fila("Terminada Descargado y Disponible eliminado", "Enviada", "Terminada", false,
         {desc, disp + QLatin1Char('!')}, C::Bloqueado, M::TerminadaDescargada);
    fila("Terminada Vencido y Descargado", "Enviada", "Terminada", false, {venc, desc},
         C::RequiereConfirmacion, M::TerminadaConPaquetesVencidos);
    fila("Terminada solo Vencido", "Enviada", "Terminada", false, {venc}, C::RequiereConfirmacion,
         M::TerminadaConPaquetesVencidos);
    fila("Terminada sin paquetes", "Enviada", "Terminada", false, {}, C::RequiereConfirmacion,
         M::TerminadaSinPaquetes);
    fila("Terminada solo paquetes eliminados", "Enviada", "Terminada", false,
         {disp + QLatin1Char('!')}, C::RequiereConfirmacion, M::TerminadaSinPaquetes);
    fila("EnvioIncierto", "EnvioIncierto", "", false, {}, C::RequiereConfirmacion, M::EnvioIncierto);
    fila("EnvioFallido", "EnvioFallido", "", false, {}, C::RequiereConfirmacion,
         M::SolicitudSinExito);
    fila("Error SAT", "Enviada", "Error", false, {}, C::RequiereConfirmacion, M::SolicitudSinExito);
    fila("Rechazada", "Enviada", "Rechazada", false, {}, C::RequiereConfirmacion,
         M::SolicitudSinExito);
    fila("Vencida", "Enviada", "Vencida", false, {}, C::RequiereConfirmacion, M::SolicitudSinExito);
    fila("Eliminada Creada", "Creada", "", true, {}, C::RequiereConfirmacion,
         M::SolicitudEliminada);
    fila("Eliminada Terminada con Disponible", "Enviada", "Terminada", true, {disp},
         C::RequiereConfirmacion, M::SolicitudEliminada);
}

void TestSqliteRepositorios::matrizDuplicados()
{
    QFETCH(QString, estadoLocal);
    QFETCH(QString, estadoSat);
    QFETCH(bool, eliminada);
    QFETCH(QStringList, paquetes);
    QFETCH(int, clasificacion);
    QFETCH(int, motivo);

    Entorno e;
    QVERIFY(e.preparar());
    SqliteConnectionProvider& prov = e.proveedor();
    const QString perfil = uuid::generarCanonico();
    QCOMPARE(insertarPerfilFixture(prov, perfil, kRfcPerfil), QString());
    const DedupKey clave = DedupKey::calcularV1(QTest::currentDataTag());
    const SolicitudFixture s = fixture(perfil, clave, estadoLocal, estadoSat, eliminada);
    QCOMPARE(insertarSolicitudFixture(prov, s), QString());
    int n = 0;
    for (QString estado : paquetes) {
        const bool paqueteEliminado = estado.endsWith(QLatin1Char('!'));
        if (paqueteEliminado) {
            estado.chop(1);
        }
        QCOMPARE(insertarPaqueteFixture(prov, uuid::generarCanonico(), s.id,
                                        QStringLiteral("PKG-%1").arg(++n), estado, paqueteEliminado),
                 QString());
    }

    // Fuera de transaccion (evaluarDuplicado previo a UI) ...
    auto r = e.p->solicitudes().clasificarDuplicado(clave);
    QVERIFY(r);
    QCOMPARE(static_cast<int>(r.valor().clasificacion), clasificacion);
    QCOMPARE(static_cast<int>(r.valor().motivo), motivo);
    QCOMPARE(r.valor().dedupKey, clave);
    QCOMPARE(r.valor().solicitudReferencia, SolicitudId::desdeTexto(s.id));

    // ... y dentro de BEGIN IMMEDIATE da lo mismo.
    QVERIFY(e.p->unidadDeTrabajo().begin());
    auto enTx = e.p->solicitudes().clasificarDuplicado(clave);
    QVERIFY(e.p->unidadDeTrabajo().rollback());
    QVERIFY(enTx);
    QCOMPARE(static_cast<int>(enTx.valor().clasificacion), clasificacion);

    // Otra clave: Libre.
    auto libre = e.p->solicitudes().clasificarDuplicado(DedupKey::calcularV1("otra"));
    QVERIFY(libre);
    QCOMPARE(libre.valor().clasificacion, ClasificacionDuplicado::Libre);
    QCOMPARE(libre.valor().motivo, MotivoDuplicado::SinCoincidencias);
    QVERIFY(!libre.valor().solicitudReferencia.has_value());
}

void TestSqliteRepositorios::duplicadosPrecedenciaYReferencia()
{
    Entorno e;
    QVERIFY(e.preparar());
    SqliteConnectionProvider& prov = e.proveedor();
    const QString perfil = uuid::generarCanonico();
    QCOMPARE(insertarPerfilFixture(prov, perfil, kRfcPerfil), QString());
    const DedupKey clave = DedupKey::calcularV1("precedencia");

    SolicitudFixture eliminada = fixture(perfil, clave, QStringLiteral("Creada"), {}, true);
    eliminada.creadaEn = QStringLiteral("2026-10-03T10:00:00.000Z");
    SolicitudFixture incierta = fixture(perfil, clave, QStringLiteral("EnvioIncierto"));
    incierta.creadaEn = QStringLiteral("2026-10-03T11:00:00.000Z");
    QCOMPARE(insertarSolicitudFixture(prov, eliminada), QString());
    QCOMPARE(insertarSolicitudFixture(prov, incierta), QString());

    // Solo RequiereConfirmacion: la referencia es la mas reciente (creada_en DESC).
    auto r = e.p->solicitudes().clasificarDuplicado(clave);
    QVERIFY(r);
    QCOMPARE(r.valor().clasificacion, ClasificacionDuplicado::RequiereConfirmacion);
    QCOMPARE(r.valor().motivo, MotivoDuplicado::EnvioIncierto);
    QCOMPARE(r.valor().solicitudReferencia, SolicitudId::desdeTexto(incierta.id));

    // Una bloqueante mas antigua gana por precedencia.
    SolicitudFixture terminada =
        fixture(perfil, clave, QStringLiteral("Enviada"), QStringLiteral("Terminada"));
    terminada.creadaEn = QStringLiteral("2026-10-03T09:00:00.000Z");
    QCOMPARE(insertarSolicitudFixture(prov, terminada), QString());
    QCOMPARE(insertarPaqueteFixture(prov, uuid::generarCanonico(), terminada.id,
                                    QStringLiteral("P1"), QStringLiteral("Disponible")),
             QString());
    r = e.p->solicitudes().clasificarDuplicado(clave);
    QVERIFY(r);
    QCOMPARE(r.valor().clasificacion, ClasificacionDuplicado::Bloqueado);
    QCOMPARE(r.valor().motivo, MotivoDuplicado::TerminadaConPaquetesPendientes);
    QCOMPARE(r.valor().solicitudReferencia, SolicitudId::desdeTexto(terminada.id));
}

void TestSqliteRepositorios::paquetesYLogsPorSolicitud()
{
    Entorno e;
    QVERIFY(e.preparar());
    SqliteConnectionProvider& prov = e.proveedor();
    const PerfilId perfil = crearPerfil(*e.p, kRfcPerfil);
    const SolicitudFixture s = fixture(perfil.texto(), DedupKey::calcularV1("detalle"),
                                       QStringLiteral("Enviada"), QStringLiteral("Terminada"));
    QCOMPARE(insertarSolicitudFixture(prov, s), QString());
    const SolicitudId id = *SolicitudId::desdeTexto(s.id);
    QCOMPARE(insertarPaqueteFixture(prov, uuid::generarCanonico(), s.id, QStringLiteral("B"),
                                    QStringLiteral("Vencido")),
             QString());
    QCOMPARE(insertarPaqueteFixture(prov, uuid::generarCanonico(), s.id, QStringLiteral("A"),
                                    QStringLiteral("Descargado")),
             QString());
    QCOMPARE(insertarPaqueteFixture(prov, uuid::generarCanonico(), s.id, QStringLiteral("C"),
                                    QStringLiteral("Disponible"), true),
             QString());

    auto paquetes = e.p->paquetes().listarVisiblesPorSolicitud(id);
    QVERIFY(paquetes);
    QCOMPARE(paquetes.valor().size(), 2);
    QCOMPARE(paquetes.valor().at(0).idPaqueteSat, QStringLiteral("A"));
    QCOMPARE(paquetes.valor().at(0).estadoDescarga, EstadoDescarga::Descargado);
    QCOMPARE(paquetes.valor().at(0).rutaLocal, std::optional<QString>(QStringLiteral("paquetes/p.zip")));
    QVERIFY(paquetes.valor().at(0).descargadoEn.has_value());
    QCOMPARE(paquetes.valor().at(1).estadoDescarga, EstadoDescarga::Vencido);
    QCOMPARE(paquetes.valor().at(1).motivoVencimiento,
             std::optional<MotivoVencimiento>(MotivoVencimiento::PaqueteExpirado));
    QCOMPARE(paquetes.valor().at(1).origenVencimiento,
             std::optional<OrigenVencimiento>(OrigenVencimiento::Sat));

    auto conteo = e.p->paquetes().contarVisiblesPorSolicitud();
    QVERIFY(conteo);
    QCOMPARE(conteo.valor().size(), 1);
    QCOMPARE(conteo.valor().value(id), 2);

    // Logs: agregar en transaccion y listar en orden.
    const QDateTime t0 = timestamp::ahoraUtc();
    LogEntradaSaneada segundo = logSaneado(id, t0.addSecs(5));
    segundo.tipoEvento = TipoEventoLog::VerificacionRealizada;
    segundo.origen = OrigenLog::Worker;
    segundo.origenCodigoSat = OrigenCodigoSat::Verificacion;
    segundo.codigoSat = QStringLiteral("5000");
    segundo.mensajeSat = QStringLiteral("Solicitud Aceptada");
    segundo.payloadResumenJson.reset();
    QVERIFY(e.p->unidadDeTrabajo().begin());
    QVERIFY(e.p->logs().agregar(segundo));
    QVERIFY(e.p->logs().agregar(logSaneado(id, t0)));
    QVERIFY(e.p->unidadDeTrabajo().commit());
    auto logs = e.p->logs().listarVisiblesPorSolicitud(id);
    QVERIFY(logs);
    QCOMPARE(logs.valor().size(), 2);
    QCOMPARE(logs.valor().at(0).tipoEvento, TipoEventoLog::SolicitudCreada);
    QCOMPARE(logs.valor().at(0).payloadResumenJson,
             std::optional<QString>(QStringLiteral("{\"mensaje\":\"creada\"}")));
    QCOMPARE(logs.valor().at(1).id, segundo.id);
    QCOMPARE(logs.valor().at(1).origen, OrigenLog::Worker);
    QCOMPARE(logs.valor().at(1).origenCodigoSat,
             std::optional<OrigenCodigoSat>(OrigenCodigoSat::Verificacion));
    QCOMPARE(logs.valor().at(1).codigoSat, std::optional<QString>(QStringLiteral("5000")));
    QCOMPARE(logs.valor().at(1).creadoEn, segundo.creadoEn);
    QVERIFY(!logs.valor().at(1).payloadResumenJson.has_value());
    QVERIFY(!logs.valor().at(1).eliminadoEn.has_value());

    // Sin paquetes ni logs: listas vacias.
    QVERIFY(e.p->paquetes().listarVisiblesPorSolicitud(SolicitudId::generar()).valor().isEmpty());
    QVERIFY(e.p->logs().listarVisiblesPorSolicitud(SolicitudId::generar()).valor().isEmpty());
}

void TestSqliteRepositorios::logRestriccionesTraducidas()
{
    Entorno e;
    QVERIFY(e.preparar());
    const PerfilId perfil = crearPerfil(*e.p, kRfcPerfil);
    const auto s = nuevaSolicitud(perfil, canonica());
    QVERIFY(insertarEnTx(*e.p, s));

    QVERIFY(e.p->unidadDeTrabajo().begin());
    LogEntradaSaneada noObjeto = logSaneado(s.id, timestamp::ahoraUtc());
    noObjeto.payloadResumenJson = QStringLiteral("[1,2]");
    auto r1 = e.p->logs().agregar(noObjeto);
    LogEntradaSaneada sinOrigen = logSaneado(s.id, timestamp::ahoraUtc());
    sinOrigen.codigoSat = QStringLiteral("5000");
    auto r2 = e.p->logs().agregar(sinOrigen);
    auto r3 = e.p->logs().agregar(logSaneado(SolicitudId::generar(), timestamp::ahoraUtc()));
    const LogEntradaSaneada ok = logSaneado(s.id, timestamp::ahoraUtc());
    QVERIFY(e.p->logs().agregar(ok));
    auto r4 = e.p->logs().agregar(ok);
    QVERIFY(e.p->unidadDeTrabajo().rollback());

    QVERIFY(!r1);
    QCOMPARE(r1.error().tipo, ErrorPersistencia::Tipo::Integridad);
    QVERIFY(!r2);
    QCOMPARE(r2.error().tipo, ErrorPersistencia::Tipo::Integridad);
    QCOMPARE(r2.error().restriccion, QStringLiteral("ck_log_codigo_sat"));
    QVERIFY(!r3);
    QCOMPARE(r3.error().tipo, ErrorPersistencia::Tipo::Integridad);
    QCOMPARE(r3.error().restriccion, QStringLiteral("foreign_key"));
    QVERIFY(!r4);
    QCOMPARE(r4.error().tipo, ErrorPersistencia::Tipo::Unicidad);
    QCOMPARE(r4.error().restriccion, QStringLiteral("pk_log_solicitud"));
}

void TestSqliteRepositorios::configuracionUnica()
{
    Entorno e;
    QVERIFY(e.preparar());
    auto c = e.p->configuracion().obtener();
    QVERIFY(c);
    QVERIFY(!c.valor().inicioAutomaticoHabilitado);
    QVERIFY(!c.valor().monitoreoPausado);
    QVERIFY(!c.valor().ultimoCierreEn.has_value());
    QVERIFY(c.valor().actualizadaEn.isValid());

    QCOMPARE(ejecutarSql(e.proveedor(), QStringLiteral("DELETE FROM configuracion_app")), QString());
    auto sinFila = e.p->configuracion().obtener();
    QVERIFY(!sinFila);
    QCOMPARE(sinFila.error().tipo, ErrorPersistencia::Tipo::NoEncontrado);
}

void TestSqliteRepositorios::eliminacionLogicaAtomicaEIdempotente()
{
    Entorno e;
    QVERIFY(e.preparar());
    SqliteConnectionProvider& prov = e.proveedor();
    const PerfilId perfil = crearPerfil(*e.p, kRfcPerfil);
    const auto s = nuevaSolicitud(perfil, canonica());
    QVERIFY(insertarEnTx(*e.p, s));
    QCOMPARE(insertarPaqueteFixture(prov, uuid::generarCanonico(), s.id.texto(),
                                    QStringLiteral("P1"), QStringLiteral("Disponible")),
             QString());
    QCOMPARE(insertarPaqueteFixture(prov, uuid::generarCanonico(), s.id.texto(),
                                    QStringLiteral("P2"), QStringLiteral("Error")),
             QString());
    QVERIFY(e.p->unidadDeTrabajo().begin());
    QVERIFY(e.p->logs().agregar(logSaneado(s.id, timestamp::ahoraUtc())));
    QVERIFY(e.p->logs().agregar(logSaneado(s.id, timestamp::ahoraUtc())));
    QVERIFY(e.p->unidadDeTrabajo().commit());

    const QDateTime cuando = timestamp::ahoraUtc();
    QVERIFY(e.p->unidadDeTrabajo().begin());
    auto sol = e.p->solicitudes().marcarEliminadaVisible(s.id, cuando);
    auto paq = e.p->paquetes().marcarEliminadosPorSolicitud(s.id, cuando);
    auto lg = e.p->logs().marcarEliminadosPorSolicitud(s.id, cuando);
    QVERIFY(e.p->unidadDeTrabajo().commit());
    QVERIFY(sol && sol.valor());
    QCOMPARE(paq.valor(), 2);
    QCOMPARE(lg.valor(), 2);

    // Mismo timestamp en las tres tablas; sin borrado fisico.
    const QString t = timestamp::aTexto(cuando);
    const QString id = citar(s.id.texto());
    QCOMPARE(columna(prov, QStringLiteral("SELECT DISTINCT eliminado_en FROM solicitud_masiva WHERE id = %1").arg(id)),
             QStringList{t});
    QCOMPARE(columna(prov, QStringLiteral("SELECT DISTINCT eliminado_en FROM paquete_solicitud "
                                          "WHERE solicitud_masiva_id = %1").arg(id)),
             QStringList{t});
    QCOMPARE(columna(prov, QStringLiteral("SELECT DISTINCT eliminado_en FROM log_solicitud "
                                          "WHERE solicitud_masiva_id = %1").arg(id)),
             QStringList{t});
    QCOMPARE(escalar(prov, QStringLiteral("SELECT count(*) FROM paquete_solicitud")).toInt(), 2);
    QCOMPARE(escalar(prov, QStringLiteral("SELECT count(*) FROM log_solicitud")).toInt(), 2);

    // Invisible en consultas normales.
    QVERIFY(!e.p->solicitudes().obtenerVisible(s.id).valor().has_value());
    QVERIFY(e.p->solicitudes().listarVisibles().valor().isEmpty());
    QVERIFY(e.p->paquetes().listarVisiblesPorSolicitud(s.id).valor().isEmpty());
    QVERIFY(e.p->logs().listarVisiblesPorSolicitud(s.id).valor().isEmpty());
    QVERIFY(e.p->paquetes().contarVisiblesPorSolicitud().valor().isEmpty());

    // Idempotente: segunda eliminacion y id inexistente no cambian nada.
    QVERIFY(e.p->unidadDeTrabajo().begin());
    QCOMPARE(e.p->solicitudes().marcarEliminadaVisible(s.id, cuando.addSecs(10)).valor(), false);
    QCOMPARE(e.p->paquetes().marcarEliminadosPorSolicitud(s.id, cuando.addSecs(10)).valor(), 0);
    QCOMPARE(e.p->logs().marcarEliminadosPorSolicitud(s.id, cuando.addSecs(10)).valor(), 0);
    QCOMPARE(e.p->solicitudes().marcarEliminadaVisible(SolicitudId::generar(), cuando).valor(), false);
    QVERIFY(e.p->unidadDeTrabajo().commit());
    QCOMPARE(columna(prov, QStringLiteral("SELECT eliminado_en FROM solicitud_masiva WHERE id = %1").arg(id)),
             QStringList{t});

    // Una solicitud eliminada deja la clave en RequiereConfirmacion.
    auto dup = e.p->solicitudes().clasificarDuplicado(s.canonica.dedupKey());
    QVERIFY(dup);
    QCOMPARE(dup.valor().clasificacion, ClasificacionDuplicado::RequiereConfirmacion);
    QCOMPARE(dup.valor().motivo, MotivoDuplicado::SolicitudEliminada);
}

void TestSqliteRepositorios::eliminacionRevertidaNoDejaRastro()
{
    Entorno e;
    QVERIFY(e.preparar());
    const PerfilId perfil = crearPerfil(*e.p, kRfcPerfil);
    const auto s = nuevaSolicitud(perfil, canonica());
    QVERIFY(insertarEnTx(*e.p, s));
    QCOMPARE(insertarPaqueteFixture(e.proveedor(), uuid::generarCanonico(), s.id.texto(),
                                    QStringLiteral("P1"), QStringLiteral("Disponible")),
             QString());

    QVERIFY(e.p->unidadDeTrabajo().begin());
    QVERIFY(e.p->solicitudes().marcarEliminadaVisible(s.id, timestamp::ahoraUtc()).valor());
    QCOMPARE(e.p->paquetes().marcarEliminadosPorSolicitud(s.id, timestamp::ahoraUtc()).valor(), 1);
    QVERIFY(e.p->unidadDeTrabajo().rollback());

    QVERIFY(e.p->solicitudes().obtenerVisible(s.id).valor().has_value());
    QCOMPARE(e.p->paquetes().listarVisiblesPorSolicitud(s.id).valor().size(), 1);
}

void TestSqliteRepositorios::filaIlegibleEsInterno()
{
    Entorno e;
    QVERIFY(e.preparar());
    const PerfilId perfil = crearPerfil(*e.p, kRfcPerfil);
    // Timestamp fuera del formato ADR 0016 (el esquema no lo restringe).
    QCOMPARE(ejecutarSql(e.proveedor(), QStringLiteral("UPDATE perfil_sat SET creado_en = "
                                                       "'2026-10-03 12:00:00' WHERE id = %1")
                                            .arg(citar(perfil.texto()))),
             QString());
    auto r = e.p->perfiles().obtener(perfil);
    QVERIFY(!r);
    QCOMPARE(r.error().tipo, ErrorPersistencia::Tipo::Interno);
    QVERIFY(r.error().mensaje.contains(QStringLiteral("perfil_sat.creado_en")));
    QVERIFY(!r.error().mensaje.contains(QStringLiteral("2026-10-03 12:00:00")));
}

void TestSqliteRepositorios::integridadFinal()
{
    Entorno e;
    QVERIFY(e.preparar());
    const PerfilId perfil = crearPerfil(*e.p, kRfcPerfil);
    const auto s = nuevaSolicitud(perfil, canonica());
    QVERIFY(insertarEnTx(*e.p, s));
    QVERIFY(e.p->unidadDeTrabajo().begin());
    QVERIFY(e.p->logs().agregar(logSaneado(s.id, timestamp::ahoraUtc())));
    QVERIFY(e.p->solicitudes().marcarEliminadaVisible(s.id, timestamp::ahoraUtc()).valor());
    QVERIFY(e.p->unidadDeTrabajo().commit());
    QCOMPARE(escalar(e.proveedor(), QStringLiteral("PRAGMA integrity_check")).toString(),
             QStringLiteral("ok"));
    QCOMPARE(columna(e.proveedor(), QStringLiteral("PRAGMA foreign_key_check")), QStringList());
}
