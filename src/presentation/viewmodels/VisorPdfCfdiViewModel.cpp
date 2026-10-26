#include "VisorPdfCfdiViewModel.h"

#include "AccionesFinder.h"
#include "CfdiListModel.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

namespace satcfdi {

namespace {

QString claveMotivo(MotivoPrevisualizacion m)
{
    switch (m) {
    case MotivoPrevisualizacion::OrigenNoDisponible:
        return QStringLiteral("origenNoDisponible");
    case MotivoPrevisualizacion::VersionNoSoportada:
        return QStringLiteral("versionNoSoportada");
    case MotivoPrevisualizacion::Ilegible:
        return QStringLiteral("ilegible");
    case MotivoPrevisualizacion::Cancelada:
        return QStringLiteral("cancelada");
    case MotivoPrevisualizacion::Fallo:
        break;
    }
    return QStringLiteral("fallo");
}

QString unir(const QStringList& partes, const QString& separador)
{
    QStringList r;
    for (const QString& p : partes) {
        if (!p.isEmpty()) {
            r.append(p);
        }
    }
    return r.join(separador);
}

} // namespace

VisorPdfCfdiViewModel::VisorPdfCfdiViewModel(QObject* parent)
    : QObject(parent)
{
}

void VisorPdfCfdiViewModel::setServicio(PrevisualizacionCfdi* servicio)
{
    if (m_servicio == servicio) {
        return;
    }
    cerrar();
    m_servicio = servicio;
    emit disponibleChanged();
}

void VisorPdfCfdiViewModel::setAccionesFinder(AccionesFinder* acciones)
{
    m_finder = acciones;
    emit accionesChanged();
}

QUrl VisorPdfCfdiViewModel::fuente() const
{
    return m_estado == Estado::Listo && m_pdf ? QUrl::fromLocalFile(m_pdf->rutaArchivo) : QUrl();
}

QString VisorPdfCfdiViewModel::errorMessage() const
{
    if (m_estado != Estado::Error) {
        return {};
    }
    if (m_motivo == QLatin1String("origenNoDisponible")) {
        return tr("El ZIP de esta factura no está disponible.");
    }
    if (m_motivo == QLatin1String("versionNoSoportada")) {
        return tr("La app no representa esta versión de CFDI.");
    }
    if (m_motivo == QLatin1String("ilegible")) {
        return tr("No se pudo leer el XML de esta factura.");
    }
    if (m_motivo == QLatin1String("cancelada")) {
        return tr("Se canceló la generación del PDF.");
    }
    if (m_motivo == QLatin1String("documento")) {
        return tr("No se pudo mostrar el PDF.");
    }
    return tr("No se pudo generar el PDF.");
}

QString VisorPdfCfdiViewModel::errorDescripcion() const
{
    if (m_estado != Estado::Error) {
        return {};
    }
    if (m_motivo == QLatin1String("origenNoDisponible")) {
        return tr("Se movió o se borró fuera de la app. Revisa la carpeta de archivos descargados e intenta de nuevo.");
    }
    if (m_motivo == QLatin1String("versionNoSoportada") || m_motivo == QLatin1String("ilegible")) {
        return tr("No se generó el PDF. El XML sigue en el archivo descargado del SAT.");
    }
    if (m_motivo == QLatin1String("documento")) {
        return tr("Intenta de nuevo o ábrelo en Vista Previa.");
    }
    return tr("Intenta de nuevo.");
}

bool VisorPdfCfdiViewModel::puedeReintentar() const
{
    return m_estado == Estado::Error && m_motivo != QLatin1String("versionNoSoportada")
           && m_motivo != QLatin1String("ilegible");
}

bool VisorPdfCfdiViewModel::puedeGuardar() const
{
    return m_pdf.has_value() && m_servicio != nullptr && !guardando()
           && (m_estado == Estado::Listo || m_estado == Estado::Error);
}

bool VisorPdfCfdiViewModel::puedeAbrirVistaPrevia() const
{
    return m_pdf.has_value() && m_finder != nullptr && !abriendoVistaPrevia()
           && (m_estado == Estado::Listo || m_estado == Estado::Error);
}

QString VisorPdfCfdiViewModel::rutaCarpetaGuardado() const
{
    return !m_carpetaGuardado.isEmpty() ? m_carpetaGuardado
                                        : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

QUrl VisorPdfCfdiViewModel::carpetaGuardado() const
{
    const QString carpeta = rutaCarpetaGuardado();
    return carpeta.isEmpty() ? QUrl() : QUrl::fromLocalFile(carpeta);
}

QUrl VisorPdfCfdiViewModel::archivoSugerido() const
{
    const QString carpeta = rutaCarpetaGuardado();
    if (carpeta.isEmpty() || !m_pdf || m_pdf->nombreSugerido.isEmpty()) {
        return {};
    }
    return QUrl::fromLocalFile(QDir(carpeta).filePath(m_pdf->nombreSugerido));
}

bool VisorPdfCfdiViewModel::abrir(const SolicitudId& solicitud, const FilaCfdi& fila)
{
    if (!m_servicio) {
        return false;
    }
    m_peticion = Peticion{solicitud, fila.paqueteId, fila.posicion};
    m_titulo = unir({fila.tipoTexto, fila.serieFolio}, QStringLiteral(" "));
    if (m_titulo.isEmpty()) {
        m_titulo = tr("Factura");
    }
    const QString total = unir({fila.total, fila.moneda}, QStringLiteral(" "));
    m_subtitulo = unir({fila.emisorRfc, fila.fecha, total}, QStringLiteral(" · "));
    m_nombreAccesible = fila.nombreAccesible;
    emit cfdiChanged();
    generar();
    return true;
}

void VisorPdfCfdiViewModel::reintentar()
{
    if (m_peticion && m_estado == Estado::Error && puedeReintentar()) {
        generar();
    }
}

void VisorPdfCfdiViewModel::cerrar()
{
    ++m_generacion;
    m_genPdf = 0;
    m_peticion.reset();
    m_pdf.reset();
    invalidarAcciones();
    setMensaje(QString(), false);
    fijarEstado(Estado::Inactivo);
}

void VisorPdfCfdiViewModel::guardarEn(const QUrl& destino)
{
    if (!puedeGuardar()) {
        return;
    }
    const QString ruta = destino.isLocalFile() ? destino.toLocalFile() : QString();
    if (ruta.isEmpty() || !QDir::isAbsolutePath(ruta)) {
        setMensaje(tr("No se pudo guardar el PDF: elige un archivo en una carpeta de tu equipo."), true);
        return;
    }
    const QFileInfo info(ruta);
    m_carpetaGuardado = info.absolutePath();
    emit carpetaGuardadoChanged();
    const QString nombre = info.fileName();
    const quint64 generacion = ++m_generacion;
    m_genGuardar = generacion;
    setMensaje(QString(), false);
    emit accionesChanged();
    auto aplicar = [this, generacion, nombre](const std::optional<PrevisualizacionCfdi::ResultadoGuardar>& r) {
        if (generacion != m_genGuardar) {
            return; // otra peticion gano
        }
        m_genGuardar = 0;
        emit accionesChanged();
        if (r && r->esExito()) {
            setMensaje(tr("PDF guardado como “%1”.").arg(nombre), false);
        } else if (r && r->error() == MotivoPrevisualizacion::Cancelada) {
            setMensaje(tr("Se canceló el guardado del PDF."), true);
        } else {
            setMensaje(tr("No se pudo guardar el PDF. Revisa que la carpeta exista y tengas permiso de escritura."),
                       true);
        }
    };
    m_servicio->guardarComo(m_pdf->rutaArchivo, ruta)
        .then(this, [aplicar](const PrevisualizacionCfdi::ResultadoGuardar& r) { aplicar(r); })
        .onCanceled(this, [aplicar] { aplicar(std::nullopt); })
        .onFailed(this, [aplicar] { aplicar(std::nullopt); });
}

void VisorPdfCfdiViewModel::abrirEnVistaPrevia()
{
    if (!puedeAbrirVistaPrevia()) {
        return;
    }
    const quint64 generacion = ++m_generacion;
    m_genVistaPrevia = generacion;
    setMensaje(QString(), false);
    emit accionesChanged();
    auto aplicar = [this, generacion](const std::optional<ResultadoAccionFinder>& r) {
        if (generacion != m_genVistaPrevia) {
            return;
        }
        m_genVistaPrevia = 0;
        emit accionesChanged();
        if (r && r->estado == ResultadoAccionFinder::Estado::Mostrado) {
            setMensaje(QString(), false);
        } else if (r && !r->mensaje.isEmpty()) {
            setMensaje(r->mensaje, true);
        } else {
            setMensaje(tr("No se pudo abrir el PDF en Vista Previa."), true);
        }
    };
    m_finder->abrirEnVistaPrevia(m_pdf->rutaArchivo)
        .then(this, [aplicar](const ResultadoAccionFinder& r) { aplicar(r); })
        .onCanceled(this, [aplicar] { aplicar(std::nullopt); })
        .onFailed(this, [aplicar] { aplicar(std::nullopt); });
}

void VisorPdfCfdiViewModel::documentoIlegible()
{
    if (m_estado == Estado::Listo) {
        fijarEstado(Estado::Error, QStringLiteral("documento"));
    }
}

void VisorPdfCfdiViewModel::generar()
{
    if (!m_servicio || !m_peticion) {
        return;
    }
    const quint64 generacion = ++m_generacion;
    m_genPdf = generacion;
    m_pdf.reset();
    invalidarAcciones();
    setMensaje(QString(), false);
    fijarEstado(Estado::Generando);
    m_servicio->generar(m_peticion->solicitud, m_peticion->paqueteId, m_peticion->posicion)
        .then(this, [this, generacion](const PrevisualizacionCfdi::ResultadoGenerar& r) {
            aplicarGeneracion(generacion, r);
        })
        .onCanceled(this, [this, generacion] { aplicarGeneracion(generacion, std::nullopt); })
        .onFailed(this, [this, generacion] { aplicarGeneracion(generacion, std::nullopt); });
}

void VisorPdfCfdiViewModel::aplicarGeneracion(quint64 generacion,
                                              const std::optional<PrevisualizacionCfdi::ResultadoGenerar>& r)
{
    if (generacion != m_genPdf) {
        return; // respuesta tardia: la ultima peticion gana
    }
    m_genPdf = 0;
    if (r && r->esExito() && !r->valor().rutaArchivo.isEmpty()) {
        m_pdf = r->valor();
        emit carpetaGuardadoChanged(); // archivoSugerido
        fijarEstado(Estado::Listo);
    } else {
        fijarEstado(Estado::Error, r && !r->esExito() ? claveMotivo(r->error()) : QStringLiteral("fallo"));
    }
    emit accionesChanged();
}

void VisorPdfCfdiViewModel::fijarEstado(Estado estado, const QString& motivo)
{
    if (m_estado == estado && m_motivo == motivo) {
        return;
    }
    m_estado = estado;
    m_motivo = motivo;
    emit estadoChanged();
    emit accionesChanged();
}

void VisorPdfCfdiViewModel::setMensaje(const QString& mensaje, bool esError)
{
    if (m_mensaje == mensaje && m_mensajeEsError == esError) {
        return;
    }
    m_mensaje = mensaje;
    m_mensajeEsError = esError;
    emit mensajeChanged();
}

void VisorPdfCfdiViewModel::invalidarAcciones()
{
    if (m_genGuardar != 0 || m_genVistaPrevia != 0) {
        m_genGuardar = 0;
        m_genVistaPrevia = 0;
        emit accionesChanged();
    }
}

} // namespace satcfdi
