#pragma once

#include "application/notificaciones/Notificador.h"
#include "application/operaciones/TiposOperacion.h"
#include "domain/operaciones/TransicionNotificable.h"
#include "domain/perfiles/PerfilId.h"
#include "ports/secrets/SecretStoreTypes.h"

#include <QHash>
#include <QObject>
#include <QSet>

#include <optional>

namespace satcfdi {

// Servicio de notificaciones (T009 D1, D9). Vive en el hilo grafico. Recibe
// hechos YA confirmados (despues del commit) del OperacionExecutor:
// transicionConfirmada y estadoCredencialCambiado; el composition root
// conecta esas senales a los slots.
//
// Reglas:
// - Una notificacion por solicitud y transicion (dedupe en memoria por
//   "<solicitud>:<tipo>"; el ejecutor solo publica transiciones persistidas,
//   asi que un reinicio no repite).
// - Nunca por paquete: solo "Descarga completa: N de N" cuando no quedan
//   paquetes por descargar.
// - Credencial: una por perfil al pasar a un estado que bloquea el
//   monitoreo (Vencida, NoVigenteAun, SinCredencial, MaterialFaltante,
//   MaterialDanado); volver a Lista no notifica pero rearma el aviso.
// - El resultado de la entrega no se observa: un permiso denegado no cambia
//   estados, logs ni reintentos.
// - Textos (T013 D6, UX-39): titulo = evento; cuerpo en dos lineas separadas
//   por "\n": resultado y "{Tipo} · {periodo D5} · RFC ***000". Ortografia
//   completa. Sin RFC completo (solo los 3 ultimos caracteres), Ids ni
//   Mensaje SAT.
class ServicioNotificaciones final : public QObject {
    Q_OBJECT

public:
    explicit ServicioNotificaciones(Notificador& notificador, QObject* parent = nullptr);

    // Composicion pura (expuesta para pruebas).
    static Notificacion componer(const TransicionNotificable& transicion);
    // nullopt para estados que no se notifican (Lista, Validando).
    static std::optional<Notificacion> componerCredencial(const PerfilId& perfil, EstadoCredencial estado);
    // "***XYZ" (ultimos 3 caracteres); vacio si no hay RFC.
    static QString rfcEnmascarado(const QString& rfc);

public slots:
    void alConfirmarTransicion(const satcfdi::TransicionNotificable& transicion);
    void alCambiarEstadoCredencial(const satcfdi::PerfilId& perfil, satcfdi::EstadoCredencial estado);

private:
    Notificador& m_notificador;
    QSet<QString> m_emitidas;                      // ids de transiciones ya notificadas
    QHash<QString, EstadoCredencial> m_credencial; // ultimo estado observado por perfil
};

} // namespace satcfdi
