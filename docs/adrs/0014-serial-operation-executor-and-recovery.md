# ADR 0014: Serializar operaciones y definir recuperacion transaccional

## Estado

Accepted

## Contexto

El worker automatico y las acciones manuales pueden operar sobre las mismas solicitudes y paquetes. Sin una regla de ejecucion, la app podria:

- Descargar dos veces el mismo paquete.
- Aplicar una respuesta SAT despues de que el usuario elimino localmente la solicitud.
- Dejar una solicitud `Terminada` sin paquetes registrados si el proceso se interrumpe entre actualizaciones.
- Dejar registros abandonados en `Enviando` o `Descargando` tras un cierre inesperado.

El MVP sigue siendo personal y local-first. No se justifica una infraestructura de colas compleja, pero si hace falta una frontera clara de concurrencia y recuperacion.

## Decision

Usar un ejecutor serial de operaciones dentro del proceso local de la app.

Reglas:

- Worker y acciones manuales no ejecutan directamente llamadas SAT ni escrituras criticas. Encolan operaciones en el ejecutor serial.
- El ejecutor corre fuera del hilo grafico.
- Solo una operacion critica se ejecuta a la vez en el MVP.
- Cada operacion revisa si la solicitud o paquete fue eliminado virtualmente antes de aplicar resultados.
- Las conexiones SQLite pertenecen al hilo del ejecutor o se crean por hilo; no se comparten con el hilo grafico.
- Los modelos QML se actualizan en el hilo grafico mediante resultados publicados por view models.
- Las operaciones que actualizan solicitud y paquetes relacionados deben usar transacciones locales.

Recuperacion al arrancar:

- `Enviando` sin respuesta SAT se marca como `EnvioIncierto` si la llamada pudo haber salido a red.
- `Creada` sin intento de envio se puede volver a programar para envio.
- `Descargando` se regresa a `Disponible` y se registra log de interrupcion local, salvo que exista evidencia de descarga completada.
- Archivos temporales de descarga se eliminan o se ignoran al arrancar.
- `Terminada` con paquetes no registrados no debe existir si la actualizacion se hizo en una transaccion; si se detecta, se programa una verificacion manual/automatica segura.

Descarga de ZIP:

- El paquete se marca como `Descargando` despues de tener token valido.
- El ZIP se escribe primero a un archivo temporal.
- El estado `Descargado` solo se persiste despues de renombrar el temporal al archivo final.
- No se abre ni valida internamente el ZIP en el MVP.
- Si SAT devuelve paquete inexistente/expirado, el paquete se marca `Vencido` y la solicitud se marca `Vencida` cuando ya no queden paquetes descargables.
- El vencimiento estimado tambien puede cerrar paquetes pendientes durante el ciclo del worker, sin borrar archivos ni recrear solicitudes automaticamente.

Acciones pendientes con monitoreo pausado:

- No se crea tabla de cola.
- `SolicitudMasiva` guarda intenciones idempotentes separadas: `verificacion_pendiente` y `descarga_pendiente`.
- Al reanudar monitoreo, el ejecutor consume esas intenciones, las borra al terminar y registra el resultado en `LogSolicitud`.

## Consecuencias

- La implementacion gana una pieza de coordinacion, pero evita condiciones de carrera tempranas.
- El modelo fisico SQLite debe incluir campos suficientes para envio incierto, codigos separados, intenciones pendientes y control de operacion.
- Los tests de aplicacion deben cubrir reinicio, eliminacion local durante una operacion y descarga interrumpida.
- Para uso personal, un ejecutor serial global es aceptable; si despues limita demasiado, se puede evolucionar a serializacion por solicitud.

## Referencias

- `docs/design/operational-rules.md`
- `docs/requirements.md`
- `docs/architecture.md`
- `docs/design/qt-project-structure.md`
