#include "TestPrevisualizacionCfdi.h"

#include "application/cfdi/PrevisualizacionCfdiService.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "infrastructure/cfdi/xml/ParserCfdi.h"
#include "ports/cfdi/AlmacenPdfCfdi.h"
#include "ports/cfdi/CfdiPdfRenderer.h"

#include "fakes/FakeArchivosZipCfdi.h"
#include "fakes/FakeCfdiArchiveReader.h"
#include "infrastructure/CfdiPdfPruebasComun.h"

#include <QCryptographicHash>
#include <QHash>
#include <QMutex>
#include <QTest>

#include <atomic>

using namespace satcfdi;

namespace {

constexpr int kEspera = 10000;
const QString kRuta = QStringLiteral("XAXX010101000/2026-09/sol/PAQ_1.zip");
const QString kPaquete = QStringLiteral("0a1b2c3d-paquete-local");

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, kEspera) || f.isCanceled() || f.resultCount() == 0) {
        return std::nullopt;
    }
    return f.result();
}

class CatalogoPdf final : public CfdiCatalogRepository {
public:
    void fijar(const PaqueteIndexable& p)
    {
        QMutexLocker b(&m_mutex);
        m_paquetes.insert(p.paqueteId, p);
    }
    void quitar(const QString& id)
    {
        QMutexLocker b(&m_mutex);
        m_paquetes.remove(id);
    }
    // CFDI indexado en (paquete, posicion) con su UUID y estado.
    void fijarCfdi(const QString& paqueteId, int posicion, const QString& uuid, EstadoParseo estado)
    {
        QMutexLocker b(&m_mutex);
        CfdiListado c;
        c.paqueteId = paqueteId;
        c.posicion = posicion;
        c.resumen.uuid = uuid;
        c.resumen.estado = estado;
        m_cfdis.insert(qMakePair(paqueteId, posicion), c);
    }
    Resultado<QList<PaqueteIndexable>, ErrorPersistencia>
    listarPaquetesIndexables(const std::optional<SolicitudId>& solicitud) override
    {
        QMutexLocker b(&m_mutex);
        QList<PaqueteIndexable> r;
        for (const PaqueteIndexable& p : std::as_const(m_paquetes)) {
            if (!solicitud || p.solicitudId == *solicitud) {
                r.append(p);
            }
        }
        return Resultado<QList<PaqueteIndexable>, ErrorPersistencia>::exito(r);
    }
    Resultado<bool, ErrorPersistencia> reemplazarIndicePaquete(const IndicePaquete&) override
    {
        return Resultado<bool, ErrorPersistencia>::exito(false);
    }
    Resultado<bool, ErrorPersistencia> actualizarMetadatosHuella(const QString&, const HuellaZip&,
                                                                 const QDateTime&) override
    {
        return Resultado<bool, ErrorPersistencia>::exito(false);
    }
    Resultado<QList<CfdiListado>, ErrorPersistencia> listarCfdi(const SolicitudId&, const FiltroListaCfdi&) override
    {
        QMutexLocker b(&m_mutex);
        return Resultado<QList<CfdiListado>, ErrorPersistencia>::exito(m_cfdis.values());
    }
    Resultado<ResumenIndiceSolicitud, ErrorPersistencia> resumirSolicitud(const SolicitudId&, int) override
    {
        return Resultado<ResumenIndiceSolicitud, ErrorPersistencia>::exito({});
    }

private:
    QMutex m_mutex;
    QHash<QString, PaqueteIndexable> m_paquetes;
    QHash<QPair<QString, int>, CfdiListado> m_cfdis;
};

class RendererFalso final : public CfdiPdfRenderer {
public:
    int version() const override { return 7; }
    Resultado<PdfRenderizado, ErrorRenderPdf> renderizar(const ResultadoParseoCfdi& cfdi) override
    {
        llamadas.fetch_add(1);
        hilo.store(QThread::currentThread());
        if (cfdi.estado() != EstadoParseo::Completo && cfdi.estado() != EstadoParseo::Parcial) {
            return Resultado<PdfRenderizado, ErrorRenderPdf>::fallo({ErrorRenderPdf::Tipo::NoRepresentable});
        }
        return Resultado<PdfRenderizado, ErrorRenderPdf>::exito({QByteArrayLiteral("%PDF-falso"), 1});
    }
    std::atomic<int> llamadas{0};
    std::atomic<QThread*> hilo{nullptr};
};

class AlmacenMemoria final : public AlmacenPdfCfdi {
public:
    std::optional<QString> buscar(const ClavePdfCfdi& clave) override
    {
        QMutexLocker b(&m_mutex);
        const auto it = m_rutas.constFind(llave(clave));
        return it == m_rutas.constEnd() ? std::nullopt : std::optional(*it);
    }
    Resultado<PdfAlmacenado, ErrorAlmacenPdf> guardar(const ClavePdfCfdi& clave, const QByteArray&) override
    {
        QMutexLocker b(&m_mutex);
        ultimaClave = clave;
        const QString ruta = QStringLiteral("/cache/") + llave(clave) + QStringLiteral(".pdf");
        m_rutas.insert(llave(clave), ruta);
        return Resultado<PdfAlmacenado, ErrorAlmacenPdf>::exito({ruta, false});
    }
    Resultado<bool, ErrorAlmacenPdf> copiar(const QString& origen, const QString& destino) override
    {
        QMutexLocker b(&m_mutex);
        copias.append({origen, destino});
        hiloCopia = QThread::currentThread();
        if (destino.contains(QStringLiteral("falla"))) {
            return Resultado<bool, ErrorAlmacenPdf>::fallo({ErrorAlmacenPdf::Tipo::Escritura});
        }
        return Resultado<bool, ErrorAlmacenPdf>::exito(true);
    }
    QString directorioCache() const override { return QStringLiteral("/cache"); }
    QString directorioTemporal() const override { return QStringLiteral("/tmp-app"); }
    void vaciar()
    {
        QMutexLocker b(&m_mutex);
        m_rutas.clear();
    }

    QList<std::pair<QString, QString>> copias;
    QThread* hiloCopia = nullptr;
    ClavePdfCfdi ultimaClave;

private:
    static QString llave(const ClavePdfCfdi& c)
    {
        return c.uuid + QLatin1Char('-') + c.sha256Xml + QLatin1Char('-') + QString::number(c.versionGenerador);
    }
    QMutex m_mutex;
    QHash<QString, QString> m_rutas;
};

// Entorno: un paquete Indexado vigente con un ZIP de 3 entradas (XML 4.0,
// XML 3.2 y XML ilegible) en las posiciones 0, 1 y 2.
struct Entorno {
    PersistenceDispatcher dispatcher;
    CatalogoPdf catalogo;
    fakes::FakeArchivosZipCfdi archivos;
    fakes::FakeCfdiArchiveReader lector;
    RendererFalso renderer;
    AlmacenMemoria almacen;
    SolicitudId solicitud = SolicitudId::generar();
    int reindexados = 0;
    std::function<void()> alReindexar; // simula el efecto del indexador
    std::unique_ptr<PrevisualizacionCfdiService> servicio;

    Entorno()
    {
        lector.agregarXml(pruebaspdf::xmlCfdi({}));
        lector.agregarXml(QByteArrayLiteral(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?><cfdi:Comprobante xmlns:cfdi=\"http://www.sat.gob.mx/cfd/3\" "
            "version=\"3.2\" fecha=\"2015-01-01T00:00:00\" total=\"10.00\" tipoDeComprobante=\"ingreso\">"
            "<cfdi:Emisor rfc=\"XAXX010101000\"/><cfdi:Receptor rfc=\"XEXX010101000\"/></cfdi:Comprobante>"));
        lector.agregarXml(QByteArrayLiteral("<?xml version=\"1.0\"?><!DOCTYPE x []><x/>"));
        archivos.fijar(kRuta, fakes::FakeArchivosZipCfdi::Archivo{});
        catalogo.fijar(paquete());
        // Identidades indexadas: 4.0 con UUID, 3.2 sin timbre, ilegible.
        catalogo.fijarCfdi(kPaquete, 0, pruebaspdf::kUuid, EstadoParseo::Completo);
        catalogo.fijarCfdi(kPaquete, 1, QString(), EstadoParseo::VersionNoSoportada);
        catalogo.fijarCfdi(kPaquete, 2, QString(), EstadoParseo::Ilegible);
        PuertosPrevisualizacionCfdi puertos{dispatcher,
                                            catalogo,
                                            lector,
                                            archivos,
                                            [](QByteArrayView xml) { return parsearCfdi(xml); },
                                            renderer,
                                            almacen,
                                            [this](const SolicitudId& id) {
                                                if (id == solicitud) {
                                                    ++reindexados;
                                                }
                                                if (alReindexar) {
                                                    alReindexar();
                                                }
                                                return QtFuture::makeReadyVoidFuture();
                                            },
                                            1};
        servicio = std::make_unique<PrevisualizacionCfdiService>(std::move(puertos));
    }
    ~Entorno() { servicio.reset(); }

    PaqueteIndexable paquete(const HuellaZip& huella = HuellaZip{100, 1000, QString(64, QLatin1Char('a'))},
                             EstadoIndicePaquete estado = EstadoIndicePaquete::Indexado) const
    {
        PaqueteIndexable p;
        p.paqueteId = kPaquete;
        p.solicitudId = solicitud;
        p.rutaLocal = kRuta;
        p.estado = estado;
        p.huella = huella;
        p.versionParser = 1;
        return p;
    }

    std::optional<PrevisualizacionCfdiService::ResultadoPdf> generar(int posicion)
    {
        return esperar(servicio->generar(solicitud, kPaquete, posicion));
    }
};

ErrorPrevisualizacion::Tipo tipoError(const std::optional<PrevisualizacionCfdiService::ResultadoPdf>& r)
{
    return r && !r->esExito() ? r->error().tipo : ErrorPrevisualizacion::Tipo::Fallo;
}

QStringList g_logs;
QMutex g_mutexLogs;
QtMessageHandler g_anterior = nullptr;
void capturar(QtMsgType tipo, const QMessageLogContext& c, const QString& m)
{
    {
        QMutexLocker b(&g_mutexLogs);
        g_logs.append(m);
    }
    if (g_anterior) {
        g_anterior(tipo, c, m);
    }
}

} // namespace

void TestPrevisualizacionCfdi::generaFueraDelHiloGraficoYUsaCache()
{
    Entorno e;
    const auto r = e.generar(0);
    QVERIFY(r && r->esExito());
    QCOMPARE(r->valor().nombreSugerido, QStringLiteral("CFDI-AB77-5A1B2C3D.pdf"));
    QVERIFY(r->valor().rutaArchivo.startsWith(QStringLiteral("/cache/")));
    QCOMPARE(e.renderer.llamadas.load(), 1);
    QCOMPARE(e.renderer.hilo.load(), e.servicio->hilo());
    QVERIFY(e.renderer.hilo.load() != QThread::currentThread());
    // Clave D7: UUID + SHA-256 del XML + version del generador.
    QCOMPARE(e.almacen.ultimaClave.uuid, pruebaspdf::kUuid);
    QCOMPARE(e.almacen.ultimaClave.versionGenerador, 7);
    QCOMPARE(e.almacen.ultimaClave.sha256Xml,
             QString::fromLatin1(
                 QCryptographicHash::hash(pruebaspdf::xmlCfdi({}), QCryptographicHash::Sha256).toHex()));
    // Acierto: no se vuelve a generar.
    const auto r2 = e.generar(0);
    QVERIFY(r2 && r2->esExito());
    QCOMPARE(r2->valor().rutaArchivo, r->valor().rutaArchivo);
    QCOMPARE(e.renderer.llamadas.load(), 1);
    // Cache borrada: se regenera.
    e.almacen.vaciar();
    QVERIFY(e.generar(0)->esExito());
    QCOMPARE(e.renderer.llamadas.load(), 2);
    QCOMPARE(e.reindexados, 0);
    // La revalidacion rapida no calcula el SHA-256 del ZIP.
    QCOMPARE(e.archivos.llamadasSha(), 0);
}

void TestPrevisualizacionCfdi::versionNoSoportadaEIlegibleSinPdf()
{
    Entorno e;
    QCOMPARE(tipoError(e.generar(1)), ErrorPrevisualizacion::Tipo::VersionNoSoportada);
    QCOMPARE(tipoError(e.generar(2)), ErrorPrevisualizacion::Tipo::Ilegible);
    QCOMPARE(e.renderer.llamadas.load(), 0);
}

void TestPrevisualizacionCfdi::posicionInexistenteEsIlegible()
{
    Entorno e;
    // Posicion no indexada: no es un CFDI de la lista.
    QCOMPARE(tipoError(e.generar(9)), ErrorPrevisualizacion::Tipo::OrigenNoDisponible);
    // Indexada pero la entrada ya no existe en el ZIP: ilegible.
    e.catalogo.fijarCfdi(kPaquete, 9, QString(), EstadoParseo::Ilegible);
    QCOMPARE(tipoError(e.generar(9)), ErrorPrevisualizacion::Tipo::Ilegible);
    QCOMPARE(tipoError(esperar(e.servicio->generar(e.solicitud, kPaquete, -1))),
             ErrorPrevisualizacion::Tipo::OrigenNoDisponible);
    QCOMPARE(e.renderer.llamadas.load(), 0);
}

void TestPrevisualizacionCfdi::origenFaltanteSinPdfYAvisaAlIndice()
{
    Entorno e;
    e.archivos.quitar(kRuta);
    QCOMPARE(tipoError(e.generar(0)), ErrorPrevisualizacion::Tipo::OrigenNoDisponible);
    QCOMPARE(e.reindexados, 1); // el indice registra OrigenNoDisponible
    QCOMPARE(e.renderer.llamadas.load(), 0);
    // Ya registrado como OrigenNoDisponible: no se vuelve a pedir.
    e.catalogo.fijar(e.paquete({}, EstadoIndicePaquete::OrigenNoDisponible));
    QCOMPARE(tipoError(e.generar(0)), ErrorPrevisualizacion::Tipo::OrigenNoDisponible);
    QCOMPARE(e.reindexados, 1);
}

void TestPrevisualizacionCfdi::paqueteFueraDelIndiceEsOrigenNoDisponible()
{
    Entorno e;
    e.catalogo.quitar(kPaquete);
    QCOMPARE(tipoError(e.generar(0)), ErrorPrevisualizacion::Tipo::OrigenNoDisponible);
    QCOMPARE(tipoError(esperar(e.servicio->generar(SolicitudId(), kPaquete, 0))),
             ErrorPrevisualizacion::Tipo::OrigenNoDisponible);
}

void TestPrevisualizacionCfdi::zipCambiadoReindexaAntesDelPdf()
{
    Entorno e;
    // El ZIP cambio: otro tamano y otro SHA-256.
    e.archivos.fijar(kRuta, {ArchivoZipLocal::Estado::Existe, 200, 2000, QString(64, QLatin1Char('b')), false});
    // El indexador deja el indice vigente con la huella nueva.
    e.alReindexar = [&e] { e.catalogo.fijar(e.paquete(HuellaZip{200, 2000, QString(64, QLatin1Char('b'))})); };
    const auto r = e.generar(0);
    QVERIFY(r && r->esExito());
    QCOMPARE(e.reindexados, 1);
    QCOMPARE(e.renderer.llamadas.load(), 1);
}

void TestPrevisualizacionCfdi::zipCambiadoSinReindexarEsOrigenNoDisponible()
{
    Entorno e;
    e.archivos.fijar(kRuta, {ArchivoZipLocal::Estado::Existe, 200, 2000, QString(64, QLatin1Char('b')), false});
    // Tras reindexar el paquete queda Pendiente (cambio durante la lectura).
    e.alReindexar = [&e] { e.catalogo.fijar(e.paquete(HuellaZip{}, EstadoIndicePaquete::Pendiente)); };
    QCOMPARE(tipoError(e.generar(0)), ErrorPrevisualizacion::Tipo::OrigenNoDisponible);
    QCOMPARE(e.reindexados, 1); // un solo reintento
    QCOMPARE(e.renderer.llamadas.load(), 0);
}

void TestPrevisualizacionCfdi::mismoContenidoOtraFechaNoEspera()
{
    Entorno e;
    // Otra fecha de modificacion, mismo SHA-256: se genera y se avisa al indice.
    e.archivos.fijar(kRuta, {ArchivoZipLocal::Estado::Existe, 100, 5000, QString(64, QLatin1Char('a')), false});
    const auto r = e.generar(0);
    QVERIFY(r && r->esExito());
    QCOMPARE(e.archivos.llamadasSha(), 1);
    QCOMPARE(e.reindexados, 1);
    QCOMPARE(e.renderer.llamadas.load(), 1);
}

void TestPrevisualizacionCfdi::indiceNoVigenteReindexa()
{
    Entorno e;
    PaqueteIndexable viejo = e.paquete();
    viejo.versionParser = 0; // otra version del parser
    e.catalogo.fijar(viejo);
    e.alReindexar = [&e] { e.catalogo.fijar(e.paquete()); };
    QVERIFY(e.generar(0)->esExito());
    QCOMPARE(e.reindexados, 1);

    // Paquete Ilegible ya revisado con la misma huella: sin reindexar.
    Entorno i;
    i.catalogo.fijar(i.paquete(HuellaZip{100, 1000, QString(64, QLatin1Char('a'))}, EstadoIndicePaquete::Ilegible));
    QCOMPARE(tipoError(i.generar(0)), ErrorPrevisualizacion::Tipo::Ilegible);
    QCOMPARE(i.reindexados, 0);
}

void TestPrevisualizacionCfdi::otroCfdiEnLaPosicionEsOrigenNoDisponible()
{
    // Tras reindexar, en esa posicion hay otro CFDI (otro UUID): sin PDF.
    Entorno e;
    e.archivos.fijar(kRuta, {ArchivoZipLocal::Estado::Existe, 200, 2000, QString(64, QLatin1Char('b')), false});
    e.alReindexar = [&e] {
        e.catalogo.fijar(e.paquete(HuellaZip{200, 2000, QString(64, QLatin1Char('b'))}));
        e.catalogo.fijarCfdi(kPaquete, 0, QStringLiteral("5A1B2C3D-0000-4000-8000-0000000000FF"),
                             EstadoParseo::Completo);
    };
    QCOMPARE(tipoError(e.generar(0)), ErrorPrevisualizacion::Tipo::OrigenNoDisponible);
    QCOMPARE(e.reindexados, 1);
    QCOMPARE(e.renderer.llamadas.load(), 0);

    // Indice vigente pero desfasado del ZIP: el XML de esa posicion trae otro UUID.
    Entorno d;
    d.catalogo.fijarCfdi(kPaquete, 0, QStringLiteral("5A1B2C3D-0000-4000-8000-0000000000EE"), EstadoParseo::Completo);
    QCOMPARE(tipoError(d.generar(0)), ErrorPrevisualizacion::Tipo::OrigenNoDisponible);
    QCOMPARE(d.renderer.llamadas.load(), 0);

    // Sin UUID indexado basta el estado: era ilegible y ahora es un CFDI valido -> sin PDF.
    Entorno i;
    i.catalogo.fijarCfdi(kPaquete, 0, QString(), EstadoParseo::Ilegible);
    QCOMPARE(tipoError(i.generar(0)), ErrorPrevisualizacion::Tipo::OrigenNoDisponible);
    QCOMPARE(i.renderer.llamadas.load(), 0);
}

void TestPrevisualizacionCfdi::ultimaPeticionGana()
{
    Entorno e;
    auto primera = e.servicio->generar(e.solicitud, kPaquete, 0);
    auto segunda = e.servicio->generar(e.solicitud, kPaquete, 0);
    const auto r1 = esperar(primera);
    const auto r2 = esperar(segunda);
    QCOMPARE(tipoError(r1), ErrorPrevisualizacion::Tipo::Cancelada);
    QVERIFY(r2 && r2->esExito());
    QCOMPARE(e.renderer.llamadas.load(), 1);
}

void TestPrevisualizacionCfdi::detenerCancela()
{
    Entorno e;
    auto enCurso = e.servicio->generar(e.solicitud, kPaquete, 0);
    e.servicio->detener();
    QCOMPARE(tipoError(esperar(enCurso)), ErrorPrevisualizacion::Tipo::Cancelada);
    QCOMPARE(tipoError(e.generar(0)), ErrorPrevisualizacion::Tipo::Cancelada);
    const auto g = esperar(e.servicio->guardarComo(QStringLiteral("/cache/x.pdf"), QStringLiteral("/dest/x.pdf")));
    QVERIFY(g && !g->esExito());
    QCOMPARE(g->error().tipo, ErrorPrevisualizacion::Tipo::Cancelada);
    QCOMPARE(e.renderer.llamadas.load(), 0);
}

void TestPrevisualizacionCfdi::guardarComo()
{
    Entorno e;
    const auto ok = esperar(e.servicio->guardarComo(QStringLiteral("/cache/a.pdf"), QStringLiteral("/dest/a.pdf")));
    QVERIFY(ok && ok->esExito());
    QCOMPARE(e.almacen.copias.size(), 1);
    QCOMPARE(e.almacen.copias.first().second, QStringLiteral("/dest/a.pdf"));
    QCOMPARE(e.almacen.hiloCopia, e.servicio->hilo());
    const auto falla = esperar(e.servicio->guardarComo(QStringLiteral("/cache/a.pdf"), QStringLiteral("/falla/a.pdf")));
    QVERIFY(falla && !falla->esExito());
    QCOMPARE(falla->error().tipo, ErrorPrevisualizacion::Tipo::Fallo);
    QCOMPARE(e.servicio->directorioCache(), QStringLiteral("/cache"));
    QCOMPARE(e.servicio->directorioTemporal(), QStringLiteral("/tmp-app"));
}

void TestPrevisualizacionCfdi::nombreSugerido()
{
    using S = PrevisualizacionCfdiService;
    QCOMPARE(S::nombreSugerido(QStringLiteral("A"), QStringLiteral("101"), QStringLiteral("a1b2c3d4-0000")),
             QStringLiteral("CFDI-A101-a1b2c3d4.pdf"));
    QCOMPARE(S::nombreSugerido({}, {}, QStringLiteral("A1B2C3D4-0000")), QStringLiteral("CFDI-A1B2C3D4.pdf"));
    QCOMPARE(S::nombreSugerido(QStringLiteral("../x/"), QStringLiteral("1 2"), {}), QStringLiteral("CFDI-x12.pdf"));
    QCOMPARE(S::nombreSugerido({}, {}, {}), QStringLiteral("CFDI.pdf"));
}

void TestPrevisualizacionCfdi::logsSinDatosFiscales()
{
    g_anterior = qInstallMessageHandler(capturar);
    {
        Entorno e;
        e.generar(0);
        e.generar(1);
        e.generar(2);
        e.archivos.fijar(kRuta, {ArchivoZipLocal::Estado::Existe, 200, 2000, QString(64, QLatin1Char('b')), false});
        e.generar(0);
        e.archivos.quitar(kRuta);
        e.generar(0);
        esperar(e.servicio->guardarComo(QStringLiteral("/cache/a.pdf"), QStringLiteral("/falla/a.pdf")));
    }
    qInstallMessageHandler(g_anterior);
    QMutexLocker b(&g_mutexLogs);
    QVERIFY(!g_logs.isEmpty());
    for (const QString& linea : std::as_const(g_logs)) {
        for (const QString& p : {pruebaspdf::kRfcEmisor, pruebaspdf::kRfcReceptor, pruebaspdf::kUuid.left(8),
                                 QStringLiteral("14300"), QStringLiteral("rH8/bw=="), kRuta, kPaquete,
                                 QStringLiteral("/cache"), QStringLiteral("/falla"), QStringLiteral("EMISORA")}) {
            QVERIFY2(!linea.contains(p), qPrintable(linea));
        }
    }
}
