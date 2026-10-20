// Benchmark del indice de CFDI (T015.2, criterio de rendimiento).
//
// 10,000 CFDI 4.0 sinteticos (20 paquetes de 500) parseados con parsearCfdi
// e indexados con SqliteCfdiCatalogRepository (una transaccion por paquete)
// en una base temporal con las migraciones embebidas (incluida 006). Mide,
// por el PersistenceDispatcher (como CatalogoCfdiService::listar), 20
// repeticiones en caliente (tras 2 de calentamiento) de:
//   1. lista completa de la solicitud ordenada por fecha (10,000 filas);
//   2. busqueda por texto normalizado ("pena": 1,000 filas);
//   3. busqueda por fragmento de UUID (1 fila).
// T023.2 agrega el catalogo mensual (CatalogoMensualRepository::consultar,
// deduplicado por UUID y con conteos por direccion y por mes del anio) sobre
// los mismos 10,000 CFDI de UN perfil en UN mes (septiembre de 2026; la
// solicitud es canonica del mes y esta vinculada a un lote guiado):
//   4. lista mensual completa (10,000 filas);
//   5. busqueda mensual por texto ("pena": 1,000 filas);
//   6. busqueda mensual por fragmento de UUID (1 fila).
// Imprime p50, p95 y maximo en ms. Criterio: p95 < 200 ms.
// Datos sinteticos: RFC genericos del SAT, nombres y UUID inventados.

#include "application/persistence/PersistenceDispatcher.h"
#include "domain/cfdi/IndiceCfdi.h"
#include "domain/common/UuidCanonico.h"
#include "domain/solicitudes/DedupKey.h"
#include "domain/solicitudes/SolicitudId.h"
#include "infrastructure/cfdi/xml/ParserCfdi.h"
#include "infrastructure/persistence/sqlite/SqliteConnectionProvider.h"
#include "infrastructure/persistence/sqlite/SqlitePersistencia.h"
#include "ports/repositories/CatalogoMensualRepository.h"
#include "ports/repositories/CfdiCatalogRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <functional>

using namespace satcfdi;

namespace {

constexpr int kPaquetes = 20;
constexpr int kCfdiPorPaquete = 500;
constexpr int kRepeticiones = 20;
constexpr int kCalentamiento = 2;
const QString kRfc = QStringLiteral("XAXX010101000");

QTextStream& salida()
{
    static QTextStream s(stdout);
    return s;
}

QByteArray xmlSintetico(int n)
{
    // Uno de cada 10 emisores con acentos (busqueda "pena").
    const QString nombre = n % 10 == 0 ? QStringLiteral("José Peña Hernández %1").arg(n % 50)
                                       : QStringLiteral("Proveedor Sintético %1").arg(n % 50);
    const QString uuid = QStringLiteral("B0000000-0000-4000-8000-%1").arg(n, 12, 10, QLatin1Char('0'));
    // T023.2: todos en septiembre de 2026 (un mes: 30 d / 10,000 = 259 s).
    const QString fecha = QDateTime(QDate(2026, 9, 1), QTime(0, 0), QTimeZone::UTC)
                              .addSecs(qint64(n) * 259)
                              .toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
    const QString tipo = QStringList{QStringLiteral("I"), QStringLiteral("E"), QStringLiteral("P")}.at(n % 3);
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<cfdi:Comprobante xmlns:cfdi=\"http://www.sat.gob.mx/cfd/4\" "
               "xmlns:tfd=\"http://www.sat.gob.mx/TimbreFiscalDigital\" Version=\"4.0\" Serie=\"S\" Folio=\"%1\" "
               "Fecha=\"%2\" Sello=\"U0VMTE8=\" FormaPago=\"03\" NoCertificado=\"00001000000000000001\" "
               "SubTotal=\"%3.00\" Moneda=\"MXN\" Total=\"%3.16\" TipoDeComprobante=\"%4\" Exportacion=\"01\" "
               "MetodoPago=\"PUE\" LugarExpedicion=\"64000\">"
               "<cfdi:Emisor Rfc=\"XAXX010101000\" Nombre=\"%5\" RegimenFiscal=\"601\"/>"
               "<cfdi:Receptor Rfc=\"XEXX010101000\" Nombre=\"RECEPTOR SINTETICO\" DomicilioFiscalReceptor=\"64000\" "
               "RegimenFiscalReceptor=\"616\" UsoCFDI=\"S01\"/>"
               "<cfdi:Conceptos><cfdi:Concepto ClaveProdServ=\"01010101\" Cantidad=\"1\" ClaveUnidad=\"H87\" "
               "Descripcion=\"Concepto\" ValorUnitario=\"%3.00\" Importe=\"%3.00\" ObjetoImp=\"01\"/></cfdi:Conceptos>"
               "<cfdi:Complemento><tfd:TimbreFiscalDigital Version=\"1.1\" UUID=\"%6\" FechaTimbrado=\"%2\" "
               "RfcProvCertif=\"XAXX010101000\" SelloCFD=\"U0VMTE8=\" NoCertificadoSAT=\"00001000000000000002\" "
               "SelloSAT=\"U0VMTE8=\"/></cfdi:Complemento></cfdi:Comprobante>")
        .arg(QString::number(n), fecha, QString::number(100 + n % 9000), tipo, nombre, uuid)
        .toUtf8();
}

bool sql(QSqlDatabase& db, const QString& texto)
{
    QSqlQuery q(db);
    if (!q.exec(texto)) {
        salida() << "error SQL: " << q.lastError().text() << Qt::endl;
        return false;
    }
    return true;
}

double percentil(QList<double> muestras, double p)
{
    std::sort(muestras.begin(), muestras.end());
    const auto rango = static_cast<qsizetype>(std::ceil(p * double(muestras.size())));
    return muestras.at(std::clamp<qsizetype>(rango - 1, 0, muestras.size() - 1));
}

struct Medicion {
    QString nombre;
    QList<double> ms;
    qsizetype filas = 0;
};

// Consulta medida: devuelve filas, o -1 ante error (ya impreso).
using Consulta = std::function<qsizetype()>;

Medicion medir(PersistenceDispatcher& dispatcher, const QString& nombre, const Consulta& consulta)
{
    Medicion m;
    m.nombre = nombre;
    for (int i = 0; i < kCalentamiento + kRepeticiones; ++i) {
        QElapsedTimer reloj;
        reloj.start();
        QFuture<qsizetype> f = dispatcher.despachar<qsizetype>(consulta);
        f.waitForFinished();
        const double ms = double(reloj.nsecsElapsed()) / 1.0e6;
        const qsizetype filas = f.result();
        if (filas < 0) {
            m.ms.clear();
            return m;
        }
        m.filas = filas;
        if (i >= kCalentamiento) {
            m.ms.append(ms);
        }
    }
    return m;
}

Consulta porSolicitud(CfdiCatalogRepository& catalogo, const SolicitudId& solicitud, const FiltroListaCfdi& filtro)
{
    return [&catalogo, solicitud, filtro]() -> qsizetype {
        const auto r = catalogo.listarCfdi(solicitud, filtro);
        if (!r.esExito()) {
            salida() << "error de consulta: " << r.error().mensaje << Qt::endl;
            return -1;
        }
        return r.valor().size();
    };
}

Consulta mensual(CatalogoMensualRepository& catalogo, const ConsultaCatalogoMensual& consulta)
{
    return [&catalogo, consulta]() -> qsizetype {
        const auto r = catalogo.consultar(consulta);
        if (!r.esExito()) {
            salida() << "error de consulta mensual: " << r.error().mensaje << Qt::endl;
            return -1;
        }
        if (r.valor().total != r.valor().cfdis.size() || !r.valor().conteos.porMes) {
            salida() << "conteos incoherentes" << Qt::endl;
            return -1;
        }
        return r.valor().cfdis.size();
    };
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) {
        salida() << "no se pudo crear el directorio temporal" << Qt::endl;
        return 1;
    }
    const QString ruta = dir.filePath(QStringLiteral("benchmark.sqlite3"));
    const auto inicio = inicializarBaseSqlite(ruta);
    if (!inicio) {
        salida() << "no se pudo inicializar la base: " << inicio.error().mensaje << Qt::endl;
        return 1;
    }

    SqlitePersistencia persistencia(ruta);
    PersistenceDispatcher dispatcher;
    CfdiCatalogRepository& catalogo = persistencia.catalogoCfdi();
    CatalogoMensualRepository& catalogoMensual = persistencia.catalogoMensual();
    QString perfilId;
    UnitOfWork& uow = persistencia.unidadDeTrabajo();
    const SolicitudId solicitud = SolicitudId::generar();

    // Fixtures e indexacion en el hilo del dispatcher (conexion propia).
    double msIndexacion = 0;
    double msParseo = 0;
    QFuture<bool> generacion = dispatcher
                  .despachar<bool>([&]() -> bool {
                      auto conexion = persistencia.proveedor().conexion();
                      if (!conexion) {
                          return false;
                      }
                      QSqlDatabase db = conexion.valor();
                      const QString perfil = uuid::generarCanonico();
                      perfilId = perfil;
                      const QString ahora = QStringLiteral("2026-10-11T12:00:00.000Z");
                      if (!sql(db, QStringLiteral("INSERT INTO perfil_sat (id, rfc, nombre, activo, creado_en, "
                                                  "actualizado_en) VALUES ('%1', '%2', 'Benchmark', 1, '%3', '%3')")
                                       .arg(perfil, kRfc, ahora))
                          || !sql(db, QStringLiteral(
                                          "INSERT INTO solicitud_masiva (id, perfil_sat_id, id_solicitud_sat, "
                                          "tipo_cfdi, operacion_sat, rfc_solicitante, rfc_emisor, fecha_inicial_sat, "
                                          "fecha_final_sat, dedup_key, estado_local, cod_estatus_solicitud, "
                                          "estado_solicitud_sat, creada_en, envio_iniciado_en, enviada_en, "
                                          "ultima_verificacion_en) VALUES ('%1', '%2', '%3', 'emitidos', "
                                          "'SolicitaDescargaEmitidos', '%4', '%4', '2026-09-01T00:00:00', "
                                          "'2026-09-30T23:59:59', '%5', 'Enviada', '5000', 'Terminada', '%6', '%6', "
                                          "'%6', '%6')")
                                          .arg(solicitud.texto(), perfil, solicitud.texto().toUpper(), kRfc,
                                               DedupKey::calcularV1("benchmark").texto(), ahora))) {
                          return false;
                      }
                      // T023.2: la solicitud pertenece a un lote guiado del mes.
                      const QString lote = uuid::generarCanonico();
                      if (!sql(db, QStringLiteral("INSERT INTO lote_descarga_guiada VALUES ('%1', '%2', '2026-09', "
                                                  "'%3')")
                                       .arg(lote, perfil, ahora))
                          || !sql(db, QStringLiteral("INSERT INTO lote_descarga_guiada_solicitud VALUES ('%1', '%2', "
                                                     "'emitidos', '2026-09-01T00:00:00', '2026-09-30T23:59:59')")
                                          .arg(lote, solicitud.texto()))) {
                          return false;
                      }
                      int n = 0;
                      for (int p = 0; p < kPaquetes; ++p) {
                          const QString paquete = uuid::generarCanonico();
                          if (!sql(db, QStringLiteral("INSERT INTO paquete_solicitud (id, solicitud_masiva_id, "
                                                      "id_paquete_sat, estado_descarga, ruta_local, disponible_en, "
                                                      "descarga_iniciada_en, descargado_en) VALUES ('%1', '%2', "
                                                      "'PAQ_%3', 'Descargado', 'x/p%3.zip', '%4', '%4', '%4')")
                                           .arg(paquete, solicitud.texto(), QString::number(p), ahora))) {
                              return false;
                          }
                          IndicePaquete indice;
                          indice.paqueteId = paquete;
                          indice.estado = EstadoIndicePaquete::Indexado;
                          indice.huella = HuellaZip{1000 + p, 1700000000000 + p, QString(64, QLatin1Char('a'))};
                          indice.versionParser = kVersionParserCfdi;
                          indice.actualizadoEn = QDateTime::currentDateTimeUtc();
                          QElapsedTimer parseo;
                          parseo.start();
                          for (int i = 0; i < kCfdiPorPaquete; ++i, ++n) {
                              ResultadoParseoCfdi r = parsearCfdi(xmlSintetico(n));
                              CfdiIndexado c;
                              c.posicion = i;
                              c.textoBusqueda = textoBusquedaCfdi(r.resumen);
                              c.resumen = std::move(r.resumen);
                              indice.cfdis.append(std::move(c));
                          }
                          indice.conteos.xmlLeidos = kCfdiPorPaquete;
                          msParseo += double(parseo.nsecsElapsed()) / 1.0e6;
                          QElapsedTimer escritura;
                          escritura.start();
                          if (!uow.begin() || !catalogo.reemplazarIndicePaquete(indice).valor() || !uow.commit()) {
                              return false;
                          }
                          msIndexacion += double(escritura.nsecsElapsed()) / 1.0e6;
                      }
                      return true;
                  });
    generacion.waitForFinished();
    const bool ok = generacion.resultCount() == 1 && generacion.result();
    if (!ok) {
        salida() << "no se pudieron generar los datos" << Qt::endl;
        return 1;
    }

    QList<Medicion> mediciones;
    mediciones.append(medir(dispatcher, QStringLiteral("lista por fecha (desc)"), porSolicitud(catalogo, solicitud, {})));
    FiltroListaCfdi pena;
    pena.texto = QStringLiteral("PEÑA");
    mediciones.append(
        medir(dispatcher, QStringLiteral("busqueda texto \"PEÑA\""), porSolicitud(catalogo, solicitud, pena)));
    FiltroListaCfdi uuid;
    uuid.texto = QStringLiteral("8000-000000004321");
    mediciones.append(medir(dispatcher, QStringLiteral("busqueda UUID parcial"), porSolicitud(catalogo, solicitud, uuid)));

    // T023.2: catalogo mensual del perfil (deduplicado, conteos por direccion
    // y por mes del anio incluidos en cada medicion).
    ConsultaCatalogoMensual mes;
    mes.perfil = *PerfilId::desdeTexto(perfilId);
    mes.mes = *MesCalendario::crear(2026, 9);
    mediciones.append(medir(dispatcher, QStringLiteral("mensual: lista del mes"), mensual(catalogoMensual, mes)));
    ConsultaCatalogoMensual mesPena = mes;
    mesPena.texto = QStringLiteral("PEÑA");
    mediciones.append(
        medir(dispatcher, QStringLiteral("mensual: busqueda texto \"PEÑA\""), mensual(catalogoMensual, mesPena)));
    ConsultaCatalogoMensual mesUuid = mes;
    mesUuid.texto = QStringLiteral("8000-000000004321");
    mediciones.append(
        medir(dispatcher, QStringLiteral("mensual: busqueda UUID parcial"), mensual(catalogoMensual, mesUuid)));

    dispatcher.despachar<void>(std::function<void()>([&persistencia] { persistencia.cerrarConexionDelHiloActual(); }));
    dispatcher.cerrar();

    QTextStream& out = salida();
    out << "benchmark-cfdi (T015.2, T023.2): " << kPaquetes << " paquetes x " << kCfdiPorPaquete << " = "
        << kPaquetes * kCfdiPorPaquete << " CFDI; " << kRepeticiones << " repeticiones en caliente ("
        << kCalentamiento << " de calentamiento)" << Qt::endl;
    out << "equipo: " << QSysInfo::prettyProductName() << " " << QSysInfo::currentCpuArchitecture()
        << "; SQLite " << inicio.valor().versionSqlite << "; Qt " << qVersion() << Qt::endl;
    out << QStringLiteral("generacion: parseo %1 ms total; escritura %2 ms total (%3 ms por paquete)")
               .arg(msParseo, 0, 'f', 1)
               .arg(msIndexacion, 0, 'f', 1)
               .arg(msIndexacion / kPaquetes, 0, 'f', 1)
        << Qt::endl;
    bool cumple = true;
    for (const Medicion& m : std::as_const(mediciones)) {
        if (m.ms.isEmpty()) {
            return 1;
        }
        const double p50 = percentil(m.ms, 0.50);
        const double p95 = percentil(m.ms, 0.95);
        const double max = *std::max_element(m.ms.cbegin(), m.ms.cend());
        cumple = cumple && p95 < 200.0;
        out << QStringLiteral("%1: filas %2; p50 %3 ms; p95 %4 ms; max %5 ms")
                   .arg(m.nombre)
                   .arg(m.filas)
                   .arg(p50, 0, 'f', 2)
                   .arg(p95, 0, 'f', 2)
                   .arg(max, 0, 'f', 2)
            << Qt::endl;
    }
    out << (cumple ? "criterio p95 < 200 ms: CUMPLE" : "criterio p95 < 200 ms: NO CUMPLE") << Qt::endl;
    return cumple ? 0 : 2;
}
