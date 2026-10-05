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
        return tr("El perfil de esta solicitud esta inactivo.");
    }
    switch (preparacion) {
    case PreparacionPerfil::Lista:
        return {};
    case PreparacionPerfil::Vencida:
        return tr("La e.firma de este perfil esta vencida. Reemplazala para continuar.");
    case PreparacionPerfil::MaterialFaltante:
    case PreparacionPerfil::MaterialDanado:
        return tr("No se pudo leer la e.firma guardada. Vuelve a importarla.");
    case PreparacionPerfil::EstadoNoDisponible:
        return tr("El llavero de macOS esta bloqueado. Desbloquea tu sesion e intenta de nuevo.");
    case PreparacionPerfil::SinCredencial:
        return tr("Este perfil no tiene e.firma registrada. Registrala para continuar.");
    case PreparacionPerfil::NoVigenteAun:
        return tr("La e.firma de este perfil aun no esta vigente.");
    case PreparacionPerfil::Verificando:
        return tr("Comprobando la e.firma del perfil...");
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
        return tr("No se sabe si el SAT registro la solicitud. No se reenviara automaticamente; revisa antes de "
                  "crear otra.");
    case EstadoLocal::EnvioFallido:
        if (cod == QLatin1String("5001")) {
            return tr("No estas autorizado para descargar estos CFDI (5001).");
        }
        if (cod == QLatin1String("5002")) {
            return tr("El SAT ya no acepta solicitudes con este mismo criterio.");
        }
        if (cod == QLatin1String("5003")) {
            return tr("La consulta supera el maximo de CFDI; usa un rango mas corto.");
        }
        if (cod == QLatin1String("5005")) {
            return tr("El SAT ya tiene una solicitud activa con este criterio.");
        }
        if (cod == QLatin1String("5011")) {
            return tr("Se alcanzo el limite diario del SAT. Intenta manana.");
        }
        if (!cod.isEmpty()) {
            return tr("El SAT rechazo la solicitud (codigo %1). No se registro en el SAT.").arg(cod);
        }
        return tr("El SAT rechazo la solicitud. No se registro en el SAT.");
    case EstadoLocal::Enviada: {
        const QString codigoEstado = d.codigoEstadoSolicitud.value_or(QString());
        if (codigoEstado == QLatin1String("5004")) {
            return tr("El SAT no encontro esta solicitud. La verificacion automatica se detuvo.");
        }
        if (codigoEstado == QLatin1String("5011")) {
            return tr("Se alcanzo el limite diario del SAT. Intenta manana.");
        }
        if (d.ultimoError && !d.ultimoError->isEmpty()) {
            return tr("No se pudo consultar el estado; se reintentara automaticamente.");
        }
        return {};
    }
    case EstadoLocal::Creada:
        // Sin desglose: solo hay codigo de creacion si el SAT respondio.
        if (!cod.isEmpty()) {
            return tr("El SAT rechazo la solicitud (codigo %1). No se registro en el SAT.").arg(cod);
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
        return tr("El paquete ya no existe en el SAT (vencido).");
    }
    if (p.estadoDescarga == EstadoDescarga::Error) {
        return esMaximoDescargas(p) ? tr("El paquete alcanzo el maximo de descargas permitidas.")
                                    : tr("No se pudo descargar el paquete. Puedes reintentar.");
    }
    return {};
}

} // namespace satcfdi::catalogo
