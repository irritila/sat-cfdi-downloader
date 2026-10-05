#include "application/operaciones/MensajesOperacionSat.h"

namespace satcfdi::mensajessat {

QString credencialVencida()
{
    return QStringLiteral("La e.firma de este perfil esta vencida. Reemplazala para continuar.");
}
QString credencialIlegible()
{
    return QStringLiteral("No se pudo leer la e.firma guardada. Vuelve a importarla.");
}
QString llaveroBloqueado()
{
    return QStringLiteral("El llavero de macOS esta bloqueado. Desbloquea tu sesion e intenta de nuevo.");
}
QString credencialNoVigenteAun()
{
    return QStringLiteral("La e.firma de este perfil aun no es vigente.");
}

QString autenticacionRechazada()
{
    return QStringLiteral("El SAT no acepto la autenticacion con esta e.firma.");
}
QString autenticacionSinConexion()
{
    return QStringLiteral("No se pudo conectar con el SAT para autenticar. Intenta mas tarde.");
}

QString solicitudAceptada()
{
    return QStringLiteral("El SAT acepto la solicitud.");
}
QString solicitudRechazada(QStringView codigo)
{
    return QStringLiteral("El SAT rechazo la solicitud (codigo %1). No se registro en el SAT.").arg(codigo);
}
QString solicitudNoAutorizada()
{
    return QStringLiteral("No estas autorizado para descargar estos CFDI (5001).");
}
QString limiteCriterio5002()
{
    return QStringLiteral("El SAT ya no acepta solicitudes con este mismo criterio.");
}
QString limiteMaximo5003()
{
    return QStringLiteral("La consulta supera el maximo de CFDI; usa un rango mas corto.");
}
QString solicitudActiva5005()
{
    return QStringLiteral("El SAT ya tiene una solicitud activa con este criterio.");
}
QString limiteDiario5011()
{
    return QStringLiteral("Se alcanzo el limite diario del SAT. Intenta manana.");
}
QString envioIncierto()
{
    return QStringLiteral(
        "No se sabe si el SAT registro la solicitud. No se reenviara automaticamente; revisa antes de crear otra.");
}
QString envioNoIniciado()
{
    return QStringLiteral("No se pudo conectar con el SAT; la solicitud no se envio.");
}

QString verificacionAceptada()
{
    return QStringLiteral("Estado consultado en el SAT.");
}
QString verificacionTransitoria()
{
    return QStringLiteral("No se pudo consultar el estado; se reintentara automaticamente.");
}
QString solicitudNoEncontrada()
{
    return QStringLiteral("El SAT no encontro esta solicitud. La verificacion automatica se detuvo.");
}

QString paqueteDescargado()
{
    return QStringLiteral("Paquete descargado.");
}
QString paqueteVencido()
{
    return QStringLiteral("El paquete ya no existe en el SAT (vencido).");
}
QString paqueteMaximoDescargas()
{
    return QStringLiteral("El paquete alcanzo el maximo de descargas permitidas.");
}
QString descargaFallida()
{
    return QStringLiteral("No se pudo descargar el paquete. Puedes reintentar.");
}

QString sinEspacio()
{
    return QStringLiteral("No hay espacio suficiente para guardar el paquete.");
}
QString sinPermisoEscritura()
{
    return QStringLiteral("No se pudo escribir en la carpeta de paquetes; revisa los permisos.");
}
QString colisionDestino()
{
    return QStringLiteral("Ya existe un archivo con ese nombre en la carpeta del paquete.");
}

} // namespace satcfdi::mensajessat
