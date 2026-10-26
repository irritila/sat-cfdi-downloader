#include "ExportacionSintetica.h"

#include "domain/cfdi/UuidCfdi.h"
#include "ports/cfdi/ArchivosZipCfdi.h"
#include "ports/cfdi/CfdiArchiveReader.h"
#include "ports/cfdi/CfdiPdfRenderer.h"
#include "ports/exportacion/DestinoExportacion.h"

#include <QBuffer>
#include <QFileInfo>

namespace materiales {

namespace {

using namespace satcfdi::exportacion;

constexpr qint64 kTamanoZipFuente = 48'000;
constexpr qint64 kModificadoZipFuente = 1'790'000'000'000; // ms UTC fijos
const QString kShaZipFuente = QStringLiteral("0000000000000000000000000000000000000000000000000000000000000000");

// Cada factura planeada tiene su "paquete" fuente: sintetico/<UUID>.zip. El
// lector devuelve el UUID como contenido y el parser lo toma como UUID: sin
// estado compartido entre el hilo grafico y el del servicio.
QString rutaFuente(const QString& uuid) { return QStringLiteral("sintetico/%1.zip").arg(uuid); }

class ParcialMemoria final : public ArchivoParcialExportacion {
public:
    explicit ParcialMemoria(quint64 bytes)
        : m_bytes(bytes)
    {
        m_buffer.open(QIODevice::ReadWrite);
    }
    QIODevice& dispositivo() override { return m_buffer; }
    std::optional<ErrorDestinoExportacion> errorEscritura() const override { return std::nullopt; }
    Resultado<Exito, ErrorDestinoExportacion> truncar(qint64) override { return Resultado<Exito, ErrorDestinoExportacion>::exito({}); }
    Resultado<Exito, ErrorDestinoExportacion> sincronizar() override { return Resultado<Exito, ErrorDestinoExportacion>::exito({}); }
    Resultado<PublicacionExportacion, ErrorDestinoExportacion> publicar() override
    {
        PublicacionExportacion p;
        p.tamanoBytes = m_bytes;
        return Resultado<PublicacionExportacion, ErrorDestinoExportacion>::exito(p);
    }
    Resultado<PublicacionExportacion, ErrorDestinoExportacion> publicarComo(const QString&) override { return publicar(); }
    void descartar() noexcept override {}
    bool enCarpetaDestino() const override { return true; }

private:
    QBuffer m_buffer;
    quint64 m_bytes;
};

class DestinoMemoria final : public DestinoExportacion {
public:
    DestinoMemoria(quint64 bytes, std::optional<ErrorDestinoExportacion::Codigo> error)
        : m_bytes(bytes)
        , m_error(error)
    {
    }
    Resultado<std::unique_ptr<ArchivoParcialExportacion>, ErrorDestinoExportacion> reservar(const QString&,
                                                                                           const QString&) override
    {
        using R = Resultado<std::unique_ptr<ArchivoParcialExportacion>, ErrorDestinoExportacion>;
        if (m_error) {
            return R::fallo(ErrorDestinoExportacion::de(*m_error));
        }
        return R::exito(std::make_unique<ParcialMemoria>(m_bytes));
    }
    Resultado<ResumenLimpiezaExportacion, ErrorDestinoExportacion> limpiarParcialesPropios() override
    {
        return Resultado<ResumenLimpiezaExportacion, ErrorDestinoExportacion>::exito({});
    }

private:
    const quint64 m_bytes;
    const std::optional<ErrorDestinoExportacion::Codigo> m_error;
};

class EscritorMemoria final : public EscritorZipExportacion {
public:
    Resultado<Exito, ErrorZipExportacion> abrir(ArchivoParcialExportacion&, QDateTime, std::function<bool()>) override
    {
        return Resultado<Exito, ErrorZipExportacion>::exito({});
    }
    Resultado<Exito, ErrorZipExportacion> agregar(const QString&, QByteArrayView) override
    {
        ++m_entradas;
        return Resultado<Exito, ErrorZipExportacion>::exito({});
    }
    Resultado<ResumenZipExportacion, ErrorZipExportacion> cerrar() override
    {
        ResumenZipExportacion r;
        r.entradas = m_entradas;
        return Resultado<ResumenZipExportacion, ErrorZipExportacion>::exito(r);
    }
    void abortar() noexcept override {}

private:
    int m_entradas = 0;
};

class LectorUuid final : public CfdiArchiveReader {
public:
    Resultado<ResumenLecturaZip, ErrorPaqueteZip> recorrer(const QString&,
                                                           const std::function<bool(const EntradaXmlCfdi&)>&) override
    {
        return Resultado<ResumenLecturaZip, ErrorPaqueteZip>::exito({});
    }
    Resultado<std::optional<QByteArray>, ErrorPaqueteZip> leerEntrada(const QString& rutaZip, int) override
    {
        return Resultado<std::optional<QByteArray>, ErrorPaqueteZip>::exito(
            QFileInfo(rutaZip).completeBaseName().toUtf8());
    }
};

class ArchivosFijos final : public ArchivosZipCfdi {
public:
    ArchivoZipLocal consultar(const QString& rutaRelativa) override
    {
        ArchivoZipLocal a;
        a.estado = ArchivoZipLocal::Estado::Existe;
        a.rutaAbsoluta = QStringLiteral("/Users/usuario/Archivo-Fiscal/paquetes/") + rutaRelativa;
        a.tamano = kTamanoZipFuente;
        a.modificadoMs = kModificadoZipFuente;
        return a;
    }
    std::optional<QString> sha256(const QString&, const std::function<bool()>&) override { return kShaZipFuente; }
};

class RendererSintetico final : public CfdiPdfRenderer {
public:
    explicit RendererSintetico(QSet<QString> sinPdf)
        : m_sinPdf(std::move(sinPdf))
    {
    }
    int version() const override { return 1; }
    Resultado<PdfRenderizado, ErrorRenderPdf> renderizar(const ResultadoParseoCfdi& cfdi) override
    {
        if (m_sinPdf.contains(cfdi.resumen.uuid)) {
            return Resultado<PdfRenderizado, ErrorRenderPdf>::fallo(ErrorRenderPdf{ErrorRenderPdf::Tipo::Fallo});
        }
        return Resultado<PdfRenderizado, ErrorRenderPdf>::exito(PdfRenderizado{QByteArray("%PDF-1.4\n% sintetico\n"), 1});
    }

private:
    QSet<QString> m_sinPdf;
};

ResultadoParseoCfdi parsearUuid(QByteArrayView bytes)
{
    ResultadoParseoCfdi r;
    r.resumen.estado = EstadoParseo::Completo;
    r.resumen.version = QStringLiteral("4.0");
    r.resumen.uuid = QString::fromUtf8(bytes);
    return r;
}

FacturaPlaneada planeada(const CfdiMensual& f, MesCalendario mes)
{
    FacturaPlaneada p;
    p.origen = ReferenciaFactura{f.cfdi.paqueteId, f.cfdi.posicion};
    p.mes = mes;
    p.direccion = f.direccion;
    p.uuidCanonico = cfdi::normalizarUuid(f.cfdi.resumen.uuid);
    p.nombre = NombreEntradaFactura{QString(carpetaDireccion(f.direccion)) + QLatin1Char('/') + mes.texto(),
                                    p.uuidCanonico.value_or(QString())};
    p.rutaPaquete = rutaFuente(p.uuidCanonico.value_or(QString()));
    p.huellaPaquete = HuellaZip{kTamanoZipFuente, kModificadoZipFuente, kShaZipFuente};
    p.solicitud = f.solicitud;
    return p;
}

} // namespace

ExportacionSintetica::ExportacionSintetica(FuenteFacturasSintetica& fuente, DatosExportacion datos)
    : m_fuente(fuente)
    , m_datos(std::move(datos))
    , m_limpieza(std::make_shared<QPromise<int>>())
{
    m_limpieza->start();
    PuertosExportacionFacturas p;
    p.planear = [this](const AlcanceExportacion& alcance) {
        using R = Resultado<PlanExportacion, ErrorPersistencia>;
        if (m_datos.errorAlPlanear) {
            return QtFuture::makeReadyValueFuture(
                R::fallo(ErrorPersistencia::de(*m_datos.errorAlPlanear, QStringLiteral("error sintetico"))));
        }
        return QtFuture::makeReadyValueFuture(R::exito(planDe(alcance)));
    };
    p.paqueteExportable = [](const SolicitudId&, const QString&) {
        return QtFuture::makeReadyValueFuture(Resultado<bool, ErrorPersistencia>::exito(true));
    };
    p.limpiezaInicial = [this] {
        if (m_datos.retenerAlIniciar && !m_limpieza->future().isFinished()) {
            return m_limpieza->future();
        }
        return QtFuture::makeReadyValueFuture(0);
    };
    p.destino = std::make_shared<DestinoMemoria>(m_datos.bytesZip, m_datos.errorAlReservar);
    p.fabricaEscritor = [] { return std::make_unique<EscritorMemoria>(); };
    p.lector = std::make_shared<LectorUuid>();
    p.archivos = std::make_shared<ArchivosFijos>();
    p.parser = parsearUuid;
    p.fabricaRenderer = [sinPdf = m_datos.sinPdf]() -> std::unique_ptr<CfdiPdfRenderer> {
        return std::make_unique<RendererSintetico>(sinPdf);
    };
    const QDateTime ahora = fuente.ahoraUtc();
    p.reloj = [ahora] { return ahora; };
    m_servicio = std::make_unique<ExportacionFacturasService>(std::move(p));
}

ExportacionSintetica::~ExportacionSintetica()
{
    liberar(); // el hilo del servicio no queda esperando al cerrar
    m_servicio.reset();
}

void ExportacionSintetica::emitirProgreso(FaseExportacion fase, int actual, int total, int exportadas, int sinPdf,
                                          int omitidas)
{
    emit m_servicio->progreso(fase, actual, total, exportadas, sinPdf, omitidas);
}

void ExportacionSintetica::liberar()
{
    if (!m_limpieza->future().isFinished()) {
        m_limpieza->addResult(0);
        m_limpieza->finish();
    }
}

PlanExportacion ExportacionSintetica::planDe(const AlcanceExportacion& alcance) const
{
    PlanExportacion plan;
    auto agregarMes = [&](const PerfilId& perfil, MesCalendario mes, std::optional<TipoDescarga> direccion) {
        for (const CfdiMensual& f : m_fuente.facturas(perfil, mes, direccion)) {
            plan.facturas.append(planeada(f, mes));
        }
        const MesDeclarado m = m_fuente.mes(perfil, mes);
        if (m.estado == EstadoMesSintetico::DisponiblesParcialmente
            && (!direccion || *direccion == TipoDescarga::Recibidos)) {
            plan.omisiones.append(
                OmisionMesExportacion{TipoDescarga::Recibidos, mes, MotivoOmisionMes::CoberturaIncompleta});
        }
    };
    if (const auto* a = std::get_if<AlcanceMesExportacion>(&alcance)) {
        agregarMes(a->perfil, a->mes, a->direccion);
    } else if (const auto* r = std::get_if<AlcanceRangoExportacion>(&alcance)) {
        for (const MesCalendario& mes : mesesDe(alcance)) {
            agregarMes(r->perfil, mes, r->direccion);
        }
    } else if (const auto* s = std::get_if<AlcanceSeleccionExportacion>(&alcance)) {
        for (const FacturaSeleccionada& elegida : s->facturas) {
            for (const CfdiMensual& f : m_fuente.facturas(s->perfil, elegida.mes, elegida.direccion)) {
                if (f.cfdi.paqueteId == elegida.referencia.paqueteId && f.cfdi.posicion == elegida.referencia.posicion) {
                    plan.facturas.append(planeada(f, elegida.mes));
                }
            }
        }
    }
    return plan;
}

} // namespace materiales
