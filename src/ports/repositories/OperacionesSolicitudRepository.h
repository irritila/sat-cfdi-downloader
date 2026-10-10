#pragma once

#include "domain/common/Resultado.h"
#include "domain/operaciones/RegistrosOperacion.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/SolicitudId.h"
#include "domain/solicitudes/SolicitudPersistida.h"
#include "ports/persistence/ErrorPersistencia.h"

#include <QDateTime>
#include <QList>

#include <optional>

namespace satcfdi {

// Repositorio de OPERACIONES del ejecutor serial y del worker (T007, D5, D9,
// D10, D13). Interfaz separada de SolicitudMasivaRepository y
// PaqueteSolicitudRepository para no romper sus implementaciones; la
// implementacion SQLite (corte B2) trabaja sobre solicitud_masiva,
// paquete_solicitud y las columnas de la migracion 003.
//
// Hilo: el del OperacionExecutor (su propia conexion SQLite por hilo); nunca
// el hilo grafico. Sincrono. No es QObject.
// Transaccion:
// - Lecturas (listar*, obtener*, leer*): Tx opcional.
// - Escrituras (marcar*, aplicar*, vencer*, registrar*, consumir*): Tx
//   REQUERIDA (UnitOfWork::begin = BEGIN IMMEDIATE), en la MISMA transaccion
//   que los logs que las acompanan (LogSolicitudRepository::agregar).
// - TODA escritura revalida, en su WHERE, eliminado_en IS NULL de la
//   solicitud (y del paquete) y el estado de origen esperado. Si no coincide
//   devuelve exito con `false`/aplicada=false SIN modificar nada (resultado
//   posterior a una eliminacion o a otra transicion: se descarta).
// Errores comunes: Transaccion (escritura sin tx), Ocupado, Almacenamiento,
// Integridad (CHECK), Interno (fila ilegible).
class OperacionesSolicitudRepository {
public:
    virtual ~OperacionesSolicitudRepository() = default;

    // --- Seleccion acotada (D9). `limite` > 0. Solo filas visibles. ---------

    // Perfiles con algun trabajo SAT pendiente: verificaciones debidas en
    // `ahoraUtc`, descargas automaticas o intenciones pendientes (de solicitud
    // o, desde T014.2, por paquete). Sirve para
    // consultar el estado de credencial UNA vez por perfil (gate D9).
    virtual Resultado<QList<PerfilId>, ErrorPersistencia> listarPerfilesConTrabajo(const QDateTime& ahoraUtc) = 0;

    // Paquetes Disponible/Descargando/Error con vencimiento_estimado_en <=
    // `ahoraUtc`, vencimiento_estimado_en ASC. Local: sin gate de credencial.
    virtual Resultado<QList<PaqueteDescargable>, ErrorPersistencia>
    listarVencimientosEstimados(const QDateTime& ahoraUtc, int limite) = 0;

    // Intenciones (verificacion_pendiente / descarga_pendiente) de los
    // `perfiles` dados, accion_pendiente_en ASC.
    virtual Resultado<QList<IntencionPendiente>, ErrorPersistencia>
    listarIntencionesPendientes(const QList<PerfilId>& perfiles, int limite) = 0;

    // Solicitudes Enviada con estado SAT nulo, Aceptada o EnProceso y
    // siguiente_verificacion_en <= `ahoraUtc` (no NULL: NULL = suspendida o
    // sin agenda), de los `perfiles`, siguiente_verificacion_en ASC. Nunca
    // selecciona Creada (D4) ni estados terminales.
    virtual Resultado<QList<SolicitudPersistida>, ErrorPersistencia>
    listarVerificacionesDebidas(const QList<PerfilId>& perfiles, const QDateTime& ahoraUtc, int limite) = 0;

    // Paquetes Disponible (no Error: solo manual) de solicitudes visibles en
    // estado SAT Terminada, de los `perfiles`, disponible_en ASC.
    virtual Resultado<QList<PaqueteDescargable>, ErrorPersistencia>
    listarDescargasAutomaticas(const QList<PerfilId>& perfiles, int limite) = 0;

    // Paquete visible por id local (cualquier estado) con datos de su solicitud.
    virtual Resultado<std::optional<PaqueteDescargable>, ErrorPersistencia>
    obtenerPaquete(const QString& paqueteId) = 0;

    // Paquetes descargables de una solicitud (Disponible o Error), para
    // consumir una intencion de descarga o "Reintentar descarga" por solicitud.
    virtual Resultado<QList<PaqueteDescargable>, ErrorPersistencia>
    listarPaquetesReintentables(const SolicitudId& solicitudId) = 0;

    // Recuperacion al arrancar (D9): solicitudes en Enviando y paquetes en
    // Descargando, visibles.
    virtual Resultado<TrabajoInterrumpido, ErrorPersistencia> listarInterrumpidos() = 0;

    // Racha de verificacion de una solicitud visible; nullopt si no existe o
    // esta eliminada.
    virtual Resultado<std::optional<RachaVerificacion>, ErrorPersistencia>
    leerRachaVerificacion(const SolicitudId& solicitudId) = 0;

    // --- Envio (D4, D7) -------------------------------------------------------

    // Creada -> Enviando, envio_iniciado_en = `ahoraUtc`. false si no esta
    // Creada o esta eliminada.
    virtual Resultado<bool, ErrorPersistencia> marcarEnviando(const SolicitudId& solicitudId,
                                                              const QDateTime& ahoraUtc) = 0;

    // Enviando -> aplicacion.destino (ver AplicacionEnvio). Tambien la usa la
    // recuperacion (Enviando -> EnvioIncierto). false si no esta Enviando o
    // esta eliminada.
    virtual Resultado<bool, ErrorPersistencia> aplicarEnvio(const AplicacionEnvio& aplicacion) = 0;

    // --- Verificacion (D8, D10, ADR 0015) --------------------------------------

    // Exito: estado SAT, agenda, racha de fallas a (NULL, 0), paquetes nuevos
    // (solo ids no registrados) y vencimiento SAT, todo en la transaccion
    // actual. Exige estado_local = Enviada.
    virtual Resultado<ResultadoAplicacionVerificacion, ErrorPersistencia>
    aplicarVerificacion(const AplicacionVerificacion& aplicacion) = 0;

    // Falla: ultimo_error, racha (clave, contador) y agenda (NULL = suspendida).
    // No toca estado SAT ni verificaciones_sin_cambio.
    virtual Resultado<bool, ErrorPersistencia> aplicarFallaVerificacion(const AplicacionFallaVerificacion& aplicacion) = 0;

    // --- Descarga (D6, D14) -----------------------------------------------------

    // Disponible o Error -> Descargando, descarga_iniciada_en = `ahoraUtc`.
    // Error solo con `permitirError` (accion manual). false si el paquete o su
    // solicitud estan eliminados o el estado no aplica.
    virtual Resultado<bool, ErrorPersistencia> marcarDescargando(const QString& paqueteId, const QDateTime& ahoraUtc,
                                                                 bool permitirError) = 0;

    // Descargando -> aplicacion.destino (ver AplicacionDescarga). false si no
    // esta Descargando o hay eliminacion.
    virtual Resultado<bool, ErrorPersistencia> aplicarDescarga(const AplicacionDescarga& aplicacion) = 0;

    // Vencimiento estimado (D10): Disponible/Descargando/Error con
    // vencimiento_estimado_en <= `ahoraUtc` -> Vencido (estimacion_local,
    // vencimiento_estimado). false si ya no aplica.
    virtual Resultado<bool, ErrorPersistencia> vencerPaqueteEstimado(const QString& paqueteId,
                                                                     const QDateTime& ahoraUtc) = 0;

    // --- Intenciones (DC5, D13) -------------------------------------------------

    // Activa la bandera y fija accion_pendiente_en = `ahoraUtc` (aun si ya
    // estaba activa: la intencion mas reciente prevalece). Ambas banderas
    // pueden coexistir. false si la solicitud esta eliminada.
    virtual Resultado<bool, ErrorPersistencia> registrarIntencion(const SolicitudId& solicitudId, TipoIntencion tipo,
                                                                  const QDateTime& ahoraUtc) = 0;

    // Limpia la bandera `tipo` SOLO si accion_pendiente_en == `capturadaEn`
    // (D13); accion_pendiente_en queda NULL si no queda ninguna bandera. false
    // si la columna cambio (otra intencion se registro mientras tanto) o la
    // solicitud esta eliminada: la intencion sigue pendiente.
    virtual Resultado<bool, ErrorPersistencia> consumirIntencion(const SolicitudId& solicitudId, TipoIntencion tipo,
                                                                 const QDateTime& capturadaEn) = 0;

    // --- Intenciones por paquete (T014.2 D2, ADR 0007 enmendado) ---------------

    // Fija reintento_pendiente_en = `ahoraUtc` (la mas reciente prevalece) SOLO
    // si el paquete es visible, de solicitud visible, esta en Error o
    // Disponible y no tiene codigo_descarga_sat 5008. false si no aplica.
    virtual Resultado<bool, ErrorPersistencia> registrarIntencionPaquete(const QString& paqueteId,
                                                                         const QDateTime& ahoraUtc) = 0;

    // Limpia reintento_pendiente_en SOLO si conserva `capturadaEn` (D13).
    // false si cambio o ya estaba limpia.
    virtual Resultado<bool, ErrorPersistencia> consumirIntencionPaquete(const QString& paqueteId,
                                                                        const QDateTime& capturadaEn) = 0;

    // Intenciones por paquete (paquete y solicitud visibles) de los `perfiles`,
    // reintento_pendiente_en ASC. No filtra por estado: el ejecutor revalida y
    // descarta con log las que ya no aplican (D3).
    virtual Resultado<QList<IntencionPaquete>, ErrorPersistencia>
    listarIntencionesPaquete(const QList<PerfilId>& perfiles, int limite) = 0;
};

} // namespace satcfdi
