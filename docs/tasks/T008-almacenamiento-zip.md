# T008: Almacenamiento local de paquetes ZIP

## Estado

Pendiente

## Prioridad y tamano

- Prioridad: Alta.
- Tamano: Mediana.

## Objetivo

Implementar `PackageStorage` para guardar paquetes ZIP fuera de SQLite con escritura temporal, rename atomico y reconciliacion al iniciar la app.

## Contexto

El MVP descarga paquetes ZIP y los conserva en una carpeta local. La base guarda metadata y rutas, pero no bytes del paquete. La app debe evitar marcar un paquete como descargado antes de que el archivo final exista y debe recuperarse de cierres inesperados.

## Alcance

### Incluye

- Ruta fija de paquetes definida por el MVP.
- Construccion determinista de rutas por RFC, periodo, solicitud y paquete.
- Archivos temporales `.part` o `.tmp`.
- Escritura de bytes a un archivo temporal.
- Rename al archivo final.
- Actualizacion coordinada de estado `PaqueteSolicitud`.
- Deteccion de archivo final, temporal y huerfano.
- Motivo y origen de vencimiento local/SAT.
- Verificacion de existencia para la vista de detalle.
- `FakePackageStorage` o almacenamiento temporal para tests.

### No incluye

- Apertura o validacion interna del ZIP.
- Extraccion de XML.
- Parsing o indexacion de CFDI.
- Eliminacion automatica de paquetes.
- Cambio de ruta desde preferencias.
- Descarga desde SAT.

## Dependencias

- `T001-modelo-fisico-sqlite.md`.
- `T003-persistencia-local.md`.
- `T007-worker-ejecutor-serial.md`.
- ADR 0004 y ADR 0014.

## Trabajo esperado

1. Definir la convencion final de rutas y nombres de archivos.
2. Implementar `PackageStorage` detras del contrato existente.
3. Crear y escribir archivos temporales en la misma carpeta que el destino final.
4. Promover el temporal mediante rename y tratar el resultado como una operacion atomica local.
5. Actualizar `PaqueteSolicitud` a `Descargado` solo despues del rename exitoso.
6. Manejar fallos de permisos, espacio, escritura y rename sin perder trazabilidad.
7. Implementar reconciliacion de estados y archivos al arrancar.
8. Conectar la existencia fisica del ZIP con el detalle sin modificar automaticamente el estado persistido.
9. Agregar tests con carpetas temporales y almacenamiento fake.

## Decisiones que debe cerrar esta tarea

- Ruta exacta bajo `~/SAT-CFDI-Downloader/paquetes/`.
- Convencion para RFC, periodo, solicitud local, `id_paquete_sat` y extension.
- Como evitar colisiones cuando un identificador SAT contiene caracteres no validos para una ruta.
- Que evento y codigo local se registra para un archivo huerfano.
- Que hacer con temporales abandonados: eliminar, conservar o registrar antes de eliminar.
- Como distinguir una descarga interrumpida de un archivo final huerfano.

## Reglas de estado

- `Disponible` significa que el paquete esta registrado y aun no existe un archivo final confirmado.
- `Descargando` significa que el ejecutor inicio la descarga despues de obtener token valido.
- `Descargado` solo se persiste despues del rename al archivo final.
- Un ZIP final existente para un paquete en `Descargando` se reconcilia como `Descargado`.
- Un temporal sin archivo final no se presenta como descargado; el paquete vuelve a `Disponible` o `Error` segun el contexto.
- `Vencido` conserva `motivo_vencimiento` y `origen_vencimiento`.
- La existencia o ausencia detectada al abrir detalle no modifica por si sola el estado persistido.
- El almacenamiento no borra ZIPs por eliminar una solicitud local ni por limpieza automatica.

## Restricciones tecnicas

- La escritura de ZIP se ejecuta mediante `OperacionExecutor`, fuera del hilo grafico.
- El archivo temporal debe estar en el mismo sistema de archivos que el destino para permitir rename atomico.
- El contenido del ZIP no se registra en logs.
- Los archivos huerfanos se conservan para no destruir informacion del usuario.
- No se usa validacion de integridad interna del ZIP en el MVP.

## Criterios de aceptacion

- [ ] La ruta final se construye de forma determinista para la misma solicitud y paquete.
- [ ] El contenido se escribe primero a un archivo temporal.
- [ ] `Descargado` solo se persiste despues del rename exitoso.
- [ ] Un fallo antes del rename no deja el paquete como `Descargado`.
- [ ] Un ZIP final existente permite reconciliar un paquete `Descargando` como `Descargado`.
- [ ] Un temporal sin archivo final se maneja segun la regla definida y queda trazable.
- [ ] Un archivo final sin registro de paquete se conserva y se registra como huerfano.
- [ ] La falta de permisos, espacio o rename produce un error visible y un log sanitizado.
- [ ] La vista de detalle puede indicar si la ruta persistida existe.
- [ ] La vista de detalle no cambia automaticamente el estado `Descargado` si falta el archivo.
- [ ] La eliminacion local de una solicitud no borra ZIPs fisicos.
- [ ] La app no abre, valida, extrae ni indexa el ZIP.
- [ ] Los tests usan carpetas temporales y no modifican paquetes del usuario.

## Verificacion

1. Descargar bytes simulados y comprobar temporal, rename y estado final.
2. Interrumpir la operacion antes del rename.
3. Simular cierre despues del rename y antes de persistir el estado.
4. Iniciar con temporal abandonado.
5. Iniciar con archivo final huerfano.
6. Probar falta de permisos y fallo de rename.
7. Eliminar logicamente la solicitud y verificar que el ZIP permanece.
8. Ejecutar `ctest --test-dir build`.

## Definicion de terminado

- `PackageStorage` implementa las operaciones de ruta, temporal, promocion y existencia.
- Los estados persistidos y los archivos quedan consistentes en los escenarios probados.
- La reconciliacion al arrancar esta implementada y registra eventos.
- El detalle muestra existencia fisica sin cambiar estados automaticamente.
- `T009` puede entregar bytes de SAT al almacenamiento sin conocer sus detalles de filesystem.

## Resultado

Pendiente.

## Riesgos y notas

- Un rename atomico reduce el riesgo de archivos incompletos, pero no sustituye el registro transaccional en SQLite.
- El MVP no garantiza que el ZIP sea valido internamente; esa validacion queda fuera de alcance.
- La retencion de ZIPs es responsabilidad del usuario en esta version personal.

## Referencias

- `T001-modelo-fisico-sqlite.md`
- `T003-persistencia-local.md`
- `T007-worker-ejecutor-serial.md`
- `docs/design/operational-rules.md`
- ADR 0004
- ADR 0014
