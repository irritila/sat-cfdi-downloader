#pragma once

#include "domain/solicitudes/SolicitudId.h"

#include <QFuture>
#include <QString>

namespace satcfdi {

// Resultado de una accion de Finder (T009.1 D1, D5, D7), ya listo para la UI.
struct ResultadoAccionFinder {
    enum class Estado {
        Mostrado,     // Finder abrio el destino
        NoEncontrado, // el destino no existe (resolucion o revalidacion del SO)
        NoAplica,     // no hay nada que mostrar (p. ej. sin paquetes descargados)
        Fallido,      // error de lectura o Finder no pudo abrirse
    };
    Estado estado = Estado::Fallido;
    QString mensaje; // D7 accesible; vacio si Mostrado
};

// Acciones de Finder que la presentacion solicita. El composition root las
// implementa: resolucion (AccesoPaquetesService, fuera del hilo grafico) y
// despues OSIntegration::mostrarEnFinder/abrirCarpetaEnFinder. Solo lectura:
// nunca cambia estados, SQLite ni logs, no crea carpetas ni abre el ZIP. El
// future se continua en el hilo grafico.
class AccionesFinder {
public:
    virtual ~AccionesFinder() = default;
    virtual QFuture<ResultadoAccionFinder> mostrarPaquete(const SolicitudId& solicitud, const QString& idPaqueteSat) = 0;
    virtual QFuture<ResultadoAccionFinder> abrirCarpetaSolicitud(const SolicitudId& solicitud) = 0;
    virtual QFuture<ResultadoAccionFinder> abrirCarpetaPaquetes() = 0;
};

} // namespace satcfdi
