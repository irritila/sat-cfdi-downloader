#include "domain/operaciones/PoliticasOperacion.h"

#include <QSet>

namespace satcfdi::politicas {

bool esRechazoDocumentadoDeCreacion(QStringView codEstatus)
{
    for (QStringView c : {u"300", u"301", u"302", u"303", u"304", u"305", u"5001", u"5002", u"5005"}) {
        if (codEstatus.trimmed() == c) {
            return true;
        }
    }
    return false;
}

EstadoLocal destinoEnvio(const ResultadoEnvio& r)
{
    const QString cod = r.codEstatus.trimmed();
    const bool conId = r.idSolicitudSat && !r.idSolicitudSat->trimmed().isEmpty();
    if (cod == u"5000") {
        return conId ? EstadoLocal::Enviada : EstadoLocal::EnvioIncierto;
    }
    if (!conId && esRechazoDocumentadoDeCreacion(cod)) {
        return EstadoLocal::EnvioFallido;
    }
    return EstadoLocal::EnvioIncierto; // 5006, no documentado o rechazo con Id (ambiguo)
}

EstadoLocal destinoEnvio(const FallaOperacion& f)
{
    switch (f.fase) {
    case FaseOperacion::Preparacion:
    case FaseOperacion::Autenticacion:
    case FaseOperacion::AntesDeEnvio:
        return EstadoLocal::Creada;
    case FaseOperacion::DespuesDeEnvio:
    case FaseOperacion::RespuestaExplicita:
    case FaseOperacion::Almacenamiento:
        return EstadoLocal::EnvioIncierto;
    }
    return EstadoLocal::EnvioIncierto;
}

bool sinCambio(const SolicitudPersistida& anterior, const QStringList& idsPaquetesConocidos,
               const ResultadoVerificacion& nueva)
{
    const QSet<QString> antes(idsPaquetesConocidos.cbegin(), idsPaquetesConocidos.cend());
    const QSet<QString> ahora(nueva.idsPaquetes.cbegin(), nueva.idsPaquetes.cend());
    return anterior.estadoSolicitudSat == std::optional<EstadoSolicitudSat>(nueva.estadoSolicitudSat)
           && anterior.codigoEstadoSolicitud == nueva.codigoEstadoSolicitud
           && anterior.numeroCfdi == nueva.numeroCfdi && antes == ahora;
}

bool esEstadoSatTerminal(EstadoSolicitudSat estado)
{
    return estado == EstadoSolicitudSat::Terminada || estado == EstadoSolicitudSat::Error
           || estado == EstadoSolicitudSat::Rechazada || estado == EstadoSolicitudSat::Vencida;
}

AgendaTrasExito agendaTrasExito(int previas, bool huboCambio, EstadoSolicitudSat estado, const QDateTime& ahoraUtc)
{
    AgendaTrasExito a;
    a.verificacionesSinCambio = huboCambio ? 0 : previas + 1;
    if (!esEstadoSatTerminal(estado)) {
        const auto intervalo =
            a.verificacionesSinCambio >= kSinCambioParaIntervaloLargo ? kIntervaloLargo : kIntervaloCorto;
        a.siguienteVerificacionEn = ahoraUtc.addSecs(std::chrono::duration_cast<std::chrono::seconds>(intervalo).count());
    }
    return a;
}

bool esFallaSuspendible(const FallaOperacion& f)
{
    if (f.fase != FaseOperacion::RespuestaExplicita || !f.codigo) {
        return false;
    }
    const QString c = f.codigo->trimmed();
    return c == u"300" || c == u"302" || c == u"303" || c == u"5004";
}

AgendaTrasFalla agendaTrasFalla(const RachaVerificacion& racha, const FallaOperacion& falla, const QDateTime& ahoraUtc)
{
    AgendaTrasFalla a;
    a.claveFalla = claveFalla(falla);
    const bool misma = racha.ultimaClaveFalla && *racha.ultimaClaveFalla == a.claveFalla;
    a.fallasIguales = misma ? racha.fallasIguales + 1 : 1;
    a.registrarLog = !misma;
    a.suspender = esFallaSuspendible(falla) && a.fallasIguales >= kFallasParaSuspender;
    if (!a.suspender) {
        a.siguienteVerificacionEn =
            ahoraUtc.addSecs(std::chrono::duration_cast<std::chrono::seconds>(kIntervaloLargo).count());
    }
    return a;
}

DesenlaceDescarga desenlaceDescarga(const FallaOperacion& f)
{
    DesenlaceDescarga d;
    if (f.fase == FaseOperacion::RespuestaExplicita && f.codigo && f.codigo->trimmed() == u"5007") {
        d.destino = EstadoDescarga::Vencido;
        d.motivo = MotivoVencimiento::PaqueteExpirado;
        d.origen = OrigenVencimiento::Sat;
    }
    return d;
}

bool vencimientoEstimadoAlcanzado(const PaquetePersistido& p, const QDateTime& ahoraUtc)
{
    if (p.eliminadoEn || !p.vencimientoEstimadoEn) {
        return false;
    }
    const bool vencible = p.estadoDescarga == EstadoDescarga::Disponible
                          || p.estadoDescarga == EstadoDescarga::Descargando
                          || p.estadoDescarga == EstadoDescarga::Error;
    return vencible && *p.vencimientoEstimadoEn <= ahoraUtc;
}

QDateTime vencimientoEstimado(const QDateTime& primeraObservacionUtc)
{
    return primeraObservacionUtc.addSecs(std::chrono::duration_cast<std::chrono::seconds>(kVidaEstimadaPaquete).count());
}

bool esReintentablePorPaquete(const PaquetePersistido& paquete)
{
    return !paquete.eliminadoEn
           && (paquete.estadoDescarga == EstadoDescarga::Error || paquete.estadoDescarga == EstadoDescarga::Disponible)
           && paquete.codigoDescargaSat.value_or(QString()).trimmed() != QStringLiteral("5008");
}

bool puedeReintentarManual(const PaquetePersistido& paquete)
{
    return paquete.estadoDescarga == EstadoDescarga::Error && esReintentablePorPaquete(paquete);
}

} // namespace satcfdi::politicas
