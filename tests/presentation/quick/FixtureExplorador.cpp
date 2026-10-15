#include "FixtureExplorador.h"

#include <QCoreApplication>
#include <QDir>
#include <QFont>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QStandardPaths>
#include <QTimeZone>

using namespace satcfdi;

namespace {

const QString kSolicitud = QStringLiteral("00000000-0000-4000-8000-000000000003");

template <typename P>
bool existe(const P& promesas, int indice, const char* que)
{
    if (indice >= 0 && indice < promesas.size()) {
        return true;
    }
    qWarning("FixtureExplorador: no hay llamada %d pendiente de %s", indice, que);
    return false;
}

class FinderEspia final : public AccionesFinder {
public:
    int carpetas = 0;
    fakes::PromesasPendientes<ResultadoAccionFinder> vistasPrevias;
    QFuture<ResultadoAccionFinder> mostrarPaquete(const SolicitudId&, const QString&) override
    {
        return QtFuture::makeReadyValueFuture(ResultadoAccionFinder{ResultadoAccionFinder::Estado::Mostrado, {}});
    }
    QFuture<ResultadoAccionFinder> abrirCarpetaSolicitud(const SolicitudId&) override
    {
        ++carpetas;
        return QtFuture::makeReadyValueFuture(ResultadoAccionFinder{ResultadoAccionFinder::Estado::Mostrado, {}});
    }
    QFuture<ResultadoAccionFinder> abrirCarpetaPaquetes() override
    {
        return QtFuture::makeReadyValueFuture(ResultadoAccionFinder{ResultadoAccionFinder::Estado::Mostrado, {}});
    }
    QFuture<ResultadoAccionFinder> abrirEnVistaPrevia(const QString&) override // T015.4
    {
        return vistasPrevias.nueva();
    }
};

std::optional<EstadoDescarga> estadoDescarga(const QString& clave)
{
    if (clave == QStringLiteral("Descargado")) {
        return EstadoDescarga::Descargado;
    }
    if (clave == QStringLiteral("Disponible")) {
        return EstadoDescarga::Disponible;
    }
    if (clave == QStringLiteral("Error")) {
        return EstadoDescarga::Error;
    }
    if (clave == QStringLiteral("Vencido")) {
        return EstadoDescarga::Vencido;
    }
    return std::nullopt;
}

} // namespace

struct FixtureExplorador::Grafo {
    Grafo()
    {
        vms.setAccionesFinder(&finder);
        vms.setConsultaCatalogo(&catalogo);
        vms.explorador()->setIntervaloBusqueda(0);
        QObject::connect(vms.explorador(), &ExploradorCfdiViewModel::previsualizacionSolicitada, &catalogo,
                         [this](const QString&, const QString& paquete, int posicion) {
                             previsualizaciones.append(QStringLiteral("%1:%2").arg(paquete).arg(posicion));
                         });
    }

    fakes::FakePerfilesSatService perfiles;
    fakes::FakeCredencialesSatService credenciales;
    SolicitudesServiceAsincrono solicitudes;
    fakes::ConsultaCatalogoFake catalogo;
    fakes::PrevisualizacionFake previsualizacion;
    FinderEspia finder;
    QStringList previsualizaciones;
    PresentacionViewModels vms{&solicitudes, &perfiles, &credenciales};
};

FixtureExplorador::FixtureExplorador(QObject* parent)
    : QObject(parent)
    , m_grafo(std::make_unique<Grafo>())
{
}

FixtureExplorador::~FixtureExplorador()
{
    if (!m_dirPdf.isEmpty()) {
        QDir(m_dirPdf).removeRecursively();
    }
}

AppViewModel* FixtureExplorador::app() const { return m_grafo->vms.app(); }
SolicitudDetailViewModel* FixtureExplorador::detalle() const { return m_grafo->vms.detalle(); }
ExploradorCfdiViewModel* FixtureExplorador::explorador() const { return m_grafo->vms.explorador(); }
QString FixtureExplorador::solicitudId() const { return kSolicitud; }

void FixtureExplorador::reiniciar()
{
    m_retirados.push_back(std::move(m_grafo));
    m_grafo = std::make_unique<Grafo>();
    emit reiniciado();
}

void FixtureExplorador::abrirDetalle(const QStringList& estados)
{
    m_grafo->vms.app()->abrirDetalle(kSolicitud);
    SolicitudDetalle d;
    d.resumen.id = SolicitudId::desdeTexto(kSolicitud).value();
    d.resumen.perfilRfc = QStringLiteral("XAXX010101000");
    d.resumen.tipoDescarga = TipoDescarga::Recibidos;
    d.resumen.fechaInicial = QDate(2026, 9, 1);
    d.resumen.fechaFinal = QDate(2026, 9, 30);
    d.resumen.estadoLocal = EstadoLocal::Enviada;
    d.resumen.estadoSat = EstadoSolicitudSat::Terminada;
    d.resumen.creadaEn = QDateTime(QDate(2026, 10, 1), QTime(10, 0), QTimeZone::UTC);
    int n = 0;
    for (const QString& clave : estados) {
        PaqueteResumen p;
        p.idPaqueteSat = QStringLiteral("PAQ_%1").arg(++n);
        p.estadoDescarga = estadoDescarga(clave).value_or(EstadoDescarga::Disponible);
        p.disponibleEn = d.resumen.creadaEn;
        if (p.estadoDescarga == EstadoDescarga::Descargado) {
            p.descargadoEn = d.resumen.creadaEn;
            ++d.resumen.paquetesDescargados;
        }
        d.paquetes.append(p);
    }
    d.resumen.totalPaquetes = int(d.paquetes.size());
    m_grafo->solicitudes.detalle.resolverUltima(SolicitudesService::ResultadoDetalle::exito(d));
}

void FixtureExplorador::resolverResumen(int indice, const QVariantMap& m)
{
    if (!existe(m_grafo->catalogo.resumenes, indice, "resumen")) {
        return;
    }
    ResumenIndiceSolicitud r;
    r.cfdiTotal = m.value(QStringLiteral("total")).toInt();
    r.indexando = m.value(QStringLiteral("indexando")).toBool();
    r.paquetesPendientes = m.value(QStringLiteral("pendientes")).toInt();
    r.paquetesNoDisponibles = m.value(QStringLiteral("noDisponibles")).toInt();
    r.paquetesIlegibles = m.value(QStringLiteral("ilegibles")).toInt();
    r.cfdiIlegibles = m.value(QStringLiteral("cfdiIlegibles")).toInt();
    r.paquetesDescargados = 1;
    m_grafo->catalogo.resolverResumen(indice, r);
}

void FixtureExplorador::fallarResumen(int indice)
{
    if (existe(m_grafo->catalogo.resumenes, indice, "resumen")) {
        m_grafo->catalogo.fallarResumen(indice);
    }
}

int FixtureExplorador::numResumenes() const { return m_grafo->catalogo.resumenes.size(); }

void FixtureExplorador::resolverLista(int indice, int n)
{
    if (existe(m_grafo->catalogo.listas, indice, "listar")) {
        m_grafo->catalogo.resolverLista(indice, fakes::cfdis(n));
    }
}

void FixtureExplorador::fallarLista(int indice)
{
    if (existe(m_grafo->catalogo.listas, indice, "listar")) {
        m_grafo->catalogo.fallarLista(indice);
    }
}

int FixtureExplorador::numListas() const { return m_grafo->catalogo.listas.size(); }

QString FixtureExplorador::ultimoTexto() const
{
    return m_grafo->catalogo.filtros.isEmpty() ? QString() : m_grafo->catalogo.filtros.constLast().texto;
}

QString FixtureExplorador::ultimoTipo() const
{
    if (m_grafo->catalogo.filtros.isEmpty() || !m_grafo->catalogo.filtros.constLast().tipo) {
        return {};
    }
    return QString(codigoTipoComprobante(*m_grafo->catalogo.filtros.constLast().tipo));
}

void FixtureExplorador::emitirIndiceCambiado()
{
    emit m_grafo->catalogo.indiceCambiado(SolicitudId::desdeTexto(kSolicitud).value());
}

int FixtureExplorador::numPrevisualizaciones() const { return int(m_grafo->previsualizaciones.size()); }

QString FixtureExplorador::ultimaPrevisualizacion() const
{
    return m_grafo->previsualizaciones.isEmpty() ? QString() : m_grafo->previsualizaciones.constLast();
}

int FixtureExplorador::numCarpetas() const { return m_grafo->finder.carpetas; }

void FixtureExplorador::activarPrevisualizacion()
{
    m_grafo->vms.setPrevisualizacionCfdi(&m_grafo->previsualizacion);
}

int FixtureExplorador::numGeneraciones() const { return m_grafo->previsualizacion.generaciones.size(); }

QString FixtureExplorador::ultimoPdfPedido() const
{
    return m_grafo->previsualizacion.pedidos.isEmpty() ? QString() : m_grafo->previsualizacion.pedidos.constLast();
}

void FixtureExplorador::resolverPdf(int indice, int paginas)
{
    if (!existe(m_grafo->previsualizacion.generaciones, indice, "generar")) {
        return;
    }
    if (m_dirPdf.isEmpty()) {
        m_dirPdf = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                       .filePath(QStringLiteral("satcfdi-quicktests-pdf-%1").arg(QCoreApplication::applicationPid()));
        QDir().mkpath(m_dirPdf);
    }
    const QString ruta = QDir(m_dirPdf).filePath(QStringLiteral("cfdi-%1-%2.pdf").arg(indice).arg(paginas));
    {
        QPdfWriter pdf(ruta);
        pdf.setPageSize(QPageSize(QPageSize::Letter));
        pdf.setResolution(72);
        QPainter p(&pdf);
        for (int i = 0; i < paginas; ++i) {
            if (i > 0) {
                pdf.newPage();
            }
            p.setFont(QFont(QStringLiteral("Helvetica"), 14));
            p.drawText(QPointF(72, 96), QStringLiteral("CFDI ficticio, pagina %1").arg(i + 1));
        }
    }
    m_grafo->previsualizacion.resolverPdf(indice, ruta, QStringLiteral("CFDI-A100-0A1B2C3D.pdf"));
}

void FixtureExplorador::fallarPdf(int indice, const QString& motivo)
{
    if (!existe(m_grafo->previsualizacion.generaciones, indice, "generar")) {
        return;
    }
    MotivoPrevisualizacion m = MotivoPrevisualizacion::Fallo;
    if (motivo == QLatin1String("origenNoDisponible")) {
        m = MotivoPrevisualizacion::OrigenNoDisponible;
    } else if (motivo == QLatin1String("versionNoSoportada")) {
        m = MotivoPrevisualizacion::VersionNoSoportada;
    } else if (motivo == QLatin1String("ilegible")) {
        m = MotivoPrevisualizacion::Ilegible;
    } else if (motivo == QLatin1String("cancelada")) {
        m = MotivoPrevisualizacion::Cancelada;
    }
    m_grafo->previsualizacion.fallarPdf(indice, m);
}

int FixtureExplorador::numGuardados() const { return m_grafo->previsualizacion.copias.size(); }

void FixtureExplorador::resolverGuardado(int indice, bool exito)
{
    if (!existe(m_grafo->previsualizacion.copias, indice, "guardarComo")) {
        return;
    }
    if (exito) {
        m_grafo->previsualizacion.resolverGuardado(indice);
    } else {
        m_grafo->previsualizacion.fallarGuardado(indice);
    }
}

int FixtureExplorador::numVistasPrevias() const { return m_grafo->finder.vistasPrevias.size(); }

void FixtureExplorador::resolverVistaPrevia(int indice, bool exito, const QString& mensaje)
{
    if (!existe(m_grafo->finder.vistasPrevias, indice, "abrirEnVistaPrevia")) {
        return;
    }
    m_grafo->finder.vistasPrevias.resolver(
        indice, ResultadoAccionFinder{exito ? ResultadoAccionFinder::Estado::Mostrado : ResultadoAccionFinder::Estado::Fallido,
                                      mensaje});
}
