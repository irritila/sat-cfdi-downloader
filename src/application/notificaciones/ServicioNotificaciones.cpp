#include "application/notificaciones/ServicioNotificaciones.h"

#include "application/operaciones/MensajesOperacionSat.h"

namespace satcfdi {

namespace {

QString descripcion(const TransicionNotificable& t)
{
    const QString tipo = t.tipoDescarga == TipoDescarga::Emitidos ? QStringLiteral("Emitidos")
                                                                  : QStringLiteral("Recibidos");
    QString texto = QStringLiteral("%1 del %2 al %3").arg(tipo, t.fechaInicialSat.left(10), t.fechaFinalSat.left(10));
    const QString rfc = ServicioNotificaciones::rfcEnmascarado(t.rfcSolicitante);
    if (!rfc.isEmpty()) {
        texto += QStringLiteral(", RFC ") + rfc;
    }
    return texto + QLatin1Char('.');
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

Notificacion ServicioNotificaciones::componer(const TransicionNotificable& t)
{
    Notificacion n;
    n.tipo = claveEstable(t.tipo);
    n.id = t.solicitudId.texto() + QLatin1Char(':') + n.tipo;
    const QString detalle = descripcion(t);
    switch (t.tipo) {
    case TipoTransicionNotificable::Terminada:
        n.titulo = QStringLiteral("Solicitud terminada");
        n.cuerpo = (t.paquetes == 1 ? QStringLiteral("Terminada (1 paquete). ")
                                    : QStringLiteral("Terminada (%1 paquetes). ").arg(t.paquetes))
                   + detalle;
        break;
    case TipoTransicionNotificable::DescargaCompleta:
        n.titulo = QStringLiteral("Descarga completa");
        n.cuerpo = QStringLiteral("Descarga completa: %1 de %2. ").arg(t.descargados).arg(t.paquetes) + detalle;
        break;
    case TipoTransicionNotificable::ErrorSat:
        n.titulo = QStringLiteral("Error en el SAT");
        n.cuerpo = QStringLiteral("El SAT reporto un error en la solicitud. ") + detalle;
        break;
    case TipoTransicionNotificable::Rechazada:
        n.titulo = QStringLiteral("Solicitud rechazada");
        n.cuerpo = QStringLiteral("El SAT rechazo la solicitud. ") + detalle;
        break;
    case TipoTransicionNotificable::Vencida:
        n.titulo = QStringLiteral("Solicitud vencida");
        n.cuerpo = QStringLiteral("La solicitud vencio en el SAT; los paquetes pueden ya no estar disponibles. ")
                   + detalle;
        break;
    }
    return n;
}

std::optional<Notificacion> ServicioNotificaciones::componerCredencial(const PerfilId& perfil,
                                                                       EstadoCredencial estado)
{
    QString cuerpo;
    switch (estado) {
    case EstadoCredencial::Lista:
    case EstadoCredencial::Validando: return std::nullopt;
    case EstadoCredencial::Vencida: cuerpo = mensajessat::credencialVencida(); break;
    case EstadoCredencial::NoVigenteAun: cuerpo = mensajessat::credencialNoVigenteAun(); break;
    case EstadoCredencial::SinCredencial:
    case EstadoCredencial::MaterialFaltante:
    case EstadoCredencial::MaterialDanado: cuerpo = mensajessat::credencialIlegible(); break;
    }
    Notificacion n;
    n.tipo = QStringLiteral("credencial");
    n.id = QStringLiteral("credencial:") + perfil.texto() + QLatin1Char(':') + claveEstable(estado);
    n.titulo = QStringLiteral("e.firma de un perfil");
    n.cuerpo = cuerpo + QStringLiteral(" El monitoreo de ese perfil esta en pausa.");
    return n;
}

void ServicioNotificaciones::alConfirmarTransicion(const TransicionNotificable& transicion)
{
    Notificacion n = componer(transicion);
    if (m_emitidas.contains(n.id)) {
        return;
    }
    m_emitidas.insert(n.id);
    m_notificador.notificar(n);
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
