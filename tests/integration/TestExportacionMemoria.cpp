// T026.2 (criterio 6, ADR 0023 "memoria acotada a una entrada y al
// directorio central"): exportacion de 10,000 CFDI sinteticos con PDF real
// (RenderizadorPdfCfdi), ZIP fuente reales (libzip) y destino real
// (DestinoExportacionPosix + EscritorZipStreaming) en QTemporaryDir.
//
// Se muestrea TASK_VM_INFO.phys_footprint del proceso (la metrica de memoria
// de macOS) cada 5 ms desde un hilo aparte durante todo el trabajo. Linea
// base: despues de crear los ZIP fuente y antes de construir el plan, asi el
// delta incluye el plan, el directorio central y la carga inicial de fuentes
// del renderer. Umbral: delta maximo <= 128 MiB (detecta acumular XML, PDF o
// el ZIP completo en memoria: solo los PDF suman cientos de MiB).
//
// Larga (minutos): solo corre con SATCFDI_EJECUTAR_PRUEBA_MEMORIA=1; si no,
// QSKIP. Etiqueta CTest `exportacion;memoria` para excluirla en CI.
// Datos sinteticos (XAXX010101000, UUID inventados).

#include "TestExportacionServicioComun.h"

#include "infrastructure/cfdi/pdf/RenderizadorPdfCfdi.h"
#include "infrastructure/storage/DestinoExportacionPosix.h"

#include <mach/mach.h>

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QStandardPaths>

#include <atomic>
#include <chrono>
#include <thread>

using namespace satcfdi;
using namespace exportacionpruebas;

namespace {

constexpr int kFacturas = 10000;
constexpr int kPorPaquete = 1000;
constexpr qint64 kUmbralBytes = qint64(128) * 1024 * 1024;

// Huella de memoria del proceso (bytes); -1 si task_info falla.
qint64 huellaMemoria()
{
    task_vm_info_data_t info{};
    mach_msg_type_number_t cuenta = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &cuenta) != KERN_SUCCESS) {
        return -1;
    }
    return qint64(info.phys_footprint);
}

double mib(qint64 bytes)
{
    return double(bytes) / (1024.0 * 1024.0);
}

// Muestreo en un hilo propio (no depende del hilo grafico ni del servicio).
class Muestreador {
public:
    explicit Muestreador(qint64 base)
        : m_maximo(base)
    {
        m_hilo = std::thread([this] {
            while (!m_fin.load()) {
                const qint64 v = huellaMemoria();
                qint64 previo = m_maximo.load();
                while (v > previo && !m_maximo.compare_exchange_weak(previo, v)) {
                }
                m_muestras.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }
    ~Muestreador() { detener(); }

    void detener()
    {
        m_fin.store(true);
        if (m_hilo.joinable()) {
            m_hilo.join();
        }
    }

    qint64 maximo() const { return m_maximo.load(); }
    int muestras() const { return m_muestras.load(); }

private:
    std::atomic<bool> m_fin{false};
    std::atomic<qint64> m_maximo;
    std::atomic<int> m_muestras{0};
    std::thread m_hilo;
};

// Entradas del ZIP (directorio central) sin cargar su contenido.
qint64 entradasZip(const QString& ruta)
{
    int error = 0;
    zip_t* zip = zip_open(QFile::encodeName(ruta).constData(), ZIP_RDONLY | ZIP_CHECKCONS, &error);
    if (!zip) {
        return -1;
    }
    const qint64 n = zip_get_num_entries(zip, 0);
    zip_discard(zip);
    return n;
}

} // namespace

class TestExportacionMemoria : public QObject {
    Q_OBJECT

private slots:
    void diezMilCfdiConPdfMemoriaAcotada();
};

void TestExportacionMemoria::diezMilCfdiConPdfMemoriaAcotada()
{
    if (qEnvironmentVariableIntValue("SATCFDI_EJECUTAR_PRUEBA_MEMORIA") != 1) {
        QSKIP("Prueba larga de memoria: definir SATCFDI_EJECUTAR_PRUEBA_MEMORIA=1 para ejecutarla.");
    }
    QVERIFY(huellaMemoria() > 0);

    // 1. Datos fuente: 10 ZIP de paquete con 1,000 CFDI cada uno.
    Banco b;
    QVERIFY(b.preparar());
    QElapsedTimer reloj;
    reloj.start();
    QList<Banco::Paquete> paquetes;
    QList<HuellaZip> huellas;
    for (int inicio = 0; inicio < kFacturas; inicio += kPorPaquete) {
        QList<QByteArray> xmls;
        xmls.reserve(kPorPaquete);
        for (int i = 0; i < kPorPaquete; ++i) {
            xmls.append(xml40(inicio + i + 1));
        }
        const Banco::Paquete p = b.paquete(xmls);
        QVERIFY(!p.id.isEmpty());
        paquetes.append(p);
        huellas.append(b.huella(p));
    }
    const qint64 msFuente = reloj.elapsed();
    QDir().mkpath(b.dir.filePath(QStringLiteral("destino")));
    QDir().mkpath(b.dir.filePath(QStringLiteral("datos")));

    // Linea base antes del plan y del servicio.
    const qint64 base = huellaMemoria();
    Muestreador muestreo(base);
    reloj.restart();

    // 2. Plan a mano con la huella real de cada ZIP (una vez por paquete).
    PlanExportacion plan;
    plan.facturas.reserve(kFacturas);
    for (qsizetype k = 0; k < paquetes.size(); ++k) {
        const Banco::Paquete& p = paquetes.at(k);
        for (int i = 0; i < kPorPaquete; ++i) {
            const int n = int(k) * kPorPaquete + i + 1;
            FacturaPlaneada f;
            f.origen = ReferenciaFactura{p.id, i};
            f.mes = kSeptiembre;
            f.direccion = TipoDescarga::Emitidos;
            f.uuidCanonico = cfdi::normalizarUuid(uuidN(n));
            f.nombre = NombreEntradaFactura{QStringLiteral("emitidas/") + kSeptiembre.texto(),
                                            f.uuidCanonico.value_or(QString())};
            f.rutaPaquete = p.relativa;
            f.huellaPaquete = huellas.at(k);
            f.solicitud = p.solicitud;
            plan.facturas.append(std::move(f));
        }
    }
    QCOMPARE(plan.conteoPrevisto(), kFacturas);

    // 3. Exportacion con piezas reales.
    DestinoExportacionPosix::Opciones o;
    o.directorioDatos = b.dir.filePath(QStringLiteral("datos"));
    DestinoExportacionPosix destino(std::move(o));
    std::optional<ResultadoExportacion> r;
    {
        ExportacionFacturasService servicio(
            puertos(destino, b.lector, *b.archivos, [] { return std::make_unique<RenderizadorPdfCfdi>(); }));
        int ultimoProgreso = 0;
        connect(&servicio, &ExportacionFacturasService::progreso, this,
                [&ultimoProgreso](FaseExportacion, int actual, int, int, int, int) { ultimoProgreso = actual; });
        r = esperarFuturo(servicio.iniciar(plan, b.dir.filePath(QStringLiteral("destino")),
                                           QStringLiteral("memoria.zip"), true),
                          30 * 60 * 1000);
        QVERIFY(r);
        QCOMPARE(ultimoProgreso, kFacturas);
        esperarFuturo(servicio.cerrar().then([] { return true; }), 30000);
    }
    const qint64 msExportacion = reloj.elapsed();
    muestreo.detener();
    const qint64 final = huellaMemoria();
    const qint64 delta = muestreo.maximo() - base;

    QCOMPARE(r->estado, ResultadoExportacion::Estado::Publicada);
    QCOMPARE(r->exportadas, kFacturas);
    QCOMPARE(r->pdfExportados, kFacturas);
    QCOMPARE(r->sinPdf, 0);
    QCOMPARE(r->omitidas, 0);
    const QString zip = QDir(b.dir.filePath(QStringLiteral("destino"))).filePath(QStringLiteral("memoria.zip"));
    QCOMPARE(entradasZip(zip), qint64(2 * kFacturas));

    qInfo("memoria exportacion: %d CFDI con PDF; fuente %lld ms; exportacion %lld ms; ZIP %.1f MiB; "
          "base %.1f MiB; maximo %.1f MiB; delta maximo %.1f MiB (umbral %.0f MiB); final %.1f MiB; %d muestras",
          kFacturas, msFuente, msExportacion, mib(qint64(r->tamanoBytes)), mib(base), mib(muestreo.maximo()),
          mib(delta), mib(kUmbralBytes), mib(final), muestreo.muestras());
    QVERIFY2(muestreo.muestras() > 10, "muestreo insuficiente");
    QVERIFY2(delta <= kUmbralBytes, qPrintable(QStringLiteral("delta maximo %1 MiB").arg(mib(delta), 0, 'f', 1)));
}

int main(int argc, char* argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QGuiApplication app(argc, argv);
    TestExportacionMemoria t;
    return QTest::qExec(&t, argc, argv);
}

#include "TestExportacionMemoria.moc"
