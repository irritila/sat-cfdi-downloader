#include "domain/logs/LogSolicitud.h"

namespace satcfdi {

QString claveEstable(TipoEventoLog tipo)
{
    using T = TipoEventoLog;
    switch (tipo) {
    case T::SolicitudCreada: return QStringLiteral("solicitud_creada");
    case T::DuplicadoConfirmado: return QStringLiteral("duplicado_confirmado");
    case T::EnvioIniciado: return QStringLiteral("envio_iniciado");
    case T::SolicitudEnviada: return QStringLiteral("solicitud_enviada");
    case T::EnvioFallido: return QStringLiteral("envio_fallido");
    case T::EnvioIncierto: return QStringLiteral("envio_incierto");
    case T::VerificacionRealizada: return QStringLiteral("verificacion_realizada");
    case T::VerificacionFallida: return QStringLiteral("verificacion_fallida");
    case T::PaquetesRegistrados: return QStringLiteral("paquetes_registrados");
    case T::DescargaIniciada: return QStringLiteral("descarga_iniciada");
    case T::PaqueteDescargado: return QStringLiteral("paquete_descargado");
    case T::DescargaFallida: return QStringLiteral("descarga_fallida");
    case T::DescargaInterrumpida: return QStringLiteral("descarga_interrumpida");
    case T::PaqueteReconciliado: return QStringLiteral("paquete_reconciliado");
    case T::PaqueteVencido: return QStringLiteral("paquete_vencido");
    case T::ArchivoHuerfano: return QStringLiteral("archivo_huerfano");
    case T::AccionPendienteRegistrada: return QStringLiteral("accion_pendiente_registrada");
    case T::AccionPendienteDescartada: return QStringLiteral("accion_pendiente_descartada");
    case T::EnvioNoIniciado: return QStringLiteral("envio_no_iniciado");
    case T::VerificacionSuspendida: return QStringLiteral("verificacion_suspendida");
    }
    Q_UNREACHABLE_RETURN(QString());
}

QString claveEstable(OrigenLog origen)
{
    switch (origen) {
    case OrigenLog::Worker: return QStringLiteral("worker");
    case OrigenLog::Usuario: return QStringLiteral("usuario");
    case OrigenLog::Recuperacion: return QStringLiteral("recuperacion");
    }
    Q_UNREACHABLE_RETURN(QString());
}

QString claveEstable(OrigenCodigoSat origen)
{
    switch (origen) {
    case OrigenCodigoSat::Creacion: return QStringLiteral("creacion");
    case OrigenCodigoSat::Verificacion: return QStringLiteral("verificacion");
    case OrigenCodigoSat::Descarga: return QStringLiteral("descarga");
    }
    Q_UNREACHABLE_RETURN(QString());
}

std::optional<TipoEventoLog> tipoEventoLogDesdeClave(QStringView clave)
{
    for (TipoEventoLog t : kTiposEventoLog) {
        if (clave == claveEstable(t)) {
            return t;
        }
    }
    return std::nullopt;
}

std::optional<OrigenLog> origenLogDesdeClave(QStringView clave)
{
    for (OrigenLog o : {OrigenLog::Worker, OrigenLog::Usuario, OrigenLog::Recuperacion}) {
        if (clave == claveEstable(o)) {
            return o;
        }
    }
    return std::nullopt;
}

std::optional<OrigenCodigoSat> origenCodigoSatDesdeClave(QStringView clave)
{
    for (OrigenCodigoSat o :
         {OrigenCodigoSat::Creacion, OrigenCodigoSat::Verificacion, OrigenCodigoSat::Descarga}) {
        if (clave == claveEstable(o)) {
            return o;
        }
    }
    return std::nullopt;
}

} // namespace satcfdi
