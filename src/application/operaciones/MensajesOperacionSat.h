#pragma once

#include <QString>
#include <QStringView>

namespace satcfdi::mensajessat {

// Catalogo de mensajes visibles de T009 (D10). Textos fijos: sin RFC, Ids,
// token, rutas ni Mensaje SAT crudo. OperacionesSatProductivo los coloca en
// FallaOperacion::diagnosticoSanitizado y en ResultadoEnvio::mensaje.

// Credencial
QString credencialVencida();
QString credencialIlegible();
QString llaveroBloqueado();
QString credencialNoVigenteAun(); // fuera del catalogo D10 (NoVigenteAun)

// Autenticacion SAT
QString autenticacionRechazada();
QString autenticacionSinConexion();

// Solicitud (creacion)
QString solicitudAceptada();
QString solicitudRechazada(QStringView codigo); // "(codigo N)"
QString solicitudNoAutorizada();                // 5001
QString limiteCriterio5002();
QString limiteMaximo5003();
QString solicitudActiva5005();
QString limiteDiario5011();
QString envioIncierto();
QString envioNoIniciado(); // fuera del catalogo D10 (AntesDeEnvio/Preparacion del envio)

// Verificacion
QString verificacionAceptada();
QString verificacionTransitoria();
QString solicitudNoEncontrada();

// Descarga
QString paqueteDescargado();
QString paqueteVencido();
QString paqueteMaximoDescargas();
QString descargaFallida();

// Almacenamiento
QString sinEspacio();
QString sinPermisoEscritura();
QString colisionDestino();

} // namespace satcfdi::mensajessat
