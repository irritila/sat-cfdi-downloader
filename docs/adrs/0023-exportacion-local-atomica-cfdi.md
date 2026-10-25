# ADR 0023: Exportacion local atomica de CFDI

## Estado

Accepted

Complementa ADR 0014: la exportacion no es una operacion del ejecutor serial. Acota la regla de ejecucion de `docs/architecture.md` a los ZIP de paquetes.

## Contexto

T026 exporta a un ZIP elegido por el usuario los XML de un perfil (por mes, rango o facturas elegidas) y, opcionalmente, su PDF. El ZIP queda sin cifrar, puede guardarse en una carpeta sincronizada y puede sumar miles de CFDI. Mientras se exporta, los paquetes locales pueden eliminarse (ADR 0015), restaurarse (ADR 0022) o seguir descargandose. No debe quedar un destino truncado, un temporal visible para el cliente de sincronizacion ni un archivo del usuario sobrescrito.

Se evaluo escribir con libzip. libzip difiere la lectura de cada entrada a `zip_close` y escribe a un temporal propio que renombra sobre la ruta: una entrada que falla aborta todo el archivo, la omision por CFDI no puede decidirse en ese momento y, sin releer, todo el contenido tendria que quedar en memoria.

## Decision

- **Plan**: una instantanea no paginada del catalogo mensual (ADR 0019, con su deduplicacion canonica) y de la evidencia de cobertura, leida en una sola transaccion por el `PersistenceDispatcher`. Las omisiones conocidas al planear (factura elegida que ya no existe, meses con descargas pendientes o cobertura incompleta) viajan en el plan. Un paquete `OrigenNoDisponible` o `Ilegible` no conserva filas, asi que se informa como linea de mes y no como CFDI individual.
- **Ejecucion**: un hilo propio, una exportacion a la vez, fuera del ejecutor serial. No llama al SAT, no cambia estados persistidos y no modifica los ZIP de paquetes; cada XML se relee por `(paqueteId, posicion)` con el lector seguro y se valida antes de escribirlo.
- **Escritura**: un escritor ZIP propio en streaming (`EscritorZipStreaming`, deflate con zlib, descriptor de datos, directorio central, ZIP64 solo cuando haga falta). Cada entrada se escribe al agregarla con bytes ya validados; el cierre solo escribe el directorio central. Sin cifrado, comentarios ni campos extra; entradas regulares `0600` con la fecha de la exportacion. libzip sigue como lector y como oraculo en pruebas.
- **Publicacion**: `.partial` `0600` con `O_EXCL|O_NOFOLLOW` en `<datos>/exportacion/parciales` (`0700`) si comparte volumen con el destino; si no, en la carpeta destino con un registro local de sus nombres. `F_FULLFSYNC`, `renamex_np(RENAME_EXCL)` y `fsync` de las carpetas. Nunca se sobrescribe: ante un nombre ocupado el parcial se conserva para publicar con otro nombre. Un fallo antes del rename borra solo el parcial propio; un fallo de `fsync` de carpeta despues del rename deja el ZIP publicado con advertencia.
- **Limpieza**: al iniciar el monitoreo se borran los parciales propios huerfanos: archivos regulares con patron exacto, sin seguir enlaces, y en una carpeta registrada solo si su identidad (`st_dev`, `st_ino`) coincide.
- `exportacion/` es estado transitorio local: no entra al respaldo (ADR 0021) ni lo sustituye la restauracion (ADR 0022).
- Logs: solo fases, conteos y codigos; nunca nombres de archivo, rutas, UUID ni RFC.

## Consecuencias

- La exportacion no frena descargas ni verificaciones, y estas no la bloquean.
- La memoria queda acotada a una entrada y al directorio central.
- Se mantiene un subconjunto ZIP propio; su formato se prueba contra libzip, `unzip -t` y `zipinfo`.
- macOS asigna `com.apple.provenance` a los archivos que crea la app y no se puede retirar; el ZIP no lleva ningun otro atributo extendido.
- La fecha DOS de las entradas va en hora local del equipo, convencion de los lectores sin campos extra de tiempo, con granularidad de 2 s.
