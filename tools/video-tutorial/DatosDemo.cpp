#include "DatosDemo.h"

#include "NotificacionesIlustradas.h"

#include "application/cfdi/CalculoIndicePaquete.h"
#include "application/cfdi/PrevisualizacionCfdiService.h"
#include "application/notificaciones/ServicioNotificaciones.h"
#include "infrastructure/os/macos/MacOSMapeos.h"
#include "infrastructure/cfdi/xml/ParserCfdi.h"
#include "infrastructure/cfdi/zip/ArchivosZipCfdiFs.h"
#include "ports/storage/RutaPaquete.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimeZone>

#include <zip.h>

#include <algorithm>

namespace video {

namespace {

const QString kRfcA = QStringLiteral("XAXX010101000");
const QString kRfcB = QStringLiteral("XEXX010101000");

QString otroRfc(const QString& rfc) { return rfc == kRfcA ? kRfcB : kRfcA; }

PerfilId perfilId(int n)
{
    const QChar c = QLatin1Char(char('0' + n));
    const QString t = QString(8, c) + QStringLiteral("-") + QString(4, c) + QStringLiteral("-4") + QString(3, c)
                      + QStringLiteral("-8") + QString(3, c) + QStringLiteral("-") + QString(12, c);
    return PerfilId::desdeTexto(t).value();
}

SolicitudId solicitudId(int n)
{
    return SolicitudId::desdeTexto(QStringLiteral("00000000-0000-4000-8000-0000000000%1").arg(n, 2, 10, QLatin1Char('0')))
        .value();
}

QString idPaquete(int solicitud)
{
    return QStringLiteral("0F1E2D3C-4B5A-4978-8796-A5B4C3D2E1%1_01").arg(solicitud, 2, 10, QLatin1Char('0'));
}

QString uuidCfdi(int solicitud, int n)
{
    return QStringLiteral("5F2B%1-4D5E-4F60-8A7B-0C1D2E3F%2")
        .arg(QString::number(0x1A30 + n * 0x111, 16).toUpper())
        .arg(solicitud, 4, 10, QLatin1Char('0'));
}

LogResumen log(TipoEventoLog tipo, OrigenLog origen, const QDateTime& en)
{
    LogResumen l;
    l.tipoEvento = tipo;
    l.origen = origen;
    l.creadoEn = en;
    return l;
}

qint64 centavos(const QString& importe)
{
    const QStringList p = importe.split(QLatin1Char('.'));
    return p.at(0).toLongLong() * 100 + p.at(1).toLongLong();
}

QString importe(qint64 c) { return QStringLiteral("%1.%2").arg(c / 100).arg(c % 100, 2, 10, QLatin1Char('0')); }

QString escaparXml(const QString& s) { return s.toHtmlEscaped(); }

// XML sintetico (D2): RFC genericos, nombres, UUID, certificados y sellos
// inventados; un concepto con IVA 16 %.
QByteArray xmlCfdi(const CfdiGuion& c, const QString& rfcEmisor, const QString& nombreEmisor,
                   const QString& rfcReceptor, const QString& nombreReceptor, const QString& uuid)
{
    const qint64 sub = centavos(c.subtotal);
    const qint64 iva = (sub * 16 + 50) / 100;
    const QString serie = c.serie.isEmpty() ? QString() : QStringLiteral("Serie=\"%1\" ").arg(escaparXml(c.serie));
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<cfdi:Comprobante xmlns:cfdi=\"http://www.sat.gob.mx/cfd/4\" "
               "xmlns:tfd=\"http://www.sat.gob.mx/TimbreFiscalDigital\" Version=\"4.0\" %1"
               "Folio=\"%2\" Fecha=\"%3\" Sello=\"U0VMTE9GSUNUSUNJT0RFTUFOVUFM=Xk9fQ2w=\" "
               "FormaPago=\"03\" NoCertificado=\"00001000000000000001\" SubTotal=\"%4\" Moneda=\"MXN\" "
               "Total=\"%5\" TipoDeComprobante=\"%6\" Exportacion=\"01\" MetodoPago=\"PUE\" "
               "LugarExpedicion=\"64000\">"
               "<cfdi:Emisor Rfc=\"%7\" Nombre=\"%8\" RegimenFiscal=\"601\"/>"
               "<cfdi:Receptor Rfc=\"%9\" Nombre=\"%10\" DomicilioFiscalReceptor=\"64000\" "
               "RegimenFiscalReceptor=\"616\" UsoCFDI=\"G03\"/>"
               "<cfdi:Conceptos>"
               "<cfdi:Concepto ClaveProdServ=\"44121600\" Cantidad=\"1\" ClaveUnidad=\"H87\" Unidad=\"Pieza\" "
               "Descripcion=\"%11\" ValorUnitario=\"%4\" Importe=\"%4\" ObjetoImp=\"02\">"
               "<cfdi:Impuestos><cfdi:Traslados><cfdi:Traslado Base=\"%4\" Impuesto=\"002\" TipoFactor=\"Tasa\" "
               "TasaOCuota=\"0.160000\" Importe=\"%12\"/></cfdi:Traslados></cfdi:Impuestos>"
               "</cfdi:Concepto>"
               "</cfdi:Conceptos>"
               "<cfdi:Impuestos TotalImpuestosTrasladados=\"%12\"><cfdi:Traslados>"
               "<cfdi:Traslado Base=\"%4\" Impuesto=\"002\" TipoFactor=\"Tasa\" TasaOCuota=\"0.160000\" "
               "Importe=\"%12\"/></cfdi:Traslados></cfdi:Impuestos>"
               "<cfdi:Complemento><tfd:TimbreFiscalDigital Version=\"1.1\" UUID=\"%13\" "
               "FechaTimbrado=\"%3\" RfcProvCertif=\"%9\" "
               "SelloCFD=\"U0VMTE9GSUNUSUNJT0RFTUFOVUFM=Xk9fQ2w=\" NoCertificadoSAT=\"00001000000000000002\" "
               "SelloSAT=\"U0VMTE9TQVRGSUNUSUNJTw==\"/></cfdi:Complemento>"
               "</cfdi:Comprobante>\n")
        .arg(serie, escaparXml(c.folio), c.fecha, c.subtotal, importe(sub + iva), c.tipo, rfcEmisor,
             escaparXml(nombreEmisor.toUpper()), rfcReceptor)
        .arg(escaparXml(nombreReceptor.toUpper()), escaparXml(c.descripcion), importe(iva), uuid)
        .toUtf8();
}

// ZIP con fechas de entrada fijas (contenido reproducible). Error o vacio.
QString escribirZip(const QString& ruta, const QList<QPair<QString, QByteArray>>& entradas)
{
    int codigo = 0;
    zip_t* z = zip_open(QFile::encodeName(ruta).constData(), ZIP_CREATE | ZIP_TRUNCATE, &codigo);
    if (z == nullptr) {
        return QStringLiteral("no se pudo crear el ZIP sintetico (libzip %1)").arg(codigo);
    }
    for (const auto& [nombre, contenido] : entradas) {
        zip_source_t* fuente = zip_source_buffer(z, contenido.constData(), zip_uint64_t(contenido.size()), 0);
        const QByteArray n = nombre.toUtf8();
        const zip_int64_t i = fuente ? zip_file_add(z, n.constData(), fuente, ZIP_FL_ENC_UTF_8) : -1;
        if (i < 0) {
            if (fuente) {
                zip_source_free(fuente);
            }
            zip_discard(z);
            return QStringLiteral("no se pudo agregar una entrada al ZIP sintetico");
        }
        zip_file_set_mtime(z, zip_uint64_t(i), time_t(1790000000), 0);
    }
    if (zip_close(z) != 0) {
        zip_discard(z);
        return QStringLiteral("no se pudo cerrar el ZIP sintetico");
    }
    return {};
}

void contarPaquetes(SolicitudDetalle& d)
{
    d.resumen.totalPaquetes = int(d.paquetes.size());
    d.resumen.paquetesDescargados = 0;
    d.resumen.paquetesPendientesDescarga = 0;
    for (const PaqueteResumen& p : d.paquetes) {
        if (p.estadoDescarga == EstadoDescarga::Descargado) {
            ++d.resumen.paquetesDescargados;
        } else if (p.estadoDescarga == EstadoDescarga::Disponible || p.estadoDescarga == EstadoDescarga::Error) {
            ++d.resumen.paquetesPendientesDescarga;
        }
    }
}

template <typename T>
QFuture<T> listo(T valor)
{
    return QtFuture::makeReadyValueFuture(std::move(valor));
}

} // namespace

// --- SolicitudesGuion ---------------------------------------------------------

SolicitudDetalle* SolicitudesGuion::buscar(const SolicitudId& id)
{
    for (SolicitudDetalle& d : detalles) {
        if (d.resumen.id == id) {
            return &d;
        }
    }
    return nullptr;
}

QFuture<SolicitudesService::ResultadoLista> SolicitudesGuion::listar()
{
    QList<SolicitudResumen> r;
    for (const SolicitudDetalle& d : detalles) {
        if (!ocultas.contains(d.resumen.id.texto())) {
            r.append(d.resumen);
        }
    }
    std::stable_sort(r.begin(), r.end(),
                     [](const SolicitudResumen& a, const SolicitudResumen& b) { return a.creadaEn > b.creadaEn; });
    return listo(ResultadoLista::exito(r));
}

QFuture<SolicitudesService::ResultadoDetalle> SolicitudesGuion::obtener(const SolicitudId& id)
{
    if (const SolicitudDetalle* d = buscar(id); d != nullptr && !ocultas.contains(id.texto())) {
        return listo(ResultadoDetalle::exito(*d));
    }
    return listo(ResultadoDetalle::fallo(ErrorObtener{}));
}

QFuture<SolicitudesService::ResultadoEvaluarDuplicado> SolicitudesGuion::evaluarDuplicado(const NuevaSolicitudRequest&)
{
    return listo(ResultadoEvaluarDuplicado::exito(EvaluacionDuplicado{}));
}

QFuture<SolicitudesService::ResultadoCrear> SolicitudesGuion::crearLocal(const NuevaSolicitudRequest& request,
                                                                         ConfirmacionDuplicado)
{
    ++creadas;
    const int n = 90 + creadas;
    SolicitudDetalle d;
    d.resumen.id = solicitudId(n);
    d.resumen.perfilRfc = rfcPorPerfilId.value(request.perfilId.texto());
    d.resumen.perfilNombre = nombrePerfilPorRfc ? nombrePerfilPorRfc(d.resumen.perfilRfc) : QString();
    d.resumen.tipoDescarga = request.tipoDescarga;
    d.resumen.fechaInicial = request.fechaInicial;
    d.resumen.fechaFinal = request.fechaFinal;
    d.resumen.creadaEn = ahora.addSecs(qint64(n) * 60);
    d.fechaInicialSat = request.fechaInicial.toString(Qt::ISODate) + QStringLiteral("T00:00:00");
    d.fechaFinalSat = request.fechaFinal.toString(Qt::ISODate) + QStringLiteral("T23:59:59");
    d.tipoComprobante = request.tipoComprobante;
    d.complemento = request.complemento;
    if (request.rfcContraparte) {
        d.rfcContrapartes = {*request.rfcContraparte};
    }
    d.logs = {log(TipoEventoLog::SolicitudCreada, OrigenLog::Usuario, d.resumen.creadaEn)};
    detalles.append(d);
    porClave.insert(QStringLiteral("nueva%1").arg(creadas), d.resumen.id);
    emit listaCambiada();
    emit solicitudActualizada(d.resumen.id);
    return listo(ResultadoCrear::exito(d.resumen.id));
}

QFuture<SolicitudesService::ResultadoEliminar> SolicitudesGuion::eliminar(const SolicitudId&)
{
    return listo(ResultadoEliminar::exito(ResultadoEliminacion{}));
}

// --- PerfilesGuion -------------------------------------------------------------

QFuture<PerfilesSatService::ResultadoLista> PerfilesGuion::listarNoEliminados()
{
    QList<PerfilResumen> r;
    for (const PerfilResumen& p : perfiles) {
        if (!ocultos.contains(p.id.texto())) {
            r.append(p);
        }
    }
    // Como el servicio real: ordenados por RFC.
    std::stable_sort(r.begin(), r.end(), [](const PerfilResumen& a, const PerfilResumen& b) { return a.rfc < b.rfc; });
    return listo(ResultadoLista::exito(r));
}

QFuture<PerfilesSatService::ResultadoPerfil> PerfilesGuion::obtener(const PerfilId& id)
{
    for (const PerfilResumen& p : perfiles) {
        if (p.id == id && !ocultos.contains(id.texto())) {
            return listo(ResultadoPerfil::exito(p));
        }
    }
    return listo(ResultadoPerfil::exito(std::nullopt));
}

QFuture<PerfilesSatService::ResultadoCrear> PerfilesGuion::crear(const QString& rfc, const QString& nombre)
{
    const QString normalizado = rfc.trimmed().toUpper();
    for (PerfilResumen& p : perfiles) {
        if (p.rfc == normalizado && ocultos.contains(p.id.texto())) {
            ocultos.remove(p.id.texto());
            p.nombre = nombre.trimmed();
            emit perfilesCambiaron();
            return listo(ResultadoCrear::exito(p));
        }
    }
    violacion = QStringLiteral("se intento crear un perfil con un RFC no declarado oculto en el guion");
    return listo(ResultadoCrear::fallo(ErrorCrearPerfil::desdePersistencia(ErrorPersistencia{})));
}

QFuture<PerfilesSatService::ResultadoActualizar> PerfilesGuion::actualizarNombre(const PerfilId& id,
                                                                                 const QString& nombre)
{
    for (PerfilResumen& p : perfiles) {
        if (p.id == id) {
            p.nombre = nombre.trimmed();
            emit perfilesCambiaron();
            return listo(ResultadoActualizar::exito(p));
        }
    }
    return listo(ResultadoActualizar::fallo(ErrorActualizarPerfil{}));
}

// --- CredencialesGuion ---------------------------------------------------------

QFuture<CredencialesSatService::ResultadoImportacion> CredencialesGuion::pendiente(const PerfilId& perfil)
{
    auto promesa = std::make_unique<QPromise<ResultadoImportacion>>();
    promesa->start();
    QFuture<ResultadoImportacion> f = promesa->future();
    m_pendientes[perfil.texto()] = std::move(promesa);
    emit credencialCambio(perfil.texto()); // Validando
    return f;
}

QFuture<CredencialesSatService::ResultadoImportacion> CredencialesGuion::importar(const PerfilId& perfil,
                                                                                  EntradaEFirma&& entrada)
{
    entrada = EntradaEFirma{}; // ni rutas ni contrasena se conservan
    return pendiente(perfil);
}

QFuture<CredencialesSatService::ResultadoImportacion> CredencialesGuion::reemplazar(const PerfilId& perfil,
                                                                                    EntradaEFirma&& entrada)
{
    entrada = EntradaEFirma{};
    return pendiente(perfil);
}

QFuture<CredencialesSatService::ResultadoEstado> CredencialesGuion::obtenerEstado(const PerfilId& perfil)
{
    if (m_pendientes.count(perfil.texto()) != 0) {
        return listo(ResultadoEstado::exito(EstadoCredencial::Validando));
    }
    return listo(ResultadoEstado::exito(resumenes.value(perfil.texto()).estado));
}

QFuture<CredencialesSatService::ResultadoResumen> CredencialesGuion::obtenerResumen(const PerfilId& perfil)
{
    ResumenCredencial r = resumenes.value(perfil.texto());
    if (m_pendientes.count(perfil.texto()) != 0) {
        r.estado = EstadoCredencial::Validando;
    }
    return listo(ResultadoResumen::exito(r));
}

QFuture<CredencialesSatService::ResultadoEliminacion> CredencialesGuion::eliminar(const PerfilId&)
{
    return listo(ResultadoEliminacion::exito(CredencialEliminada{}));
}

QFuture<CredencialesSatService::ResultadoReconciliacion> CredencialesGuion::reconciliar()
{
    return listo(ResultadoReconciliacion::exito(ResumenReconciliacion{}));
}

CredencialesSatService::ResultadoMaterial CredencialesGuion::obtenerMaterialFirma(const PerfilId&)
{
    return ResultadoMaterial::fallo(ErrorCredencialSat::hiloNoPermitido());
}

bool CredencialesGuion::completar(const PerfilId& perfil, const ResumenCredencial& resumen)
{
    const auto it = m_pendientes.find(perfil.texto());
    if (it == m_pendientes.end()) {
        return false;
    }
    std::unique_ptr<QPromise<ResultadoImportacion>> promesa = std::move(it->second);
    m_pendientes.erase(it);
    resumenes.insert(perfil.texto(), resumen);
    CredencialImportada importada;
    importada.estado = resumen.estado;
    importada.metadata.numeroSerie = QStringLiteral("00001000000000000001");
    importada.metadata.vigenteDesde = resumen.vigenteDesde.value_or(QDateTime());
    importada.metadata.vigenteHasta = resumen.vigenteHasta.value_or(QDateTime());
    // Como el servicio real: la senal antes de completar el future.
    emit credencialCambio(perfil.texto());
    promesa->addResult(ResultadoImportacion::exito(importada));
    promesa->finish();
    return true;
}

void CredencialesGuion::fijar(const PerfilId& perfil, const std::optional<ResumenCredencial>& resumen)
{
    if (resumen) {
        resumenes.insert(perfil.texto(), *resumen);
    } else {
        resumenes.remove(perfil.texto());
    }
    emit credencialCambio(perfil.texto());
}

// --- PrimerUsoGuion -------------------------------------------------------------

PrimerUsoGuion::PrimerUsoGuion(const PerfilesGuion& perfiles, const CredencialesGuion& credenciales,
                               const SolicitudesGuion& solicitudes)
    : m_perfiles(perfiles)
    , m_credenciales(credenciales)
    , m_solicitudes(solicitudes)
{
}

QFuture<ConsultaPrimerUso::ResultadoPrimerUso> PrimerUsoGuion::consultar()
{
    EstadoPrimerUso e;
    for (const PerfilResumen& p : m_perfiles.perfiles) {
        if (m_perfiles.ocultos.contains(p.id.texto())) {
            continue;
        }
        e.hayPerfiles = true;
        // Como ConsultaPrimerUsoPersistida: credencial Lista (no en validacion).
        if (!m_credenciales.hayPendiente(p.id.texto())
            && m_credenciales.resumenes.value(p.id.texto()).estado == EstadoCredencial::Lista) {
            e.hayEFirmaLista = true;
        }
    }
    for (const SolicitudDetalle& d : m_solicitudes.detalles) {
        e.haySolicitudes = e.haySolicitudes || !m_solicitudes.ocultas.contains(d.resumen.id.texto());
    }
    return listo(ResultadoPrimerUso::exito(e));
}

// --- CatalogoGuion -------------------------------------------------------------

QFuture<ConsultaCatalogoCfdi::ResultadoLista> CatalogoGuion::listar(const SolicitudId& solicitud,
                                                                    const FiltroListaCfdi& filtro)
{
    QList<CfdiListado> r;
    if (!visibles.contains(solicitud.texto())) {
        return listo(ResultadoLista::exito(r));
    }
    for (const CfdiListado& c : indexados.value(solicitud.texto())) {
        const ResumenCfdi& x = c.resumen;
        if (filtro.tipo && x.tipo != *filtro.tipo) {
            continue;
        }
        const QString texto = QStringList{x.emisor.rfc, x.emisor.nombre, x.receptor.rfc, x.receptor.nombre, x.uuid,
                                          x.serie, x.folio}
                                  .join(QLatin1Char(' '));
        if (!filtro.texto.isEmpty() && !texto.contains(filtro.texto, Qt::CaseInsensitive)) {
            continue;
        }
        r.append(c);
    }
    // Como el repositorio real: fecha descendente.
    std::stable_sort(r.begin(), r.end(),
                     [](const CfdiListado& a, const CfdiListado& b) { return a.resumen.fecha > b.resumen.fecha; });
    return listo(ResultadoLista::exito(r));
}

QFuture<ConsultaCatalogoCfdi::ResultadoResumen> CatalogoGuion::resumen(const SolicitudId& solicitud)
{
    if (!visibles.contains(solicitud.texto())) {
        return listo(ResultadoResumen::exito(ResumenIndiceSolicitud{}));
    }
    return listo(ResultadoResumen::exito(resumenIndexado.value(solicitud.texto())));
}

// --- PrevisualizacionZip ---------------------------------------------------------

QFuture<PrevisualizacionCfdi::ResultadoGenerar> PrevisualizacionZip::generar(const SolicitudId&,
                                                                            const QString& paqueteId, int posicion)
{
    if (modo == Modo::Fallo) {
        return listo(ResultadoGenerar::fallo(MotivoPrevisualizacion::Fallo));
    }
    if (modo == Modo::SinRespuesta) {
        auto promesa = std::make_unique<QPromise<ResultadoGenerar>>();
        promesa->start();
        QFuture<ResultadoGenerar> f = promesa->future();
        m_pendientes.push_back(std::move(promesa));
        return f;
    }
    const QString zip = zipPorPaquete.value(paqueteId);
    const auto entrada = m_lector.leerEntrada(zip, posicion);
    if (!entrada.esExito() || !entrada.valor()) {
        return listo(ResultadoGenerar::fallo(MotivoPrevisualizacion::OrigenNoDisponible));
    }
    const ResultadoParseoCfdi cfdi = parsearCfdi(*entrada.valor());
    const auto pdf = m_renderer.renderizar(cfdi);
    if (!pdf.esExito()) {
        return listo(ResultadoGenerar::fallo(MotivoPrevisualizacion::Fallo));
    }
    const QString nombre =
        PrevisualizacionCfdiService::nombreSugerido(cfdi.resumen.serie, cfdi.resumen.folio, cfdi.resumen.uuid);
    const QString ruta = QDir(dirPdf).filePath(QStringLiteral("%1-%2").arg(posicion).arg(nombre));
    QFile f(ruta);
    if (!f.open(QIODevice::WriteOnly) || f.write(pdf.valor().contenido) != pdf.valor().contenido.size()) {
        return listo(ResultadoGenerar::fallo(MotivoPrevisualizacion::Fallo));
    }
    return listo(ResultadoGenerar::exito(PdfPrevisualizado{ruta, nombre}));
}

QFuture<PrevisualizacionCfdi::ResultadoGuardar> PrevisualizacionZip::guardarComo(const QString&, const QString&)
{
    // El video nunca escribe fuera de su temporal: Guardar no hace nada.
    return listo(ResultadoGuardar::exito(true));
}

// --- DemoVideo ---------------------------------------------------------------------

DemoVideo::DemoVideo(const DatosGuion& datos, QString dirTemporal)
    : facturas(datosFacturas(datos))
    , exportacion(facturas, datosExportacion())
    , m_datos(datos)
    , m_dir(std::move(dirTemporal))
    , m_ahora(QDateTime::fromString(datos.ahora, Qt::ISODate).toUTC())
{
    solicitudes.ahora = m_ahora;
    // Como T022 D6: ejecutar() termina sin crear solicitudes.
    facturas.setSolicitudesCreadasAlEjecutar(0);
    facturas.proveedorPerfiles = [this] { return perfilesFacturas(); };
    historial.proveedor = [this](const PerfilId& perfil) { return historialDe(perfil); };
}

// Solo el reloj del guion: sin perfiles ni meses declarados.
materiales::DatosFacturas DemoVideo::datosFacturas(const DatosGuion& datos)
{
    materiales::DatosFacturas d;
    d.ahoraUtc = QDateTime::fromString(datos.ahora, Qt::ISODate).toUTC();
    return d;
}

materiales::DatosExportacion DemoVideo::datosExportacion()
{
    materiales::DatosExportacion d;
    d.retenerAlIniciar = false;
    return d;
}

// Perfiles visibles y activos del guion con el estado de su e.firma.
QList<PerfilFacturas> DemoVideo::perfilesFacturas() const
{
    QList<PerfilFacturas> lista;
    for (const PerfilResumen& p : perfiles.perfiles) {
        if (perfiles.ocultos.contains(p.id.texto()) || !p.activo) {
            continue;
        }
        const auto it = credenciales.resumenes.constFind(p.id.texto());
        const EstadoCredencial estado = it == credenciales.resumenes.cend() ? EstadoCredencial::SinCredencial
                                                                           : it->estado;
        lista.append(PerfilFacturas{p.id, p.rfc, p.nombre, estado});
    }
    return lista;
}

namespace {

ResultadoHistorial resultadoHistorial(const SolicitudResumen& r)
{
    if (r.paquetesDescargados > 0) {
        return ResultadoHistorial::Completa;
    }
    if (r.estadoLocal == EstadoLocal::EnvioFallido || r.estadoSat == EstadoSolicitudSat::Error
        || r.estadoSat == EstadoSolicitudSat::Rechazada || r.estadoSat == EstadoSolicitudSat::Vencida) {
        return ResultadoHistorial::RequiereAtencion;
    }
    return ResultadoHistorial::EnPreparacion;
}

} // namespace

// Una entrada "Anterior" por solicitud visible del perfil (mas reciente
// primero), con "Ver detalles tecnicos" hacia su detalle.
QList<EntradaHistorial> DemoVideo::historialDe(const PerfilId& perfil) const
{
    QString rfc;
    for (const PerfilResumen& p : perfiles.perfiles) {
        if (p.id == perfil) {
            rfc = p.rfc;
        }
    }
    QList<EntradaHistorial> entradas = lotesDe(perfil); // T024.4
    for (const SolicitudDetalle& d : solicitudes.detalles) {
        const SolicitudResumen& r = d.resumen;
        if (rfc.isEmpty() || r.perfilRfc != rfc || solicitudes.ocultas.contains(r.id.texto())) {
            continue;
        }
        EntradaHistorial e;
        e.tipo = EntradaHistorial::Tipo::Anterior;
        e.perfil = perfil;
        const std::optional<MesCalendario> mes = MesCalendario::de(r.fechaInicial);
        if (mes && MesCalendario::de(r.fechaFinal) == mes) {
            e.mes = mes;
        }
        e.desde = r.fechaInicial;
        e.hasta = r.fechaFinal;
        e.creadoEn = r.creadaEn;
        e.resultado = resultadoHistorial(r);
        const bool emitidos = r.tipoDescarga == TipoDescarga::Emitidos;
        e.tieneEmitidas = emitidos;
        e.tieneRecibidas = !emitidos;
        (emitidos ? e.diagnosticoEmitidas : e.diagnosticoRecibidas) = r.id;
        entradas.append(e);
    }
    std::stable_sort(entradas.begin(), entradas.end(),
                     [](const EntradaHistorial& a, const EntradaHistorial& b) { return a.creadoEn > b.creadoEn; });
    return entradas;
}

void DemoVideo::aplicarEstado(SolicitudDetalle& d, const QString& estado, const QString& clave)
{
    const int n = d.resumen.id.texto().right(2).toInt();
    const QDateTime creada = d.resumen.creadaEn;
    d.logs = {log(TipoEventoLog::SolicitudCreada, OrigenLog::Usuario, creada)};
    d.resumen.estadoLocal = EstadoLocal::Creada;
    d.resumen.estadoSat.reset();
    d.idSolicitudSat.reset();
    d.codEstatusSolicitud.reset();
    d.mensajeSolicitudSat.reset();
    d.codigoEstadoSolicitud.reset();
    d.numeroCfdi.reset();
    d.enviadaEn.reset();
    d.ultimaVerificacionEn.reset();
    d.paquetes.clear();
    if (estado == QLatin1String("enviando")) {
        d.resumen.estadoLocal = EstadoLocal::Enviando;
        d.logs.append(log(TipoEventoLog::EnvioIniciado, OrigenLog::Usuario, creada.addSecs(30)));
    } else if (estado != QLatin1String("creada")) {
        d.resumen.estadoLocal = EstadoLocal::Enviada;
        d.resumen.estadoSat = EstadoSolicitudSat::Aceptada;
        d.idSolicitudSat = QStringLiteral("4e80345d-917f-40bb-a98f-4a73939343%1").arg(n, 2, 10, QLatin1Char('0'));
        d.codEstatusSolicitud = QStringLiteral("5000");
        d.mensajeSolicitudSat = QStringLiteral("Solicitud Aceptada");
        d.enviadaEn = creada.addSecs(60);
        d.logs.append(log(TipoEventoLog::SolicitudEnviada, OrigenLog::Usuario, creada.addSecs(60)));
    }
    if (estado == QLatin1String("enProceso") || estado == QLatin1String("terminada")
        || estado == QLatin1String("descargada")) {
        d.resumen.estadoSat = EstadoSolicitudSat::EnProceso;
        d.ultimaVerificacionEn = creada.addSecs(10 * 60);
        d.logs.append(log(TipoEventoLog::VerificacionRealizada, OrigenLog::Worker, creada.addSecs(10 * 60)));
    }
    if (estado == QLatin1String("error")) {
        d.resumen.estadoSat = EstadoSolicitudSat::Error;
        d.codigoEstadoSolicitud = QStringLiteral("5004");
        d.ultimaVerificacionEn = creada.addSecs(10 * 60);
        d.logs.append(log(TipoEventoLog::VerificacionRealizada, OrigenLog::Worker, creada.addSecs(10 * 60)));
    }
    const QString paquete = m_paquetePorSolicitud.value(clave);
    const bool descargando = estado == QLatin1String("descargando");
    if (estado == QLatin1String("terminada") || descargando || estado == QLatin1String("descargada")) {
        d.resumen.estadoSat = EstadoSolicitudSat::Terminada;
        d.codigoEstadoSolicitud = QStringLiteral("5000");
        d.numeroCfdi = catalogo.indexados.value(d.resumen.id.texto()).size();
        d.ultimaVerificacionEn = creada.addSecs(30 * 60);
        d.logs.append(log(TipoEventoLog::VerificacionRealizada, OrigenLog::Worker, creada.addSecs(30 * 60)));
        if (!paquete.isEmpty()) {
            PaqueteResumen p;
            p.idPaqueteSat = paquete;
            p.estadoDescarga = descargando ? EstadoDescarga::Descargando : EstadoDescarga::Disponible;
            p.disponibleEn = creada.addSecs(30 * 60);
            if (estado == QLatin1String("descargada")) {
                p.estadoDescarga = EstadoDescarga::Descargado;
                p.descargadoEn = creada.addSecs(31 * 60);
            }
            d.paquetes = {p};
            d.logs.append(log(TipoEventoLog::PaquetesRegistrados, OrigenLog::Worker, creada.addSecs(30 * 60)));
            if (estado == QLatin1String("descargada")) {
                d.logs.append(log(TipoEventoLog::PaqueteDescargado, OrigenLog::Worker, creada.addSecs(31 * 60)));
            }
        }
    }
    contarPaquetes(d);
    if (estado == QLatin1String("descargada")) {
        catalogo.visibles.insert(d.resumen.id.texto());
    } else {
        catalogo.visibles.remove(d.resumen.id.texto());
    }
}

QString DemoVideo::preparar()
{
    QHash<QString, PerfilResumen> perfilPorClave;
    int np = 0;
    for (const PerfilGuion& p : m_datos.perfiles) {
        const PerfilResumen r{perfilId(++np), p.rfc, p.nombre, true};
        perfilPorClave.insert(p.clave, r);
        perfiles.perfiles.append(r);
        if (!p.visible) {
            perfiles.ocultos.insert(r.id.texto());
        }
        solicitudes.rfcPorPerfilId.insert(r.id.texto(), r.rfc);
        ResumenCredencial c;
        if (p.efirma == QLatin1String("lista")) {
            c.estado = EstadoCredencial::Lista;
            c.vigenteDesde = QDateTime(QDate(2025, 1, 1), QTime(0, 0), QTimeZone::UTC);
            c.vigenteHasta = QDateTime(QDate(2029, 1, 1), QTime(0, 0), QTimeZone::UTC);
            credenciales.resumenes.insert(r.id.texto(), c);
        } else if (p.efirma == QLatin1String("porVencer")) {
            c.estado = EstadoCredencial::Lista;
            c.vigenteDesde = m_ahora.addYears(-4);
            c.vigenteHasta = m_ahora.addDays(21);
            credenciales.resumenes.insert(r.id.texto(), c);
        }
        if (!p.vigenteHasta.isEmpty() && credenciales.resumenes.contains(r.id.texto())) {
            credenciales.resumenes[r.id.texto()].vigenteHasta = QDateTime::fromString(p.vigenteHasta, Qt::ISODate).toUTC();
        }
        m_perfilId.insert(p.clave, r.id);
    }
    solicitudes.nombrePerfilPorRfc = [this](const QString& rfc) {
        for (const PerfilResumen& p : perfiles.perfiles) {
            if (p.rfc == rfc) {
                return p.nombre;
            }
        }
        return QString();
    };

    const QString raiz = QDir(m_dir).filePath(QStringLiteral("paquetes"));
    previsualizacion.dirPdf = QDir(m_dir).filePath(QStringLiteral("pdf"));
    if (!QDir().mkpath(raiz) || !QDir().mkpath(previsualizacion.dirPdf)) {
        return QStringLiteral("no se pudo crear el directorio temporal de paquetes");
    }
    LectorZipLibzip lector;
    ArchivosZipCfdiFs archivos(raiz);
    EntornoCalculoIndice entorno{lector, archivos, [](QByteArrayView xml) { return parsearCfdi(xml); },
                                 kVersionParserCfdi};

    int ns = 0;
    for (const SolicitudGuion& s : m_datos.solicitudes) {
        ++ns;
        const PerfilResumen perfil = perfilPorClave.value(s.perfil);
        SolicitudDetalle d;
        d.resumen.id = solicitudId(ns);
        d.resumen.perfilRfc = perfil.rfc;
        d.resumen.perfilNombre = perfil.nombre;
        d.resumen.tipoDescarga = s.tipo == QLatin1String("emitidos") ? TipoDescarga::Emitidos : TipoDescarga::Recibidos;
        d.resumen.fechaInicial = QDate::fromString(s.desde, Qt::ISODate);
        d.resumen.fechaFinal = QDate::fromString(s.hasta, Qt::ISODate);
        d.resumen.creadaEn = m_ahora.addSecs(qint64(ns) * 60);
        d.fechaInicialSat = s.desde + QStringLiteral("T00:00:00");
        d.fechaFinalSat = s.hasta + QStringLiteral("T23:59:59");
        solicitudes.porClave.insert(s.clave, d.resumen.id);

        if (!s.cfdis.isEmpty()) {
            const QString paquete = idPaquete(ns);
            const auto relativa =
                rutapaquete::derivarRutaRelativa(UbicacionPaquete{perfil.rfc, d.fechaInicialSat, d.resumen.id.texto(), paquete});
            if (!relativa.esExito()) {
                return QStringLiteral("solicitud '%1': ruta de paquete invalida").arg(s.clave);
            }
            const QString zip = QDir(raiz).filePath(relativa.valor());
            QDir().mkpath(QFileInfo(zip).absolutePath());
            QList<QPair<QString, QByteArray>> entradas;
            const bool recibidos = d.resumen.tipoDescarga == TipoDescarga::Recibidos;
            int nc = 0;
            for (const CfdiGuion& c : s.cfdis) {
                const QString uuid = uuidCfdi(ns, nc++);
                const QString rfcOtro = otroRfc(perfil.rfc);
                const QByteArray xml = recibidos ? xmlCfdi(c, rfcOtro, c.contraparte, perfil.rfc, perfil.nombre, uuid)
                                                 : xmlCfdi(c, perfil.rfc, perfil.nombre, rfcOtro, c.contraparte, uuid);
                entradas.append({uuid + QStringLiteral(".xml"), xml});
            }
            if (const QString e = escribirZip(zip, entradas); !e.isEmpty()) {
                return QStringLiteral("solicitud '%1': %2").arg(s.clave, e);
            }
            // Indice con el codigo real (lector ZIP + parser + calculo).
            PaqueteIndexable indexable;
            indexable.paqueteId = paquete;
            indexable.solicitudId = d.resumen.id;
            indexable.rutaLocal = relativa.valor();
            const CalculoPaquete calculo = calcularIndicePaquete(indexable, entorno, [] { return true; }, m_ahora);
            if (calculo.tipo != CalculoPaquete::Tipo::Reemplazar
                || calculo.indice.estado != EstadoIndicePaquete::Indexado
                || calculo.indice.cfdis.size() != s.cfdis.size()) {
                return QStringLiteral("solicitud '%1': el ZIP sintetico no se indexo completo").arg(s.clave);
            }
            QList<CfdiListado> listados;
            ResumenIndiceSolicitud resumen;
            resumen.paquetesDescargados = 1;
            resumen.paquetesIndexados = 1;
            for (const CfdiIndexado& c : calculo.indice.cfdis) {
                listados.append(CfdiListado{paquete, c.posicion, c.resumen});
                ++resumen.cfdiTotal;
                resumen.cfdiParciales += c.resumen.estado == EstadoParseo::Parcial ? 1 : 0;
            }
            catalogo.indexados.insert(d.resumen.id.texto(), listados);
            catalogo.resumenIndexado.insert(d.resumen.id.texto(), resumen);
            previsualizacion.zipPorPaquete.insert(paquete, zip);
            m_paquetePorSolicitud.insert(s.clave, paquete);
        }
        aplicarEstado(d, s.estado, s.clave);
        if (!s.visible) {
            solicitudes.ocultas.insert(d.resumen.id.texto());
        }
        solicitudes.detalles.append(d);
    }
    if (const QString e = prepararMeses(raiz, entorno); !e.isEmpty()) {
        return e;
    }
    // T024.4: estado inicial del respaldo (datos.respaldo); respaldarAhora()
    // queda en curso hasta progresoRespaldo/resultadoRespaldo del guion.
    if (m_datos.respaldo.declarado) {
        EstadoRespaldoPresentacion e = materiales::ControlRespaldoSintetico::configurado();
        e.carpetaConfigurada = m_datos.respaldo.carpeta;
        e.nombreCarpeta = m_datos.respaldo.carpeta ? QStringLiteral("Respaldos") : QString();
        e.contrasenaConfigurada = m_datos.respaldo.contrasena;
        e.diarioActivado = m_datos.respaldo.diario;
        e.ultimoExitoUtc.reset();
        e.ultimoIntentoUtc.reset();
        respaldo.ultimo.reset();
        if (!m_datos.respaldo.ultimoExito.isEmpty()) {
            ResultadoRespaldoPresentacion r = materiales::ControlRespaldoSintetico::exitoDeEjemplo();
            r.fechaUtc = QDateTime::fromString(m_datos.respaldo.ultimoExito, Qt::ISODate).toUTC();
            respaldo.ultimo = r;
            e.ultimoExitoUtc = r.fechaUtc;
            e.ultimoIntentoUtc = r.fechaUtc;
        }
        respaldo.estadoActual = e;
    }
    return {};
}

QString DemoVideo::avanzar(const QString& clave, const QString& estado, const QString& verificadaEn)
{
    const auto it = solicitudes.porClave.constFind(clave);
    SolicitudDetalle* d = it == solicitudes.porClave.constEnd() ? nullptr : solicitudes.buscar(*it);
    if (d == nullptr) {
        return QStringLiteral("la solicitud '%1' no existe (las creadas en la UI son nueva1, nueva2...)").arg(clave);
    }
    aplicarEstado(*d, estado, clave);
    if (!verificadaEn.isEmpty()) {
        const QDateTime en = QDateTime::fromString(verificadaEn, Qt::ISODate).toUTC();
        d->ultimaVerificacionEn = en;
        d->logs.append(log(TipoEventoLog::VerificacionRealizada, OrigenLog::Usuario, en));
    }
    const SolicitudId id = d->resumen.id;
    emit solicitudes.listaCambiada();
    emit solicitudes.solicitudActualizada(id);
    emit catalogo.indiceCambiado(id);
    return {};
}

QString DemoVideo::cambiarEFirma(const QString& perfil, const QString& efirma, const QString& vigenteHasta)
{
    const auto it = m_perfilId.constFind(perfil);
    if (it == m_perfilId.constEnd()) {
        return QStringLiteral("perfil desconocido '%1'").arg(perfil);
    }
    if (efirma == QLatin1String("sinEFirma")) {
        credenciales.fijar(*it, std::nullopt);
    } else {
        ResumenCredencial c;
        c.estado = EstadoCredencial::Lista;
        c.vigenteDesde = m_ahora.addYears(-1);
        c.vigenteHasta = efirma == QLatin1String("porVencer") ? m_ahora.addDays(21) : m_ahora.addYears(3);
        if (!vigenteHasta.isEmpty()) {
            c.vigenteHasta = QDateTime::fromString(vigenteHasta, Qt::ISODate).toUTC();
        }
        credenciales.fijar(*it, c);
    }
    emit perfiles.perfilesCambiaron();
    return {};
}

QString DemoVideo::completarEFirma(const QString& perfil, const QString& vigenteHasta)
{
    const auto it = m_perfilId.constFind(perfil);
    if (it == m_perfilId.constEnd()) {
        return QStringLiteral("perfil desconocido '%1'").arg(perfil);
    }
    ResumenCredencial c;
    c.estado = EstadoCredencial::Lista;
    c.vigenteDesde = m_ahora.addYears(-1);
    c.vigenteHasta = vigenteHasta.isEmpty() ? m_ahora.addYears(3)
                                            : QDateTime::fromString(vigenteHasta, Qt::ISODate).toUTC();
    if (!credenciales.completar(*it, c)) {
        return QStringLiteral("el perfil '%1' no tiene una e.firma en validacion (falta pulsar Registrar)").arg(perfil);
    }
    emit perfiles.perfilesCambiaron();
    return {};
}

QString DemoVideo::mostrarSolicitudes(const QStringList& claves)
{
    solicitudes.ocultas.clear();
    QSet<QString> visibles;
    for (const QString& c : claves) {
        const auto it = solicitudes.porClave.constFind(c);
        if (it == solicitudes.porClave.constEnd()) {
            return QStringLiteral("solicitud desconocida '%1'").arg(c);
        }
        visibles.insert(it->texto());
    }
    for (const SolicitudDetalle& d : solicitudes.detalles) {
        if (!visibles.contains(d.resumen.id.texto())) {
            solicitudes.ocultas.insert(d.resumen.id.texto());
        }
    }
    emit solicitudes.listaCambiada();
    return {};
}

namespace {

class NotificadorCaptura final : public Notificador {
public:
    QList<Notificacion> recibidas;
    void notificar(const Notificacion& n) override { recibidas.append(n); }
};

QVariantMap notificacionIlustrada(const Notificacion& n)
{
    QStringList acciones;
    for (const OSIntegration::AccionNotificacion a : accionesExtraNotificacion(n)) {
        acciones.append(macos::textoAccionNotificacion(a));
    }
    return {{QStringLiteral("titulo"), n.titulo},
            {QStringLiteral("cuerpo"), n.cuerpo},
            {QStringLiteral("acciones"), acciones}};
}

constexpr int kUmbralVencimiento = 30; // T014.3 D2

} // namespace

QVariantList DemoVideo::notificaciones(const QVariantMap& parametros, QString& error) const
{
    QVariantList r;
    for (const QVariant& v : parametros.value(QStringLiteral("notificaciones")).toList()) {
        const QVariantMap p = v.toMap();
        const QString tipo = p.value(QStringLiteral("tipo")).toString();
        if (tipo == QLatin1String("descargaCompleta")) {
            const QString clave = p.value(QStringLiteral("solicitud")).toString();
            const auto it = solicitudes.porClave.constFind(clave);
            const SolicitudDetalle* d = nullptr;
            for (const SolicitudDetalle& x : solicitudes.detalles) {
                if (it != solicitudes.porClave.constEnd() && x.resumen.id == *it) {
                    d = &x;
                }
            }
            if (d == nullptr) {
                error = QStringLiteral("notificacion: solicitud desconocida '%1'").arg(clave);
                return {};
            }
            NotificadorCaptura notificador;
            ServicioNotificaciones servicio(notificador);
            TransicionNotificable t;
            t.solicitudId = d->resumen.id;
            t.tipo = TipoTransicionNotificable::DescargaCompleta;
            t.tipoDescarga = d->resumen.tipoDescarga;
            t.rfcSolicitante = d->resumen.perfilRfc;
            t.fechaInicialSat = d->fechaInicialSat;
            t.fechaFinalSat = d->fechaFinalSat;
            t.paquetes = std::max(1, d->resumen.totalPaquetes);
            t.descargados = t.paquetes;
            servicio.alConfirmarTransicion(t);
            if (notificador.recibidas.isEmpty()) {
                error = QStringLiteral("notificacion: ServicioNotificaciones no genero la de descarga completa");
                return {};
            }
            r.append(notificacionIlustrada(notificador.recibidas.constFirst()));
        } else if (tipo == QLatin1String("facturasDescargadas")) {
            // T024.4: la de ServicioNotificaciones al completar la descarga
            // de una direccion del mes (mismo texto que la app).
            const QString perfil = p.value(QStringLiteral("perfil")).toString();
            const QString mes = p.value(QStringLiteral("mes")).toString();
            const EstadoMes* m = estadoMes(perfil, mes);
            if (m == nullptr) {
                error = QStringLiteral("notificacion: el mes %1 de '%2' no esta en datos.meses").arg(mes, perfil);
                return {};
            }
            const TipoDescarga direccion = p.value(QStringLiteral("direccion")).toString() == QLatin1String("recibidas")
                                               ? TipoDescarga::Recibidos
                                               : TipoDescarga::Emitidos;
            QString rfc;
            int n = 0;
            for (const PerfilGuion& x : m_datos.perfiles) {
                ++n;
                if (x.clave == perfil) {
                    rfc = x.rfc;
                    break;
                }
            }
            TransicionNotificable t;
            t.solicitudId = materiales::solicitudIdFija((n * 1'000'000 + m->mes.anio() * 100 + m->mes.mes()) * 10
                                                        + (direccion == TipoDescarga::Emitidos ? 1 : 2));
            t.tipo = TipoTransicionNotificable::DescargaCompleta;
            t.tipoDescarga = direccion;
            t.rfcSolicitante = rfc;
            t.fechaInicialSat = m->mes.primerDia().toString(Qt::ISODate) + QStringLiteral("T00:00:00");
            t.fechaFinalSat = m->mes.ultimoDia().toString(Qt::ISODate) + QStringLiteral("T23:59:59");
            t.paquetes = 1;
            t.descargados = 1;
            const std::optional<Notificacion> notificacion = ServicioNotificaciones::componer(t);
            if (!notificacion) {
                error = QStringLiteral("notificacion: ServicioNotificaciones no genero la de facturas descargadas");
                return {};
            }
            r.append(notificacionIlustrada(*notificacion));
        } else {
            const QString clave = p.value(QStringLiteral("perfil")).toString();
            const auto it = m_perfilId.constFind(clave);
            if (it == m_perfilId.constEnd()) {
                error = QStringLiteral("notificacion: perfil desconocido '%1'").arg(clave);
                return {};
            }
            const ResumenCredencial c = credenciales.resumenes.value(it->texto());
            if (!c.vigenteHasta) {
                error = QStringLiteral("notificacion: el perfil '%1' no tiene e.firma con vigencia").arg(clave);
                return {};
            }
            QString rfc;
            for (const PerfilResumen& x : perfiles.perfiles) {
                if (x.id == *it) {
                    rfc = x.rfc;
                }
            }
            const int dias = int(m_ahora.date().daysTo(c.vigenteHasta->toUTC().date()));
            r.append(notificacionIlustrada(
                ServicioNotificaciones::componerVencimiento(*it, rfc, *c.vigenteHasta, kUmbralVencimiento, dias)));
        }
    }
    return r;
}

// --- T024.4: acciones vigiladas (D10) ------------------------------------------------

QFuture<ResultadoAccionFinder> AccionesVigiladas::prohibida(const QString& que)
{
    if (violacion.isEmpty()) {
        violacion = QStringLiteral("la UI pidio %1 durante la grabacion (D10: usa un clic con \"sinEfecto\" y la "
                                   "ilustracion finder)")
                        .arg(que);
    }
    return listo(ResultadoAccionFinder{ResultadoAccionFinder::Estado::Mostrado, {}});
}

QFuture<ResultadoAccionFinder> AccionesVigiladas::mostrarPaquete(const SolicitudId&, const QString&)
{
    return prohibida(QStringLiteral("mostrar el ZIP en Finder"));
}

QFuture<ResultadoAccionFinder> AccionesVigiladas::abrirCarpetaSolicitud(const SolicitudId&)
{
    return prohibida(QStringLiteral("abrir la carpeta en Finder"));
}

QFuture<ResultadoAccionFinder> AccionesVigiladas::abrirCarpetaPaquetes()
{
    return prohibida(QStringLiteral("abrir la carpeta de descargas en Finder"));
}

QFuture<ResultadoAccionFinder> AccionesVigiladas::abrirEnVistaPrevia(const QString&)
{
    return prohibida(QStringLiteral("abrir el PDF en Vista Previa"));
}

QString DemoVideo::violacion() const
{
    return !perfiles.violacion.isEmpty() ? perfiles.violacion : acciones.violacion;
}

// --- T024.4: descarga guiada simulada (datos.meses, avanzarMes) ------------------------

namespace {

using materiales::EstadoDireccionSintetico;

std::optional<CausaAtencion> causaDeClave(const QString& clave)
{
    for (int i = 0; i <= int(CausaAtencion::IndexacionSinProgramar); ++i) {
        const auto c = CausaAtencion(i);
        if (claveEstable(c) == clave) {
            return c;
        }
    }
    return std::nullopt;
}

EstadoDireccionSintetico estadoDireccion(const QString& estado)
{
    if (estado == QLatin1String("preparando")) {
        return EstadoDireccionSintetico::Preparando;
    }
    if (estado == QLatin1String("disponibles") || estado == QLatin1String("disponiblesParcialmente")) {
        return EstadoDireccionSintetico::Disponibles;
    }
    if (estado == QLatin1String("requiereAtencion")) {
        return EstadoDireccionSintetico::RequiereAtencion;
    }
    return EstadoDireccionSintetico::SinDescargar;
}

bool conFacturas(EstadoDireccionSintetico e)
{
    return e == EstadoDireccionSintetico::Disponibles || e == EstadoDireccionSintetico::RequiereAtencion;
}

// Solicitud fuente fija de un mes y direccion (no choca con solicitudId(n)).
SolicitudId solicitudDeMes(int perfil, MesCalendario mes, TipoDescarga direccion)
{
    return materiales::solicitudIdFija((perfil * 1'000'000 + mes.anio() * 100 + mes.mes()) * 10
                                       + (direccion == TipoDescarga::Emitidos ? 1 : 2));
}

QString claveMesGuion(const QString& perfil, const QString& mes) { return perfil + QLatin1Char('/') + mes; }

} // namespace

const DemoVideo::EstadoMes* DemoVideo::estadoMes(const QString& perfil, const QString& mes) const
{
    const auto it = m_meses.constFind(claveMesGuion(perfil, mes));
    return it == m_meses.constEnd() ? nullptr : &*it;
}

QString DemoVideo::prepararMeses(const QString& raiz, EntornoCalculoIndice& entorno)
{
    QHash<QString, int> indicePerfil;
    QHash<QString, const PerfilGuion*> perfilPorClave;
    int np = 0;
    for (const PerfilGuion& p : m_datos.perfiles) {
        indicePerfil.insert(p.clave, ++np);
        perfilPorClave.insert(p.clave, &p);
        // Todos (tambien los visible:false): Facturas pide sus meses en cuanto
        // la UI los crea; la visibilidad la decide proveedorPerfiles.
        materiales::PerfilDeclarado d;
        d.id = m_perfilId.value(p.clave);
        d.rfc = p.rfc;
        d.nombre = p.nombre;
        facturas.declararPerfil(d);
    }
    for (const MesGuion& g : m_datos.meses) {
        const PerfilGuion& perfil = *perfilPorClave.value(g.perfil);
        EstadoMes m;
        m.datos = g;
        m.perfil = m_perfilId.value(g.perfil);
        m.mes = *MesCalendario::crear(g.mes.left(4).toInt(), g.mes.mid(5, 2).toInt());
        const EstadoDireccionSintetico e = estadoDireccion(g.estado);
        const bool emitidas = g.direccion != QLatin1String("recibidas");
        const bool recibidas = g.direccion != QLatin1String("emitidas");
        // La otra direccion de un estado de una sola direccion: Disponibles
        // si requiere atencion (la otra se descargo), si no Sin descargar.
        const EstadoDireccionSintetico otra = g.estado == QLatin1String("requiereAtencion")
                                                  ? EstadoDireccionSintetico::Disponibles
                                                  : EstadoDireccionSintetico::SinDescargar;
        m.emitidas = emitidas ? e : otra;
        m.recibidas = recibidas ? e : otra;
        if (!g.causa.isEmpty()) {
            m.causa = causaDeClave(g.causa).value_or(CausaAtencion::ErrorSat);
        }
        m.lote = g.estado != QLatin1String("sinDescargar");
        if (!g.descargadoEn.isEmpty()) {
            m.creadoEn = QDateTime::fromString(g.descargadoEn, Qt::ISODate).toUTC();
        } else {
            // Primer dia del mes siguiente, 16:00 UTC (nunca despues de "ahora").
            m.creadoEn = std::min(QDateTime(m.mes.ultimoDia().addDays(1), QTime(16, 0), QTimeZone::UTC), m_ahora);
        }
        for (const TipoDescarga direccion : {TipoDescarga::Emitidos, TipoDescarga::Recibidos}) {
            const bool esEmitidas = direccion == TipoDescarga::Emitidos;
            const QList<CfdiGuion>& cfdis = esEmitidas ? g.emitidas : g.recibidas;
            QList<CfdiMensual>& lista = esEmitidas ? m.listaEmitidas : m.listaRecibidas;
            if (cfdis.isEmpty()) {
                continue;
            }
            const int n = indicePerfil.value(g.perfil);
            const SolicitudId solicitud = solicitudDeMes(n, m.mes, direccion);
            // Mismo formato que los del SAT (y la ruta canonica de la app).
            const QString paquete = QStringLiteral("0F1E2D3C-4B5A-4978-8796-%1_01")
                                        .arg(solicitud.texto().right(12));
            const auto ruta = rutapaquete::derivarRutaRelativa(
                UbicacionPaquete{perfil.rfc, m.mes.primerDia().toString(Qt::ISODate) + QStringLiteral("T00:00:00"),
                                 solicitud.texto(), paquete});
            if (!ruta.esExito()) {
                return QStringLiteral("mes '%1' de '%2': ruta de paquete invalida").arg(g.mes, g.perfil);
            }
            const QString relativa = ruta.valor();
            const QString zip = QDir(raiz).filePath(relativa);
            QDir().mkpath(QFileInfo(zip).absolutePath());
            QList<QPair<QString, QByteArray>> entradas;
            const int semilla = (m.mes.anio() % 100) * 12 + m.mes.mes() + (esEmitidas ? 0 : 50) + n * 7;
            int nc = 0;
            for (const CfdiGuion& c : cfdis) {
                const QString uuid = uuidCfdi(semilla, nc++);
                const QString rfcOtro = otroRfc(perfil.rfc);
                const QByteArray xml = esEmitidas ? xmlCfdi(c, perfil.rfc, perfil.nombre, rfcOtro, c.contraparte, uuid)
                                                  : xmlCfdi(c, rfcOtro, c.contraparte, perfil.rfc, perfil.nombre, uuid);
                entradas.append({uuid + QStringLiteral(".xml"), xml});
            }
            if (const QString e2 = escribirZip(zip, entradas); !e2.isEmpty()) {
                return QStringLiteral("mes '%1' de '%2': %3").arg(g.mes, g.perfil, e2);
            }
            PaqueteIndexable indexable;
            indexable.paqueteId = paquete;
            indexable.solicitudId = solicitud;
            indexable.rutaLocal = relativa;
            const CalculoPaquete calculo = calcularIndicePaquete(indexable, entorno, [] { return true; }, m_ahora);
            if (calculo.tipo != CalculoPaquete::Tipo::Reemplazar
                || calculo.indice.estado != EstadoIndicePaquete::Indexado
                || calculo.indice.cfdis.size() != cfdis.size()) {
                return QStringLiteral("mes '%1' de '%2': el ZIP sintetico no se indexo completo").arg(g.mes, g.perfil);
            }
            for (const CfdiIndexado& c : calculo.indice.cfdis) {
                lista.append(CfdiMensual{CfdiListado{paquete, c.posicion, c.resumen}, direccion, solicitud, true});
            }
            // Como la app: fecha descendente.
            std::stable_sort(lista.begin(), lista.end(), [](const CfdiMensual& a, const CfdiMensual& b) {
                return a.cfdi.resumen.fecha > b.cfdi.resumen.fecha;
            });
            previsualizacion.zipPorPaquete.insert(paquete, zip);
        }
        const QString clave = claveMesGuion(g.perfil, g.mes);
        m_meses.insert(clave, m);
        publicarMes(m_meses[clave], false);
    }
    facturas.alEjecutar = [this](const PreplanGuiado& preplan) {
        // Como el worker: las direcciones sin facturas pasan a Preparando y el
        // lote aparece en el Historial (el guion lo termina con avanzarMes).
        for (EstadoMes& m : m_meses) {
            if (m.perfil != preplan.perfil || m.mes != preplan.mes) {
                continue;
            }
            for (EstadoDireccionSintetico* e : {&m.emitidas, &m.recibidas}) {
                if (*e != EstadoDireccionSintetico::Disponibles) {
                    *e = EstadoDireccionSintetico::Preparando;
                }
            }
            m.lote = true;
            m.creadoEn = m_ahora;
            publicarMes(m, false); // ejecutar() ya recarga el mes
            emit historial.datosCambiados(m.perfil);
        }
    };
    return {};
}

void DemoVideo::publicarMes(EstadoMes& m, bool avisar)
{
    materiales::MesDeclarado d;
    d.estadoEmitidas = m.emitidas;
    d.estadoRecibidas = m.recibidas;
    d.causa = m.causa;
    d.direccionCausa =
        m.recibidas == EstadoDireccionSintetico::RequiereAtencion ? TipoDescarga::Recibidos : TipoDescarga::Emitidos;
    d.listaEmitidas = conFacturas(m.emitidas) ? m.listaEmitidas : QList<CfdiMensual>{};
    d.listaRecibidas = conFacturas(m.recibidas) ? m.listaRecibidas : QList<CfdiMensual>{};
    d.emitidas = int(d.listaEmitidas->size());
    d.recibidas = int(d.listaRecibidas->size());
    facturas.fijarMes(m.perfil, m.mes.anio(), m.mes.mes(), d, avisar);
    if (avisar) {
        emit historial.datosCambiados(m.perfil);
    }
}

QString DemoVideo::avanzarMes(const QString& perfil, const QString& mes, const QString& direccion,
                              const QString& estado, const QString& causa)
{
    const auto it = m_meses.find(claveMesGuion(perfil, mes));
    if (it == m_meses.end()) {
        return QStringLiteral("avanzarMes: el mes %1 de '%2' no esta en datos.meses").arg(mes, perfil);
    }
    EstadoMes& m = *it;
    const EstadoDireccionSintetico e = estadoDireccion(estado);
    const bool emitidas = direccion.isEmpty() || direccion != QLatin1String("recibidas");
    const bool recibidas = direccion.isEmpty() || direccion != QLatin1String("emitidas");
    if (emitidas) {
        m.emitidas = e;
    }
    if (recibidas) {
        m.recibidas = e;
    }
    // Parcial: la direccion indicada completa y la otra sin descargar.
    if (estado == QLatin1String("disponiblesParcialmente")) {
        (emitidas ? m.recibidas : m.emitidas) = EstadoDireccionSintetico::SinDescargar;
    }
    if (!causa.isEmpty()) {
        m.causa = causaDeClave(causa).value_or(m.causa);
    }
    if (estado != QLatin1String("sinDescargar")) {
        m.lote = true;
        if (!m.creadoEn.isValid() || m.creadoEn > m_ahora) {
            m.creadoEn = m_ahora;
        }
    }
    publicarMes(m, true);
    return {};
}

QList<EntradaHistorial> DemoVideo::lotesDe(const PerfilId& perfil) const
{
    QList<EntradaHistorial> r;
    for (auto it = m_meses.cbegin(); it != m_meses.cend(); ++it) {
        const EstadoMes& m = *it;
        if (m.perfil != perfil || !m.lote) {
            continue;
        }
        EntradaHistorial e;
        e.tipo = EntradaHistorial::Tipo::Guiada;
        e.loteId = QStringLiteral("lote-") + it.key();
        e.perfil = perfil;
        e.mes = m.mes;
        e.desde = m.mes.primerDia();
        e.hasta = m.mes.ultimoDia();
        e.creadoEn = m.creadoEn;
        e.emitidas = conFacturas(m.emitidas) ? int(m.listaEmitidas.size()) : 0;
        e.recibidas = conFacturas(m.recibidas) ? int(m.listaRecibidas.size()) : 0;
        e.tieneEmitidas = m.emitidas != EstadoDireccionSintetico::SinDescargar;
        e.tieneRecibidas = m.recibidas != EstadoDireccionSintetico::SinDescargar;
        const auto hay = [&m](EstadoDireccionSintetico x) { return m.emitidas == x || m.recibidas == x; };
        if (hay(EstadoDireccionSintetico::RequiereAtencion)) {
            e.resultado = ResultadoHistorial::RequiereAtencion;
            e.puedeReintentar = true;
        } else if (hay(EstadoDireccionSintetico::Preparando)) {
            e.resultado = ResultadoHistorial::EnPreparacion;
        } else if (e.emitidas + e.recibidas == 0) {
            e.resultado = ResultadoHistorial::SinFacturas;
        } else {
            e.resultado = ResultadoHistorial::Completa;
        }
        r.append(e);
    }
    return r;
}

QString DemoVideo::progresoRespaldo(const QString& fase, qint64 bytes, qint64 total)
{
    if (!respaldo.estadoActual.enCurso) {
        return QStringLiteral("progresoRespaldo: no hay un respaldo en curso (falta pulsar \"Respaldar ahora\")");
    }
    using Fase = respaldo::RespaldoService::Fase;
    const Fase f = fase == QLatin1String("captura")       ? Fase::Captura
                   : fase == QLatin1String("escritura")   ? Fase::Escritura
                   : fase == QLatin1String("publicacion") ? Fase::Publicacion
                                                          : Fase::Depuracion;
    respaldo.emitirProgreso(f, quint64(bytes), quint64(total));
    return {};
}

QString DemoVideo::resultadoRespaldo(const QString& resultado, const QString& fechaIso, qint64 bytes)
{
    if (!respaldo.estadoActual.enCurso) {
        return QStringLiteral("resultadoRespaldo: no hay un respaldo en curso (falta pulsar \"Respaldar ahora\")");
    }
    ResultadoRespaldoPresentacion r;
    r.fechaUtc = fechaIso.isEmpty() ? m_ahora : QDateTime::fromString(fechaIso, Qt::ISODate).toUTC();
    r.bytes = quint64(bytes);
    if (resultado == QLatin1String("conAvisos")) {
        r.estado = respaldo::ResultadoRespaldo::Estado::ExitoConAdvertencias;
        r.omitidos = 1;
    } else if (resultado == QLatin1String("error")) {
        r.estado = respaldo::ResultadoRespaldo::Estado::ErrorDestino;
        r.errorDestino = respaldo::ErrorDestino::Categoria::SinEscritura;
        r.bytes = 0;
    }
    respaldo.terminarRespaldo(r);
    return {};
}

} // namespace video
