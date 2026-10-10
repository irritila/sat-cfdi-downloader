#include "application/requests/DemoSolicitudesService.h"

#include "application/requests/PreparacionSolicitud.h"
#include "domain/common/TimestampUtc.h"
#include "domain/solicitudes/Duplicados.h"
#include "domain/solicitudes/SolicitudCanonica.h"

#include <QFuture>
#include <QTimeZone>

#include <utility>

namespace satcfdi {

namespace {

SolicitudId idFijo(int n)
{
    // UUID canonicos deterministas para datos demo y pruebas.
    const auto texto = QStringLiteral("00000000-0000-4000-8000-%1")
                           .arg(n, 12, 10, QLatin1Char('0'));
    return *SolicitudId::desdeTexto(texto);
}

QDateTime utc(int dia, int hora)
{
    return QDateTime(QDate(2026, 9, dia), QTime(hora, 0), QTimeZone(QTimeZone::UTC));
}

PaqueteResumen paquete(int solicitud, int n, EstadoDescarga estado)
{
    PaqueteResumen p;
    p.idPaqueteSat = QStringLiteral("DEMO-PAQ-%1_%2")
                         .arg(solicitud, 4, 10, QLatin1Char('0'))
                         .arg(n, 2, 10, QLatin1Char('0'));
    p.estadoDescarga = estado;
    p.disponibleEn = utc(20, 9);
    if (estado == EstadoDescarga::Descargado) {
        p.descargadoEn = utc(20, 10);
    }
    if (estado == EstadoDescarga::Vencido) {
        p.vencidoEn = utc(23, 9);
    }
    return p;
}

// Llena resumen/filtros del detalle a partir de la forma canonica.
void aplicarCanonica(SolicitudDetalle& d, const SolicitudCanonica& c, QDate inicial, QDate final_)
{
    d.resumen.perfilRfc = c.rfcSolicitante();
    d.resumen.rfcContraparte = c.rfcContraparte();
    d.resumen.tipoDescarga = c.tipoDescarga();
    d.resumen.fechaInicial = inicial;
    d.resumen.fechaFinal = final_;
    d.fechaInicialSat = c.fechaInicialSat();
    d.fechaFinalSat = c.fechaFinalSat();
    d.rfcContrapartes = c.tipoDescarga() == TipoDescarga::Emitidos
                            ? c.rfcReceptores()
                            : (c.rfcEmisor() ? QStringList{*c.rfcEmisor()} : QStringList{});
    d.tipoComprobante = c.tipoComprobante();
    d.complemento = c.complemento();
}

LogResumen logUsuario(TipoEventoLog tipo, const QDateTime& en)
{
    LogResumen l;
    l.tipoEvento = tipo;
    l.origen = OrigenLog::Usuario;
    l.creadoEn = en;
    return l;
}

struct Semilla {
    EstadoLocal local;
    std::optional<EstadoSolicitudSat> sat;
    QList<EstadoDescarga> paquetes;
};

QList<DemoSolicitudesService::Registro>
solicitudesRepresentativas(const QList<PerfilResumen>& perfiles)
{
    using L = EstadoLocal;
    using S = EstadoSolicitudSat;
    using D = EstadoDescarga;

    // Una por cada clave de estadoResumen. Con estado SAT, el estado local es
    // Enviada (solo una solicitud enviada puede tener estado SAT).
    const QList<Semilla> semillas = {
        {L::Creada, std::nullopt, {}},
        {L::Enviando, std::nullopt, {}},
        {L::Enviada, std::nullopt, {}},
        {L::EnvioFallido, std::nullopt, {}},
        {L::EnvioIncierto, std::nullopt, {}},
        {L::Enviada, S::Aceptada, {}},
        {L::Enviada, S::EnProceso, {}},
        {L::Enviada, S::Terminada,
         {D::Disponible, D::Descargando, D::Descargado, D::Error}},
        {L::Enviada, S::Error, {}},
        {L::Enviada, S::Rechazada, {}},
        {L::Enviada, S::Vencida, {D::Vencido}},
    };

    QList<DemoSolicitudesService::Registro> resultado;
    resultado.reserve(semillas.size());
    for (qsizetype i = 0; i < semillas.size(); ++i) {
        const Semilla& s = semillas.at(i);
        const int n = int(i) + 1;
        const PerfilResumen& perfil = perfiles.at(i % perfiles.size());

        EntradaSolicitudCanonica entrada;
        entrada.tipoDescarga = (n % 3 == 0) ? TipoDescarga::Recibidos : TipoDescarga::Emitidos;
        entrada.rfcPerfil = perfil.rfc;
        entrada.fechaInicial = QDate(2026, 8, 1);
        entrada.fechaFinal = QDate(2026, 8, 31);
        if (n % 2 == 0) {
            entrada.rfcContrapartes = {QStringLiteral("XAXX010101000")};
        }
        // Los datos demo son validos por construccion.
        const SolicitudCanonica canonica = SolicitudCanonica::normalizar(entrada).valor();

        DemoSolicitudesService::Registro r;
        SolicitudDetalle& d = r.detalle;
        d.resumen.id = idFijo(n);
        aplicarCanonica(d, canonica, entrada.fechaInicial, entrada.fechaFinal);
        d.resumen.estadoLocal = s.local;
        d.resumen.estadoSat = s.sat;
        d.resumen.creadaEn = utc(1 + n, 8);
        for (qsizetype p = 0; p < s.paquetes.size(); ++p) {
            d.paquetes.append(paquete(n, int(p) + 1, s.paquetes.at(p)));
        }
        d.resumen.totalPaquetes = int(d.paquetes.size());
        // T014.1 D2: mismos conteos que SolicitudesServicePersistido.
        for (const PaqueteResumen& paq : d.paquetes) {
            if (paq.estadoDescarga == EstadoDescarga::Descargado) {
                ++d.resumen.paquetesDescargados;
            } else if (paq.estadoDescarga == EstadoDescarga::Disponible || paq.estadoDescarga == EstadoDescarga::Error) {
                ++d.resumen.paquetesPendientesDescarga;
            }
        }
        d.logs.append(logUsuario(TipoEventoLog::SolicitudCreada, d.resumen.creadaEn));
        r.dedupKey = canonica.dedupKey();
        resultado.prepend(std::move(r)); // mas recientes primero
    }
    return resultado;
}

CoincidenciaDuplicado coincidencia(const DemoSolicitudesService::Registro& r)
{
    CoincidenciaDuplicado c;
    c.id = r.detalle.resumen.id;
    c.estadoLocal = r.detalle.resumen.estadoLocal;
    c.estadoSat = r.detalle.resumen.estadoSat;
    c.eliminada = r.eliminadoEn.has_value();
    for (const PaqueteResumen& p : r.detalle.paquetes) {
        c.paquetesNoEliminados.append(p.estadoDescarga);
    }
    return c;
}

} // namespace

DemoSolicitudesService::DemoSolicitudesService(QList<PerfilResumen> perfiles,
                                               Datos datos,
                                               QObject* parent)
    : SolicitudesService(parent)
    , m_perfiles(std::move(perfiles))
{
    if (datos == Datos::Representativos && !m_perfiles.isEmpty()) {
        m_registros = solicitudesRepresentativas(m_perfiles);
    }
}

void DemoSolicitudesService::setPerfiles(QList<PerfilResumen> perfiles)
{
    m_perfiles = std::move(perfiles);
}

QFuture<SolicitudesService::ResultadoLista> DemoSolicitudesService::listar()
{
    QList<SolicitudResumen> lista;
    lista.reserve(m_registros.size());
    for (const Registro& r : std::as_const(m_registros)) {
        if (!r.eliminadoEn) {
            lista.append(r.detalle.resumen);
        }
    }
    return QtFuture::makeReadyValueFuture(ResultadoLista::exito(std::move(lista)));
}

QFuture<SolicitudesService::ResultadoDetalle>
DemoSolicitudesService::obtener(const SolicitudId& id)
{
    for (const Registro& r : std::as_const(m_registros)) {
        if (!id.esNulo() && !r.eliminadoEn && r.detalle.resumen.id == id) {
            return QtFuture::makeReadyValueFuture(ResultadoDetalle::exito(r.detalle));
        }
    }
    return QtFuture::makeReadyValueFuture(ResultadoDetalle::fallo(
        ErrorObtener{ErrorObtener::Tipo::NoEncontrada,
                     QStringLiteral("La solicitud no existe."), std::nullopt}));
}

Resultado<EvaluacionDuplicado, ErrorCrear>
DemoSolicitudesService::evaluar(const NuevaSolicitudRequest& request,
                                std::optional<SolicitudCanonica>* canonica) const
{
    auto preparada = prepararSolicitud(request, rfcPerfilActivo(request.perfilId));
    if (!preparada.esExito()) {
        return Resultado<EvaluacionDuplicado, ErrorCrear>::fallo(std::move(preparada).error());
    }
    const DedupKey clave = preparada.valor().dedupKey();
    QList<CoincidenciaDuplicado> coincidencias;
    for (const Registro& r : m_registros) {
        if (r.dedupKey == clave) {
            coincidencias.append(coincidencia(r));
        }
    }
    if (canonica != nullptr) {
        canonica->emplace(std::move(preparada).valor());
    }
    return Resultado<EvaluacionDuplicado, ErrorCrear>::exito(
        clasificarCoincidencias(clave, coincidencias));
}

QFuture<SolicitudesService::ResultadoEvaluarDuplicado>
DemoSolicitudesService::evaluarDuplicado(const NuevaSolicitudRequest& request)
{
    return QtFuture::makeReadyValueFuture(evaluar(request, nullptr));
}

QFuture<SolicitudesService::ResultadoCrear>
DemoSolicitudesService::crearLocal(const NuevaSolicitudRequest& request,
                                   ConfirmacionDuplicado confirmacion)
{
    std::optional<SolicitudCanonica> canonica;
    auto evaluacion = evaluar(request, &canonica);
    if (!evaluacion.esExito()) {
        return QtFuture::makeReadyValueFuture(ResultadoCrear::fallo(std::move(evaluacion).error()));
    }
    const EvaluacionDuplicado& e = evaluacion.valor();
    if (e.clasificacion == ClasificacionDuplicado::Bloqueado) {
        return QtFuture::makeReadyValueFuture(ResultadoCrear::fallo(ErrorCrear::dedupBloqueado(e)));
    }
    const bool confirmada = confirmacion == ConfirmacionDuplicado::Confirmada;
    if (e.clasificacion == ClasificacionDuplicado::RequiereConfirmacion && !confirmada) {
        return QtFuture::makeReadyValueFuture(
            ResultadoCrear::fallo(ErrorCrear::requiereConfirmacion(e)));
    }

    Registro r;
    SolicitudDetalle& d = r.detalle;
    d.resumen.id = SolicitudId::generar();
    aplicarCanonica(d, *canonica, request.fechaInicial, request.fechaFinal);
    d.resumen.estadoLocal = EstadoLocal::Creada;
    d.resumen.creadaEn = timestamp::ahoraUtc();
    d.resumen.totalPaquetes = 0;
    d.logs.append(logUsuario(TipoEventoLog::SolicitudCreada, d.resumen.creadaEn));
    if (e.clasificacion == ClasificacionDuplicado::RequiereConfirmacion) {
        d.logs.append(logUsuario(TipoEventoLog::DuplicadoConfirmado, d.resumen.creadaEn));
    }
    r.dedupKey = canonica->dedupKey();

    const SolicitudId id = d.resumen.id;
    m_registros.prepend(std::move(r));
    emit listaCambiada();
    emit solicitudActualizada(id);
    return QtFuture::makeReadyValueFuture(ResultadoCrear::exito(id));
}

QFuture<SolicitudesService::ResultadoEliminar>
DemoSolicitudesService::eliminar(const SolicitudId& id)
{
    for (Registro& r : m_registros) {
        if (!id.esNulo() && !r.eliminadoEn && r.detalle.resumen.id == id) {
            const QDateTime ahora = timestamp::ahoraUtc();
            r.eliminadoEn = ahora;
            emit listaCambiada();
            emit solicitudEliminada(id);
            return QtFuture::makeReadyValueFuture(
                ResultadoEliminar::exito(ResultadoEliminacion{true, ahora}));
        }
    }
    return QtFuture::makeReadyValueFuture(
        ResultadoEliminar::exito(ResultadoEliminacion{false, std::nullopt}));
}

std::optional<QString> DemoSolicitudesService::rfcPerfilActivo(const PerfilId& id) const
{
    for (const PerfilResumen& p : m_perfiles) {
        if (!id.esNulo() && p.id == id && p.activo) {
            return p.rfc;
        }
    }
    return std::nullopt;
}

} // namespace satcfdi
