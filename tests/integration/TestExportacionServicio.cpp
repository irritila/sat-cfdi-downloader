// T026.2: ExportacionFacturasService con piezas reales (LectorZipLibzip,
// ArchivosZipCfdiFs, parsearCfdi, EscritorZipStreaming, DestinoExportacionPosix
// o FakeDestinoExportacion, RenderizadorPdfCfdi o FakeCfdiPdfRenderer) y, para
// el criterio 1, SQLite real con PlanificadorExportacion y los puertos por
// PersistenceDispatcher que usara el composition root. El hilo y el executor
// se prueban con LectorCfdiCompuerta y el OperacionExecutor real con
// FakeOperacionesSat (tests/unit/EntornoOperaciones.h).
//
// Datos sinteticos: RFC genericos del SAT (XAXX010101000, XEXX010101000) y
// UUID inventados.

#include "TestExportacionServicioComun.h"

#include "infrastructure/SqlitePruebasComun.h"
#include "unit/EntornoOperaciones.h"

#include "application/cfdi/CalculoIndicePaquete.h"
#include "application/cfdi/PlanificadorExportacion.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "fakes/FakeCfdiPdfRenderer.h"
#include "fakes/FakeDestinoExportacion.h"
#include "fakes/LectorCfdiCompuerta.h"
#include "infrastructure/cfdi/pdf/RenderizadorPdfCfdi.h"
#include "infrastructure/storage/DestinoExportacionPosix.h"
#include "ports/repositories/CfdiCatalogRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QPointer>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QMutex>
#include <QPdfDocument>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSignalSpy>

#include <sys/stat.h>

#include <atomic>
#include <stdexcept>

using namespace satcfdi;
using namespace exportacionpruebas;

namespace {

const QString kCarpetaFake = QStringLiteral("/destino");

// --- Captura de logs (D20) -------------------------------------------------

QMutex g_mutexLogs;
QStringList g_logs;
QtMessageHandler g_anterior = nullptr;

void capturarLog(QtMsgType tipo, const QMessageLogContext& contexto, const QString& mensaje)
{
    {
        QMutexLocker l(&g_mutexLogs);
        g_logs.append(QString::fromLatin1(contexto.category ? contexto.category : "") + QLatin1Char(' ') + mensaje);
    }
    if (g_anterior) {
        g_anterior(tipo, contexto, mensaje);
    }
}

struct CapturaLogs {
    CapturaLogs()
    {
        QMutexLocker l(&g_mutexLogs);
        g_logs.clear();
        g_anterior = qInstallMessageHandler(capturarLog);
    }
    ~CapturaLogs() { qInstallMessageHandler(g_anterior); }
    QStringList lineas() const
    {
        QMutexLocker l(&g_mutexLogs);
        return g_logs;
    }
};

// --- Fixture SQLite (criterio 1) --------------------------------------------

struct EntornoSqlite {
    QTemporaryDir dir;
    std::unique_ptr<SqlitePersistencia> p;
    QString perfil;
    QString raiz;
    QHash<QString, QByteArray> xmlPorUuid;
    int siguienteSolicitud = 1;
    int siguientePaquete = 1;

    bool preparar()
    {
        if (!dir.isValid() || !inicializarBaseSqlite(pruebas::rutaBase(dir))) {
            return false;
        }
        raiz = dir.filePath(QStringLiteral("paquetes"));
        p = std::make_unique<SqlitePersistencia>(pruebas::rutaBase(dir));
        perfil = uuid::generarCanonico();
        return pruebas::insertarPerfilFixture(p->proveedor(), perfil, kRfc).isEmpty();
    }

    ~EntornoSqlite()
    {
        if (p) {
            p->cerrarConexionDelHiloActual();
        }
    }

    QString solicitud(TipoDescarga d, MesCalendario mes)
    {
        using namespace pruebas;
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

    QString paquete(const QString& solicitudId, MesCalendario mes, const QList<int>& uuids, bool emitidos)
    {
        using namespace pruebas;
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
            return {};
        }
        QList<std::pair<QByteArray, QByteArray>> entradas;
        for (const int n : uuids) {
            fakes::CfdiSintetico c;
            c.uuid = uuidN(n);
            c.folio = QString::number(n);
            c.fecha = QStringLiteral("%1-%2T10:00:00").arg(mes.texto()).arg(1 + n % 28, 2, 10, QLatin1Char('0'));
            c.rfcEmisor = emitidos ? kRfc : QStringLiteral("XEXX010101000");
            const QByteArray xml = fakes::xmlCfdi40(c);
            xmlPorUuid.insert(c.uuid, xml);
            entradas.append({c.uuid.toUtf8() + ".xml", xml});
        }
        const QString absoluta = QDir(raiz).filePath(ruta.valor());
        if (!QDir().mkpath(QFileInfo(absoluta).absolutePath())
            || !fakes::escribirArchivo(absoluta, fakes::crearZipSintetico(entradas))) {
            return {};
        }
        return id;
    }

    // Indice con la regla de produccion (T015.2), en instantes crecientes.
    bool indexar(const QStringList& ordenPaquetes)
    {
        auto paquetes = p->catalogoCfdi().listarPaquetesIndexables(std::nullopt);
        if (!paquetes) {
            return false;
        }
        LectorZipLibzip lector;
        ArchivosZipCfdiFs archivos(raiz);
        EntornoCalculoIndice entorno{lector, archivos, &parsearCfdi, 1};
        int segundo = 0;
        for (const QString& id : ordenPaquetes) {
            const auto it = std::find_if(paquetes.valor().cbegin(), paquetes.valor().cend(),
                                         [&id](const PaqueteIndexable& x) { return x.paqueteId == id; });
            if (it == paquetes.valor().cend()) {
                return false;
            }
            const CalculoPaquete c =
                calcularIndicePaquete(*it, entorno, [] { return true; }, kInstante.addSecs(-3600 + segundo++));
            if (c.tipo != CalculoPaquete::Tipo::Reemplazar || !p->unidadDeTrabajo().begin()) {
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
};

// Servicio sobre el dispatcher real, con los puertos de produccion.
struct ServicioSqlite {
    EntornoSqlite& e;
    PersistenceDispatcher dispatcher;
    PlanificadorExportacion planificador;
    ArchivosZipCfdiFs archivos;
    LectorZipLibzip lector;
    std::unique_ptr<DestinoExportacionPosix> destino;
    std::unique_ptr<ExportacionFacturasService> servicio;

    explicit ServicioSqlite(EntornoSqlite& entorno)
        : e(entorno)
        , planificador(entorno.p->catalogoMensual(), [] { return kInstante; }, 1)
        , archivos(entorno.raiz)
    {
        DestinoExportacionPosix::Opciones o;
        o.directorioDatos = e.dir.filePath(QStringLiteral("datos"));
        QDir().mkpath(o.directorioDatos);
        destino = std::make_unique<DestinoExportacionPosix>(o);
        PuertosExportacionFacturas pu = puertos(*destino, lector, archivos, {});
        pu.planear = ExportacionFacturasService::planearPorDispatcher(dispatcher, planificador);
        pu.paqueteExportable =
            ExportacionFacturasService::paqueteExportablePorCatalogo(dispatcher, e.p->catalogoCfdi());
        servicio = std::make_unique<ExportacionFacturasService>(std::move(pu));
    }

    ~ServicioSqlite()
    {
        servicio.reset();
        SqlitePersistencia* p = e.p.get();
        esperarFuturo(dispatcher.despachar<int>([p] {
            p->cerrarConexionDelHiloActual();
            return 0;
        }));
        dispatcher.cerrar();
    }

    std::optional<ResultadoExportacion> exportar(const AlcanceExportacion& alcance, const QString& carpeta,
                                                 PlanExportacion* planUsado = nullptr)
    {
        const auto plan = esperarFuturo(servicio->planear(alcance));
        if (!plan || !plan->esExito()) {
            return std::nullopt;
        }
        if (planUsado) {
            *planUsado = plan->valor();
        }
        QDir().mkpath(carpeta);
        return esperarFuturo(servicio->iniciar(plan->valor(), carpeta, nombreSugeridoZip(alcance, kInstante.date()),
                                               false));
    }
};

QStringList nombresXml(const QMap<QString, QStringList>& porCarpeta)
{
    QStringList r;
    for (auto it = porCarpeta.cbegin(); it != porCarpeta.cend(); ++it) {
        for (const QString& u : it.value()) {
            r.append(it.key() + QLatin1Char('/') + u + QStringLiteral(".xml"));
        }
    }
    r.sort();
    return r;
}

// Lineas de CFDI de omitidas.txt (tres componentes antes de ": ").
QStringList lineasCfdi(const QByteArray& omitidas)
{
    QStringList r;
    for (const QString& l : QString::fromUtf8(omitidas).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (l.section(QStringLiteral(": "), 0, 0).count(QLatin1Char('/')) == 2) {
            r.append(l);
        }
    }
    return r;
}

} // namespace

class TestExportacionServicio : public QObject {
    Q_OBJECT

private slots:
    // Criterio 1
    void rangoAmbasZipExactoShaYDeduplicado();
    void eliminacionLocalAntesDeLeerSeOmitePorDispatcher();
    // Criterio 2
    void pdfJuntoAlXmlYSinPdf();
    void pdfTextoHostilSeDibujaLiteral();
    // Criterio 3
    void omisionesDeLecturaYOmitidasTxtExacto();
    // Criterio 4
    void hiloPropioFueraDelExecutorYCancelacion();
    void cancelarTrasPublicarNoCambiaResultado();
    void planSinFacturasNoReserva();
    // Contrato del servicio
    void destinoExistenteEsperaOtroNombre();
    void cerrarDescartaParcialYUneHilo();
    void limpiezaInicialFallidaAborta();
    // Revision R (A1-A3)
    void planearSiempreTermina();
    void eliminacionEntreLecturaYAgregar();
    void destructorConLectorBloqueadoTienePlazo();
    // Criterio 7
    void logsSinDatosSonda();
};

// --- Criterio 1 ----------------------------------------------------------------

void TestExportacionServicio::rangoAmbasZipExactoShaYDeduplicado()
{
    EntornoSqlite e;
    QVERIFY(e.preparar());
    const QString agoE = e.solicitud(TipoDescarga::Emitidos, kAgosto);
    const QString agoR = e.solicitud(TipoDescarga::Recibidos, kAgosto);
    const QString sepE = e.solicitud(TipoDescarga::Emitidos, kSeptiembre);
    const QString sepR = e.solicitud(TipoDescarga::Recibidos, kSeptiembre);
    const QStringList paquetes = {
        e.paquete(agoE, kAgosto, {1, 2}, true),         e.paquete(agoR, kAgosto, {10}, false),
        e.paquete(sepE, kSeptiembre, {3, 4}, true),     e.paquete(sepE, kSeptiembre, {5, 3}, true), // UUID 3 repetido
        e.paquete(sepR, kSeptiembre, {11, 12}, false),
    };
    QVERIFY(!paquetes.contains(QString()));
    QVERIFY(e.indexar(paquetes));
    const QMap<QString, QStringList> esperados = {
        {QStringLiteral("emitidas/2026-08"), {uuidN(1), uuidN(2)}},
        {QStringLiteral("emitidas/2026-09"), {uuidN(3), uuidN(4), uuidN(5)}},
        {QStringLiteral("recibidas/2026-08"), {uuidN(10)}},
        {QStringLiteral("recibidas/2026-09"), {uuidN(11), uuidN(12)}},
    };

    ServicioSqlite s(e);
    const QString destino = e.dir.filePath(QStringLiteral("Usuario/Documentos"));
    const PerfilId perfil = *PerfilId::desdeTexto(e.perfil);
    const auto r = s.exportar(AlcanceRangoExportacion{perfil, kAgosto, kSeptiembre, std::nullopt}, destino);
    QVERIFY(r);
    QCOMPARE(r->estado, ResultadoExportacion::Estado::Publicada);
    QCOMPARE(r->exportadas, 8); // 9 XML fuente; el UUID 3 una vez
    QCOMPARE(r->omitidas, 0);
    QCOMPARE(r->sinPdf, 0);
    QCOMPARE(r->nombrePublicado, QStringLiteral("Archivo-Fiscal-2026-08_2026-09.zip"));

    const auto entradas = leerZipArchivo(QDir(destino).filePath(r->nombrePublicado));
    QVERIFY(entradas);
    QCOMPARE(entradas->keys(), nombresXml(esperados)); // exactas, sin omitidas.txt
    for (auto it = entradas->cbegin(); it != entradas->cend(); ++it) {
        const QString uuid = QFileInfo(it.key()).completeBaseName();
        QVERIFY(e.xmlPorUuid.contains(uuid));
        QCOMPARE(sha256(it.value()), sha256(e.xmlPorUuid.value(uuid))); // bytes del paquete fuente
    }
    // Solo el ZIP final en el destino; ningun `.partial` en la carpeta privada.
    QCOMPARE(QDir(destino).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
             QStringList{r->nombrePublicado});
    const QString privada = DestinoExportacionPosix::carpetaParciales(e.dir.filePath(QStringLiteral("datos")));
    QVERIFY(QDir(privada).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());

    // Solo Emitidas: solo entradas emitidas/.
    const QString destino2 = e.dir.filePath(QStringLiteral("destino2"));
    const auto soloE =
        s.exportar(AlcanceRangoExportacion{perfil, kAgosto, kSeptiembre, TipoDescarga::Emitidos}, destino2);
    QVERIFY(soloE);
    QCOMPARE(soloE->estado, ResultadoExportacion::Estado::Publicada);
    const auto entradasE = leerZipArchivo(QDir(destino2).filePath(soloE->nombrePublicado));
    QVERIFY(entradasE);
    QCOMPARE(entradasE->keys(), nombresXml({{QStringLiteral("emitidas/2026-08"), esperados.value(QStringLiteral("emitidas/2026-08"))},
                                            {QStringLiteral("emitidas/2026-09"), esperados.value(QStringLiteral("emitidas/2026-09"))}}));
}

// C3: la visibilidad se revalida por el dispatcher; una solicitud eliminada
// localmente despues de planear se omite como "ya no disponible".
void TestExportacionServicio::eliminacionLocalAntesDeLeerSeOmitePorDispatcher()
{
    EntornoSqlite e;
    QVERIFY(e.preparar());
    const QString agoE = e.solicitud(TipoDescarga::Emitidos, kAgosto);
    const QString agoR = e.solicitud(TipoDescarga::Recibidos, kAgosto);
    const QStringList paquetes = {e.paquete(agoE, kAgosto, {1, 2}, true), e.paquete(agoR, kAgosto, {10}, false)};
    QVERIFY(e.indexar(paquetes));

    ServicioSqlite s(e);
    const auto plan = esperarFuturo(
        s.servicio->planear(AlcanceMesExportacion{*PerfilId::desdeTexto(e.perfil), kAgosto, std::nullopt}));
    QVERIFY(plan && plan->esExito());
    QCOMPARE(plan->valor().conteoPrevisto(), 3);
    QCOMPARE(pruebas::ejecutarSql(e.p->proveedor(),
                                  QStringLiteral("UPDATE solicitud_masiva SET eliminado_en = %1 WHERE id = %2")
                                      .arg(pruebas::citar(pruebas::kAhora), pruebas::citar(agoR))),
             QString());
    const QString destino = e.dir.filePath(QStringLiteral("destino"));
    QVERIFY(QDir().mkpath(destino));
    const auto r = esperarFuturo(s.servicio->iniciar(plan->valor(), destino, QStringLiteral("agosto.zip"), false));
    QVERIFY(r);
    QCOMPARE(r->estado, ResultadoExportacion::Estado::Publicada);
    QCOMPARE(r->exportadas, 2);
    QCOMPARE(r->omitidas, 1);
    const auto entradas = leerZipArchivo(QDir(destino).filePath(QStringLiteral("agosto.zip")));
    QVERIFY(entradas);
    QCOMPARE(entradas->value(QStringLiteral("omitidas.txt")),
             (QStringLiteral("recibidas/2026-08/") + uuidN(10) + QStringLiteral(": ya no disponible\n")).toUtf8());
    QVERIFY(!entradas->contains(QStringLiteral("recibidas/2026-08/") + uuidN(10) + QStringLiteral(".xml")));
}

// --- Criterio 2 ----------------------------------------------------------------

void TestExportacionServicio::pdfJuntoAlXmlYSinPdf()
{
    Banco b;
    QVERIFY(b.preparar());
    const Banco::Paquete p = b.paquete({xml40(1), xml40(2), xml32(3)});
    QVERIFY(!p.id.isEmpty());
    PlanExportacion plan;
    plan.facturas = {b.factura(p, 0, 1), b.factura(p, 1, 2), b.factura(p, 2, 3)};
    const QString c = QStringLiteral("emitidas/2026-09/");

    // a) Renderer real: cada XML representable con su PDF del mismo nombre;
    //    el CFDI 3.2 (VersionNoSoportada) solo con XML y "sin PDF".
    {
        fakes::FakeDestinoExportacion destino;
        int creados = 0;
        ExportacionFacturasService s(puertos(destino, b.lector, *b.archivos, [&creados] {
            ++creados;
            return std::make_unique<RenderizadorPdfCfdi>();
        }));
        const auto r = esperarFuturo(s.iniciar(plan, kCarpetaFake, QStringLiteral("con-pdf.zip"), true));
        QVERIFY(r);
        QCOMPARE(r->estado, ResultadoExportacion::Estado::Publicada);
        QCOMPARE(creados, 1); // un renderer propio por exportacion (D13)
        QCOMPARE(r->exportadas, 3);
        QCOMPARE(r->pdfExportados, 2);
        QCOMPARE(r->sinPdf, 1);
        QCOMPARE(r->omitidas, 0);
        const auto zip = leerZipBytes(*destino.archivo(kCarpetaFake, QStringLiteral("con-pdf.zip")));
        QVERIFY(zip);
        QCOMPARE(zip->keys(), (QStringList{c + uuidN(1) + QStringLiteral(".pdf"), c + uuidN(1) + QStringLiteral(".xml"), c + uuidN(2) + QStringLiteral(".pdf"),
                                           c + uuidN(2) + QStringLiteral(".xml"), c + uuidN(3) + QStringLiteral(".xml"), QStringLiteral("omitidas.txt")}));
        QVERIFY(zip->value(c + uuidN(1) + QStringLiteral(".pdf")).startsWith("%PDF-"));
        QCOMPARE(zip->value(QStringLiteral("omitidas.txt")), (c + uuidN(3) + QStringLiteral(": sin PDF\n")).toUtf8());
        QCOMPARE(sha256(zip->value(c + uuidN(3) + QStringLiteral(".xml"))), sha256(xml32(3)));
    }
    // b) Renderer que falla: solo XML; cada uno "sin PDF". VersionNoSoportada
    //    no llega al renderer.
    {
        fakes::FakeDestinoExportacion destino;
        auto estado = std::make_shared<fakes::FakeCfdiPdfRenderer::Estado>();
        ExportacionFacturasService s(
            puertos(destino, b.lector, *b.archivos, fakes::FakeCfdiPdfRenderer::fabrica(estado)));
        const auto r = esperarFuturo(s.iniciar(plan, kCarpetaFake, QStringLiteral("falla.zip"), true));
        QVERIFY(r);
        QCOMPARE(r->estado, ResultadoExportacion::Estado::Publicada);
        QCOMPARE(estado->creados.load(), 1);
        QCOMPARE(estado->llamadas.load(), 2);
        QCOMPARE(r->exportadas, 3);
        QCOMPARE(r->pdfExportados, 0);
        QCOMPARE(r->sinPdf, 3);
        const auto zip = leerZipBytes(*destino.archivo(kCarpetaFake, QStringLiteral("falla.zip")));
        QVERIFY(zip);
        QCOMPARE(zip->keys(), (QStringList{c + uuidN(1) + QStringLiteral(".xml"), c + uuidN(2) + QStringLiteral(".xml"), c + uuidN(3) + QStringLiteral(".xml"),
                                           QStringLiteral("omitidas.txt")}));
        const QByteArray omitidas = zip->value(QStringLiteral("omitidas.txt"));
        QCOMPARE(omitidas, (c + uuidN(1) + QStringLiteral(": sin PDF\n") + c + uuidN(2) + QStringLiteral(": sin PDF\n")
                            + c + uuidN(3) + QStringLiteral(": sin PDF\n"))
                               .toUtf8());
        QCOMPARE(lineasCfdi(omitidas).filter(QStringLiteral(": sin PDF")).size(), r->sinPdf);
    }
    // c) Sin PDF: ningun `.pdf` ni renderer.
    {
        fakes::FakeDestinoExportacion destino;
        auto estado = std::make_shared<fakes::FakeCfdiPdfRenderer::Estado>();
        ExportacionFacturasService s(
            puertos(destino, b.lector, *b.archivos, fakes::FakeCfdiPdfRenderer::fabrica(estado)));
        const auto r = esperarFuturo(s.iniciar(plan, kCarpetaFake, QStringLiteral("sin-pdf.zip"), false));
        QVERIFY(r);
        QCOMPARE(r->estado, ResultadoExportacion::Estado::Publicada);
        QCOMPARE(estado->creados.load(), 0);
        QCOMPARE(r->sinPdf, 0);
        const auto zip = leerZipBytes(*destino.archivo(kCarpetaFake, QStringLiteral("sin-pdf.zip")));
        QVERIFY(zip);
        QCOMPARE(zip->keys(), (QStringList{c + uuidN(1) + QStringLiteral(".xml"), c + uuidN(2) + QStringLiteral(".xml"), c + uuidN(3) + QStringLiteral(".xml")}));
    }
}

// Texto con marcado y URL en el XML: el renderer real lo dibuja como texto
// literal. Evidencia de "sin red": (1) el texto extraido del PDF (Qt Pdf)
// contiene el marcado tal cual, o sea que nada lo interpreto como HTML (no
// hay QTextDocument ni cargador de recursos que resuelva `<img src>`); (2) el
// PDF no tiene acciones /URI, /Launch ni /GoToR; (3) la prueba hermana
// TestExportacionServicioSinRed exporta el mismo CFDI en un proceso que no
// carga QtNetwork (comprobado con dyld), de modo que ninguna peticion es
// posible.
void TestExportacionServicio::pdfTextoHostilSeDibujaLiteral()
{
    Banco b;
    QVERIFY(b.preparar());
    QByteArray xml = xml40(7);
    const QByteArray hostil = "&lt;b&gt;negrita&lt;/b&gt; &lt;img src=http://127.0.0.1:9/x.png&gt; file:///etc/hosts";
    xml.replace("Descripcion=\"Concepto sintetico\"", "Descripcion=\"" + hostil + "\"");
    QVERIFY(xml.contains(hostil));
    const Banco::Paquete p = b.paquete({xml});
    PlanExportacion plan;
    plan.facturas = {b.factura(p, 0, 7)};
    fakes::FakeDestinoExportacion destino;
    ExportacionFacturasService s(
        puertos(destino, b.lector, *b.archivos, [] { return std::make_unique<RenderizadorPdfCfdi>(); }));
    const auto r = esperarFuturo(s.iniciar(plan, kCarpetaFake, QStringLiteral("hostil.zip"), true));
    QVERIFY(r);
    QCOMPARE(r->pdfExportados, 1);
    const auto zip = leerZipBytes(*destino.archivo(kCarpetaFake, QStringLiteral("hostil.zip")));
    QVERIFY(zip);
    QByteArray pdf = zip->value(QStringLiteral("emitidas/2026-09/") + uuidN(7) + QStringLiteral(".pdf"));
    QVERIFY(pdf.startsWith("%PDF-"));
    QVERIFY(!pdf.contains("/URI") && !pdf.contains("/Launch") && !pdf.contains("/GoToR"));

    QBuffer buffer(&pdf);
    QVERIFY(buffer.open(QIODevice::ReadOnly));
    QPdfDocument doc;
    doc.load(&buffer);
    QTRY_COMPARE(doc.status(), QPdfDocument::Status::Ready);
    // D13 (seguridad 4): metadatos del PDF sin RFC, nombres ni UUID.
    using M = QPdfDocument::MetaDataField;
    for (const M campo : {M::Title, M::Author, M::Subject, M::Keywords, M::Creator, M::Producer}) {
        const QString valor = doc.metaData(campo).toString();
        for (const QString& sonda : {kRfc, QStringLiteral("XEXX010101000"), QStringLiteral("EMISOR SINTETICO"),
                                     QStringLiteral("RECEPTOR SINTETICO"), uuidN(7)}) {
            QVERIFY2(!valor.contains(sonda, Qt::CaseInsensitive), qPrintable(valor));
        }
    }
    QCOMPARE(doc.metaData(M::Title).toString(), QStringLiteral("Visualización de CFDI"));
    QString texto;
    for (int i = 0; i < doc.pageCount(); ++i) {
        texto += doc.getAllText(i).text();
    }
    texto.remove(QRegularExpression(QStringLiteral("\\s")));
    QVERIFY2(texto.contains(QStringLiteral("<b>negrita</b>")), qPrintable(texto.left(400)));
    QVERIFY(texto.contains(QStringLiteral("<imgsrc=http://127.0.0.1:9/x.png>")));
    QVERIFY(texto.contains(QStringLiteral("file:///etc/hosts")));
}

// --- Criterio 3 ----------------------------------------------------------------

void TestExportacionServicio::omisionesDeLecturaYOmitidasTxtExacto()
{
    Banco b;
    QVERIFY(b.preparar());
    const Banco::Paquete p0 = b.paquete({xml40(1)});      // cambia durante la lectura
    const Banco::Paquete p1 = b.paquete({xml40(2)});      // se exporta (sin PDF)
    const Banco::Paquete p2 = b.paquete({xml40(3)});      // desaparece antes de leer
    const Banco::Paquete p3 = b.paquete({xml40(4)});      // cambia de huella antes de leer
    const Banco::Paquete p4 = b.paquete({xml40(5)});      // entrada ausente (posicion 3)
    const Banco::Paquete p5 = b.paquete({xml40(6)});      // ZIP corrupto
    const Banco::Paquete p6 = b.paquete({xml40(7)});      // UUID distinto (plan: 70)
    const Banco::Paquete p7 = b.paquete({"<<< no es XML"}); // XML ilegible
    const Banco::Paquete p8 = b.paquete({xml40(9)}, SolicitudId::generar(), kAgosto); // solicitud eliminada
    const Banco::Paquete p9 = b.paquete({xml40(10)});     // paquete no visible (dispatcher)
    for (const Banco::Paquete* p : {&p0, &p1, &p2, &p3, &p4, &p5, &p6, &p7, &p8, &p9}) {
        QVERIFY(!p->id.isEmpty());
    }
    // ZIP corrupto con la huella planeada (el indice lo vio asi).
    QVERIFY(fakes::escribirArchivo(p5.absoluta, QByteArray("PK\x03\x04 corrupto")));

    PlanExportacion plan;
    plan.facturas = {
        b.factura(p0, 0, 1),  b.factura(p1, 0, 2), b.factura(p2, 0, 3), b.factura(p3, 0, 4),
        b.factura(p4, 3, 5),  b.factura(p5, 0, 6), b.factura(p6, 0, 70), b.factura(p7, 0, 8),
        b.factura(p8, 0, 9, TipoDescarga::Recibidos, kAgosto), b.factura(p9, 0, 10),
    };
    plan.omisiones = {
        OmisionFacturaExportacion{TipoDescarga::Emitidos, kSeptiembre, uuidN(11),
                                  MotivoOmisionFactura::NoDisponibleEnEsteEquipo},
        OmisionMesExportacion{TipoDescarga::Emitidos, kSeptiembre, MotivoOmisionMes::DescargasPendientes},
        OmisionMesExportacion{TipoDescarga::Recibidos, kAgosto, MotivoOmisionMes::CoberturaIncompleta},
    };
    // Cambios despues de planear.
    QVERIFY(QFile::remove(p2.absoluta));
    QVERIFY(b.reescribir(p3, {xml40(4), xml40(40)}));

    fakes::LectorCfdiCompuerta compuerta(b.lector);
    compuerta.cerrar();
    int consultas = 0;
    const ConsultaPaqueteExportableFn visible = [&consultas, &p9](const SolicitudId&, const QString& paquete) {
        ++consultas;
        return QtFuture::makeReadyValueFuture(Resultado<bool, ErrorPersistencia>::exito(paquete != p9.id));
    };
    fakes::FakeDestinoExportacion destino;
    auto renderer = std::make_shared<fakes::FakeCfdiPdfRenderer::Estado>();
    ExportacionFacturasService s(
        puertos(destino, compuerta, *b.archivos, fakes::FakeCfdiPdfRenderer::fabrica(renderer), visible));
    auto futuro = s.iniciar(plan, kCarpetaFake, QStringLiteral("omisiones.zip"), true);
    // Bloqueada en la lectura de p0: el ZIP cambia y la solicitud de p8 se
    // elimina localmente mientras tanto (D19).
    QVERIFY(compuerta.esperarBloqueo());
    QVERIFY(b.reescribir(p0, {xml40(1), xml40(100)}));
    s.notificarSolicitudEliminada(p8.solicitud);
    compuerta.liberar();
    const auto r = esperarFuturo(futuro);
    QVERIFY(r);
    QCOMPARE(r->estado, ResultadoExportacion::Estado::Publicada);
    QVERIFY(consultas <= 10);

    const QString e = QStringLiteral("emitidas/2026-09");
    const QString cambio = QStringLiteral(": cambió desde que se indexó");
    const QString ya = QStringLiteral(": ya no disponible");
    const QString ilegible = QStringLiteral(": ilegible");
    const QStringList esperadas = {
        e + QStringLiteral(": descargas pendientes"),
        e + QLatin1Char('/') + uuidN(1) + cambio,
        e + QLatin1Char('/') + uuidN(2) + QStringLiteral(": sin PDF"),
        e + QLatin1Char('/') + uuidN(3) + ya,
        e + QLatin1Char('/') + uuidN(4) + cambio,
        e + QLatin1Char('/') + uuidN(5) + ya,
        e + QLatin1Char('/') + uuidN(6) + ilegible,
        e + QLatin1Char('/') + uuidN(8) + ilegible,
        e + QLatin1Char('/') + uuidN(10) + ya,
        e + QLatin1Char('/') + uuidN(11) + QStringLiteral(": no disponible en este equipo"),
        e + QLatin1Char('/') + uuidN(70) + cambio,
        QStringLiteral("recibidas/2026-08: cobertura incompleta"),
        QStringLiteral("recibidas/2026-08/") + uuidN(9) + ya,
    };
    const auto zip = leerZipBytes(*destino.archivo(kCarpetaFake, QStringLiteral("omisiones.zip")));
    QVERIFY(zip);
    QCOMPARE(zip->keys(), (QStringList{e + QLatin1Char('/') + uuidN(2) + QStringLiteral(".xml"), QStringLiteral("omitidas.txt")}));
    const QByteArray omitidas = zip->value(QStringLiteral("omitidas.txt"));
    QCOMPARE(omitidas, (esperadas.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8()); // byte a byte (D17)

    // Conteos de la app = lineas de CFDI por motivo.
    const QStringList cfdi = lineasCfdi(omitidas);
    QCOMPARE(r->exportadas, 1);
    QCOMPARE(r->omitidas, int(cfdi.size() - cfdi.filter(QStringLiteral(": sin PDF")).size()));
    QCOMPARE(r->omitidas, 10);
    QCOMPARE(r->sinPdf, int(cfdi.filter(QStringLiteral(": sin PDF")).size()));
    QCOMPARE(r->sinPdf, 1);
    QCOMPARE(r->lineasMes, 2);
    QCOMPARE(destino.parcialesVivos(), 0);
}

// --- Criterio 4 ----------------------------------------------------------------

void TestExportacionServicio::hiloPropioFueraDelExecutorYCancelacion()
{
    Banco b;
    QVERIFY(b.preparar());
    const Banco::Paquete p = b.paquete({xml40(1), xml40(2)});
    PlanExportacion plan;
    plan.facturas = {b.factura(p, 0, 1), b.factura(p, 1, 2)};

    fakes::LectorCfdiCompuerta compuerta(b.lector);
    compuerta.cerrar();
    fakes::FakeDestinoExportacion destino;
    ExportacionFacturasService s(puertos(destino, compuerta, *b.archivos, {}));
    QSignalSpy terminadas(&s, &ExportacionFacturasService::terminada);
    auto futuro = s.iniciar(plan, kCarpetaFake, QStringLiteral("bloqueada.zip"), false);
    QVERIFY(compuerta.esperarBloqueo());

    // Hilo propio: ni el de la app ni el del executor.
    QThread* hiloLectura = compuerta.hilo();
    QVERIFY(hiloLectura);
    QVERIFY(hiloLectura != QCoreApplication::instance()->thread());
    QCOMPARE(hiloLectura, s.hilo());

    // Una operacion SAT fake en el executor real termina mientras la
    // exportacion sigue bloqueada.
    pruebasT007::Entorno ejecucion;
    const SolicitudId id = ejecucion.sembrar(EstadoLocal::Enviada).id;
    const auto verificacion = pruebasT007::esperar(ejecucion.ejecutor->verificar(id, OrigenLog::Worker));
    QVERIFY(verificacion);
    QVERIFY(compuerta.bloqueado());
    QVERIFY(!futuro.isFinished());
    QVERIFY(!ejecucion.sat.hilos().isEmpty());
    QVERIFY(!ejecucion.sat.hilos().contains(hiloLectura));

    // Otra exportacion durante la primera no inicia otra.
    const auto otra = esperarFuturo(s.iniciar(plan, kCarpetaFake, QStringLiteral("otra.zip"), false));
    QVERIFY(otra);
    QCOMPARE(otra->estado, ResultadoExportacion::Estado::Error);
    QCOMPARE(otra->error->codigo, ErrorExportacion::Codigo::EnCurso);
    QCOMPARE(destino.reservas(), 1);
    QVERIFY(s.enCurso());

    // Cancelar dos veces: un solo resultado Cancelada, sin ZIP ni `.partial`.
    s.cancelar();
    s.cancelar();
    compuerta.liberar();
    const auto r = esperarFuturo(futuro);
    QVERIFY(r);
    QCOMPARE(r->estado, ResultadoExportacion::Estado::Cancelada);
    QVERIFY(!r->error);
    QTRY_COMPARE(terminadas.size(), 1);
    s.cancelar();
    QTest::qWait(20);
    QCOMPARE(terminadas.size(), 1);
    QCOMPARE(destino.parcialesVivos(), 0);
    QCOMPARE(destino.descartes(), 1);
    QVERIFY(destino.archivos(kCarpetaFake).isEmpty());
    QVERIFY(!s.enCurso());
}

void TestExportacionServicio::cancelarTrasPublicarNoCambiaResultado()
{
    Banco b;
    QVERIFY(b.preparar());
    const Banco::Paquete p = b.paquete({xml40(1)});
    PlanExportacion plan;
    plan.facturas = {b.factura(p, 0, 1)};
    fakes::FakeDestinoExportacion destino;
    ExportacionFacturasService s(puertos(destino, b.lector, *b.archivos, {}));
    QSignalSpy terminadas(&s, &ExportacionFacturasService::terminada);
    QSignalSpy progreso(&s, &ExportacionFacturasService::progreso);
    // cancelar() en el instante del rename (ya se decidio publicar).
    destino.antesDeRenombrar = [&s] { QMetaObject::invokeMethod(&s, [&s] { s.cancelar(); }, Qt::QueuedConnection); };
    auto futuro = s.iniciar(plan, kCarpetaFake, QStringLiteral("publicada.zip"), false);
    const auto r = esperarFuturo(futuro);
    QVERIFY(r);
    QCOMPARE(r->estado, ResultadoExportacion::Estado::Publicada);
    s.cancelar();
    s.cancelar();
    QTest::qWait(20);
    QCOMPARE(terminadas.size(), 1);
    QCOMPARE(terminadas.at(0).at(0).value<ResultadoExportacion>(), *r);
    QVERIFY(destino.archivo(kCarpetaFake, QStringLiteral("publicada.zip")));
    QCOMPARE(destino.parcialesVivos(), 0);
    // Progreso por fases (sin PDF no hay fase Pdf); el ultimo es Finalizando
    // con el conteo completo.
    QList<FaseExportacion> fases;
    for (const QList<QVariant>& emision : std::as_const(progreso)) {
        fases.append(emision.at(0).value<FaseExportacion>());
    }
    QCOMPARE(fases, (QList<FaseExportacion>{FaseExportacion::Preparando, FaseExportacion::Xml,
                                            FaseExportacion::Finalizando}));
    const QList<QVariant> ultimo = progreso.last();
    QCOMPARE(ultimo.at(1).toInt(), 1); // actual
    QCOMPARE(ultimo.at(2).toInt(), 1); // total
    QCOMPARE(ultimo.at(3).toInt(), 1); // exportadas
}

void TestExportacionServicio::planSinFacturasNoReserva()
{
    Banco b;
    QVERIFY(b.preparar());
    fakes::FakeDestinoExportacion destino;
    int limpiezas = 0;
    PuertosExportacionFacturas pu = puertos(destino, b.lector, *b.archivos, {});
    pu.limpiezaInicial = [&limpiezas] {
        ++limpiezas;
        return QtFuture::makeReadyValueFuture(0);
    };
    ExportacionFacturasService s(std::move(pu));
    PlanExportacion plan;
    plan.omisiones = {OmisionMesExportacion{TipoDescarga::Emitidos, kSeptiembre, MotivoOmisionMes::DescargasPendientes}};
    const auto r = esperarFuturo(s.iniciar(plan, kCarpetaFake, QStringLiteral("vacio.zip"), true));
    QVERIFY(r);
    QCOMPARE(r->estado, ResultadoExportacion::Estado::SinFacturas);
    QCOMPARE(r->lineasMes, 1);
    QCOMPARE(destino.reservas(), 0);
    QCOMPARE(limpiezas, 0);
    QVERIFY(destino.archivos(kCarpetaFake).isEmpty());
    QVERIFY(!s.enCurso());
}

// --- Contrato del servicio ----------------------------------------------------

void TestExportacionServicio::destinoExistenteEsperaOtroNombre()
{
    Banco b;
    QVERIFY(b.preparar());
    const Banco::Paquete p = b.paquete({xml40(1)});
    PlanExportacion plan;
    plan.facturas = {b.factura(p, 0, 1)};
    fakes::FakeDestinoExportacion destino;
    // El nombre aparece entre la reserva y el rename.
    bool colision = false;
    destino.antesDeRenombrar = [&destino, &colision] {
        if (!colision) {
            colision = true;
            destino.ponerArchivo(kCarpetaFake, QStringLiteral("facturas.zip"), "ajeno");
        }
    };
    ExportacionFacturasService s(puertos(destino, b.lector, *b.archivos, {}));
    QSignalSpy existe(&s, &ExportacionFacturasService::destinoExistente);
    auto futuro = s.iniciar(plan, kCarpetaFake, QStringLiteral("facturas.zip"), false);
    QTRY_COMPARE(existe.size(), 1);
    QCOMPARE(s.estado(), ExportacionFacturasService::EstadoTrabajo::EsperandoNombre);
    QVERIFY(!futuro.isFinished());
    QCOMPARE(destino.parcialesVivos(), 1); // el `.partial` se conserva

    QVERIFY(s.publicarComo(QStringLiteral("facturas (2)")));
    const auto r = esperarFuturo(futuro);
    QVERIFY(r);
    QCOMPARE(r->estado, ResultadoExportacion::Estado::Publicada);
    QCOMPARE(r->nombrePublicado, QStringLiteral("facturas (2).zip")); // saneado (D15)
    QCOMPARE(*destino.archivo(kCarpetaFake, QStringLiteral("facturas.zip")), QByteArray("ajeno")); // intacto
    QVERIFY(destino.archivo(kCarpetaFake, QStringLiteral("facturas (2).zip")));
    QCOMPARE(destino.parcialesVivos(), 0);
    QCOMPARE(s.publicarComo(QStringLiteral("otro")).error().codigo, ErrorExportacion::Codigo::Estado);
}

void TestExportacionServicio::cerrarDescartaParcialYUneHilo()
{
    Banco b;
    QVERIFY(b.preparar());
    const Banco::Paquete p = b.paquete({xml40(1)});
    PlanExportacion plan;
    plan.facturas = {b.factura(p, 0, 1)};

    // a) Cerrar con la lectura bloqueada.
    {
        fakes::LectorCfdiCompuerta compuerta(b.lector);
        compuerta.cerrar();
        fakes::FakeDestinoExportacion destino;
        ExportacionFacturasService s(puertos(destino, compuerta, *b.archivos, {}));
        auto futuro = s.iniciar(plan, kCarpetaFake, QStringLiteral("a.zip"), false);
        QVERIFY(compuerta.esperarBloqueo());
        QFuture<void> cierre = s.cerrar();
        QVERIFY(!cierre.isFinished());
        compuerta.liberar();
        QVERIFY(QTest::qWaitFor([&] { return cierre.isFinished(); }, 30000));
        QVERIFY(s.hilo()->isFinished());
        const auto r = esperarFuturo(futuro);
        QVERIFY(r);
        QCOMPARE(r->estado, ResultadoExportacion::Estado::Cancelada);
        QCOMPARE(destino.parcialesVivos(), 0);
        QVERIFY(destino.archivos(kCarpetaFake).isEmpty());
        const auto despues = esperarFuturo(s.iniciar(plan, kCarpetaFake, QStringLiteral("b.zip"), false));
        QVERIFY(despues);
        QCOMPARE(despues->error->codigo, ErrorExportacion::Codigo::Detenido);
    }
    // b) Cerrar en EsperandoNombre: el `.partial` conservado se borra.
    {
        fakes::FakeDestinoExportacion destino;
        destino.antesDeRenombrar = [&destino] { destino.ponerArchivo(kCarpetaFake, QStringLiteral("a.zip")); };
        ExportacionFacturasService s(puertos(destino, b.lector, *b.archivos, {}));
        QSignalSpy existe(&s, &ExportacionFacturasService::destinoExistente);
        auto futuro = s.iniciar(plan, kCarpetaFake, QStringLiteral("a.zip"), false);
        QTRY_COMPARE(existe.size(), 1);
        QCOMPARE(destino.parcialesVivos(), 1);
        QFuture<void> cierre = s.cerrar();
        QVERIFY(QTest::qWaitFor([&] { return cierre.isFinished(); }, 30000));
        const auto r = esperarFuturo(futuro);
        QVERIFY(r);
        QCOMPARE(r->estado, ResultadoExportacion::Estado::Cancelada);
        QCOMPARE(destino.parcialesVivos(), 0);
        QCOMPARE(s.publicarComo(QStringLiteral("c")).error().codigo, ErrorExportacion::Codigo::Detenido);
    }
}

void TestExportacionServicio::limpiezaInicialFallidaAborta()
{
    Banco b;
    QVERIFY(b.preparar());
    const Banco::Paquete p = b.paquete({xml40(1)});
    PlanExportacion plan;
    plan.facturas = {b.factura(p, 0, 1)};
    for (const LimpiezaInicialExportacionFn& limpieza :
         {limpiezaHecha(-1), LimpiezaInicialExportacionFn([] { return QFuture<int>(); })}) {
        fakes::FakeDestinoExportacion destino;
        ExportacionFacturasService s(puertos(destino, b.lector, *b.archivos, {}, siempreVisible(), limpieza));
        const auto r = esperarFuturo(s.iniciar(plan, kCarpetaFake, QStringLiteral("x.zip"), false));
        QVERIFY(r);
        QCOMPARE(r->estado, ResultadoExportacion::Estado::Error);
        QCOMPARE(r->error->codigo, ErrorExportacion::Codigo::LimpiezaInicial);
        QCOMPARE(destino.reservas(), 0);
    }
    // La exportacion espera una limpieza aun en curso antes de reservar.
    fakes::FakeDestinoExportacion destino;
    QPromise<int> limpieza;
    limpieza.start();
    const QFuture<int> futuroLimpieza = limpieza.future();
    ExportacionFacturasService s(
        puertos(destino, b.lector, *b.archivos, {}, siempreVisible(), [futuroLimpieza] { return futuroLimpieza; }));
    auto futuro = s.iniciar(plan, kCarpetaFake, QStringLiteral("x.zip"), false);
    QTest::qWait(50);
    QCOMPARE(destino.reservas(), 0);
    limpieza.addResult(2);
    limpieza.finish();
    const auto r = esperarFuturo(futuro);
    QVERIFY(r);
    QCOMPARE(r->estado, ResultadoExportacion::Estado::Publicada);
}

// --- Criterio 7 ----------------------------------------------------------------

void TestExportacionServicio::logsSinDatosSonda()
{
    Banco b;
    QVERIFY(b.preparar());
    const Banco::Paquete p = b.paquete({xml40(1), xml32(2)});
    const Banco::Paquete desaparece = b.paquete({xml40(3)});
    PlanExportacion plan;
    plan.facturas = {b.factura(p, 0, 1), b.factura(p, 1, 2), b.factura(desaparece, 0, 3)};
    plan.omisiones = {OmisionMesExportacion{TipoDescarga::Emitidos, kSeptiembre, MotivoOmisionMes::CoberturaIncompleta}};
    QVERIFY(QFile::remove(desaparece.absoluta));

    const QString carpeta = b.dir.filePath(QStringLiteral("Users/usuario/Documentos"));
    QVERIFY(QDir().mkpath(carpeta));
    const QString hostil = QStringLiteral("Facturas $(whoami) ‮gpj.exe;rm -rf ~");
    const QString saneado = sanearNombreZip(hostil);
    DestinoExportacionPosix::Opciones o;
    o.directorioDatos = b.dir.filePath(QStringLiteral("datos"));
    QVERIFY(QDir().mkpath(o.directorioDatos));
    DestinoExportacionPosix destino(o);

    CapturaLogs captura;
    QLoggingCategory::setFilterRules(QStringLiteral("satcfdi.*=true"));
    {
        ExportacionFacturasService s(
            puertos(destino, b.lector, *b.archivos, [] { return std::make_unique<RenderizadorPdfCfdi>(); }));
        QSignalSpy existe(&s, &ExportacionFacturasService::destinoExistente);
        const auto r = esperarFuturo(s.iniciar(plan, carpeta, hostil, true));
        QVERIFY(r);
        QCOMPARE(r->estado, ResultadoExportacion::Estado::Publicada);
        QCOMPARE(r->nombrePublicado, saneado);
        QCOMPARE(r->omitidas, 1);
        QCOMPARE(r->sinPdf, 1);
        // Mismo nombre otra vez: error de destino (DestinoExistente) registrado.
        const auto otra = esperarFuturo(s.iniciar(plan, carpeta, hostil, true));
        QVERIFY(otra);
        QCOMPARE(otra->error->destino->codigo, exportacion::ErrorDestinoExportacion::Codigo::DestinoExistente);
        // Carpeta inexistente bajo /Users/usuario.
        const auto sinCarpeta =
            esperarFuturo(s.iniciar(plan, carpeta + QStringLiteral("/no-existe"), QStringLiteral("y.zip"), false));
        QVERIFY(sinCarpeta);
        QCOMPARE(sinCarpeta->estado, ResultadoExportacion::Estado::Error);
        s.cancelar();
        esperarFuturo(s.planear(AlcanceMesExportacion{PerfilId::generar(), kSeptiembre, std::nullopt}));
    }
    QLoggingCategory::setFilterRules(QString());

    const QStringList lineas = captura.lineas();
    QVERIFY(lineas.filter(QStringLiteral("satcfdi.cfdi.exportacion")).size() >= 3);
    QVERIFY(!lineas.filter(QStringLiteral("exportacion terminada; estado: publicada")).isEmpty());
    QStringList sondas = {kRfc,
                          QStringLiteral("XEXX010101000"),
                          QStringLiteral("/Users/usuario"),
                          QStringLiteral("Users/usuario"),
                          saneado,
                          hostil,
                          QStringLiteral("whoami"),
                          QStringLiteral("gpj"),
                          b.dir.path(),
                          b.raiz,
                          p.id,
                          desaparece.id,
                          p.relativa,
                          p.solicitud.texto(),
                          QStringLiteral(".partial"),
                          QStringLiteral("emitidas/")};
    for (const int n : {1, 2, 3}) {
        sondas << uuidN(n) << uuidN(n).toLower() << uuidN(n).left(8);
    }
    for (const QString& linea : lineas) {
        for (const QString& sonda : std::as_const(sondas)) {
            QVERIFY2(!linea.contains(sonda, Qt::CaseInsensitive), qPrintable(QStringLiteral("sonda en log")));
        }
    }
}

// --- Revision R ------------------------------------------------------------------

// A1: el future de planear() siempre termina con un resultado tipado.
void TestExportacionServicio::planearSiempreTermina()
{
    using RP = Resultado<PlanExportacion, ErrorPersistencia>;
    Banco b;
    QVERIFY(b.preparar());
    fakes::FakeDestinoExportacion destino;
    const AlcanceExportacion alcance = AlcanceMesExportacion{PerfilId::generar(), kSeptiembre, std::nullopt};
    const auto conPlanear = [&](PlanearExportacionFn fn) {
        PuertosExportacionFacturas pu = puertos(destino, b.lector, *b.archivos, {});
        pu.planear = std::move(fn);
        return std::make_unique<ExportacionFacturasService>(std::move(pu));
    };

    // Excepcion dentro del future (p. ej. la tarea del dispatcher lanzo).
    {
        auto s = conPlanear([](const AlcanceExportacion&) {
            QPromise<RP> p;
            p.start();
            p.setException(std::make_exception_ptr(std::runtime_error("sintetica")));
            p.finish();
            return p.future();
        });
        const auto r = esperarFuturo(s->planear(alcance));
        QVERIFY(r);
        QCOMPARE(r->error().codigo, ErrorExportacion::Codigo::Planificacion);
        QCOMPARE(r->error().persistencia, std::optional(ErrorPersistencia::Tipo::Interno));
    }
    // El puerto lanza al llamarlo.
    {
        auto s = conPlanear([](const AlcanceExportacion&) -> QFuture<RP> { throw std::runtime_error("sintetica"); });
        const auto r = esperarFuturo(s->planear(alcance));
        QVERIFY(r);
        QCOMPARE(r->error().codigo, ErrorExportacion::Codigo::Planificacion);
    }
    // Dispatcher cerrado: future cancelado.
    {
        auto s = conPlanear([](const AlcanceExportacion&) { return QFuture<RP>(); });
        const auto r = esperarFuturo(s->planear(alcance));
        QVERIFY(r);
        QCOMPARE(r->error().codigo, ErrorExportacion::Codigo::Detenido);
    }
    // Fallo de persistencia tipado.
    {
        auto s = conPlanear([](const AlcanceExportacion&) {
            return QtFuture::makeReadyValueFuture(RP::fallo(
                ErrorPersistencia::de(ErrorPersistencia::Tipo::Almacenamiento, QStringLiteral("sintetico"))));
        });
        const auto r = esperarFuturo(s->planear(alcance));
        QVERIFY(r);
        QCOMPARE(r->error().codigo, ErrorExportacion::Codigo::Planificacion);
        QCOMPARE(r->error().persistencia, std::optional(ErrorPersistencia::Tipo::Almacenamiento));
    }
    // Plan pendiente al cerrar() -> Detenido; el dispatcher termina despues.
    {
        auto pendiente = std::make_shared<QPromise<RP>>();
        pendiente->start();
        auto s = conPlanear([pendiente](const AlcanceExportacion&) { return pendiente->future(); });
        auto futuro = s->planear(alcance);
        QTest::qWait(20);
        QVERIFY(!futuro.isFinished());
        s->cerrar();
        const auto r = esperarFuturo(futuro);
        QVERIFY(r);
        QCOMPARE(r->error().codigo, ErrorExportacion::Codigo::Detenido);
        s.reset();
        pendiente->addResult(RP::exito(PlanExportacion{}));
        pendiente->finish();
        QTest::qWait(20);
        QCOMPARE(futuro.result().error().codigo, ErrorExportacion::Codigo::Detenido);
    }
    // Plan pendiente al destruir el servicio.
    {
        auto pendiente = std::make_shared<QPromise<RP>>();
        pendiente->start();
        auto s = conPlanear([pendiente](const AlcanceExportacion&) { return pendiente->future(); });
        auto futuro = s->planear(alcance);
        s.reset();
        const auto r = esperarFuturo(futuro);
        QVERIFY(r);
        QCOMPARE(r->error().codigo, ErrorExportacion::Codigo::Detenido);
    }
}

// A2 (C6): la senal de eliminacion llega con la lectura ya liberada y antes de
// agregar el XML: la factura pendiente se omite; la ya escrita se queda.
void TestExportacionServicio::eliminacionEntreLecturaYAgregar()
{
    Banco b;
    QVERIFY(b.preparar());
    const Banco::Paquete p = b.paquete({xml40(1), xml40(2)});
    PlanExportacion plan;
    plan.facturas = {b.factura(p, 0, 1), b.factura(p, 1, 2)}; // misma solicitud
    std::atomic<bool> enVentana{false};
    std::atomic<bool> seguir{false};
    fakes::FakeDestinoExportacion destino;
    PuertosExportacionFacturas pu = puertos(destino, b.lector, *b.archivos, {});
    pu.antesDeAgregarXml = [&](int indice) {
        if (indice == 1) {
            enVentana = true;
            while (!seguir.load()) {
                QThread::msleep(1);
            }
        }
    };
    ExportacionFacturasService s(std::move(pu));
    auto futuro = s.iniciar(plan, kCarpetaFake, QStringLiteral("ventana.zip"), false);
    QTRY_VERIFY(enVentana.load()); // factura 1: leida, revalidada y reparseada
    s.notificarSolicitudEliminada(p.solicitud);
    seguir = true;
    const auto r = esperarFuturo(futuro);
    QVERIFY(r);
    QCOMPARE(r->estado, ResultadoExportacion::Estado::Publicada);
    QCOMPARE(r->exportadas, 1);
    QCOMPARE(r->omitidas, 1);
    const auto zip = leerZipBytes(*destino.archivo(kCarpetaFake, QStringLiteral("ventana.zip")));
    QVERIFY(zip);
    const QString c = QStringLiteral("emitidas/2026-09/");
    QCOMPARE(zip->keys(), (QStringList{c + uuidN(1) + QStringLiteral(".xml"), QStringLiteral("omitidas.txt")}));
    QCOMPARE(zip->value(QStringLiteral("omitidas.txt")), (c + uuidN(2) + QStringLiteral(": ya no disponible\n")).toUtf8());
}

// A3 (C5): con leerEntrada bloqueado, el destructor no espera sin limite;
// el hilo abandonado termina solo, sin tocar el servicio, y borra su `.partial`.
// C7: lector, archivos y destino viven SOLO por el servicio (la prueba guarda
// weak_ptr): el hilo abandonado los retiene hasta terminar y luego se liberan.
void TestExportacionServicio::destructorConLectorBloqueadoTienePlazo()
{
    Banco b;
    QVERIFY(b.preparar());
    const Banco::Paquete p = b.paquete({xml40(1)});
    PlanExportacion plan;
    plan.facturas = {b.factura(p, 0, 1)};
    // Destino real: el `.partial` se comprueba en disco aun liberado el destino.
    const QString datos = b.dir.filePath(QStringLiteral("datos"));
    const QString carpeta = b.dir.filePath(QStringLiteral("destino"));
    QVERIFY(QDir().mkpath(datos) && QDir().mkpath(carpeta));
    const auto entradas = [](const QString& c) {
        return QDir(c).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).size();
    };
    const QString privada = DestinoExportacionPosix::carpetaParciales(datos);
    std::weak_ptr<fakes::LectorCfdiCompuerta> compuerta;
    std::weak_ptr<DestinoExportacionPosix> destino;
    std::weak_ptr<ArchivosZipCfdi> archivos;
    std::unique_ptr<ExportacionFacturasService> s;
    {
        auto c = std::make_shared<fakes::LectorCfdiCompuerta>(std::make_shared<LectorZipLibzip>());
        c->cerrar();
        DestinoExportacionPosix::Opciones o;
        o.directorioDatos = datos;
        auto d = std::make_shared<DestinoExportacionPosix>(o);
        auto a = std::make_shared<ArchivosZipCfdiFs>(b.raiz);
        compuerta = c;
        destino = d;
        archivos = a;
        PuertosExportacionFacturas pu = puertos(d, c, a, {});
        pu.plazoDestructor = std::chrono::milliseconds(200);
        s = std::make_unique<ExportacionFacturasService>(std::move(pu));
    }
    // Si algo falla, abrir la compuerta para que el hilo termine.
    const auto abrir = qScopeGuard([&compuerta] {
        if (auto c = compuerta.lock()) {
            c->liberar();
        }
    });
    QSignalSpy terminadas(s.get(), &ExportacionFacturasService::terminada);
    auto futuro = s->iniciar(plan, carpeta, QStringLiteral("colgado.zip"), false);
    QVERIFY(compuerta.lock()->esperarBloqueo());
    QPointer<QThread> hilo(const_cast<QThread*>(s->hilo()));
    QFuture<void> cierre = s->cerrar();
    QTest::qWait(50);
    QVERIFY(!cierre.isFinished()); // el hilo sigue en leerEntrada

    QElapsedTimer reloj;
    reloj.start();
    s.reset();
    QVERIFY2(reloj.elapsed() < 5000, "el destructor espero sin limite");
    QVERIFY(reloj.elapsed() >= 150);
    QVERIFY(cierre.isFinished());
    const auto r = esperarFuturo(futuro);
    QVERIFY(r);
    QCOMPARE(r->estado, ResultadoExportacion::Estado::Cancelada);
    QVERIFY(hilo);
    QVERIFY(hilo->isRunning());
    // Servicio destruido: los puertos siguen vivos solo por el hilo.
    QVERIFY(!compuerta.expired() && !destino.expired() && !archivos.expired());
    QCOMPARE(entradas(privada), 1);

    // Al regresar la lectura, el hilo (que vuelve a consultar `archivos` y
    // descarta por `destino`) borra su `.partial`, termina y el QThread se
    // borra en el hilo grafico; despues se liberan los puertos.
    compuerta.lock()->liberar();
    QTRY_VERIFY_WITH_TIMEOUT(hilo.isNull(), 10000);
    QCOMPARE(terminadas.size(), 0); // nada llega al servicio destruido
    QTRY_VERIFY_WITH_TIMEOUT(compuerta.expired() && destino.expired() && archivos.expired(), 10000);
    QCOMPARE(entradas(privada), 0);
    QCOMPARE(entradas(carpeta), 0);
}

QTEST_MAIN(TestExportacionServicio)

#include "TestExportacionServicio.moc"
