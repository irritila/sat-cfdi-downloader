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

## Referencias

- `docs/requirements.md`
- `docs/architecture.md`
