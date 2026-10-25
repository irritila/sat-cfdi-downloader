// T026.1 (corte 3): conductor de exportacion sin UI con piezas reales.
// SQLite real (QTemporaryDir), ZIP de paquetes sinteticos reales indexados con
// calcularIndicePaquete + LectorZipLibzip + parsearCfdi,
// PlanificadorExportacion::planear, AsignadorNombresExportacion, relectura
// con LectorZipLibzip::leerEntrada, EscritorZipStreaming y
// DestinoExportacionPosix (publicar). El ZIP final se inspecciona con libzip.
// T026.2 reemplaza este conductor por ExportacionFacturasService; aqui solo
// se prueba que las piezas encajan (criterios 1 y 2 de T026.1).
//
// Datos sinteticos: RFC genericos del SAT (XAXX010101000, XEXX010101000) y
// UUID inventados.

#include "infrastructure/SqlitePruebasComun.h"

#include "application/cfdi/CalculoIndicePaquete.h"
#include "application/cfdi/PlanificadorExportacion.h"
#include "domain/cfdi/ExportacionFacturas.h"
#include "domain/cfdi/UuidCfdi.h"
#include "fakes/ZipCfdiSintetico.h"
#include "infrastructure/cfdi/xml/ParserCfdi.h"
#include "infrastructure/cfdi/zip/ArchivosZipCfdiFs.h"
#include "infrastructure/cfdi/zip/EscritorZipStreaming.h"
#include "infrastructure/cfdi/zip/LectorZipLibzip.h"
#include "infrastructure/storage/DestinoExportacionPosix.h"
#include "ports/repositories/CfdiCatalogRepository.h"
#include "ports/repositories/UnitOfWork.h"
#include "ports/storage/RutaPaquete.h"

#include <zip.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QTest>
#include <QTimeZone>

#include <sys/stat.h>

#include <memory>

using namespace satcfdi;
using namespace satcfdi::pruebas;

namespace {

const QString kRfc = QStringLiteral("XAXX010101000");
const QString kRfcContraparte = QStringLiteral("XEXX010101000");
const MesCalendario kJulio = *MesCalendario::crear(2026, 7);
const MesCalendario kAgosto = *MesCalendario::crear(2026, 8);
const MesCalendario kSeptiembre = *MesCalendario::crear(2026, 9);
const QDateTime kInstante = QDateTime(QDate(2026, 10, 20), QTime(18, 0), QTimeZone(QTimeZone::UTC));

QString uuidN(int n)
{
    return QStringLiteral("E0000000-0000-4000-8000-%1").arg(n, 12, 10, QLatin1Char('0'));
}

QByteArray sha256(const QByteArray& datos)
{
    return QCryptographicHash::hash(datos, QCryptographicHash::Sha256);
}

// Entradas de un ZIP leidas con libzip (nombre -> contenido), verificando
// consistencia (ZIP_CHECKCONS).
std::optional<QMap<QString, QByteArray>> leerZip(const QString& ruta)
{
    int error = 0;
    zip_t* zip = zip_open(QFile::encodeName(ruta).constData(), ZIP_RDONLY | ZIP_CHECKCONS, &error);
    if (!zip) {
        return std::nullopt;
    }
    QMap<QString, QByteArray> entradas;
    const zip_int64_t n = zip_get_num_entries(zip, 0);
    for (zip_int64_t i = 0; i < n; ++i) {
        zip_stat_t st;
        zip_stat_init(&st);
        if (zip_stat_index(zip, zip_uint64_t(i), 0, &st) != 0) {
            zip_discard(zip);
            return std::nullopt;
        }
        QByteArray contenido(qsizetype(st.size), Qt::Uninitialized);
        zip_file_t* f = zip_fopen_index(zip, zip_uint64_t(i), 0);
        if (!f || zip_fread(f, contenido.data(), st.size) != zip_int64_t(st.size)) {
            if (f) {
                zip_fclose(f);
            }
            zip_discard(zip);
            return std::nullopt;
        }
        zip_fclose(f);
        entradas.insert(QString::fromUtf8(st.name), contenido);
    }
    zip_discard(zip);
    return entradas;
}

// Perfil con solicitudes canonicas por mes y direccion, paquetes con ZIP
// reales en la raiz de paquetes e indice calculado por el codigo de produccion.
struct Entorno {
    QTemporaryDir dir;
    std::unique_ptr<SqlitePersistencia> p;
    QString perfil;
    QString raiz;
    QHash<QString, QString> rutaPorPaquete; // paqueteId -> ruta absoluta del ZIP
    QHash<QString, QByteArray> xmlPorUuid;  // XML fuente por UUID
    int siguienteSolicitud = 1;
    int siguientePaquete = 1;

    bool preparar()
    {
        if (!dir.isValid() || !inicializarBaseSqlite(rutaBase(dir))) {
            return false;
        }
        raiz = dir.filePath(QStringLiteral("paquetes"));
        p = std::make_unique<SqlitePersistencia>(rutaBase(dir));
        perfil = uuid::generarCanonico();
        return insertarPerfilFixture(p->proveedor(), perfil, kRfc).isEmpty();
    }

    ~Entorno()
    {
        if (p) {
            p->cerrarConexionDelHiloActual();
        }
    }

    // Solicitud canonica Terminada del mes completo; devuelve su id.
    QString solicitud(TipoDescarga d, MesCalendario mes)
    {
        const QString id = QStringLiteral("5e000000-0000-4000-8000-%1").arg(siguienteSolicitud++, 12, 10,
                                                                           QLatin1Char('0'));
        const bool emitidos = d == TipoDescarga::Emitidos;
        const QString error = ejecutarSql(
            p->proveedor(),
            QStringLiteral(
                "INSERT INTO solicitud_masiva (id, perfil_sat_id, id_solicitud_sat, tipo_cfdi, operacion_sat, "
                "rfc_solicitante, rfc_emisor, rfc_receptor, fecha_inicial_sat, fecha_final_sat, dedup_key, "
                "estado_local, cod_estatus_solicitud, estado_solicitud_sat, creada_en, envio_iniciado_en, enviada_en, "
                "ultima_verificacion_en) VALUES (%1, %2, %3, %4, %5, %6, %7, %8, %9, %10, %11, 'Enviada', '5000', "
                "'Terminada', %12, %12, %12, %12)")
                .arg(citar(id), citar(perfil), citar(id.toUpper()),
                     emitidos ? QStringLiteral("'emitidos'") : QStringLiteral("'recibidos'"),
                     emitidos ? QStringLiteral("'SolicitaDescargaEmitidos'")
                              : QStringLiteral("'SolicitaDescargaRecibidos'"),
                     citar(kRfc), emitidos ? citar(kRfc) : QStringLiteral("NULL"),
                     emitidos ? QStringLiteral("NULL") : citar(kRfc), citar(mes.inicioSat()))
                .arg(citar(mes.finSat()), citar(DedupKey::calcularV1(id.toUtf8()).texto()), citar(pruebas::kAhora)));
        if (!error.isEmpty()) {
            qWarning("fixture solicitud: %s", qPrintable(error));
            return {};
        }
        return id;
    }

    // Paquete Descargado de la solicitud con CFDI (uuid) del mes. Con
    // `escribirZip` false el ZIP no existe en disco (OrigenNoDisponible).
    QString paquete(const QString& solicitudId, MesCalendario mes, const QList<int>& uuids, bool emitidos,
                    bool escribirZip = true)
    {
        const QString id = uuid::generarCanonico();
        const QString idSat = QStringLiteral("PAQ_%1").arg(siguientePaquete++);
        const auto ruta = rutapaquete::derivarRutaRelativa({kRfc, mes.inicioSat(), solicitudId, idSat});
        if (!ruta) {
            return {};
        }
        const QString error =
            ejecutarSql(p->proveedor(),
                        QStringLiteral("INSERT INTO paquete_solicitud (id, solicitud_masiva_id, id_paquete_sat, "
                                       "estado_descarga, ruta_local, disponible_en, descarga_iniciada_en, "
                                       "descargado_en) VALUES (%1, %2, %3, 'Descargado', %4, %5, %5, %5)")
                            .arg(citar(id), citar(solicitudId), citar(idSat), citar(ruta.valor()), citar(pruebas::kAhora)));
        if (!error.isEmpty()) {
            qWarning("fixture paquete: %s", qPrintable(error));
            return {};
        }
        QList<std::pair<QByteArray, QByteArray>> entradas;
        for (const int n : uuids) {
            fakes::CfdiSintetico c;
            c.uuid = uuidN(n);
            c.folio = QString::number(n);
            c.fecha = QStringLiteral("%1-%2T10:00:00").arg(mes.texto()).arg(1 + n % 28, 2, 10, QLatin1Char('0'));
            c.rfcEmisor = emitidos ? kRfc : kRfcContraparte;
            const QByteArray xml = fakes::xmlCfdi40(c);
            xmlPorUuid.insert(c.uuid, xml);
            entradas.append({c.uuid.toUtf8() + ".xml", xml});
        }
        const QString absoluta = QDir(raiz).filePath(ruta.valor());
        rutaPorPaquete.insert(id, absoluta);
        if (escribirZip
            && (!QDir().mkpath(QFileInfo(absoluta).absolutePath())
                || !fakes::escribirArchivo(absoluta, fakes::crearZipSintetico(entradas)))) {
            return {};
        }
        return id;
    }

    // Indexa todos los paquetes con la regla de produccion (T015.2), uno a uno
    // y en instantes crecientes (la fuente canonica de un UUID repetido es la
    // indexada antes, ADR 0019 D13).
    bool indexar()
    {
        auto paquetes = p->catalogoCfdi().listarPaquetesIndexables(std::nullopt);
        if (!paquetes) {
            return false;
        }
        LectorZipLibzip lector;
        ArchivosZipCfdiFs archivos(raiz);
        EntornoCalculoIndice entorno{lector, archivos, &parsearCfdi, 1};
        // Orden por id de paquete SAT (orden de creacion de la fixture).
        QList<PaqueteIndexable> orden = paquetes.valor();
        std::sort(orden.begin(), orden.end(), [this](const PaqueteIndexable& a, const PaqueteIndexable& b) {
            return rutaPorPaquete.value(a.paqueteId) < rutaPorPaquete.value(b.paqueteId);
        });
        int segundo = 0;
        for (const PaqueteIndexable& paq : std::as_const(orden)) {
            const CalculoPaquete c =
                calcularIndicePaquete(paq, entorno, [] { return true; }, kInstante.addSecs(-3600 + segundo++));
            if (c.tipo != CalculoPaquete::Tipo::Reemplazar) {
                return false;
            }
            if (!p->unidadDeTrabajo().begin()) {
                return false;
            }
            auto r = p->catalogoCfdi().reemplazarIndicePaquete(c.indice);
            if (!r || !r.valor() || !p->unidadDeTrabajo().commit()) {
                (void)p->unidadDeTrabajo().rollback();
                return false;
            }
        }
        return true;
    }

    // Dos meses (agosto y septiembre) con emitidas y recibidas indexadas; el
    // UUID 3 esta en dos paquetes de septiembre emitidas. Devuelve los UUID
    // canonicos esperados por (direccion, mes).
    QMap<QString, QStringList> dosMeses()
    {
        const QString agoE = solicitud(TipoDescarga::Emitidos, kAgosto);
        const QString agoR = solicitud(TipoDescarga::Recibidos, kAgosto);
        const QString sepE = solicitud(TipoDescarga::Emitidos, kSeptiembre);
        const QString sepR = solicitud(TipoDescarga::Recibidos, kSeptiembre);
        paquete(agoE, kAgosto, {1, 2}, true);
        paquete(agoR, kAgosto, {10}, false);
        paquete(sepE, kSeptiembre, {3, 4}, true);
        paquete(sepE, kSeptiembre, {5, 3}, true); // UUID 3 repetido en otro paquete
        paquete(sepR, kSeptiembre, {11, 12}, false);
        return {
            {QStringLiteral("emitidas/2026-08"), {uuidN(1), uuidN(2)}},
            {QStringLiteral("emitidas/2026-09"), {uuidN(3), uuidN(4), uuidN(5)}},
            {QStringLiteral("recibidas/2026-08"), {uuidN(10)}},
            {QStringLiteral("recibidas/2026-09"), {uuidN(11), uuidN(12)}},
        };
    }
};

struct ResultadoExportacion {
    QString rutaZip;
    PlanExportacion plan;
};

// Conductor: plan -> asignador -> relectura y validacion del UUID -> escritor
// en streaming -> omitidas.txt -> publicar. Falla (QVERIFY) ante cualquier
// error; las omisiones de lectura son de T026.2 y aqui no se esperan.
std::optional<ResultadoExportacion> exportar(Entorno& e, const AlcanceExportacion& alcance, const QString& destino)
{
    PlanificadorExportacion planificador(e.p->catalogoMensual(), [] { return kInstante; }, 1);
    auto plan = planificador.planear(alcance);
    if (!plan) {
        qWarning("planear: %s", qPrintable(plan.error().mensaje));
        return std::nullopt;
    }
    DestinoExportacionPosix::Opciones opciones;
    opciones.directorioDatos = e.dir.filePath(QStringLiteral("datos"));
    if (!QDir().mkpath(opciones.directorioDatos) || !QDir().mkpath(destino)) {
        return std::nullopt;
    }
    DestinoExportacionPosix destinoPosix(opciones);
    const QString nombre = sanearNombreZip(nombreSugeridoZip(alcance, kInstante.date()));
    auto parcial = destinoPosix.reservar(destino, nombre);
    if (!parcial) {
        qWarning("reservar: %s", qPrintable(exportacion::claveEstable(parcial.error().codigo)));
        return std::nullopt;
    }
    std::unique_ptr<exportacion::ArchivoParcialExportacion> archivo = std::move(parcial).valor();
    EscritorZipStreaming escritor;
    if (!escritor.abrir(*archivo, kInstante)) {
        return std::nullopt;
    }
    LectorZipLibzip lector;
    AsignadorNombresExportacion asignador;
    ArchivosZipCfdiFs archivos(e.raiz);
    for (const FacturaPlaneada& f : plan.valor().facturas) {
        // Ruta y huella planeadas = las del ZIP real del paquete fuente.
        const ArchivoZipLocal zip = archivos.consultar(f.rutaPaquete);
        if (zip.estado != ArchivoZipLocal::Estado::Existe || zip.rutaAbsoluta != e.rutaPorPaquete.value(f.origen.paqueteId)
            || !f.huellaPaquete.mismosMetadatos(zip.tamano, zip.modificadoMs)
            || archivos.sha256(zip.rutaAbsoluta, [] { return true; }) != f.huellaPaquete.sha256
            || sha256(leerArchivo(zip.rutaAbsoluta)).toHex() != f.huellaPaquete.sha256.toLatin1()) {
            qWarning("huella o ruta planeada distinta");
            return std::nullopt;
        }
        auto xml = lector.leerEntrada(zip.rutaAbsoluta, f.origen.posicion);
        if (!xml || !xml.valor()) {
            qWarning("relectura fallo");
            return std::nullopt;
        }
        // Revalidacion D11: el XML releido tiene el UUID esperado.
        if (cfdi::normalizarUuid(parsearCfdi(*xml.valor()).resumen.uuid) != f.uuidCanonico) {
            qWarning("uuid distinto");
            return std::nullopt;
        }
        // El plan trae el nombre D18; se comprueba contra un asignador propio.
        if (asignador.siguiente(f.direccion, f.mes, f.uuidCanonico.value_or(QString())) != f.nombre) {
            qWarning("nombre distinto");
            return std::nullopt;
        }
        if (auto r = escritor.agregar(f.nombre.xml(), *xml.valor()); !r) {
            qWarning("agregar: %s", qPrintable(exportacion::claveEstable(r.error().codigo)));
            return std::nullopt;
        }
    }
    if (const auto omitidas = formatearOmitidas(plan.valor().omisiones)) {
        if (!escritor.agregar(QStringLiteral("omitidas.txt"), *omitidas)) {
            return std::nullopt;
        }
    }
    if (!escritor.cerrar()) {
        return std::nullopt;
    }
    auto publicado = archivo->publicar();
    if (!publicado || publicado.valor().advertenciaDurabilidad) {
        return std::nullopt;
    }
    return ResultadoExportacion{QDir(destino).filePath(nombre), std::move(plan).valor()};
}

} // namespace

class TestExportacionIntegracion : public QObject {
    Q_OBJECT

private slots:
    void rangoAmbasEntradasExactasYShaDelFuente();
    void soloEmitidasSoloCarpetaEmitidas();
    void mesConOrigenNoDisponibleProduceOmitidas();
};

void TestExportacionIntegracion::rangoAmbasEntradasExactasYShaDelFuente()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QMap<QString, QStringList> esperados = e.dosMeses();
    QVERIFY(e.indexar());

    const QString destino = e.dir.filePath(QStringLiteral("Usuario/Documentos"));
    const auto r = exportar(e, AlcanceRangoExportacion{*PerfilId::desdeTexto(e.perfil), kAgosto, kSeptiembre,
                                                        std::nullopt},
                            destino);
    QVERIFY(r);
    QCOMPARE(QFileInfo(r->rutaZip).fileName(), QStringLiteral("Archivo-Fiscal-2026-08_2026-09.zip"));
    QCOMPARE(r->plan.conteoPrevisto(), 8); // 9 XML fuente, el UUID 3 una vez
    QVERIFY(r->plan.omisiones.isEmpty());

    // Entradas exactas: una por CFDI canonico, sin omitidas.txt.
    QStringList nombresEsperados;
    for (auto it = esperados.cbegin(); it != esperados.cend(); ++it) {
        for (const QString& u : it.value()) {
            nombresEsperados.append(it.key() + QLatin1Char('/') + u + QStringLiteral(".xml"));
        }
    }
    nombresEsperados.sort();
    const auto entradas = leerZip(r->rutaZip);
    QVERIFY(entradas);
    QCOMPARE(entradas->keys(), nombresEsperados);
    // SHA-256 identico al XML del paquete fuente.
    for (auto it = entradas->cbegin(); it != entradas->cend(); ++it) {
        const QString uuid = QFileInfo(it.key()).completeBaseName();
        QVERIFY(e.xmlPorUuid.contains(uuid));
        QCOMPARE(sha256(it.value()), sha256(e.xmlPorUuid.value(uuid)));
    }

    // ZIP final 0600; en la carpeta destino solo el ZIP final y en la privada
    // ningun `.partial`.
    struct stat st {};
    QCOMPARE(::stat(QFile::encodeName(r->rutaZip).constData(), &st), 0);
    QCOMPARE(int(st.st_mode & 07777), 0600);
    QCOMPARE(QDir(destino).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
             QStringList{QFileInfo(r->rutaZip).fileName()});
    const QString privada = DestinoExportacionPosix::carpetaParciales(e.dir.filePath(QStringLiteral("datos")));
    QVERIFY(QDir(privada).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
}

void TestExportacionIntegracion::soloEmitidasSoloCarpetaEmitidas()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QMap<QString, QStringList> esperados = e.dosMeses();
    QVERIFY(e.indexar());

    const auto r = exportar(e, AlcanceRangoExportacion{*PerfilId::desdeTexto(e.perfil), kAgosto, kSeptiembre,
                                                        TipoDescarga::Emitidos},
                            e.dir.filePath(QStringLiteral("destino")));
    QVERIFY(r);
    const auto entradas = leerZip(r->rutaZip);
    QVERIFY(entradas);
    QStringList nombresEsperados;
    for (const QString& carpeta : {QStringLiteral("emitidas/2026-08"), QStringLiteral("emitidas/2026-09")}) {
        for (const QString& u : esperados.value(carpeta)) {
            nombresEsperados.append(carpeta + QLatin1Char('/') + u + QStringLiteral(".xml"));
        }
    }
    nombresEsperados.sort();
    QCOMPARE(entradas->keys(), nombresEsperados);
    for (const QString& n : entradas->keys()) {
        QVERIFY(n.startsWith(QStringLiteral("emitidas/")));
    }
}

void TestExportacionIntegracion::mesConOrigenNoDisponibleProduceOmitidas()
{
    Entorno e;
    QVERIFY(e.preparar());
    const QMap<QString, QStringList> esperados = e.dosMeses();
    // Julio: emitidas con un paquete indexado y otro cuyo ZIP falta
    // (OrigenNoDisponible); recibidas sin descargar.
    const QString julE = e.solicitud(TipoDescarga::Emitidos, kJulio);
    e.paquete(julE, kJulio, {20}, true);
    e.paquete(julE, kJulio, {21}, true, false);
    QVERIFY(e.indexar());
    QCOMPARE(escalar(e.p->proveedor(), QStringLiteral("SELECT count(*) FROM indice_cfdi_paquete "
                                                     "WHERE estado = 'OrigenNoDisponible'"))
                 .toInt(),
             1);

    const auto r = exportar(e, AlcanceRangoExportacion{*PerfilId::desdeTexto(e.perfil), kJulio, kSeptiembre,
                                                        std::nullopt},
                            e.dir.filePath(QStringLiteral("destino")));
    QVERIFY(r);
    QCOMPARE(QFileInfo(r->rutaZip).fileName(), QStringLiteral("Archivo-Fiscal-2026-07_2026-09.zip"));
    // El CFDI del paquete legible de julio se exporta; el del ZIP faltante no
    // tiene filas y se refleja como linea de mes (decision C1).
    QCOMPARE(r->plan.conteoPrevisto(), 9);
    const QList<OmisionExportacion> lineasMes = {
        OmisionMesExportacion{TipoDescarga::Emitidos, kJulio, MotivoOmisionMes::CoberturaIncompleta},
        OmisionMesExportacion{TipoDescarga::Recibidos, kJulio, MotivoOmisionMes::CoberturaIncompleta},
    };
    QCOMPARE(r->plan.omisiones, lineasMes);

    const auto entradas = leerZip(r->rutaZip);
    QVERIFY(entradas);
    QVERIFY(entradas->contains(QStringLiteral("omitidas.txt")));
    const QByteArray omitidas = entradas->value(QStringLiteral("omitidas.txt"));
    QCOMPARE(omitidas, QByteArray("emitidas/2026-07: cobertura incompleta\n"
                                  "recibidas/2026-07: cobertura incompleta\n"));
    QCOMPARE(std::optional<QByteArray>(omitidas), formatearOmitidas(r->plan.omisiones));
    QVERIFY(entradas->contains(QStringLiteral("emitidas/2026-07/") + uuidN(20) + QStringLiteral(".xml")));
    QVERIFY(!entradas->contains(QStringLiteral("emitidas/2026-07/") + uuidN(21) + QStringLiteral(".xml")));
    QCOMPARE(entradas->size(), 1 + 1 + 8); // omitidas.txt + julio + los dos meses
    for (const QString& carpeta : esperados.keys()) {
        for (const QString& u : esperados.value(carpeta)) {
            QVERIFY(entradas->contains(carpeta + QLatin1Char('/') + u + QStringLiteral(".xml")));
        }
    }
}

QTEST_GUILESS_MAIN(TestExportacionIntegracion)

#include "TestExportacionIntegracion.moc"
