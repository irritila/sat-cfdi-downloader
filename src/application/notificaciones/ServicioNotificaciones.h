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
// - Nunca por paquete: "Facturas descargadas" cuando no quedan paquetes
//   por descargar.
// - T024.1 (D4): una por direccion. "Terminada" con paquetes no notifica
//   (componer devuelve nullopt); con 0 paquetes o SinFacturas (5004) es
//   "Sin facturas en el periodo". Ids, tipos y destinos no cambian.
// - Credencial: una por perfil al pasar a un estado que bloquea el
//   monitoreo (Vencida, NoVigenteAun, SinCredencial, MaterialFaltante,
//   MaterialDanado); volver a Lista no notifica pero rearma el aviso.
// - El resultado de la entrega no se observa: un permiso denegado no cambia
//   estados, logs ni reintentos.
// - Textos (T013 D6, UX-39, T024.1 D3/D4): titulo = evento; cuerpo en dos
//   lineas separadas por "\n": resultado con la direccion ("emitidas"/
//   "recibidas") y "{periodo D5} · RFC ***000" (el perfil). Ortografia
//   completa. Sin RFC completo (solo los 3 ultimos caracteres), Ids, Mensaje
//   SAT ni "solicitud"/"paquete".
class ServicioNotificaciones final : public QObject {
    Q_OBJECT

public:
    explicit ServicioNotificaciones(Notificador& notificador, QObject* parent = nullptr);

    // Composicion pura (expuesta para pruebas). nullopt: la transicion no se
    // notifica (T024.1 D4: Terminada con paquetes).
    static std::optional<Notificacion> componer(const TransicionNotificable& transicion);
    // nullopt para estados que no se notifican (Lista, Validando).
    static std::optional<Notificacion> componerCredencial(const PerfilId& perfil, EstadoCredencial estado);
    // T014.3 D2: aviso de vencimiento de e.firma. id
    // "vencimiento:<perfilId>:<vigenteHasta ISO UTC>:<umbral>", tipo
    // "efirma_por_vencer".
    static Notificacion componerVencimiento(const PerfilId& perfil, const QString& rfc, const QDateTime& vigenteHasta,
                                            int umbral, int dias);
    // "***XYZ" (ultimos 3 caracteres); vacio si no hay RFC.
    static QString rfcEnmascarado(const QString& rfc);

public slots:
    void alConfirmarTransicion(const satcfdi::TransicionNotificable& transicion);
    void alCambiarEstadoCredencial(const satcfdi::PerfilId& perfil, satcfdi::EstadoCredencial estado);

public:
    // T014.3: lo invoca AvisoVencimientoEFirma tras persistir el dedupe. Dedupe
    // tambien en memoria por id. Fire-and-forget (permiso denegado: no pasa nada).
    void notificarVencimiento(const PerfilId& perfil, const QString& rfc, const QDateTime& vigenteHasta, int umbral,
                              int dias);

private:
    Notificador& m_notificador;
    QSet<QString> m_emitidas;                      // ids de transiciones ya notificadas
    QHash<QString, EstadoCredencial> m_credencial; // ultimo estado observado por perfil
};

} // namespace satcfdi
