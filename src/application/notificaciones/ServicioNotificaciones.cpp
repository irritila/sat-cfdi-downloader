#include "application/notificaciones/ServicioNotificaciones.h"

#include "application/common/FechasLegibles.h"

#include <QStringList>

namespace satcfdi {

namespace {

// T024.1 (D4): direccion en minusculas para el cuerpo ("emitidas"/"recibidas").
QString direccion(const TransicionNotificable& t)
{
    return t.tipoDescarga == TipoDescarga::Emitidos ? QStringLiteral("emitidas") : QStringLiteral("recibidas");
}

// Linea 2 (UX-39, T024.1 D4): "{periodo} · RFC ***000". El perfil se
// identifica por su RFC enmascarado (T013 D6: nunca el RFC completo).
QString contexto(const TransicionNotificable& t)
{
    QStringList partes;
    const QString periodo = fechaslegibles::rango(fechaslegibles::diaDeTextoSat(t.fechaInicialSat),
                                                  fechaslegibles::diaDeTextoSat(t.fechaFinalSat));
    if (!periodo.isEmpty()) {
        partes << periodo;
    }
    const QString rfc = ServicioNotificaciones::rfcEnmascarado(t.rfcSolicitante);
    if (!rfc.isEmpty()) {
        partes << QStringLiteral("RFC ") + rfc;
    }
    return partes.join(QStringLiteral(" \u00B7 "));
}

QString dosLineas(const QString& resultado, const QString& contextoLinea)
{
    return contextoLinea.isEmpty() ? resultado : resultado + QLatin1Char('\n') + contextoLinea;
}

} // namespace

ServicioNotificaciones::ServicioNotificaciones(Notificador& notificador, QObject* parent)
    : QObject(parent)
    , m_notificador(notificador)
{
}

QString ServicioNotificaciones::rfcEnmascarado(const QString& rfc)
{
    const QString limpio = rfc.trimmed();
    if (limpio.isEmpty()) {
        return {};
    }
    return QStringLiteral("***") + limpio.right(3);
}

std::optional<Notificacion> ServicioNotificaciones::componer(const TransicionNotificable& t)
{
    // T024.1 (D4): "Terminada" con archivos no se notifica; la notificacion
    // util llega con "descarga_completa". Con 0 archivos es un periodo sin
    // facturas. Id, tipo y destino no cambian (dedupe y rutas de T023.5).
    if (t.tipo == TipoTransicionNotificable::Terminada && t.paquetes > 0) {
        return std::nullopt;
    }
    Notificacion n;
    n.tipo = claveEstable(t.tipo);
    n.id = t.solicitudId.texto() + QLatin1Char(':') + n.tipo;
    n.destino = DestinoNotificacion{DestinoNotificacion::Tipo::Solicitud, t.solicitudId.texto()};
    const QString dir = direccion(t);
    QString resultado;
    switch (t.tipo) {
    case TipoTransicionNotificable::Terminada:
    case TipoTransicionNotificable::SinFacturas:
        // T023.6 (D22): informativo, sin "rechazó" ni "error".
        n.titulo = QStringLiteral("Sin facturas en el periodo");
        resultado = QStringLiteral("El SAT no tiene facturas %1 en este periodo.").arg(dir);
        break;
    case TipoTransicionNotificable::DescargaCompleta:
        n.titulo = QStringLiteral("Facturas descargadas");
        resultado = QStringLiteral("Las facturas %1 ya están en tu equipo.").arg(dir);
        break;
    case TipoTransicionNotificable::ErrorSat:
        n.titulo = QStringLiteral("No se pudieron descargar las facturas");
        resultado = QStringLiteral("El SAT reportó un error al preparar las facturas %1.").arg(dir);
        break;
    case TipoTransicionNotificable::Rechazada:
        n.titulo = QStringLiteral("El SAT no aceptó preparar las facturas");
        resultado = QStringLiteral("No se descargaron las facturas %1.").arg(dir);
        break;
    case TipoTransicionNotificable::Vencida:
        n.titulo = QStringLiteral("Facturas no descargadas");
        resultado = QStringLiteral("Venció el plazo del SAT para descargar las facturas %1.").arg(dir);
        break;
    }
    n.cuerpo = dosLineas(resultado, contexto(t));
    return n;
}

std::optional<Notificacion> ServicioNotificaciones::componerCredencial(const PerfilId& perfil,
                                                                       EstadoCredencial estado)
{
    QString causa;
    QString accion = QStringLiteral("Reemplázala en Perfiles SAT.");
    switch (estado) {
    case EstadoCredencial::Lista:
    case EstadoCredencial::Validando: return std::nullopt;
    case EstadoCredencial::Vencida: causa = QStringLiteral("La e.firma está vencida"); break;
    case EstadoCredencial::NoVigenteAun: causa = QStringLiteral("La e.firma aún no es vigente"); break;
    case EstadoCredencial::SinCredencial:
        causa = QStringLiteral("El perfil no tiene e.firma registrada");
        accion = QStringLiteral("Regístrala en Perfiles SAT.");
        break;
    case EstadoCredencial::MaterialFaltante:
    case EstadoCredencial::MaterialDanado: causa = QStringLiteral("No se pudo leer la e.firma guardada"); break;
    }
    Notificacion n;
    n.tipo = QStringLiteral("credencial");
    n.id = QStringLiteral("credencial:") + perfil.texto() + QLatin1Char(':') + claveEstable(estado);
    n.destino = DestinoNotificacion{DestinoNotificacion::Tipo::Perfil, perfil.texto()};
    n.titulo = QStringLiteral("e.firma no disponible");
    n.cuerpo = dosLineas(causa + QStringLiteral(". El monitoreo de ese perfil está en pausa."), accion);
    return n;
}

Notificacion ServicioNotificaciones::componerVencimiento(const PerfilId& perfil, const QString& rfc,
                                                        const QDateTime& vigenteHasta, int umbral, int dias)
{
    Notificacion n;
    n.tipo = QStringLiteral("efirma_por_vencer");
    n.id = QStringLiteral("vencimiento:%1:%2:%3")
               .arg(perfil.texto(), vigenteHasta.toUTC().toString(Qt::ISODate))
               .arg(umbral);
    n.destino = DestinoNotificacion{DestinoNotificacion::Tipo::Perfil, perfil.texto()};
    n.titulo = QStringLiteral("e.firma por vencer");
    const QString fecha = fechaslegibles::fecha(vigenteHasta.toLocalTime().date());
    QString resultado;
    if (dias <= 0) {
        resultado = QStringLiteral("La e.firma vence hoy (%1).").arg(fecha);
    } else if (dias == 1) {
        resultado = QStringLiteral("La e.firma vence en 1 día (%1).").arg(fecha);
    } else {
        resultado = QStringLiteral("La e.firma vence en %1 días (%2).").arg(dias).arg(fecha);
    }
    QString contextoLinea = QStringLiteral("Reemplázala en Perfiles SAT.");
    const QString rfcCorto = rfcEnmascarado(rfc);
    if (!rfcCorto.isEmpty()) {
        contextoLinea = QStringLiteral("RFC ") + rfcCorto + QStringLiteral(" \u00B7 ") + contextoLinea;
    }
    n.cuerpo = dosLineas(resultado, contextoLinea);
    return n;
}

void ServicioNotificaciones::notificarVencimiento(const PerfilId& perfil, const QString& rfc,
                                                  const QDateTime& vigenteHasta, int umbral, int dias)
{
    Notificacion n = componerVencimiento(perfil, rfc, vigenteHasta, umbral, dias);
    if (m_emitidas.contains(n.id)) {
        return;
    }
    m_emitidas.insert(n.id);
    m_notificador.notificar(n);
}

void ServicioNotificaciones::alConfirmarTransicion(const TransicionNotificable& transicion)
{
    const std::optional<Notificacion> n = componer(transicion);
    if (!n || m_emitidas.contains(n->id)) {
        return;
    }
    m_emitidas.insert(n->id);
    m_notificador.notificar(*n);
}

void ServicioNotificaciones::alCambiarEstadoCredencial(const PerfilId& perfil, EstadoCredencial estado)
{
    const auto anterior = m_credencial.constFind(perfil.texto());
    if (anterior != m_credencial.cend() && *anterior == estado) {
        return;
    }
    m_credencial.insert(perfil.texto(), estado);
    if (auto n = componerCredencial(perfil, estado)) {
        m_notificador.notificar(*n);
    }
}

} // namespace satcfdi
