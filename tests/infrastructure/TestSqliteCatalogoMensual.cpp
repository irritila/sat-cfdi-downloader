#include "TestSqliteCatalogoMensual.h"

#include "SqlitePruebasComun.h"

#include "domain/cfdi/CoberturaMensual.h"
#include "domain/cfdi/MesCalendario.h"
#include "domain/operaciones/RegistrosOperacion.h"
#include "ports/repositories/CatalogoMensualRepository.h"
#include "ports/repositories/LoteDescargaGuiadaRepository.h"
#include "ports/repositories/OperacionesSolicitudRepository.h"
#include "ports/repositories/PaqueteSolicitudRepository.h"
#include "ports/repositories/SolicitudMasivaRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <QTest>
#include <QTimeZone>

#include <functional>
#include <memory>

using namespace satcfdi;
using namespace satcfdi::pruebas;

namespace {

// Datos sinteticos: RFC genericos del SAT, UUID inventados.
const QString kRfcGenerico = QStringLiteral("XAXX010101000");
const QString kRfcExtranjero = QStringLiteral("XEXX010101000");
const MesCalendario kSeptiembre = *MesCalendario::crear(2026, 9);
const QDateTime kT0 = QDateTime(QDate(2026, 10, 11), QTime(12, 0, 0), QTimeZone(QTimeZone::UTC));
const QString kSha = QString(64, QLatin1Char('a'));

bool consistente(SqliteConnectionProvider& prov)
{
    return escalar(prov, QStringLiteral("PRAGMA integrity_check")).toString() == QStringLiteral("ok")
           && columna(prov, QStringLiteral("PRAGMA foreign_key_check")).isEmpty();
}

SolicitudId sid(const QString& texto)
{
    return *SolicitudId::desdeTexto(texto);
}

PerfilId pid(const QString& texto)
{
    return *PerfilId::desdeTexto(texto);
}

// Solicitud fixture con direccion, tramo y filtros, coherente con los CHECK.
struct Sol {
    QString id = uuid::generarCanonico();
    QString perfil;
    QString rfc = kRfcGenerico;
    TipoDescarga dir = TipoDescarga::Emitidos;
    QString inicio = kSeptiembre.inicioSat();
    QString fin = kSeptiembre.finSat();
    QString estadoLocal = QStringLiteral("Enviada");
    QString estadoSat = QStringLiteral("Terminada");
    QString tipoComprobante; // vacio = sin filtro
    QString creadaEn = kAhora;
};

QString insertarSol(SqliteConnectionProvider& prov, const Sol& s)
{
    const bool enviada = s.estadoLocal == QStringLiteral("Enviada");
    const bool conIntento = s.estadoLocal != QStringLiteral("Creada") && s.estadoLocal != QStringLiteral("EnvioFallido");
    const bool conSat = !s.estadoSat.isEmpty();
    const bool emitidos = s.dir == TipoDescarga::Emitidos;
    return ejecutarSql(
        prov,
        QStringLiteral(
            "INSERT INTO solicitud_masiva (id, perfil_sat_id, id_solicitud_sat, tipo_cfdi, operacion_sat, "
            "rfc_solicitante, rfc_emisor, rfc_receptor, fecha_inicial_sat, fecha_final_sat, tipo_comprobante, "
            "dedup_key, estado_local, cod_estatus_solicitud, estado_solicitud_sat, creada_en, envio_iniciado_en, "
            "enviada_en, ultima_verificacion_en) VALUES (%1, %2, %3, %4, %5, %6, %7, %8, %9, %10, %11, %12, %13, "
            "%14, %15, %16, %17, %18, %19)")
            .arg(citar(s.id), citar(s.perfil), enviada ? citar(s.id.toUpper()) : QStringLiteral("NULL"),
                 emitidos ? QStringLiteral("'emitidos'") : QStringLiteral("'recibidos'"),
                 emitidos ? QStringLiteral("'SolicitaDescargaEmitidos'") : QStringLiteral("'SolicitaDescargaRecibidos'"),
                 citar(s.rfc), emitidos ? citar(s.rfc) : QStringLiteral("NULL"),
                 emitidos ? QStringLiteral("NULL") : citar(s.rfc), citar(s.inicio))
            .arg(citar(s.fin), s.tipoComprobante.isEmpty() ? QStringLiteral("NULL") : citar(s.tipoComprobante),
                 citar(DedupKey::calcularV1(s.id.toUtf8()).texto()), citar(s.estadoLocal),
                 enviada ? QStringLiteral("'5000'") : QStringLiteral("NULL"),
                 conSat ? citar(s.estadoSat) : QStringLiteral("NULL"), citar(s.creadaEn),
                 conIntento ? citar(kAhora) : QStringLiteral("NULL"), enviada ? citar(kAhora) : QStringLiteral("NULL"))
            .arg(conSat ? citar(kAhora) : QStringLiteral("NULL")));
}

QString insertarIndice(SqliteConnectionProvider& prov, const QString& paquete, const QString& estado,
                       int version = 1, const QString& actualizadoEn = kAhora)
{
    const bool conHuella = estado == QStringLiteral("Indexado") || estado == QStringLiteral("Ilegible");
    const bool conCausa = estado == QStringLiteral("OrigenNoDisponible") || estado == QStringLiteral("Ilegible");
    return ejecutarSql(prov, QStringLiteral("INSERT INTO indice_cfdi_paquete (paquete_solicitud_id, estado, "
                                            "tamano_bytes, modificado_en_ms, sha256, version_parser, causa, "
                                            "actualizado_en) VALUES (%1, %2, %3, %4, %5, %6, %7, %8)")
                                 .arg(citar(paquete), citar(estado), conHuella ? QStringLiteral("10") : QStringLiteral("NULL"),
                                      conHuella ? QStringLiteral("1700000000000") : QStringLiteral("NULL"),
                                      conHuella ? citar(kSha) : QStringLiteral("NULL"), QString::number(version),
                                      conCausa ? QStringLiteral("'no-existe'") : QStringLiteral("NULL"),
                                      citar(actualizadoEn)));
}

QString insertarCfdi(SqliteConnectionProvider& prov, const QString& paquete, int posicion, const QString& solicitud,
                     const QString& uuid, const QString& fecha, const QString& tipo = QStringLiteral("I"),
                     const QString& nombre = QStringLiteral("proveedor sintetico"))
{
    return ejecutarSql(prov, QStringLiteral("INSERT INTO cfdi_indexado (paquete_solicitud_id, posicion, "
                                            "solicitud_masiva_id, estado_parseo, version, tipo_comprobante, fecha, "
                                            "rfc_emisor, nombre_emisor, rfc_receptor, uuid, texto_busqueda) VALUES "
                                            "(%1, %2, %3, 'Completo', '4.0', %4, %5, %6, %7, %8, %9, %10)")
                                 .arg(citar(paquete), QString::number(posicion), citar(solicitud), citar(tipo),
                                      fecha.isEmpty() ? QStringLiteral("NULL") : citar(fecha), citar(kRfcGenerico),
                                      citar(nombre), citar(kRfcExtranjero),
                                      uuid.isNull() ? QStringLiteral("NULL") : citar(uuid))
                                 .arg(citar(QStringLiteral("xaxx010101000 %1 %2").arg(nombre, uuid.toLower()))));
}

struct Entorno {
    QTemporaryDir dir;
    std::unique_ptr<SqlitePersistencia> p;
    QString perfil;

    bool preparar()
    {
        if (!dir.isValid() || !inicializarBaseSqlite(rutaBase(dir))) {
            return false;
        }
        p = std::make_unique<SqlitePersistencia>(rutaBase(dir));
        perfil = uuid::generarCanonico();
        return insertarPerfilFixture(prov(), perfil, kRfcGenerico).isEmpty();
    }
    ~Entorno()
    {
        if (p) {
            p->cerrarConexionDelHiloActual();
        }
    }

    SqliteConnectionProvider& prov() { return p->proveedor(); }

    // Solicitud del perfil del entorno; devuelve su id (vacio si falla).
    QString solicitud(Sol s)
    {
        if (s.perfil.isEmpty()) {
            s.perfil = perfil;
        }
        const QString error = insertarSol(prov(), s);
        if (!error.isEmpty()) {
            qWarning("fixture solicitud: %s", qPrintable(error));
            return {};
        }
        return s.id;
    }

    QString paquete(const QString& solicitudId, const QString& estado = QStringLiteral("Descargado"))
    {
        const QString id = uuid::generarCanonico();
        return insertarPaqueteFixture(prov(), id, solicitudId, QStringLiteral("PAQ_") + id.left(8), estado).isEmpty()
                   ? id
                   : QString();
    }

    // Paquete Descargado e indexado en `indexadoEn` con CFDI (uuid, fecha).
    QString paqueteConCfdi(const QString& solicitudId, const QList<std::pair<QString, QString>>& cfdis,
                           const QString& indexadoEn = kAhora, const QString& tipo = QStringLiteral("I"),
                           const QString& nombre = QStringLiteral("proveedor sintetico"))
    {
        const QString paq = paquete(solicitudId);
        if (paq.isEmpty() || !insertarIndice(prov(), paq, QStringLiteral("Indexado"), 1, indexadoEn).isEmpty()) {
            return {};
        }
        int posicion = 0;
        for (const auto& [uuid, fecha] : cfdis) {
            const QString error = insertarCfdi(prov(), paq, posicion++, solicitudId, uuid, fecha, tipo, nombre);
            if (!error.isEmpty()) {
                qWarning("fixture cfdi: %s", qPrintable(error));
                return {};
            }
        }
        return paq;
    }

    Resultado<CatalogoMensual, ErrorPersistencia> consultar(ConsultaCatalogoMensual c)
    {
        if (c.perfil.esNulo()) {
            c.perfil = pid(perfil);
        }
        return p->catalogoMensual().consultar(c);
    }

    static ConsultaCatalogoMensual de(MesCalendario mes, std::optional<TipoDescarga> direccion = std::nullopt)
    {
        ConsultaCatalogoMensual c;
        c.mes = mes;
        c.direccion = direccion;
        return c;
    }

    template <typename F>
    auto enTransaccion(F&& f)
    {
        (void)p->unidadDeTrabajo().begin();
        auto r = f();
        if (r) {
            (void)p->unidadDeTrabajo().commit();
        } else {
            (void)p->unidadDeTrabajo().rollback();
        }
        return r;
    }

    bool eliminarSolicitud(const QString& id)
    {
        auto r = enTransaccion([&] { return p->solicitudes().marcarEliminadaVisible(sid(id), kT0); });
        auto r2 = enTransaccion([&] { return p->paquetes().marcarEliminadosPorSolicitud(sid(id), kT0); });
        return r && r.valor() && r2;
    }

    NuevoLoteGuiado lote(const QList<std::pair<QString, TipoDescarga>>& solicitudes,
                         const QString& inicio = kSeptiembre.inicioSat(), const QString& fin = kSeptiembre.finSat())
    {
        NuevoLoteGuiado l;
        l.id = uuid::generarCanonico();
        l.perfil = pid(perfil);
        l.mes = kSeptiembre;
        l.creadoEn = kT0;
        for (const auto& [id, direccion] : solicitudes) {
            l.tramos.append(TramoLoteGuiado{direccion, sid(id), inicio, fin});
        }
        return l;
    }
};

} // namespace

void TestSqliteCatalogoMensual::migracion007DesdeVersionVigente()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ruta = rutaBase(dir);
    auto embebidas = migracionesSqliteEmbebidas();
    QVERIFY(embebidas);
    QCOMPARE(embebidas.valor().size(), 7);
    QCOMPARE(embebidas.valor().last().nombre, QStringLiteral("007_catalogo_mensual"));
    // Base vigente antes de T023.2 (001-006) con datos e indice.
    QVERIFY(inicializarBaseSqlite(ruta, embebidas.valor().mid(0, 6)));
    const QString huellaSql = QStringLiteral(
        "SELECT (SELECT group_concat(id || rfc, ',') FROM perfil_sat) || '|' || "
        "(SELECT group_concat(id || estado_local || ifnull(estado_solicitud_sat, '-') || fecha_inicial_sat, ',') "
        "FROM solicitud_masiva) || '|' || (SELECT group_concat(id || estado_descarga, ',') FROM paquete_solicitud) "
        "|| '|' || (SELECT group_concat(paquete_solicitud_id || estado, ',') FROM indice_cfdi_paquete) || '|' || "
        "(SELECT count(*) FROM cfdi_indexado)");
    QString huellaAntes;
    QString perfil;
    QString solicitud;
    {
        SqliteConnectionProvider prov(ruta);
        perfil = uuid::generarCanonico();
        QCOMPARE(insertarPerfilFixture(prov, perfil, kRfcGenerico), QString());
        Sol s;
        s.perfil = perfil;
        solicitud = s.id;
        QCOMPARE(insertarSol(prov, s), QString());
        const QString paq = uuid::generarCanonico();
        QCOMPARE(insertarPaqueteFixture(prov, paq, s.id, QStringLiteral("PAQ_1"), QStringLiteral("Descargado")),
                 QString());
        QCOMPARE(insertarIndice(prov, paq, QStringLiteral("Indexado")), QString());
        QCOMPARE(insertarCfdi(prov, paq, 0, s.id, QStringLiteral("AAAAAAAA-0000-4000-8000-000000000001"),
                              QStringLiteral("2026-09-10T10:00:00")),
                 QString());
        QVERIFY(!ejecutarSql(prov, QStringLiteral("SELECT * FROM lote_descarga_guiada")).isEmpty()); // aun no existe
        huellaAntes = escalar(prov, huellaSql).toString();
        QVERIFY(!huellaAntes.isEmpty());
        prov.cerrarConexionDelHiloActual();
    }

    auto migrada = inicializarBaseSqlite(ruta);
    QVERIFY2(migrada, migrada ? "" : qPrintable(migrada.error().mensaje));
    QCOMPARE(migrada.valor().migracion.versionInicial, 6);
    QCOMPARE(migrada.valor().migracion.aplicadas, QList<int>{7});
    QCOMPARE(migrada.valor().migracion.versionFinal, 7);
    {
        SqliteConnectionProvider prov(ruta);
        QCOMPARE(escalar(prov, huellaSql).toString(), huellaAntes);
        // Filas existentes: sin conjunto vacio confirmado (no acreditan).
        QCOMPARE(columna(prov, QStringLiteral("SELECT conjunto_paquetes_vacio_confirmado_en FROM solicitud_masiva")),
                 QStringList{QStringLiteral("<NULL>")});
        QCOMPARE(columna(prov, QStringLiteral("SELECT name FROM sqlite_master WHERE name IN ("
                                              "'lote_descarga_guiada', 'lote_descarga_guiada_solicitud', "
                                              "'ix_lote_guiado_perfil_mes', 'ux_lote_guiado_solicitud') ORDER BY name")),
                 (QStringList{QStringLiteral("ix_lote_guiado_perfil_mes"),
                              QStringLiteral("lote_descarga_guiada"), QStringLiteral("lote_descarga_guiada_solicitud"),
                              QStringLiteral("ux_lote_guiado_solicitud")}));
        // CHECKs, PK por direccion, unicidad de solicitud y FK.
        const QString lote = uuid::generarCanonico();
        QVERIFY(ejecutarSql(prov, QStringLiteral("INSERT INTO lote_descarga_guiada VALUES (%1, %2, '2026-13', %3)")
                                      .arg(citar(lote), citar(perfil), citar(kAhora)))
                    .contains(QStringLiteral("CHECK")));
        QVERIFY(ejecutarSql(prov, QStringLiteral("INSERT INTO lote_descarga_guiada VALUES (%1, %2, '2026-9', %3)")
                                      .arg(citar(lote), citar(perfil), citar(kAhora)))
                    .contains(QStringLiteral("CHECK")));
        QVERIFY(ejecutarSql(prov, QStringLiteral("INSERT INTO lote_descarga_guiada VALUES (%1, %2, '2026-09', %3)")
                                      .arg(citar(lote), citar(uuid::generarCanonico()), citar(kAhora)))
                    .contains(QStringLiteral("FOREIGN KEY")));
        QCOMPARE(ejecutarSql(prov, QStringLiteral("INSERT INTO lote_descarga_guiada VALUES (%1, %2, '2026-09', %3)")
                                       .arg(citar(lote), citar(perfil), citar(kAhora))),
                 QString());
        const QString tramo = QStringLiteral("INSERT INTO lote_descarga_guiada_solicitud VALUES (%1, %2, %3, %4, %5)");
        QVERIFY(ejecutarSql(prov, tramo.arg(citar(lote), citar(solicitud), QStringLiteral("'emitidos'"),
                                            citar(kSeptiembre.finSat()), citar(kSeptiembre.inicioSat())))
                    .contains(QStringLiteral("CHECK")));
        QVERIFY(ejecutarSql(prov, tramo.arg(citar(lote), citar(uuid::generarCanonico()), QStringLiteral("'emitidos'"),
                                            citar(kSeptiembre.inicioSat()), citar(kSeptiembre.finSat())))
                    .contains(QStringLiteral("FOREIGN KEY")));
        QCOMPARE(ejecutarSql(prov, tramo.arg(citar(lote), citar(solicitud), QStringLiteral("'emitidos'"),
                                             citar(kSeptiembre.inicioSat()), citar(kSeptiembre.finSat()))),
                 QString());
        QVERIFY(ejecutarSql(prov, tramo.arg(citar(lote), citar(solicitud), QStringLiteral("'recibidos'"),
                                            citar(kSeptiembre.inicioSat()), citar(kSeptiembre.finSat())))
                    .contains(QStringLiteral("UNIQUE"))); // una solicitud, un lote
        QVERIFY(consistente(prov));
        prov.cerrarConexionDelHiloActual();
    }
    // Idempotente.
    auto repetida = inicializarBaseSqlite(ruta);
    QVERIFY(repetida);
    QVERIFY(repetida.valor().migracion.aplicadas.isEmpty());
    QCOMPARE(repetida.valor().migracion.versionInicial, 7);
    // El repositorio de solicitudes lee la columna nueva.
    SqlitePersistencia p(ruta);
    auto leida = p.solicitudes().obtenerVisible(sid(solicitud));
    QVERIFY(leida && leida.valor());
    QVERIFY(!leida.valor()->conjuntoPaquetesVacioConfirmadoEn);
    p.cerrarConexionDelHiloActual();
}

void TestSqliteCatalogoMensual::conjuntoVacioConfirmadoAlVerificar()
{
    Entorno e;
    QVERIFY(e.preparar());
    Sol enProceso;
    enProceso.estadoSat = QStringLiteral("EnProceso");
    const QString sinPaquetes = e.solicitud(enProceso);
    Sol otra = enProceso;
    otra.id = uuid::generarCanonico();
    const QString conPaquetes = e.solicitud(otra);
    QVERIFY(!sinPaquetes.isEmpty() && !conPaquetes.isEmpty());
    QVERIFY(!e.paquete(conPaquetes, QStringLiteral("Disponible")).isEmpty());

    auto verificar = [&](const QString& id, bool vacio, const QDateTime& en) {
        AplicacionVerificacion a;
        a.solicitudId = sid(id);
        a.verificadaEn = en;
        a.estadoSolicitudSat = EstadoSolicitudSat::Terminada;
        a.codigoEstadoSolicitud = QStringLiteral("5000");
        a.numeroCfdi = 0;
        a.conjuntoPaquetesVacio = vacio;
        return e.enTransaccion([&] { return e.p->operaciones().aplicarVerificacion(a); });
    };
    auto confirmado = [&](const QString& id) {
        return e.p->solicitudes().obtenerVisible(sid(id)).valor()->conjuntoPaquetesVacioConfirmadoEn;
    };

    // Sin bandera (p. ej. numero_cfdi = 0 sin conjunto explicito): NULL.
    QVERIFY(verificar(sinPaquetes, false, kT0).valor().aplicada);
    QVERIFY(!confirmado(sinPaquetes));
    // Terminada con conjunto vacio: se fija en la misma transaccion.
    QVERIFY(verificar(sinPaquetes, true, kT0.addSecs(60)).valor().aplicada);
    QCOMPARE(confirmado(sinPaquetes), std::optional(kT0.addSecs(60)));
    // Una verificacion posterior conserva el primer instante.
    QVERIFY(verificar(sinPaquetes, true, kT0.addSecs(600)).valor().aplicada);
    QCOMPARE(confirmado(sinPaquetes), std::optional(kT0.addSecs(60)));
    // Con paquetes ya registrados no se confirma un conjunto vacio.
    QVERIFY(verificar(conPaquetes, true, kT0).valor().aplicada);
    QVERIFY(!confirmado(conPaquetes));

    // La evidencia mensual la refleja y la regla la acredita.
    auto evidencia = e.p->catalogoMensual().leerEvidencia(pid(e.perfil), kSeptiembre, 1);
    QVERIFY(evidencia);
    QCOMPARE(evidencia.valor().size(), 2);
    const auto r = evaluarCoberturaMensual(pid(e.perfil), kSeptiembre, kT0, evidencia.valor());
    QCOMPARE(r.emitidos.solicitudesAcreditadas, QList<SolicitudId>{sid(sinPaquetes)});
    QCOMPARE(r.estado, EstadoMes::Preparando); // el paquete Disponible de la otra
    QVERIFY(consistente(e.prov()));
}

void TestSqliteCatalogoMensual::lotesEliminacionParcialYPerfilRecreado()
{
    Entorno e;
    QVERIFY(e.preparar());
    Sol emitidos;
    Sol recibidos;
    recibidos.dir = TipoDescarga::Recibidos;
    recibidos.estadoSat = QStringLiteral("EnProceso");
    const QString idE = e.solicitud(emitidos);
    const QString idR = e.solicitud(recibidos);
    QVERIFY(!idE.isEmpty() && !idR.isEmpty());
    const NuevoLoteGuiado nuevo = e.lote({{idE, TipoDescarga::Emitidos}, {idR, TipoDescarga::Recibidos}});
    QVERIFY(e.enTransaccion([&] { return e.p->lotesDescargaGuiada().insertar(nuevo); }));
    // Otro lote mas reciente de otro mes para el orden y el filtro.
    Sol octubre;
    octubre.inicio = QStringLiteral("2026-10-01T00:00:00");
    octubre.fin = QStringLiteral("2026-10-05T10:00:00");
    const QString idO = e.solicitud(octubre);
    NuevoLoteGuiado otro = e.lote({{idO, TipoDescarga::Emitidos}}, octubre.inicio, octubre.fin);
    otro.mes = *MesCalendario::crear(2026, 10);
    otro.creadoEn = kT0.addSecs(3600);
    QVERIFY(e.enTransaccion([&] { return e.p->lotesDescargaGuiada().insertar(otro); }));

    auto todos = e.p->lotesDescargaGuiada().listar(pid(e.perfil), std::nullopt);
    QVERIFY(todos);
    QCOMPARE(todos.valor().size(), 2);
    QCOMPARE(todos.valor().at(0).id, otro.id); // creado_en DESC
    auto lotes = e.p->lotesDescargaGuiada().listar(pid(e.perfil), kSeptiembre);
    QVERIFY(lotes);
    QCOMPARE(lotes.valor().size(), 1);
    const LoteGuiado& l = lotes.valor().first();
    QCOMPARE(l.id, nuevo.id);
    QCOMPARE(l.mes, kSeptiembre);
    QCOMPARE(l.creadoEn, kT0);
    QCOMPARE(l.tramos.size(), 2);
    QCOMPARE(l.tramos.at(0).tramo, nuevo.tramos.at(0));
    QCOMPARE(l.tramos.at(1).tramo, nuevo.tramos.at(1));
    QCOMPARE(l.tramos.at(1).estado, std::optional(EstadoResumen::EnProceso));
    QVERIFY(!l.eliminacionParcial);
    QCOMPARE(l.resultado, ResultadoLote::EnCurso);

    // Se elimina la solicitud en curso: el lote conserva mes, fecha y tramo,
    // marca la eliminacion parcial y recalcula su resultado sin ella.
    QVERIFY(e.eliminarSolicitud(idR));
    auto tras = e.p->lotesDescargaGuiada().listar(pid(e.perfil), kSeptiembre);
    QVERIFY(tras);
    const LoteGuiado& t = tras.valor().first();
    QCOMPARE(t.mes, kSeptiembre);
    QCOMPARE(t.creadoEn, kT0);
    QCOMPARE(t.tramos.size(), 2);
    QVERIFY(t.tramos.at(1).eliminada);
    QVERIFY(!t.tramos.at(1).estado);
    QVERIFY(t.eliminacionParcial);
    QCOMPARE(t.resultado, ResultadoLote::Terminado);
    QVERIFY(e.eliminarSolicitud(idE));
    QCOMPARE(e.p->lotesDescargaGuiada().listar(pid(e.perfil), kSeptiembre).valor().first().resultado,
             ResultadoLote::SinSolicitudesVigentes);

    // T023.5: solicitudes sin lote (previas a T023 o con filtros) y ubicacion.
    Sol filtrada;
    filtrada.tipoComprobante = QStringLiteral("I");
    const QString idFiltrada = e.solicitud(filtrada);
    auto sinLote = e.p->lotesDescargaGuiada().listarSinLote(pid(e.perfil));
    QVERIFY(sinLote);
    QCOMPARE(sinLote.valor().size(), 1); // las vinculadas no aparecen; las eliminadas tampoco
    QCOMPARE(sinLote.valor().first().id, sid(idFiltrada));
    QVERIFY(sinLote.valor().first().conFiltros);
    QCOMPARE(sinLote.valor().first().fechaInicialSat, kSeptiembre.inicioSat());
    auto u = e.p->lotesDescargaGuiada().ubicar(sid(idO));
    QVERIFY(u && u.valor());
    QCOMPARE(u.valor()->mesLote, std::optional(*MesCalendario::crear(2026, 10)));
    QVERIFY(e.p->lotesDescargaGuiada().ubicar(sid(idE)).valor() == std::nullopt); // eliminada
    QVERIFY(!e.p->lotesDescargaGuiada().ubicar(sid(idFiltrada)).valor()->mesLote);

    // Perfil eliminado y recreado con el mismo RFC: los lotes se ocultan con
    // el perfil y no se reasignan al nuevo.
    QCOMPARE(ejecutarSql(e.prov(), QStringLiteral("UPDATE perfil_sat SET eliminado_en = %1 WHERE id = %2")
                                       .arg(citar(kAhora), citar(e.perfil))),
             QString());
    const QString recreado = uuid::generarCanonico();
    QCOMPARE(insertarPerfilFixture(e.prov(), recreado, kRfcGenerico), QString());
    QVERIFY(e.p->lotesDescargaGuiada().listar(pid(e.perfil), std::nullopt).valor().isEmpty());
    QVERIFY(e.p->lotesDescargaGuiada().listar(pid(recreado), std::nullopt).valor().isEmpty());
    QVERIFY(e.p->lotesDescargaGuiada().listarSinLote(pid(e.perfil)).valor().isEmpty());
    QVERIFY(!e.p->lotesDescargaGuiada().ubicar(sid(idFiltrada)).valor());
    QCOMPARE(escalar(e.prov(), QStringLiteral("SELECT count(*) FROM lote_descarga_guiada WHERE perfil_sat_id = %1")
                                   .arg(citar(e.perfil)))
                 .toInt(),
             2); // el historial se conserva en la base
    QVERIFY(consistente(e.prov()));
}

void TestSqliteCatalogoMensual::insertarLoteRevalida()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString idE = e.solicitud(Sol{});
    QVERIFY(!idE.isEmpty());
    LoteDescargaGuiadaRepository& lotes = e.p->lotesDescargaGuiada();
    auto tipo = [](const Resultado<Exito, ErrorPersistencia>& r) { return r ? QStringLiteral("ok") : QString::number(int(r.error().tipo)); };
    const QString integridad = QString::number(int(ErrorPersistencia::Tipo::Integridad));

    // Sin transaccion.
    auto sinTx = lotes.insertar(e.lote({{idE, TipoDescarga::Emitidos}}));
    QVERIFY(!sinTx && sinTx.error().tipo == ErrorPersistencia::Tipo::Transaccion);
    // Direccion distinta de la solicitud, tramo distinto, direccion repetida
    // y solicitud de otro perfil.
    QCOMPARE(tipo(e.enTransaccion([&] { return lotes.insertar(e.lote({{idE, TipoDescarga::Recibidos}})); })),
             integridad);
    QCOMPARE(tipo(e.enTransaccion([&] {
                 return lotes.insertar(e.lote({{idE, TipoDescarga::Emitidos}}, QStringLiteral("2026-09-02T00:00:00")));
             })),
             integridad);
    QCOMPARE(tipo(e.enTransaccion([&] {
                 return lotes.insertar(e.lote({{idE, TipoDescarga::Emitidos}, {idE, TipoDescarga::Emitidos}}));
             })),
             integridad);
    const QString otroPerfil = uuid::generarCanonico();
    QCOMPARE(insertarPerfilFixture(e.prov(), otroPerfil, kRfcExtranjero), QString());
    Sol ajena;
    ajena.perfil = otroPerfil;
    ajena.rfc = kRfcExtranjero;
    const QString idAjena = e.solicitud(ajena);
    QCOMPARE(tipo(e.enTransaccion([&] { return lotes.insertar(e.lote({{idAjena, TipoDescarga::Emitidos}})); })),
             integridad);
    // No canonicas: con filtro SAT (tipo de comprobante) o con contraparte.
    Sol filtrada;
    filtrada.tipoComprobante = QStringLiteral("I");
    const QString idFiltrada = e.solicitud(filtrada);
    QCOMPARE(tipo(e.enTransaccion([&] { return lotes.insertar(e.lote({{idFiltrada, TipoDescarga::Emitidos}})); })),
             integridad);
    Sol conContraparte;
    conContraparte.dir = TipoDescarga::Recibidos;
    const QString idContraparte = e.solicitud(conContraparte);
    QCOMPARE(ejecutarSql(e.prov(), QStringLiteral("UPDATE solicitud_masiva SET rfc_emisor = %1 WHERE id = %2")
                                       .arg(citar(kRfcExtranjero), citar(idContraparte))),
             QString());
    QCOMPARE(tipo(e.enTransaccion([&] { return lotes.insertar(e.lote({{idContraparte, TipoDescarga::Recibidos}})); })),
             integridad);
    // Tramo de otro mes que el del lote (octubre en un lote de septiembre).
    Sol octubre;
    octubre.inicio = QStringLiteral("2026-10-01T00:00:00");
    octubre.fin = QStringLiteral("2026-10-05T00:00:00");
    const QString idOctubre = e.solicitud(octubre);
    QCOMPARE(tipo(e.enTransaccion([&] {
                 return lotes.insertar(e.lote({{idOctubre, TipoDescarga::Emitidos}}, octubre.inicio, octubre.fin));
             })),
             integridad);
    QCOMPARE(escalar(e.prov(), QStringLiteral("SELECT count(*) FROM lote_descarga_guiada")).toInt(), 0);

    // Valido; despues la misma solicitud en otro lote viola la unicidad.
    QCOMPARE(tipo(e.enTransaccion([&] { return lotes.insertar(e.lote({{idE, TipoDescarga::Emitidos}})); })),
             QStringLiteral("ok"));
    QCOMPARE(tipo(e.enTransaccion([&] { return lotes.insertar(e.lote({{idE, TipoDescarga::Emitidos}})); })),
             QString::number(int(ErrorPersistencia::Tipo::Unicidad)));
    // Solicitud eliminada o perfil eliminado.
    const QString idEliminada = e.solicitud(Sol{});
    QVERIFY(e.eliminarSolicitud(idEliminada));
    QCOMPARE(tipo(e.enTransaccion([&] { return lotes.insertar(e.lote({{idEliminada, TipoDescarga::Emitidos}})); })),
             integridad);
    QCOMPARE(escalar(e.prov(), QStringLiteral("SELECT count(*) FROM lote_descarga_guiada")).toInt(), 1);
    QVERIFY(consistente(e.prov()));
}

void TestSqliteCatalogoMensual::evidenciaMensual()
{
    Entorno e;
    QVERIFY(e.preparar());
    Sol completa;
    completa.creadaEn = QStringLiteral("2026-10-01T00:00:00.000Z");
    const QString idCompleta = e.solicitud(completa);
    const QString paqIndexado = e.paquete(idCompleta);
    QCOMPARE(insertarIndice(e.prov(), paqIndexado, QStringLiteral("Indexado")), QString());

    Sol recibidos;
    recibidos.dir = TipoDescarga::Recibidos;
    recibidos.creadaEn = QStringLiteral("2026-10-02T00:00:00.000Z");
    const QString idRecibidos = e.solicitud(recibidos);
    const QString paqSinIndice = e.paquete(idRecibidos);
    const QString paqOtraVersion = e.paquete(idRecibidos);
    QCOMPARE(insertarIndice(e.prov(), paqOtraVersion, QStringLiteral("Indexado"), 3), QString());
    const QString paqIlegible = e.paquete(idRecibidos);
    // Fila de indice con estado desconocido (dato corrupto): se omite el CHECK.
    QCOMPARE(ejecutarSql(e.prov(), QStringLiteral("PRAGMA ignore_check_constraints = ON")), QString());
    QCOMPARE(ejecutarSql(e.prov(), QStringLiteral("INSERT INTO indice_cfdi_paquete (paquete_solicitud_id, estado, "
                                                  "version_parser, actualizado_en) VALUES (%1, 'Corrupto', 1, %2)")
                                       .arg(citar(paqIlegible), citar(kAhora))),
             QString());
    QCOMPARE(ejecutarSql(e.prov(), QStringLiteral("PRAGMA ignore_check_constraints = OFF")), QString());
    const QString paqError = e.paquete(idRecibidos, QStringLiteral("Error"));
    const QString paqEliminado = e.paquete(idRecibidos);
    QCOMPARE(ejecutarSql(e.prov(), QStringLiteral("UPDATE paquete_solicitud SET eliminado_en = %1 WHERE id = %2")
                                       .arg(citar(kAhora), citar(paqEliminado))),
             QString());

    // Fuera del universo: otro mes, eliminada, otro perfil. Dentro: rango que
    // intersecta el mes (no canonico) y una con filtro.
    Sol octubre;
    octubre.inicio = QStringLiteral("2026-10-01T00:00:00");
    octubre.fin = QStringLiteral("2026-10-31T23:59:59");
    QVERIFY(!e.solicitud(octubre).isEmpty());
    const QString idEliminada = e.solicitud(Sol{});
    QVERIFY(e.eliminarSolicitud(idEliminada));
    const QString otroPerfil = uuid::generarCanonico();
    QCOMPARE(insertarPerfilFixture(e.prov(), otroPerfil, kRfcExtranjero), QString());
    Sol ajena;
    ajena.perfil = otroPerfil;
    ajena.rfc = kRfcExtranjero;
    QVERIFY(!e.solicitud(ajena).isEmpty());
    Sol cruzada;
    cruzada.inicio = QStringLiteral("2026-08-20T00:00:00");
    cruzada.fin = QStringLiteral("2026-09-05T00:00:00");
    cruzada.creadaEn = QStringLiteral("2026-10-03T00:00:00.000Z");
    const QString idCruzada = e.solicitud(cruzada);
    Sol filtrada;
    filtrada.tipoComprobante = QStringLiteral("I");
    filtrada.creadaEn = QStringLiteral("2026-10-04T00:00:00.000Z");
    const QString idFiltrada = e.solicitud(filtrada);

    auto leida = e.p->catalogoMensual().leerEvidencia(pid(e.perfil), kSeptiembre, 2);
    QVERIFY2(leida, leida ? "" : qPrintable(leida.error().mensaje));
    const QList<EvidenciaSolicitudMensual>& ev = leida.valor();
    QCOMPARE(ev.size(), 4);
    QCOMPARE(ev.at(0).solicitud.id, sid(idCompleta)); // creada_en, id
    QCOMPARE(ev.at(1).solicitud.id, sid(idRecibidos));
    QCOMPARE(ev.at(2).solicitud.id, sid(idCruzada));
    QCOMPARE(ev.at(3).solicitud.id, sid(idFiltrada));
    QCOMPARE(ev.at(0).paquetes.size(), 1);
    QCOMPARE(ev.at(0).paquetes.at(0).indice, std::optional(EstadoIndicePaquete::Indexado));
    QVERIFY(!ev.at(0).paquetes.at(0).indiceVigente); // version 1 frente a vigente 2
    QVERIFY(!ev.at(0).indexacionProgramada);
    QCOMPARE(ev.at(1).paquetes.size(), 4); // el eliminado no aparece
    for (const EvidenciaPaqueteMensual& p : ev.at(1).paquetes) {
        if (p.paqueteId == paqSinIndice) {
            QVERIFY(!p.indice);
            QVERIFY(p.indiceLegible);
        } else if (p.paqueteId == paqIlegible) {
            QVERIFY(!p.indiceLegible);
        } else if (p.paqueteId == paqError) {
            QCOMPARE(p.estadoDescarga, EstadoDescarga::Error);
        } else {
            QCOMPARE(p.paqueteId, paqOtraVersion);
        }
    }
    // Con la version vigente 1 el paquete indexado acredita.
    auto vigente = e.p->catalogoMensual().leerEvidencia(pid(e.perfil), kSeptiembre, 1);
    QVERIFY(vigente);
    QVERIFY(vigente.valor().at(0).paquetes.at(0).indiceVigente);
    const auto r = evaluarCoberturaMensual(pid(e.perfil), kSeptiembre, kT0, vigente.valor());
    QCOMPARE(r.emitidos.solicitudesAcreditadas, QList<SolicitudId>{sid(idCompleta)});
    QCOMPARE(r.solicitudesNoCanonicas, 2);
    QCOMPARE(r.estado, EstadoMes::RequiereAtencion);
    QCOMPARE(r.causa->causa, CausaAtencion::PaqueteError);
    QCOMPARE(r.causa->direccion, TipoDescarga::Recibidos);
    QVERIFY(r.causas.contains(CausaMensual{CausaAtencion::IndiceIlegible, TipoDescarga::Recibidos, sid(idRecibidos)}));
    QVERIFY(r.causas.contains(
        CausaMensual{CausaAtencion::IndexacionSinProgramar, TipoDescarga::Recibidos, sid(idRecibidos)}));

    // Perfil eliminado: sin evidencia.
    QCOMPARE(ejecutarSql(e.prov(), QStringLiteral("UPDATE perfil_sat SET eliminado_en = %1 WHERE id = %2")
                                       .arg(citar(kAhora), citar(e.perfil))),
             QString());
    QVERIFY(e.p->catalogoMensual().leerEvidencia(pid(e.perfil), kSeptiembre, 1).valor().isEmpty());
}

namespace {

QStringList uuids(const CatalogoMensual& c)
{
    QStringList r;
    for (const CfdiMensual& m : c.cfdis) {
        r.append(m.cfdi.resumen.uuid.isNull() ? QStringLiteral("<NULL>") : m.cfdi.resumen.uuid);
    }
    return r;
}

const QString kU1 = QStringLiteral("AAAAAAAA-0000-4000-8000-000000000001");
const QString kU2 = QStringLiteral("AAAAAAAA-0000-4000-8000-000000000002");
const QString kU3 = QStringLiteral("AAAAAAAA-0000-4000-8000-000000000003");
const QString kU4 = QStringLiteral("AAAAAAAA-0000-4000-8000-000000000004");

} // namespace

void TestSqliteCatalogoMensual::catalogoFiltraPerfilMesYDireccion()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString idE = e.solicitud(Sol{});
    Sol rec;
    rec.dir = TipoDescarga::Recibidos;
    const QString idR = e.solicitud(rec);
    QVERIFY(!e.paqueteConCfdi(idE, {{kU1, QStringLiteral("2026-09-10T10:00:00")},
                                    {kU2, QStringLiteral("2026-09-20T10:00:00")},
                                    {kU3, QStringLiteral("2026-10-01T00:00:00")}, // otro mes
                                    {kU4, QString()}})                            // sin fecha: ningun mes
                 .isEmpty());
    QVERIFY(!e.paqueteConCfdi(idR, {{QStringLiteral("BBBBBBBB-0000-4000-8000-000000000001"),
                                     QStringLiteral("2026-09-15T08:00:00")}})
                 .isEmpty());
    // Otro perfil en el mismo mes: no aparece.
    const QString otroPerfil = uuid::generarCanonico();
    QCOMPARE(insertarPerfilFixture(e.prov(), otroPerfil, kRfcExtranjero), QString());
    Sol ajena;
    ajena.perfil = otroPerfil;
    ajena.rfc = kRfcExtranjero;
    const QString idAjena = e.solicitud(ajena);
    QVERIFY(!e.paqueteConCfdi(idAjena, {{QStringLiteral("CCCCCCCC-0000-4000-8000-000000000001"),
                                         QStringLiteral("2026-09-11T00:00:00")}})
                 .isEmpty());

    auto todos = e.consultar(Entorno::de(kSeptiembre));
    QVERIFY2(todos, todos ? "" : qPrintable(todos.error().mensaje));
    QCOMPARE(uuids(todos.valor()), (QStringList{kU2, QStringLiteral("BBBBBBBB-0000-4000-8000-000000000001"), kU1}));
    QCOMPARE(todos.valor().total, 3);
    QCOMPARE(todos.valor().conteos.emitidos, 2);
    QCOMPARE(todos.valor().conteos.recibidos, 1);
    QVERIFY(todos.valor().conteos.porMes);
    std::array<int, 12> esperado{};
    esperado[8] = 3;
    esperado[9] = 1;
    QCOMPARE(*todos.valor().conteos.porMes, esperado);
    QCOMPARE(todos.valor().cfdis.at(1).direccion, TipoDescarga::Recibidos);
    QCOMPARE(todos.valor().cfdis.at(1).solicitud, sid(idR));
    QVERIFY(!todos.valor().cfdis.at(1).fuenteGuiada);

    auto emitidos = e.consultar(Entorno::de(kSeptiembre, TipoDescarga::Emitidos));
    QCOMPARE(uuids(emitidos.valor()), (QStringList{kU2, kU1}));
    QCOMPARE(emitidos.valor().total, 2);
    QCOMPARE(emitidos.valor().conteos.recibidos, 1); // conteo de la otra pestana
    esperado[8] = 2;
    QCOMPARE(*emitidos.valor().conteos.porMes, esperado);
    auto recibidos = e.consultar(Entorno::de(kSeptiembre, TipoDescarga::Recibidos));
    QCOMPARE(recibidos.valor().total, 1);
    QCOMPARE(recibidos.valor().cfdis.size(), 1);
    QCOMPARE(uuids(e.consultar(Entorno::de(*MesCalendario::crear(2026, 10))).valor()), QStringList{kU3});

    ConsultaCatalogoMensual sinAnuales = Entorno::de(kSeptiembre);
    sinAnuales.conConteosAnuales = false;
    QVERIFY(!e.consultar(sinAnuales).valor().conteos.porMes);
    // Perfil eliminado: catalogo vacio.
    QCOMPARE(ejecutarSql(e.prov(), QStringLiteral("UPDATE perfil_sat SET eliminado_en = %1 WHERE id = %2")
                                       .arg(citar(kAhora), citar(e.perfil))),
             QString());
    QCOMPARE(e.consultar(Entorno::de(kSeptiembre)).valor().total, 0);
}

void TestSqliteCatalogoMensual::catalogoLimitesDeAnio()
{
    Entorno e;
    QVERIFY(e.preparar());
    Sol s;
    s.inicio = QStringLiteral("2025-12-01T00:00:00");
    s.fin = QStringLiteral("2026-01-31T23:59:59");
    const QString id = e.solicitud(s);
    QVERIFY(!e.paqueteConCfdi(id, {{kU1, QStringLiteral("2025-12-31T23:59:59")},
                                   {kU2, QStringLiteral("2026-01-01T00:00:00")}})
                 .isEmpty());
    const MesCalendario diciembre = *MesCalendario::crear(2025, 12);
    const MesCalendario enero = *MesCalendario::crear(2026, 1);
    auto dic = e.consultar(Entorno::de(diciembre));
    auto ene = e.consultar(Entorno::de(enero));
    QVERIFY(dic && ene);
    QCOMPARE(uuids(dic.valor()), QStringList{kU1});
    QCOMPARE(uuids(ene.valor()), QStringList{kU2});
    std::array<int, 12> anio2025{};
    anio2025[11] = 1;
    std::array<int, 12> anio2026{};
    anio2026[0] = 1;
    QCOMPARE(*dic.valor().conteos.porMes, anio2025);
    QCOMPARE(*ene.valor().conteos.porMes, anio2026);
}

void TestSqliteCatalogoMensual::catalogoDeduplicaPorUuidConFuenteGuiada()
{
    Entorno e;
    QVERIFY(e.preparar());
    // Manual con filtros, indexada ANTES; guiada (en un lote), indexada despues.
    Sol manual;
    manual.tipoComprobante = QStringLiteral("I");
    const QString idManual = e.solicitud(manual);
    const QString idGuiada = e.solicitud(Sol{});
    QVERIFY(!e.paqueteConCfdi(idManual, {{QStringLiteral(" aaaaaaaa-0000-4000-8000-000000000001 "),
                                          QStringLiteral("2026-09-10T10:00:00")},
                                         {kU2, QStringLiteral("2026-09-11T10:00:00")}},
                              QStringLiteral("2026-10-01T00:00:00.000Z"))
                 .isEmpty());
    QVERIFY(!e.paqueteConCfdi(idGuiada, {{kU1, QStringLiteral("2026-09-10T10:00:00")}},
                              QStringLiteral("2026-10-05T00:00:00.000Z"))
                 .isEmpty());
    QVERIFY(e.enTransaccion([&] { return e.p->lotesDescargaGuiada().insertar(e.lote({{idGuiada, TipoDescarga::Emitidos}})); }));

    auto r = e.consultar(Entorno::de(kSeptiembre));
    QVERIFY2(r, r ? "" : qPrintable(r.error().mensaje));
    QCOMPARE(r.valor().cfdis.size(), 2);
    QCOMPARE(r.valor().total, 2);
    QCOMPARE(r.valor().conteos.emitidos, 2);
    QCOMPARE((*r.valor().conteos.porMes)[8], 2);
    const CfdiMensual& u1 = r.valor().cfdis.at(1);
    QCOMPARE(u1.cfdi.resumen.uuid, kU1);
    QCOMPARE(u1.solicitud, sid(idGuiada));
    QVERIFY(u1.fuenteGuiada);
    QCOMPARE(r.valor().cfdis.at(0).solicitud, sid(idManual)); // kU2 solo en la manual

    // T023.5: conteos por fuente canonica, con la misma deduplicacion (la
    // suma del mes coincide con el catalogo) y sin limitarse a un mes.
    QVERIFY(!e.paqueteConCfdi(idManual, {{kU3, QStringLiteral("2026-10-02T10:00:00")}}).isEmpty());
    auto c = e.p->catalogoMensual().conteosPorSolicitud(pid(e.perfil));
    QVERIFY(c);
    QCOMPARE(c.valor().value(sid(idGuiada)), 1);
    QCOMPARE(c.valor().value(sid(idManual)), 2); // kU2 (septiembre) y kU3 (octubre)
    QVERIFY(e.p->catalogoMensual().conteosPorSolicitud(PerfilId::generar()).valor().isEmpty());
}

void TestSqliteCatalogoMensual::catalogoEliminarFuenteEligeLaSiguiente()
{
    Entorno e;
    QVERIFY(e.preparar());
    Sol a;
    a.tipoComprobante = QStringLiteral("I");
    Sol b;
    b.tipoComprobante = QStringLiteral("E");
    const QString idGuiada = e.solicitud(Sol{});
    const QString idA = e.solicitud(a);
    const QString idB = e.solicitud(b);
    const QString fecha = QStringLiteral("2026-09-10T10:00:00");
    QVERIFY(!e.paqueteConCfdi(idGuiada, {{kU1, fecha}}, QStringLiteral("2026-10-09T00:00:00.000Z")).isEmpty());
    // B indexada antes que A: es la siguiente fuente tras la guiada.
    QVERIFY(!e.paqueteConCfdi(idA, {{kU1, fecha}}, QStringLiteral("2026-10-03T00:00:00.000Z")).isEmpty());
    QVERIFY(!e.paqueteConCfdi(idB, {{kU1, fecha}}, QStringLiteral("2026-10-02T00:00:00.000Z")).isEmpty());
    QVERIFY(e.enTransaccion([&] { return e.p->lotesDescargaGuiada().insertar(e.lote({{idGuiada, TipoDescarga::Emitidos}})); }));

    auto fuente = [&] {
        auto r = e.consultar(Entorno::de(kSeptiembre));
        return r && r.valor().cfdis.size() == 1 && r.valor().total == 1 ? r.valor().cfdis.first().solicitud.texto()
                                                                       : QStringLiteral("<incoherente>");
    };
    QCOMPARE(fuente(), idGuiada);
    QVERIFY(e.eliminarSolicitud(idGuiada));
    QCOMPARE(fuente(), idB);
    QVERIFY(e.eliminarSolicitud(idB));
    QCOMPARE(fuente(), idA);
    QVERIFY(e.eliminarSolicitud(idA));
    QCOMPARE(e.consultar(Entorno::de(kSeptiembre)).valor().total, 0);

    // Empate de indexacion: menor id de paquete.
    Entorno f;
    QVERIFY(f.preparar());
    const QString x = f.solicitud(Sol{});
    const QString y = f.solicitud(Sol{});
    const QString px = f.paqueteConCfdi(x, {{kU2, fecha}});
    const QString py = f.paqueteConCfdi(y, {{kU2, fecha}});
    QVERIFY(!px.isEmpty() && !py.isEmpty());
    auto r = f.consultar(Entorno::de(kSeptiembre));
    QCOMPARE(r.valor().cfdis.size(), 1);
    QCOMPARE(r.valor().cfdis.first().cfdi.paqueteId, std::min(px, py));
}

void TestSqliteCatalogoMensual::catalogoSinUuidNoSeDeduplica()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString x = e.solicitud(Sol{});
    const QString y = e.solicitud(Sol{});
    const QString fecha = QStringLiteral("2026-09-10T10:00:00");
    QVERIFY(!e.paqueteConCfdi(x, {{QString(), fecha}, {QStringLiteral(""), fecha}, {kU1, fecha}}).isEmpty());
    QVERIFY(!e.paqueteConCfdi(y, {{QString(), fecha}, {QStringLiteral("  "), fecha}, {kU1, fecha}}).isEmpty());
    auto r = e.consultar(Entorno::de(kSeptiembre));
    QVERIFY(r);
    QCOMPARE(r.valor().cfdis.size(), 5); // 4 sin UUID + 1 deduplicado
    QCOMPARE(r.valor().total, 5);
    QCOMPARE(r.valor().conteos.emitidos, 5);
    QCOMPARE(uuids(r.valor()).count(kU1), 1);
}

void TestSqliteCatalogoMensual::catalogoOcultaEliminadosYCuentaIgualQueLaLista()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString fecha = QStringLiteral("2026-09-10T10:00:00");
    const QString visible = e.solicitud(Sol{});
    Sol rec;
    rec.dir = TipoDescarga::Recibidos;
    const QString recibidos = e.solicitud(rec);
    QVERIFY(!e.paqueteConCfdi(visible, {{kU1, fecha}, {QString(), fecha}}).isEmpty());
    QVERIFY(!e.paqueteConCfdi(recibidos, {{kU2, fecha}}).isEmpty());
    // Paquete eliminado (su indice cae por el trigger de 006) y solicitud
    // marcada eliminada sin tocar sus paquetes: ninguno aparece ni cuenta.
    const QString paqEliminado = e.paqueteConCfdi(visible, {{kU3, fecha}});
    QCOMPARE(ejecutarSql(e.prov(), QStringLiteral("UPDATE paquete_solicitud SET eliminado_en = %1 WHERE id = %2")
                                       .arg(citar(kAhora), citar(paqEliminado))),
             QString());
    const QString eliminada = e.solicitud(Sol{});
    QVERIFY(!e.paqueteConCfdi(eliminada, {{kU4, fecha}, {kU1, fecha}}, QStringLiteral("2026-01-01T00:00:00.000Z"))
                 .isEmpty());
    QCOMPARE(ejecutarSql(e.prov(), QStringLiteral("UPDATE solicitud_masiva SET eliminado_en = %1 WHERE id = %2")
                                       .arg(citar(kAhora), citar(eliminada))),
             QString());

    auto todos = e.consultar(Entorno::de(kSeptiembre));
    QVERIFY(todos);
    QCOMPARE(todos.valor().cfdis.size(), 3);
    QVERIFY(!uuids(todos.valor()).contains(kU3));
    QVERIFY(!uuids(todos.valor()).contains(kU4));
    // kU1 sigue desde la solicitud visible (la eliminada no compite).
    for (const CfdiMensual& m : todos.valor().cfdis) {
        QVERIFY(m.solicitud != sid(eliminada));
    }
    // Conteos de pestanas y por mes coinciden con la lista en cada vista.
    for (const std::optional<TipoDescarga>& d :
         {std::optional<TipoDescarga>(), std::optional(TipoDescarga::Emitidos), std::optional(TipoDescarga::Recibidos)}) {
        auto r = e.consultar(Entorno::de(kSeptiembre, d));
        QVERIFY(r);
        QCOMPARE(r.valor().total, int(r.valor().cfdis.size()));
        QCOMPARE((*r.valor().conteos.porMes)[8], int(r.valor().cfdis.size()));
        QCOMPARE(r.valor().conteos.emitidos,
                 int(e.consultar(Entorno::de(kSeptiembre, TipoDescarga::Emitidos)).valor().cfdis.size()));
        QCOMPARE(r.valor().conteos.recibidos,
                 int(e.consultar(Entorno::de(kSeptiembre, TipoDescarga::Recibidos)).valor().cfdis.size()));
    }
    QCOMPARE(todos.valor().conteos.emitidos, 2);
    QCOMPARE(todos.valor().conteos.recibidos, 1);
}

void TestSqliteCatalogoMensual::catalogoFiltrosTextoTipoYPaginacion()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QString id = e.solicitud(Sol{});
    QVERIFY(!e.paqueteConCfdi(id, {{kU1, QStringLiteral("2026-09-01T10:00:00")}, {kU2, QStringLiteral("2026-09-02T10:00:00")}},
                              kAhora, QStringLiteral("I"), QStringLiteral("jose pena hernandez"))
                 .isEmpty());
    QVERIFY(!e.paqueteConCfdi(id, {{kU3, QStringLiteral("2026-09-03T10:00:00")}, {kU4, QStringLiteral("2026-09-04T10:00:00")}},
                              kAhora, QStringLiteral("E"))
                 .isEmpty());

    ConsultaCatalogoMensual texto = Entorno::de(kSeptiembre);
    texto.texto = QStringLiteral("  PEÑA ");
    auto rt = e.consultar(texto);
    QCOMPARE(uuids(rt.valor()), (QStringList{kU2, kU1}));
    QCOMPARE(rt.valor().total, 2);
    QCOMPARE(rt.valor().conteos.emitidos, 2);
    QCOMPARE((*rt.valor().conteos.porMes)[8], 2);
    ConsultaCatalogoMensual comodin = Entorno::de(kSeptiembre);
    comodin.texto = QStringLiteral("%");
    QCOMPARE(e.consultar(comodin).valor().total, 0);

    ConsultaCatalogoMensual tipo = Entorno::de(kSeptiembre);
    tipo.tipo = TipoComprobante::Egreso;
    QCOMPARE(uuids(e.consultar(tipo).valor()), (QStringList{kU4, kU3}));
    QCOMPARE(e.consultar(tipo).valor().conteos.emitidos, 2);

    ConsultaCatalogoMensual pagina = Entorno::de(kSeptiembre);
    pagina.orden = OrdenListaCfdi::FechaAscendente;
    pagina.limite = 2;
    pagina.desplazamiento = 1;
    auto rp = e.consultar(pagina);
    QCOMPARE(uuids(rp.valor()), (QStringList{kU2, kU3}));
    QCOMPARE(rp.valor().total, 4); // total sin paginar
    pagina.limite = 0;
    pagina.desplazamiento = 3;
    QCOMPARE(uuids(e.consultar(pagina).valor()), QStringList{kU4});
    pagina.limite = -1;
    QVERIFY(!e.consultar(pagina));
}

void TestSqliteCatalogoMensual::catalogoVinculoNoCanonicoNoEsGuiado()
{
    // Un vinculo de lote que no cumple canonicidad (escrito sin el
    // repositorio, p. ej. dato heredado o corrupto) no da prioridad de fuente.
    Entorno e;
    QVERIFY(e.preparar());
    Sol filtrada;
    filtrada.tipoComprobante = QStringLiteral("I");
    const QString idFiltrada = e.solicitud(filtrada);
    const QString idCanonica = e.solicitud(Sol{});   // vinculada a un lote de OTRO mes
    const QString idManual = e.solicitud(Sol{});     // sin lote, indexada antes
    const QString fecha = QStringLiteral("2026-09-10T10:00:00");
    QVERIFY(!e.paqueteConCfdi(idManual, {{kU1, fecha}}, QStringLiteral("2026-10-01T00:00:00.000Z")).isEmpty());
    QVERIFY(!e.paqueteConCfdi(idFiltrada, {{kU1, fecha}}, QStringLiteral("2026-10-05T00:00:00.000Z")).isEmpty());
    QVERIFY(!e.paqueteConCfdi(idCanonica, {{kU1, fecha}}, QStringLiteral("2026-10-06T00:00:00.000Z")).isEmpty());
    const QString loteSep = uuid::generarCanonico();
    const QString loteOct = uuid::generarCanonico();
    const QString lote = QStringLiteral("INSERT INTO lote_descarga_guiada VALUES (%1, %2, %3, %4)");
    const QString tramo = QStringLiteral("INSERT INTO lote_descarga_guiada_solicitud VALUES (%1, %2, 'emitidos', %3, %4)");
    QCOMPARE(ejecutarSql(e.prov(), lote.arg(citar(loteSep), citar(e.perfil), QStringLiteral("'2026-09'"), citar(kAhora))),
             QString());
    QCOMPARE(ejecutarSql(e.prov(), lote.arg(citar(loteOct), citar(e.perfil), QStringLiteral("'2026-10'"), citar(kAhora))),
             QString());
    QCOMPARE(ejecutarSql(e.prov(), tramo.arg(citar(loteSep), citar(idFiltrada), citar(kSeptiembre.inicioSat()),
                                             citar(kSeptiembre.finSat()))),
             QString());
    QCOMPARE(ejecutarSql(e.prov(), tramo.arg(citar(loteOct), citar(idCanonica), citar(kSeptiembre.inicioSat()),
                                             citar(kSeptiembre.finSat()))),
             QString());

    auto r = e.consultar(Entorno::de(kSeptiembre));
    QVERIFY2(r, r ? "" : qPrintable(r.error().mensaje));
    QCOMPARE(r.valor().cfdis.size(), 1);
    QCOMPARE(r.valor().cfdis.first().solicitud, sid(idManual)); // la indexacion mas antigua
    QVERIFY(!r.valor().cfdis.first().fuenteGuiada);

    // Control: un vinculo canonico del mes si gana aunque se indexo despues.
    const QString idGuiada = e.solicitud(Sol{});
    QVERIFY(!e.paqueteConCfdi(idGuiada, {{kU1, fecha}}, QStringLiteral("2026-10-07T00:00:00.000Z")).isEmpty());
    QVERIFY(e.enTransaccion([&] { return e.p->lotesDescargaGuiada().insertar(e.lote({{idGuiada, TipoDescarga::Emitidos}})); }));
    auto g = e.consultar(Entorno::de(kSeptiembre));
    QCOMPARE(g.valor().cfdis.first().solicitud, sid(idGuiada));
    QVERIFY(g.valor().cfdis.first().fuenteGuiada);
}

void TestSqliteCatalogoMensual::loteVariosTramosPorDireccion()
{
    // ADR 0020: una solicitud por hueco (inicial, interno y final) en una
    // direccion, mas una en la otra.
    Entorno e;
    QVERIFY(e.preparar());
    auto solicitud = [&](TipoDescarga d, const char* inicio, const char* fin) {
        Sol s;
        s.dir = d;
        s.inicio = QString::fromLatin1(inicio);
        s.fin = QString::fromLatin1(fin);
        const QString id = e.solicitud(s);
        return TramoLoteGuiado{d, sid(id), s.inicio, s.fin};
    };
    const TramoLoteGuiado final_ = solicitud(TipoDescarga::Emitidos, "2026-09-21T00:00:00", "2026-09-30T23:59:59");
    const TramoLoteGuiado inicial = solicitud(TipoDescarga::Emitidos, "2026-09-01T00:00:00", "2026-09-04T23:59:59");
    const TramoLoteGuiado interno = solicitud(TipoDescarga::Emitidos, "2026-09-11T00:00:00", "2026-09-11T23:59:59");
    const TramoLoteGuiado recibidos = solicitud(TipoDescarga::Recibidos, "2026-09-01T00:00:00", "2026-09-30T23:59:59");
    NuevoLoteGuiado lote = e.lote({});
    lote.tramos = {final_, recibidos, inicial, interno};
    QVERIFY(e.enTransaccion([&] { return e.p->lotesDescargaGuiada().insertar(lote); }));
    auto l = e.p->lotesDescargaGuiada().listar(pid(e.perfil), kSeptiembre);
    QVERIFY(l);
    QCOMPARE(l.valor().size(), 1);
    const QList<TramoLoteConsultado>& tramos = l.valor().first().tramos;
    QCOMPARE(tramos.size(), 4);
    QCOMPARE(tramos.at(0).tramo, inicial); // emitidos por fecha inicial
    QCOMPARE(tramos.at(1).tramo, interno);
    QCOMPARE(tramos.at(2).tramo, final_);
    QCOMPARE(tramos.at(3).tramo, recibidos);
    QCOMPARE(l.valor().first().resultado, ResultadoLote::Terminado);
    // Eliminar uno de los tramos de la direccion: eliminacion parcial.
    QVERIFY(e.eliminarSolicitud(interno.solicitud.texto()));
    auto tras = e.p->lotesDescargaGuiada().listar(pid(e.perfil), kSeptiembre);
    QVERIFY(tras.valor().first().eliminacionParcial);
    QCOMPARE(tras.valor().first().resultado, ResultadoLote::Terminado);

    // Traslape en una misma direccion (aunque sea de un segundo): rechazado
    // sin escribir nada. En direcciones distintas no hay traslape.
    const TramoLoteGuiado a = solicitud(TipoDescarga::Emitidos, "2026-09-01T00:00:00", "2026-09-10T00:00:00");
    const TramoLoteGuiado b = solicitud(TipoDescarga::Emitidos, "2026-09-10T00:00:00", "2026-09-15T00:00:00");
    NuevoLoteGuiado traslapado = e.lote({});
    traslapado.tramos = {b, a};
    auto r = e.enTransaccion([&] { return e.p->lotesDescargaGuiada().insertar(traslapado); });
    QVERIFY(!r && r.error().tipo == ErrorPersistencia::Tipo::Integridad);
    QCOMPARE(escalar(e.prov(), QStringLiteral("SELECT count(*) FROM lote_descarga_guiada")).toInt(), 1);
    const TramoLoteGuiado contiguo = solicitud(TipoDescarga::Emitidos, "2026-09-10T00:00:01", "2026-09-15T00:00:00");
    NuevoLoteGuiado valido = e.lote({});
    valido.tramos = {a, contiguo};
    QVERIFY(e.enTransaccion([&] { return e.p->lotesDescargaGuiada().insertar(valido); }));
    // La PK y la unicidad siguen impidiendo reutilizar una solicitud.
    NuevoLoteGuiado repetido = e.lote({});
    repetido.tramos = {b};
    QVERIFY(e.enTransaccion([&] { return e.p->lotesDescargaGuiada().insertar(repetido); }));
    NuevoLoteGuiado otraVez = e.lote({});
    otraVez.tramos = {b};
    auto u = e.enTransaccion([&] { return e.p->lotesDescargaGuiada().insertar(otraVez); });
    QVERIFY(!u && u.error().tipo == ErrorPersistencia::Tipo::Unicidad);
}
