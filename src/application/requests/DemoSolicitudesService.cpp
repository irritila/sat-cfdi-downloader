#include "application/requests/DemoSolicitudesService.h"

#include <QTimeZone>
#include <QFuture>

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
    return p;
}

struct Semilla {
    EstadoLocal local;
    std::optional<EstadoSolicitudSat> sat;
    QList<EstadoDescarga> paquetes;
};

QList<SolicitudDetalle> solicitudesRepresentativas(const QList<PerfilResumen>& perfiles)
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

    QList<SolicitudDetalle> resultado;
    resultado.reserve(semillas.size());
    for (qsizetype i = 0; i < semillas.size(); ++i) {
        const Semilla& s = semillas.at(i);
        const int n = int(i) + 1;
        const PerfilResumen& perfil = perfiles.at(i % perfiles.size());

        SolicitudDetalle d;
        d.resumen.id = idFijo(n);
        d.resumen.perfilRfc = perfil.rfc;
        if (n % 2 == 0) {
            d.resumen.rfcContraparte = QStringLiteral("XAXX010101000");
        }
        d.resumen.tipoDescarga = (n % 3 == 0) ? TipoDescarga::Recibidos
                                              : TipoDescarga::Emitidos;
        d.resumen.fechaInicial = QDate(2026, 8, 1);
        d.resumen.fechaFinal = QDate(2026, 8, 31);
        d.resumen.estadoLocal = s.local;
        d.resumen.estadoSat = s.sat;
        d.resumen.creadaEn = utc(1 + n, 8);
        for (qsizetype p = 0; p < s.paquetes.size(); ++p) {
            d.paquetes.append(paquete(n, int(p) + 1, s.paquetes.at(p)));
        }
        d.resumen.totalPaquetes = int(d.paquetes.size());
        resultado.prepend(std::move(d)); // mas recientes primero
    }
    return resultado;
}

} // namespace

DemoSolicitudesService::DemoSolicitudesService(QList<PerfilResumen> perfiles,
                                               Datos datos,
                                               QObject* parent)
    : SolicitudesService(parent)
    , m_perfiles(std::move(perfiles))
{
    if (datos == Datos::Representativos && !m_perfiles.isEmpty()) {
        m_solicitudes = solicitudesRepresentativas(m_perfiles);
    }
}

QFuture<SolicitudesService::ResultadoLista> DemoSolicitudesService::listar()
{
    QList<SolicitudResumen> lista;
    lista.reserve(m_solicitudes.size());
    for (const SolicitudDetalle& d : std::as_const(m_solicitudes)) {
        lista.append(d.resumen);
    }
    return QtFuture::makeReadyValueFuture(ResultadoLista::exito(std::move(lista)));
}

QFuture<SolicitudesService::ResultadoDetalle>
DemoSolicitudesService::obtener(const SolicitudId& id)
{
    for (const SolicitudDetalle& d : std::as_const(m_solicitudes)) {
        if (!id.esNulo() && d.resumen.id == id) {
            return QtFuture::makeReadyValueFuture(ResultadoDetalle::exito(d));
        }
    }
    return QtFuture::makeReadyValueFuture(ResultadoDetalle::fallo(
        ErrorObtener{ErrorObtener::Tipo::NoEncontrada,
                     QStringLiteral("La solicitud no existe.")}));
}

QFuture<SolicitudesService::ResultadoCrear>
DemoSolicitudesService::crear(const NuevaSolicitudRequest& request)
{
    using C = ErrorValidacion::Codigo;
    QList<ErrorValidacion> errores;

    const PerfilResumen* perfil = nullptr;
    if (request.perfilId.esNulo()) {
        errores.append({C::PerfilRequerido, QStringLiteral("Selecciona un perfil SAT.")});
    } else if (perfil = perfilActivo(request.perfilId); perfil == nullptr) {
        errores.append({C::PerfilInexistente,
                        QStringLiteral("El perfil SAT seleccionado no existe o no esta activo.")});
    }
    if (!request.fechaInicial.isValid()) {
        errores.append({C::FechaInicialRequerida, QStringLiteral("Indica la fecha inicial.")});
    }
    if (!request.fechaFinal.isValid()) {
        errores.append({C::FechaFinalRequerida, QStringLiteral("Indica la fecha final.")});
    }
    if (request.fechaInicial.isValid() && request.fechaFinal.isValid()
        && request.fechaFinal < request.fechaInicial) {
        errores.append({C::RangoFechasInvalido,
                        QStringLiteral("La fecha final debe ser igual o posterior a la fecha inicial.")});
    }
    if (!errores.isEmpty()) {
        return QtFuture::makeReadyValueFuture(
            ResultadoCrear::fallo(ErrorCrear::validacion(std::move(errores))));
    }

    SolicitudDetalle d;
    d.resumen.id = SolicitudId::generar();
    d.resumen.perfilRfc = perfil->rfc;
    if (request.rfcContraparte && !request.rfcContraparte->trimmed().isEmpty()) {
        d.resumen.rfcContraparte = request.rfcContraparte->trimmed();
    }
    d.resumen.tipoDescarga = request.tipoDescarga;
    d.resumen.fechaInicial = request.fechaInicial;
    d.resumen.fechaFinal = request.fechaFinal;
    d.resumen.estadoLocal = EstadoLocal::Creada;
    d.resumen.creadaEn = QDateTime::currentDateTimeUtc();
    d.resumen.totalPaquetes = 0;

    const SolicitudId id = d.resumen.id;
    m_solicitudes.prepend(std::move(d));
    emit solicitudActualizada(id);
    return QtFuture::makeReadyValueFuture(ResultadoCrear::exito(id));
}

const PerfilResumen* DemoSolicitudesService::perfilActivo(const PerfilId& id) const
{
    for (const PerfilResumen& p : m_perfiles) {
        if (p.id == id && p.activo) {
            return &p;
        }
    }
    return nullptr;
}

} // namespace satcfdi
