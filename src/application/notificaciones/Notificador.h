#pragma once

#include <QString>

namespace satcfdi {

// Notificacion local decidida por la aplicacion (T009 D1, D9). Mismos campos
// que OSIntegration::NotificacionLocal; el composition root adapta una a otra.
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
