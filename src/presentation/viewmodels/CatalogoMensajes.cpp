#include "CatalogoMensajes.h"

#include <QCoreApplication>

namespace satcfdi::catalogo {

namespace {
QString tr(const char* texto)
{
    return QCoreApplication::translate("CatalogoMensajes", texto);
}
} // namespace

QString motivoCredencial(PreparacionPerfil preparacion, bool perfilActivo)
{
    if (!perfilActivo) {
        return tr("El perfil de esta solicitud está inactivo.");
    }
    switch (preparacion) {
    case PreparacionPerfil::Lista:
        return {};
    case PreparacionPerfil::Vencida:
        return tr("La e.firma de este perfil está vencida. Reemplázala para continuar.");
    case PreparacionPerfil::MaterialFaltante:
    case PreparacionPerfil::MaterialDanado:
        return tr("No se pudo leer la e.firma guardada. Vuelve a importarla.");
    case PreparacionPerfil::EstadoNoDisponible:
        return tr("El llavero de macOS está bloqueado. Desbloquea tu sesión e intenta de nuevo.");
    case PreparacionPerfil::SinCredencial:
        return tr("Este perfil no tiene e.firma registrada. Regístrala para continuar.");
    case PreparacionPerfil::NoVigenteAun:
        return tr("La e.firma de este perfil aún no está vigente.");
    case PreparacionPerfil::Verificando:
        return tr("Comprobando la e.firma del perfil…");
    }
    return {};
}

QString mensajeSolicitud(const SolicitudDetalle& d)
{
    const QString cod = d.codEstatusSolicitud.value_or(QString());
    // T009: la ultima falla ya trae su texto D10 (fase, codigo y causa
    // desglosados por la aplicacion): se usa tal cual, salvo en EnvioIncierto,
    // cuyo mensaje es fijo. Nunca se muestra el Mensaje SAT crudo.
    const QString desglosado =
        d.ultimoErrorDesglosado ? d.ultimoErrorDesglosado->mensaje.trimmed() : QString();
    if (!desglosado.isEmpty() && d.resumen.estadoLocal != EstadoLocal::EnvioIncierto) {
        return desglosado;
    }
    switch (d.resumen.estadoLocal) {
    case EstadoLocal::EnvioIncierto:
        return tr("No se sabe si el SAT registró la solicitud. No se reenviará automáticamente; revisa antes de "
                  "crear otra.");
    case EstadoLocal::EnvioFallido:
        if (cod == QLatin1String("5001")) {
            return tr("No estás autorizado para descargar estos CFDI (5001).");
        }
        if (cod == QLatin1String("5002")) {
            return tr("El SAT ya no acepta solicitudes con este mismo criterio.");
        }
        if (cod == QLatin1String("5003")) {
            return tr("La consulta supera el máximo de CFDI; usa un rango más corto.");
        }
        if (cod == QLatin1String("5005")) {
            return tr("El SAT ya tiene una solicitud activa con este criterio.");
        }
        if (cod == QLatin1String("5011")) {
            return tr("Se alcanzó el límite diario del SAT. Intenta mañana.");
        }
        if (!cod.isEmpty()) {
            return tr("El SAT rechazó la solicitud (código %1). No se registró en el SAT.").arg(cod);
        }
        return tr("El SAT rechazó la solicitud. No se registró en el SAT.");
    case EstadoLocal::Enviada: {
        const QString codigoEstado = d.codigoEstadoSolicitud.value_or(QString());
        if (codigoEstado == QLatin1String("5004")) {
            return tr("El SAT no encontró esta solicitud. La verificación automática se detuvo.");
        }
        if (codigoEstado == QLatin1String("5011")) {
            return tr("Se alcanzó el límite diario del SAT. Intenta mañana.");
        }
        if (d.ultimoError && !d.ultimoError->isEmpty()) {
            return tr("No se pudo consultar el estado; se reintentará automáticamente.");
        }
        return {};
    }
    case EstadoLocal::Creada:
        // Sin desglose: solo hay codigo de creacion si el SAT respondio.
        if (!cod.isEmpty()) {
            return tr("El SAT rechazó la solicitud (código %1). No se registró en el SAT.").arg(cod);
        }
        break;
    case EstadoLocal::Enviando:
        break;
    }
    return {};
}

bool esMaximoDescargas(const PaqueteResumen& p)
{
    return p.codigoDescargaSat && *p.codigoDescargaSat == QLatin1String("5008");
}

QString mensajePaquete(const PaqueteResumen& p)
{
    if (p.estadoDescarga == EstadoDescarga::Vencido
        || (p.codigoDescargaSat && *p.codigoDescargaSat == QLatin1String("5007"))) {
        return tr("El paquete ya no existe en el SAT (vencido). Crea una solicitud nueva para el mismo periodo.");
    }
    if (p.estadoDescarga == EstadoDescarga::Error) {
        return esMaximoDescargas(p) ? tr("El paquete alcanzó el máximo de descargas permitidas.")
                                    : tr("No se pudo descargar el paquete. Puedes reintentar.");
    }
    return {};
}

} // namespace satcfdi::catalogo
