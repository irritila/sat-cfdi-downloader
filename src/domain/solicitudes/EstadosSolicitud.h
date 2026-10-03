#pragma once

#include <QString>
#include <QStringView>

#include <optional>

namespace satcfdi {

// Dimension local del ciclo de envio (operational-rules: estado_local).
enum class EstadoLocal {
    Creada,
    Enviando,
    Enviada,
    EnvioFallido,
    EnvioIncierto,
};

// Estado devuelto por SAT (operational-rules: estado_solicitud_sat).
// Se modela como std::optional<EstadoSolicitudSat>: nulo mientras SAT no haya
// devuelto estado. Se mantiene separado de EstadoLocal.
enum class EstadoSolicitudSat {
    Aceptada,
    EnProceso,
    Terminada,
    Error,
    Rechazada,
    Vencida,
};

enum class TipoDescarga {
    Emitidos,
    Recibidos,
};

// Claves estables (identicas al nombre del enumerador). No son texto visible:
// la presentacion las traduce a texto accesible.
QString claveEstable(EstadoLocal estado);
QString claveEstable(EstadoSolicitudSat estado);
QString claveEstable(TipoDescarga tipo);

// Inversa de claveEstable(TipoDescarga): "Emitidos" o "Recibidos".
std::optional<TipoDescarga> tipoDescargaDesdeClave(QStringView clave);

} // namespace satcfdi
