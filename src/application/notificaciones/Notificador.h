#pragma once

#include <QString>

namespace satcfdi {

// T014.4 D1: destino interno de una notificacion, en vocabulario de
// application. `id` es el UUID canonico interno (SolicitudId/PerfilId); nunca
// RFC, rutas ni datos SAT. El composition root lo traduce a
// OSIntegration::DestinoNotificacion.
// - Solicitud: terminada, descarga_completa, error_sat, rechazada, vencida.
// - Perfil: credencial, efirma_por_vencer.
struct DestinoNotificacion {
    enum class Tipo { Ninguno, Solicitud, Perfil };
    Tipo tipo = Tipo::Ninguno;
    QString id;

    friend bool operator==(const DestinoNotificacion&, const DestinoNotificacion&) = default;
};

// Notificacion local decidida por la aplicacion (T009 D1, D9). Mismos campos
// que OSIntegration::NotificacionLocal (salvo accionesExtra, que decide el
// composition root por `tipo`); el root adapta una a otra.
// - id: clave de dedupe ("<solicitud>:<tipo>" o "credencial:<perfil>:<estado>");
//   el SO reemplaza una notificacion con el mismo id.
// - tipo: "terminada", "descarga_completa", "error_sat", "rechazada",
//   "vencida", "credencial" o "efirma_por_vencer" (T014.3).
// - titulo/cuerpo: textos fijos (catalogo D10) sin RFC completo, Ids, token,
//   rutas ni Mensaje SAT crudo.
struct Notificacion {
    QString id;
    QString tipo;
    QString titulo;
    QString cuerpo;
    DestinoNotificacion destino; // T014.4 D1

    friend bool operator==(const Notificacion&, const Notificacion&) = default;
};

// Puerto de salida de notificaciones (application no depende de
// OSIntegration). Hilo grafico. Asincrono y "dispara y olvida": nunca pide
// permiso; con permiso denegado o no disponible simplemente no se muestra.
// El resultado (notificacionTerminada del adaptador) NO vuelve a la
// aplicacion: no cambia estados, logs ni reintentos.
class Notificador {
public:
    virtual ~Notificador() = default;
    virtual void notificar(const Notificacion& notificacion) = 0;
};

} // namespace satcfdi
