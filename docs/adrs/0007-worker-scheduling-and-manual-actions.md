# ADR 0007: Monitorear con worker local y ejecutar acciones manuales bajo reglas de pausa

## Estado

Accepted

## Contexto

El servicio SAT no entrega paquetes inmediatamente. El flujo principal requiere crear solicitud, consultar estado en el tiempo y descargar paquetes cuando esten disponibles.

La app es personal y local. No existe backend remoto ni daemon independiente.

## Decision

Usar un worker local dentro del proceso de la app.

Reglas:

- El worker consulta solicitudes pendientes cada 10 minutos.
- Si una solicitud no cambia despues de tres verificaciones consecutivas, el intervalo sube a 30 minutos.
- La pausa del monitoreo persiste entre sesiones.
- Si el monitoreo esta pausado, el worker no consulta ni descarga.
- Las acciones manuales `Verificar ahora` y `Reintentar descarga` se ejecutan desde `ServicioAcciones`.
- Si el monitoreo esta pausado, esas acciones quedan pendientes en `SolicitudMasiva` hasta reanudar.
- Las acciones pendientes se representan como dos intenciones idempotentes: `verificacion_pendiente` y `descarga_pendiente`.
- El estado `ErrorSat` de `SolicitudMasiva` es terminal para el worker automatico.
- `EnvioFallido` y `EnvioIncierto` no tienen `IdSolicitud`, son terminales en el MVP y no deben verificarse ni reenviarse.
- El estado `Error` de `PaqueteSolicitud` no se reintenta automaticamente; requiere accion manual.

## Consecuencias

- El usuario mantiene control explicito sobre pausa/reanudacion.
- No se introduce una cola separada de acciones pendientes en el MVP.
- La implementacion debe mostrar acciones pendientes sin convertirlas en estados principales de solicitud.
- Si esta regla resulta demasiado compleja en uso real, puede simplificarse con un ADR posterior.

## Enmienda T014.2 (2026-10-10): intencion por paquete

Contexto: el reintento por paquete (T014.2 D1) necesita, con el monitoreo en pausa, recordar que el usuario pidio reintentar UN paquete y no toda la solicitud (D2). Las dos banderas de `SolicitudMasiva` no lo pueden expresar.

Decision:

- Una intencion manual puede ser de solicitud (`verificacion_pendiente`, `descarga_pendiente` con `accion_pendiente_en`) o de paquete: `paquete_solicitud.reintento_pendiente_en` (migracion 004), NULL sin intencion. No se agrega una tabla de intenciones: cada paquete tiene a lo sumo una intencion idempotente, igual que la solicitud, y una columna conserva la regla de limpieza condicional (D13 de T007) sin una cola nueva.
- Solo se registra para paquetes visibles en `Error` o `Disponible` sin `5008`; la mas reciente prevalece.
- Al reanudar, las intenciones de solicitud y de paquete se procesan juntas en el orden de su instante (`accion_pendiente_en` / `reintento_pendiente_en`; a igual instante, la de solicitud primero).
- El ejecutor revalida el paquete al consumir la intencion. Si ya no aplica (descargado, vencido, `5008`, eliminado) la descarta con `accion_pendiente_descartada` y sin trafico SAT. Una intencion de solicitud y otra de uno de sus paquetes nunca descargan el mismo paquete dos veces.
- Excepcion: si la solicitud o el paquete ya no son visibles, la intencion por paquete se limpia sin log, por T007 D5 (no se escriben resultados ni logs en una solicitud eliminada). Eliminar la solicitud limpia la columna al marcar sus paquetes; si la eliminacion ocurre despues de seleccionar la intencion, el ejecutor la limpia al no encontrar el paquete visible.
- La recuperacion al arrancar no toca la columna: la intencion sobrevive al reinicio y a la reconciliacion de un `Descargando` interrumpido.

Motivo: reintento selectivo en pausa con el minimo cambio de esquema (ADD COLUMN, sin reconstruir tablas) y con la misma semantica que las intenciones existentes.

## Referencias

- `docs/requirements.md`
- `docs/architecture.md`
- `docs/tasks/T014.2-reintento-por-paquete.md` (D1-D3)
