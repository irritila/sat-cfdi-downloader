#include "infrastructure/persistence/sqlite/SqliteCfdiFilas.h"
#include "infrastructure/persistence/sqlite/SqliteConnectionProvider.h"
#include "infrastructure/persistence/sqlite/SqliteFilas.h"
#include "infrastructure/persistence/sqlite/SqliteRepositorios.h"
#include "infrastructure/persistence/sqlite/SqliteSoporte.h"

#include "domain/cfdi/IndiceCfdi.h"

#include <QHash>
#include <QSet>
#include <QSqlQuery>

#include <utility>

// T023.2: catalogo mensual (ADR 0019, D13) y evidencia de cobertura sobre las
// migraciones 006 y 007. Ningun valor de fila sale en errores ni logs.
//
// Deduplicacion sin funciones de ventana (DM4: SQLite >= 3.9): para cada UUID
// no vacio se toma la fila con la menor `clave` de fuente mediante min() con
// columnas "bare" (SQLite toma esas columnas de la fila del minimo, garantia
// documentada desde 3.7.11). La clave concatena campos de ancho fijo en el
// orden de ADR 0019: guiada ('0') antes que no guiada ('1'), actualizado_en
// del indice (ISO-8601 de 24 caracteres), id de paquete (UUID de 36) y
// posicion (10 digitos).

namespace satcfdi {

namespace {

// Universo deduplicado `unicos` del perfil entre [:desde, :hasta) por fecha
// del comprobante (sin el subrango [:exdesde, :exhasta) si `excluir`).
// Marcadores: :perfil, :desde, :hasta (y :exdesde, :exhasta), una vez cada uno.
QString cteUnicos(bool excluir)
{
    return QStringLiteral(
    "WITH base AS ("
    " SELECT c.paquete_solicitud_id AS paq, c.posicion AS pos, s.id AS sol, s.tipo_cfdi AS dir,"
    " g.solicitud_masiva_id IS NOT NULL AS guiada, c.fecha AS fecha, c.tipo_comprobante AS tipo,"
    " c.texto_busqueda AS texto, upper(trim(c.uuid)) AS u,"
    " (CASE WHEN g.solicitud_masiva_id IS NULL THEN '1' ELSE '0' END) || i.actualizado_en"
    " || c.paquete_solicitud_id || printf('%010d', c.posicion) AS clave"
    " FROM solicitud_masiva s"
    " JOIN cfdi_indexado c ON c.solicitud_masiva_id = s.id"
    " JOIN paquete_solicitud p ON p.id = c.paquete_solicitud_id AND p.eliminado_en IS NULL"
    " JOIN indice_cfdi_paquete i ON i.paquete_solicitud_id = c.paquete_solicitud_id"
    // Guiada solo si la solicitud vinculada es canonica del mes de su lote
    // (misma regla que esSolicitudCanonicaDelMes; tipo_solicitud_sat,
    // estado_comprobante_sat y operacion_sat ya los fija el CHECK de 001):
    // un vinculo incoherente no da prioridad en la deduplicacion.
    " LEFT JOIN lote_descarga_guiada_solicitud g ON g.solicitud_masiva_id = s.id"
    " AND g.tipo_cfdi = s.tipo_cfdi AND g.fecha_inicial_sat = s.fecha_inicial_sat"
    " AND g.fecha_final_sat = s.fecha_final_sat"
    " AND s.tipo_comprobante IS NULL AND s.complemento IS NULL AND s.rfc_receptores_json IS NULL"
    " AND ((s.tipo_cfdi = 'emitidos' AND s.rfc_receptor IS NULL AND s.rfc_emisor = s.rfc_solicitante)"
    " OR (s.tipo_cfdi = 'recibidos' AND s.rfc_emisor IS NULL AND s.rfc_receptor = s.rfc_solicitante))"
    " AND EXISTS (SELECT 1 FROM lote_descarga_guiada l WHERE l.id = g.lote_id"
    " AND l.perfil_sat_id = s.perfil_sat_id AND substr(s.fecha_inicial_sat, 1, 7) = l.mes"
    " AND substr(s.fecha_final_sat, 1, 7) = l.mes)"
    " WHERE s.perfil_sat_id = :perfil AND s.eliminado_en IS NULL"
    " AND EXISTS (SELECT 1 FROM perfil_sat ps WHERE ps.id = s.perfil_sat_id AND ps.eliminado_en IS NULL)"
    " AND c.fecha >= :desde AND c.fecha < :hasta")
           + (excluir ? QStringLiteral(" AND NOT (c.fecha >= :exdesde AND c.fecha < :exhasta)") : QString())
           + QStringLiteral(
    "), unicos AS ("
    " SELECT paq, pos, sol, dir, guiada, fecha, tipo, texto, min(clave) AS clave"
    " FROM base WHERE u IS NOT NULL AND u <> '' GROUP BY u"
    " UNION ALL"
    " SELECT paq, pos, sol, dir, guiada, fecha, tipo, texto, clave"
    " FROM base WHERE u IS NULL OR u = ''"
    ") ");
}

struct Filtros {
    std::optional<QString> direccion; // valor de tipo_cfdi
    std::optional<QString> tipo;      // clave de tipo de comprobante
    QString patron;                   // LIKE, vacio = sin texto
};

// " WHERE ..." sobre `x` (alias de unicos). Marcadores :dir, :tipo, :texto.
QString whereFiltros(const Filtros& f, bool conDireccion)
{
    QStringList condiciones;
    if (conDireccion && f.direccion) {
        condiciones.append(QStringLiteral("x.dir = :dir"));
    }
    if (f.tipo) {
        condiciones.append(QStringLiteral("x.tipo = :tipo"));
    }
    if (!f.patron.isEmpty()) {
        condiciones.append(QStringLiteral("x.texto LIKE :texto ESCAPE '\\'"));
    }
    return condiciones.isEmpty() ? QString() : QStringLiteral(" WHERE ") + condiciones.join(QStringLiteral(" AND "));
}

struct Rango {
    QString desde;
    QString hasta;
    std::optional<std::pair<QString, QString>> excluido;
};

void enlazar(QSqlQuery& q, const PerfilId& perfil, const Rango& rango, const Filtros& f, bool conDireccion)
{
    q.bindValue(QStringLiteral(":perfil"), sqlite::texto(perfil.texto()));
    q.bindValue(QStringLiteral(":desde"), sqlite::texto(rango.desde));
    q.bindValue(QStringLiteral(":hasta"), sqlite::texto(rango.hasta));
    if (rango.excluido) {
        q.bindValue(QStringLiteral(":exdesde"), sqlite::texto(rango.excluido->first));
        q.bindValue(QStringLiteral(":exhasta"), sqlite::texto(rango.excluido->second));
    }
    if (conDireccion && f.direccion) {
        q.bindValue(QStringLiteral(":dir"), sqlite::texto(*f.direccion));
    }
    if (f.tipo) {
        q.bindValue(QStringLiteral(":tipo"), sqlite::texto(*f.tipo));
    }
    if (!f.patron.isEmpty()) {
        q.bindValue(QStringLiteral(":texto"), sqlite::texto(f.patron));
    }
}

// Limite inferior de fecha del mes ("YYYY-MM-01"): compara como texto con
// `fecha` ISO del comprobante, sin depender de su sufijo de hora.
QString inicioFecha(MesCalendario mes)
{
    return mes.primerDia().toString(u"yyyy-MM-dd");
}

// Limite superior exclusivo del mes: inicio del siguiente, o un texto mayor
// que cualquier fecha de 9999-12 si no hay siguiente.
QString finExclusivo(MesCalendario mes)
{
    const auto siguiente = mes.siguiente();
    return siguiente ? inicioFecha(*siguiente) : QStringLiteral("9999-12-99");
}

} // namespace

SqliteCatalogoMensualRepository::SqliteCatalogoMensualRepository(SqliteConnectionProvider& proveedor)
    : m_proveedor(proveedor)
{
}

Resultado<CatalogoMensual, ErrorPersistencia>
SqliteCatalogoMensualRepository::consultar(const ConsultaCatalogoMensual& c)
{
    using R = Resultado<CatalogoMensual, ErrorPersistencia>;
    constexpr QStringView kContexto = u"cfdi_indexado.catalogo_mensual";
    if (c.limite < 0 || c.desplazamiento < 0) {
        return R::fallo(ErrorPersistencia::de(ErrorPersistencia::Tipo::Interno,
                                              QStringLiteral("cfdi_indexado.catalogo_mensual: argumento invalido")));
    }
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();

    Filtros f;
    if (c.direccion) {
        f.direccion = valorTipoCfdi(*c.direccion);
    }
    if (c.tipo) {
        // Desconocido no tiene clave: no coincide con ninguna fila.
        f.tipo = QString(codigoTipoComprobante(*c.tipo));
    }
    const QString texto = normalizarTextoBusqueda(c.texto);
    if (!texto.isEmpty()) {
        f.patron = sqlite::patronLike(texto);
    }
    const QString desdeMes = inicioFecha(c.mes);
    const QString hastaMes = finExclusivo(c.mes);
    CatalogoMensual catalogo;

    // 1. Pagina de la lista.
    {
        QSqlQuery q(db);
        q.setForwardOnly(true);
        QString sql = cteUnicos(false) + QStringLiteral("SELECT ") + sqlite::columnasCfdi(u"c")
                      + QStringLiteral(", x.dir, x.sol, x.guiada FROM unicos x JOIN cfdi_indexado c "
                                       "ON c.paquete_solicitud_id = x.paq AND c.posicion = x.pos")
                      + whereFiltros(f, true)
                      + (c.orden == OrdenListaCfdi::FechaDescendente
                             ? QStringLiteral(" ORDER BY x.fecha DESC, x.paq, x.pos")
                             : QStringLiteral(" ORDER BY x.fecha ASC, x.paq, x.pos"));
        if (c.limite > 0 || c.desplazamiento > 0) {
            sql += QStringLiteral(" LIMIT :limite OFFSET :desplazamiento");
        }
        if (auto r = sqlite::preparar(q, sql, kContexto); !r) {
            return R::fallo(std::move(r).error());
        }
        enlazar(q, c.perfil, Rango{desdeMes, hastaMes, std::nullopt}, f, true);
        if (c.limite > 0 || c.desplazamiento > 0) {
            q.bindValue(QStringLiteral(":limite"), sqlite::entero(c.limite > 0 ? c.limite : -1));
            q.bindValue(QStringLiteral(":desplazamiento"), sqlite::entero(c.desplazamiento));
        }
        if (auto r = sqlite::ejecutar(q, kContexto); !r) {
            return R::fallo(std::move(r).error());
        }
        while (q.next()) {
            auto fila = sqlite::leerCfdi(q);
            if (!fila) {
                return R::fallo(std::move(fila).error());
            }
            CfdiMensual m;
            m.cfdi = std::move(fila).valor();
            const auto direccion = tipoDescargaDesdeTipoCfdi(q.value(sqlite::kNumColumnasCfdi).toString());
            const auto solicitud = SolicitudId::desdeTexto(q.value(sqlite::kNumColumnasCfdi + 1).toString());
            if (!direccion || !solicitud) {
                return R::fallo(sqlite::filaIlegible(u"solicitud_masiva", u"tipo_cfdi"));
            }
            m.direccion = *direccion;
            m.solicitud = *solicitud;
            m.fuenteGuiada = q.value(sqlite::kNumColumnasCfdi + 2).toInt() != 0;
            catalogo.cfdis.append(std::move(m));
        }
    }

    // 2. Conteos. Si la lista no esta paginada ni filtrada por direccion, ya
    //    es el universo del mes: sus conteos por direccion salen de ella y el
    //    mes se excluye de la consulta anual. Si no, UNA consulta agrupada por
    //    (mes, direccion) sobre el anio (o solo el mes) con texto y tipo:
    //    - emitidos/recibidos: filas del mes pedido, sin filtrar direccion;
    //    - porMes: filas del anio de la direccion pedida (o de ambas).
    const bool mesDesdeLista = c.limite == 0 && c.desplazamiento == 0 && !c.direccion;
    if (mesDesdeLista) {
        for (const CfdiMensual& m : std::as_const(catalogo.cfdis)) {
            ++(m.direccion == TipoDescarga::Emitidos ? catalogo.conteos.emitidos : catalogo.conteos.recibidos);
        }
    }
    std::array<int, 12> porMes{};
    if (!mesDesdeLista || c.conConteosAnuales) {
        const MesCalendario desde = c.conConteosAnuales ? *MesCalendario::crear(c.mes.anio(), 1) : c.mes;
        const MesCalendario hasta = c.conConteosAnuales ? *MesCalendario::crear(c.mes.anio(), 12) : c.mes;
        Rango rango{inicioFecha(desde), finExclusivo(hasta), std::nullopt};
        if (mesDesdeLista) {
            rango.excluido = std::pair{desdeMes, hastaMes};
        }
        QSqlQuery q(db);
        q.setForwardOnly(true);
        const QString sql = cteUnicos(mesDesdeLista)
                            + QStringLiteral("SELECT substr(x.fecha, 6, 2), x.dir, count(*) FROM unicos x")
                            + whereFiltros(f, false) + QStringLiteral(" GROUP BY 1, 2");
        if (auto r = sqlite::preparar(q, sql, kContexto); !r) {
            return R::fallo(std::move(r).error());
        }
        enlazar(q, c.perfil, rango, f, false);
        if (auto r = sqlite::ejecutar(q, kContexto); !r) {
            return R::fallo(std::move(r).error());
        }
        while (q.next()) {
            bool ok = false;
            const int mes = q.value(0).toString().toInt(&ok);
            const auto direccion = tipoDescargaDesdeTipoCfdi(q.value(1).toString());
            if (!ok || mes < 1 || mes > 12 || !direccion) {
                return R::fallo(sqlite::filaIlegible(u"cfdi_indexado", u"fecha"));
            }
            const int n = q.value(2).toInt();
            if (mes == c.mes.mes()) {
                (*direccion == TipoDescarga::Emitidos ? catalogo.conteos.emitidos : catalogo.conteos.recibidos) += n;
            }
            if (!c.direccion || *c.direccion == *direccion) {
                porMes[std::size_t(mes - 1)] += n;
            }
        }
    }
    if (c.direccion) {
        catalogo.total = *c.direccion == TipoDescarga::Emitidos ? catalogo.conteos.emitidos : catalogo.conteos.recibidos;
    } else {
        catalogo.total = catalogo.conteos.emitidos + catalogo.conteos.recibidos;
    }
    if (c.conConteosAnuales) {
        if (mesDesdeLista) {
            porMes[std::size_t(c.mes.mes() - 1)] = catalogo.total;
        }
        catalogo.conteos.porMes = porMes;
    }
    return R::exito(std::move(catalogo));
}

Resultado<QList<EvidenciaSolicitudMensual>, ErrorPersistencia>
SqliteCatalogoMensualRepository::leerEvidencia(const PerfilId& perfil, MesCalendario mes, int versionParser)
{
    using R = Resultado<QList<EvidenciaSolicitudMensual>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"solicitud_masiva.evidencia_mensual";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlDatabase db = conexion.valor();
    // Solicitudes visibles del perfil visible cuyo rango intersecta el mes
    // (las fechas SAT comparan como texto: formato fijo).
    const QString kCriterio = QStringLiteral(
        "s.perfil_sat_id = :perfil AND s.eliminado_en IS NULL "
        "AND s.fecha_inicial_sat <= :fin AND s.fecha_final_sat >= :inicio "
        "AND EXISTS (SELECT 1 FROM perfil_sat ps WHERE ps.id = s.perfil_sat_id AND ps.eliminado_en IS NULL)");
    auto enlazarCriterio = [&](QSqlQuery& q) {
        q.bindValue(QStringLiteral(":perfil"), sqlite::texto(perfil.texto()));
        q.bindValue(QStringLiteral(":inicio"), sqlite::texto(mes.inicioSat()));
        q.bindValue(QStringLiteral(":fin"), sqlite::texto(mes.finSat()));
    };

    QList<EvidenciaSolicitudMensual> evidencia;
    QHash<QString, qsizetype> posicion;
    {
        QSqlQuery q(db);
        q.setForwardOnly(true);
        // Columnas sin alias: leerSolicitud lee por nombre.
        const QString sql = QStringLiteral("SELECT ") + sqlite::columnasSolicitud()
                            + QStringLiteral(" FROM solicitud_masiva s WHERE ") + kCriterio
                            + QStringLiteral(" ORDER BY s.creada_en, s.id");
        if (auto r = sqlite::preparar(q, sql, kContexto); !r) {
            return R::fallo(std::move(r).error());
        }
        enlazarCriterio(q);
        if (auto r = sqlite::ejecutar(q, kContexto); !r) {
            return R::fallo(std::move(r).error());
        }
        while (q.next()) {
            auto s = sqlite::leerSolicitud(q);
            if (!s) {
                return R::fallo(std::move(s).error());
            }
            EvidenciaSolicitudMensual e;
            e.solicitud = std::move(s).valor();
            posicion.insert(e.solicitud.id.texto(), evidencia.size());
            evidencia.append(std::move(e));
        }
    }
    if (evidencia.isEmpty()) {
        return R::exito(std::move(evidencia));
    }
    {
        QSqlQuery q(db);
        q.setForwardOnly(true);
        const QString sql = QStringLiteral(
                                "SELECT p.solicitud_masiva_id, p.id, p.estado_descarga, i.estado, i.version_parser "
                                "FROM paquete_solicitud p JOIN solicitud_masiva s ON s.id = p.solicitud_masiva_id "
                                "LEFT JOIN indice_cfdi_paquete i ON i.paquete_solicitud_id = p.id "
                                "WHERE p.eliminado_en IS NULL AND ")
                            + kCriterio + QStringLiteral(" ORDER BY p.id");
        if (auto r = sqlite::preparar(q, sql, kContexto); !r) {
            return R::fallo(std::move(r).error());
        }
        enlazarCriterio(q);
        if (auto r = sqlite::ejecutar(q, kContexto); !r) {
            return R::fallo(std::move(r).error());
        }
        while (q.next()) {
            const auto it = posicion.constFind(q.value(0).toString());
            if (it == posicion.cend()) {
                continue;
            }
            EvidenciaPaqueteMensual p;
            p.paqueteId = q.value(1).toString();
            const auto estado = estadoDescargaDesdeClave(q.value(2).toString());
            if (!estado) {
                return R::fallo(sqlite::filaIlegible(u"paquete_solicitud", u"estado_descarga"));
            }
            p.estadoDescarga = *estado;
            if (!q.value(3).isNull()) {
                p.indice = estadoIndiceDesdeClave(q.value(3).toString());
                p.indiceLegible = p.indice.has_value();
                p.indiceVigente = q.value(4).toInt() == versionParser;
            }
            evidencia[*it].paquetes.append(std::move(p));
        }
    }
    return R::exito(std::move(evidencia));
}

// T023.5: CFDI por solicitud fuente canonica, en todo el rango de fechas
// (cada UUID tiene una sola fecha, asi que deduplicar sobre todo el rango
// equivale a deduplicar mes por mes).
Resultado<QHash<SolicitudId, int>, ErrorPersistencia>
SqliteCatalogoMensualRepository::conteosPorSolicitud(const PerfilId& perfil)
{
    using R = Resultado<QHash<SolicitudId, int>, ErrorPersistencia>;
    constexpr QStringView kContexto = u"cfdi_indexado.conteos_por_solicitud";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    QSqlQuery q(conexion.valor());
    q.setForwardOnly(true);
    const QString sql = cteUnicos(false) + QStringLiteral("SELECT x.sol, count(*) FROM unicos x GROUP BY x.sol");
    if (auto r = sqlite::preparar(q, sql, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    enlazar(q, perfil, Rango{QStringLiteral("0000-01-01"), QStringLiteral("9999-12-99"), std::nullopt}, Filtros{},
            false);
    if (auto r = sqlite::ejecutar(q, kContexto); !r) {
        return R::fallo(std::move(r).error());
    }
    QHash<SolicitudId, int> conteos;
    while (q.next()) {
        const auto solicitud = SolicitudId::desdeTexto(q.value(0).toString());
        if (!solicitud) {
            return R::fallo(sqlite::filaIlegible(u"cfdi_indexado", u"solicitud_masiva_id"));
        }
        conteos.insert(*solicitud, q.value(1).toInt());
    }
    return R::exito(std::move(conteos));
}

// T026.1: instantanea de exportacion. Una transaccion diferida propia (salvo
// que el llamador ya tenga un UnitOfWork en este hilo): en WAL, todas las
// lecturas siguientes ven el mismo snapshot, de modo que lista y evidencia de
// todos los meses son coherentes entre si. consultar() y leerEvidencia() usan
// la misma conexion del hilo y por tanto la misma transaccion. Se cierra con
// COMMIT (o ROLLBACK si una lectura falla); no marca la transaccion en el
// proveedor porque no admite escrituras.
Resultado<InstantaneaExportacion, ErrorPersistencia>
SqliteCatalogoMensualRepository::capturarParaExportar(const PerfilId& perfil, const QList<MesCalendario>& meses,
                                                      int versionParser)
{
    using R = Resultado<InstantaneaExportacion, ErrorPersistencia>;
    constexpr QStringView kContexto = u"cfdi_indexado.instantanea_exportacion";
    auto conexion = sqlite::conexionLectura(m_proveedor);
    if (!conexion) {
        return R::fallo(std::move(conexion).error());
    }
    const bool propia = !m_proveedor.transaccionActiva();
    if (propia) {
        QSqlQuery q(conexion.valor());
        if (auto r = sqlite::ejecutarDirecto(q, QStringLiteral("BEGIN DEFERRED"), kContexto); !r) {
            return R::fallo(std::move(r).error());
        }
    }
    auto terminar = [&](bool confirmar) -> Resultado<Exito, ErrorPersistencia> {
        if (!propia) {
            return Resultado<Exito, ErrorPersistencia>::exito({});
        }
        QSqlQuery q(conexion.valor());
        if (confirmar) {
            auto r = sqlite::ejecutarDirecto(q, QStringLiteral("COMMIT"), kContexto);
            if (r) {
                return r;
            }
            (void)sqlite::ejecutarDirecto(q, QStringLiteral("ROLLBACK"), kContexto);
            return r;
        }
        return sqlite::ejecutarDirecto(q, QStringLiteral("ROLLBACK"), kContexto);
    };

    InstantaneaExportacion instantanea;
    QList<MesCalendario> leidos;
    for (const MesCalendario mes : meses) {
        if (leidos.contains(mes)) {
            continue;
        }
        leidos.append(mes);
        ConsultaCatalogoMensual c;
        c.perfil = perfil;
        c.mes = mes;
        c.orden = OrdenListaCfdi::FechaAscendente;
        c.conConteosAnuales = false;
        auto catalogo = consultar(c);
        if (!catalogo) {
            (void)terminar(false);
            return R::fallo(std::move(catalogo).error());
        }
        auto evidencia = leerEvidencia(perfil, mes, versionParser);
        if (!evidencia) {
            (void)terminar(false);
            return R::fallo(std::move(evidencia).error());
        }
        instantanea.meses.append(
            MesInstantaneaExportacion{mes, std::move(catalogo).valor().cfdis, std::move(evidencia).valor()});
    }
    // Ruta y huella de los paquetes fuente, en la misma transaccion.
    QSet<QString> usados;
    for (const MesInstantaneaExportacion& m : std::as_const(instantanea.meses)) {
        for (const CfdiMensual& c : m.cfdis) {
            usados.insert(c.cfdi.paqueteId);
        }
    }
    if (!usados.isEmpty()) {
        QSqlQuery q(conexion.valor());
        q.setForwardOnly(true);
        const QString sql = QStringLiteral(
            "SELECT p.id, p.ruta_local, i.tamano_bytes, i.modificado_en_ms, i.sha256 FROM paquete_solicitud p "
            "JOIN solicitud_masiva s ON s.id = p.solicitud_masiva_id AND s.eliminado_en IS NULL "
            "LEFT JOIN indice_cfdi_paquete i ON i.paquete_solicitud_id = p.id "
            "WHERE s.perfil_sat_id = :perfil AND p.eliminado_en IS NULL");
        auto r = sqlite::preparar(q, sql, kContexto);
        if (r) {
            q.bindValue(QStringLiteral(":perfil"), sqlite::texto(perfil.texto()));
            r = sqlite::ejecutar(q, kContexto);
        }
        if (!r) {
            (void)terminar(false);
            return R::fallo(std::move(r).error());
        }
        while (q.next()) {
            const QString id = q.value(0).toString();
            if (!usados.contains(id)) {
                continue;
            }
            PaqueteFuenteExportacion fuente;
            fuente.rutaLocal = q.value(1).isNull() ? QString() : q.value(1).toString();
            if (!q.value(4).isNull()) {
                fuente.huella = HuellaZip{q.value(2).toLongLong(), q.value(3).toLongLong(), q.value(4).toString()};
            }
            instantanea.paquetes.insert(id, std::move(fuente));
        }
    }
    if (auto r = terminar(true); !r) {
        return R::fallo(std::move(r).error());
    }
    return R::exito(std::move(instantanea));
}

} // namespace satcfdi
