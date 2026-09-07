# Requerimientos - SAT CFDI Downloader

Estado: borrador simple para revision iterativa.

## 1. Contexto

SAT CFDI Downloader es una aplicacion de escritorio para uso personal. El usuario sera una sola persona actuando como contador desde su propio equipo.

La aplicacion no se esta disenando como producto comercial, SaaS, portal multiusuario ni herramienta para clientes hipoteticos. Las decisiones deben mantenerse simples y enfocadas en el uso real actual.

## 2. Objetivo

Permitir crear solicitudes de descarga masiva de CFDI ante el SAT, guardar localmente la informacion de cada solicitud, monitorear su estatus en segundo plano, descargar los paquetes disponibles y consultar el detalle de cada solicitud desde una interfaz de escritorio.

La aplicacion debe poder mantenerse ejecutandose en segundo plano, iniciar automaticamente con la sesion de macOS y ofrecer acceso rapido desde el menu bar de macOS.

El MVP descarga y guarda paquetes ZIP. No extrae, parsea ni indexa XML.

## 3. Alcance del MVP

El MVP incluye:

- Aplicacion desktop para macOS.
- Un solo usuario local.
- Perfiles SAT de contribuyentes/RFC.
- Registro seguro de e.firma por perfil SAT.
- Crear nueva solicitud de descarga masiva.
- Guardar localmente la metadata de cada solicitud.
- Listar solicitudes creadas.
- Mostrar el estatus actual de cada solicitud.
- Monitorear solicitudes en segundo plano mediante un worker local.
- Iniciar automaticamente con la sesion de macOS, si el usuario lo habilita.
- Mostrar un icono en el menu bar de macOS para acceso rapido.
- Descargar paquetes cuando el SAT los deje disponibles.
- Guardar paquetes descargados localmente.
- Navegar al detalle de una solicitud.
- Mostrar en el detalle los parametros enviados, respuesta SAT, estatus, paquetes y logs basicos.
- Emitir notificaciones nativas de macOS para eventos relevantes.

## 4. Decisiones del MVP

- Tipo de solicitud inicial: CFDI/XML. Metadata queda fuera del MVP.
- Plataforma inicial: macOS.
- Stack inicial: Qt 6 con QML / Qt Quick Controls y C++.
- Descarga de paquetes: automatica cuando SAT reporte paquetes disponibles.
- Inicio automatico: deshabilitado por defecto; el usuario puede habilitarlo. Cuando este habilitado, la app inicia solo con el icono de menu bar y el worker; no abre la ventana principal.
- Apertura manual: cuando el usuario abre la app manualmente, la ventana principal debe mostrarse aunque el proceso ya este ejecutandose en el menu bar.
- Activacion macOS: la app usa modo foreground al abrir manualmente y modo background/menu bar al iniciar por Login Item.
- Cerrar ventana principal: oculta la app al menu bar de macOS y mantiene el worker activo.
- Salir de la app: requiere accion explicita desde el icono de menu bar o comando equivalente de macOS.
- Registro al salir: el cierre de la app guarda estado a nivel aplicacion. No se crea `LogSolicitud` por cada solicitud activa; solo se registra `LogSolicitud` si una operacion concreta de solicitud o paquete fue interrumpida por la salida.
- Pausa del worker: si el usuario pausa el monitoreo, la pausa persiste entre sesiones hasta que el usuario reanude.
- Frecuencia inicial del worker: cada 10 minutos para solicitudes pendientes.
- Backoff inicial: si una solicitud permanece sin cambios despues de tres verificaciones consecutivas, verificarla cada 30 minutos.
- Carpeta local por defecto: `~/SAT-CFDI-Downloader/paquetes/{rfc}/{yyyy-mm}/{solicitud_id}/`.
- En la ruta local, `{yyyy-mm}` corresponde al mes de la fecha inicial solicitada y `{solicitud_id}` corresponde al identificador SAT de la solicitud.
- Retencion local: el MVP no borra paquetes ZIP automaticamente.

## 5. Fuera de alcance actual

Queda fuera del MVP:

- Backend remoto.
- Multiusuario.
- Roles y permisos.
- Portal para terceros.
- Dashboards contables.
- Conciliacion.
- Validacion de vigencia/cancelacion de CFDI.
- Extraccion, parseo o indexacion de XML.
- Consulta local de CFDI por campos internos del XML.
- Solicitudes de metadata como flujo alterno.
- Exportaciones avanzadas.
- Integraciones con sistemas contables.
- Timbrado o cancelacion de CFDI.
- Procesamiento fiscal avanzado.
- Servicio remoto o daemon multiusuario independiente de la sesion local.

## 6. Flujo principal

1. El usuario registra o selecciona un perfil SAT.
2. El usuario crea una solicitud de descarga masiva con los filtros permitidos por el SAT.
3. La aplicacion envia la solicitud al SAT.
4. La aplicacion guarda localmente la metadata de la solicitud.
5. La solicitud aparece en la lista con su estatus actual.
6. El worker local consulta periodicamente el estatus de solicitudes pendientes.
7. Cuando el SAT devuelve paquetes disponibles, la aplicacion los descarga.
8. La lista se actualiza con el avance de cada solicitud.
9. El usuario abre el detalle de una solicitud para revisar estatus, metadata, paquetes y logs.

Cuando el inicio automatico este habilitado, la aplicacion debe iniciar con la sesion de macOS, mostrar solo el icono de menu bar y continuar el monitoreo sin abrir automaticamente la ventana principal.

Al cerrar la ventana principal, la aplicacion debe permanecer activa en el menu bar de macOS. Para detener el worker y cerrar el proceso, el usuario debe ejecutar una accion explicita de salida.

## 7. Pantallas del MVP

### 7.1 Perfiles SAT

Pantalla para administrar los RFC que el usuario usara para operar ante el SAT.

Debe permitir:

- Crear perfil SAT.
- Editar nombre descriptivo del perfil.
- Registrar certificado, llave privada y contrasena de e.firma.
- Ver si el perfil esta listo para crear solicitudes.

### 7.2 Solicitudes

Pantalla principal de la aplicacion.

Debe mostrar una lista de solicitudes masivas creadas.

Columnas sugeridas:

- RFC.
- Tipo de solicitud.
- Fecha inicial.
- Fecha final.
- Estatus actual.
- Codigo SAT.
- Mensaje SAT.
- Numero de paquetes.
- Fecha de creacion.
- Ultima actualizacion.

Acciones sugeridas:

- Crear nueva solicitud.
- Actualizar estatus.
- Abrir detalle.
- Descargar paquetes disponibles.
- Reintentar descarga fallida.
- Eliminar solicitud local.

La accion de descargar o reintentar descarga solo aplica cuando existen paquetes pendientes, fallidos o no descargados por pausa del worker.

### 7.3 Nueva solicitud

Formulario para crear una solicitud de descarga masiva.

Debe pedir solo datos que el servicio del SAT soporte.

Campos iniciales:

- Perfil SAT.
- Tipo: emitidos o recibidos.
- Fecha inicial.
- Fecha final.
- Tipo de solicitud fijo: CFDI/XML para el MVP.
- RFC contraparte, si aplica segun SAT.
- Tipo de comprobante, si aplica segun SAT.
- Complemento, si aplica segun SAT.

Nota: los campos del formulario deben mantenerse alineados con la especificacion local del servicio SAT.

El mapeo de esos campos a las operaciones SAT `SolicitaDescargaEmitidos` y `SolicitaDescargaRecibidos` queda definido en `docs/adrs/0013-sat-request-operations-v15.md`.

Antes de enviar una solicitud al SAT, la aplicacion debe validar localmente que no exista otra solicitud activa/no eliminada para el mismo perfil SAT con los mismos filtros relevantes para el servicio. Si existe, debe bloquear el envio y mostrar la solicitud existente, sin llamar al SAT.

Para el MVP, `EstadoComprobante` no se expone en UI. La aplicacion lo fija internamente en `Vigente` para solicitudes CFDI/XML.

### 7.4 Detalle de solicitud

Pantalla para revisar una solicitud especifica.

Debe mostrar:

- Perfil SAT usado.
- Filtros enviados.
- Identificador de solicitud SAT.
- Estatus actual.
- Codigo y mensaje SAT.
- Numero de CFDI reportados por SAT, si aplica.
- Identificadores de paquetes, si existen.
- Estado de descarga de cada paquete.
- Timestamps relevantes.
- Logs basicos de la solicitud.
- Respuesta SAT relevante para diagnostico.

Acciones sugeridas:

- Verificar ahora.
- Descargar paquetes disponibles.
- Reintentar descarga fallida.
- Eliminar solicitud local.

Eliminar una solicitud local no modifica nada en SAT. En el MVP marca virtualmente como eliminados los registros locales de la aplicacion, pero no borra automaticamente paquetes ZIP ya descargados.

Si una solicitud queda vencida sin haber descargado todos sus paquetes, el detalle debe mostrar un aviso visible. La aplicacion no debe recrear solicitudes por si sola; el usuario podra crear otra solicitud manualmente usando los filtros visibles.

Si un paquete esta marcado como descargado pero el ZIP ya no existe en la ruta local, el detalle debe mostrarlo como archivo no encontrado sin cambiar automaticamente el estado persistido del paquete.

### 7.5 Icono de menu bar

La aplicacion debe mostrar un icono de acceso rapido en el menu bar de macOS.

Debe permitir:

- Ver si el worker esta activo.
- Abrir la ventana principal.
- Ver un resumen simple de solicitudes pendientes o con error.
- Pausar o reanudar el monitoreo.
- Ejecutar una revision manual.
- Salir de la aplicacion.

Si el usuario pausa el monitoreo desde el menu bar, la pausa debe persistir aunque cierre y vuelva a abrir la aplicacion.

### 7.6 Notificaciones del sistema

La aplicacion debe emitir notificaciones nativas de macOS cuando:

- Una solicitud pase a terminada.
- Una descarga de paquete concluya.
- Una solicitud pase a error, rechazada o vencida.

Cuando una solicitud pase a vencida, la notificacion debe indicar que los paquetes pueden ya no estar disponibles y que podria requerirse crear una nueva solicitud.

## 8. Worker local

La aplicacion debe tener un worker local que corra en segundo plano mientras la aplicacion este ejecutandose. La ventana principal puede estar cerrada u oculta, pero el proceso debe seguir activo desde el icono de menu bar.

Responsabilidades:

- Buscar solicitudes pendientes.
- Consultar su estatus ante SAT.
- Actualizar la base local con la respuesta.
- Detectar paquetes disponibles.
- Descargar paquetes pendientes.
- Registrar errores.
- Evitar repetir descargas ya completadas.

El worker no debe crear solicitudes nuevas por decision propia. Solo debe continuar el monitoreo y descarga de solicitudes creadas por el usuario.

El worker debe iniciar automaticamente al iniciar la sesion de macOS cuando el usuario haya habilitado esa preferencia. Este comportamiento pertenece a la sesion local del usuario, no a un servicio remoto ni a un proceso compartido entre usuarios.

El intervalo inicial de verificacion es de 10 minutos por solicitud pendiente. Si una solicitud no cambia despues de tres verificaciones consecutivas, el intervalo para esa solicitud sube a 30 minutos.

Si el monitoreo esta pausado, el worker no debe consultar ni descargar hasta que el usuario lo reanude. La pausa debe guardarse localmente y sobrevivir al reinicio de la aplicacion.

Si el monitoreo esta pausado, las acciones manuales que impliquen verificar estatus o descargar paquetes deben quedar pendientes hasta que el usuario reanude el monitoreo.

Para mantener simple el MVP, las acciones manuales pendientes se guardan como dos intenciones idempotentes en `SolicitudMasiva`: verificacion pendiente y descarga pendiente. No existe una entidad separada para cola de acciones.

## 9. Estados

La aplicacion debe mostrar estados simples y entendibles. El modelo persiste por separado el ciclo local de envio y el estado devuelto por SAT; la UI puede presentar un resumen derivado.

Estados sugeridos para `SolicitudMasiva`:

- Creada.
- Enviando.
- Enviada.
- Aceptada.
- En proceso.
- Terminada.
- Error SAT.
- Envio fallido.
- Envio incierto.
- Rechazada.
- Vencida.

Estados sugeridos para `PaqueteSolicitud`:

- Disponible.
- Descargando.
- Descargado.
- Error.
- Vencido.

La lista de solicitudes puede mostrar un estado agregado de descarga cuando una solicitud terminada tenga paquetes en `Disponible`, `Descargando`, `Descargado`, `Error` o `Vencido`, pero el estado SAT de la solicitud y el estado local de paquetes deben mantenerse separados.

El estado `Error SAT` de una solicitud es terminal para el worker automatico. Cualquier reintento debe ejecutarse como accion manual del usuario.

Una solicitud en `Envio fallido` no tiene `IdSolicitud` SAT y es terminal en el MVP. Una solicitud en `Envio incierto` tampoco tiene `IdSolicitud`; la aplicacion no debe reenviarla porque no sabe si SAT alcanzo a registrar la solicitud. El usuario puede crear una nueva solicitud manualmente.

El estado `Error` de un paquete no se reintenta automaticamente por el worker. Cualquier reintento de descarga debe ejecutarse como accion manual del usuario.

Los estados internos de la aplicacion deben conservar los codigos originales del SAT para diagnostico.

La aplicacion debe guardar por separado:

- Codigo y mensaje de creacion de solicitud (`CodEstatus` y `Mensaje`).
- Estado y codigo de verificacion (`EstadoSolicitud`, `CodigoEstadoSolicitud` y mensaje).
- Codigo y mensaje de descarga por paquete.

Estados SAT documentados para `EstadoSolicitud`:

| Valor | Estado |
| --- | --- |
| 1 | Aceptada |
| 2 | En proceso |
| 3 | Terminada |
| 4 | Error |
| 5 | Rechazada |
| 6 | Vencida |

## 10. Datos locales

La aplicacion debe guardar datos en una base local.

Entidades iniciales:

- `PerfilSat`: RFC y datos descriptivos del contribuyente.
- `CredencialSat`: referencia segura a certificado, llave privada y contrasena.
- `SolicitudMasiva`: solicitud creada por el usuario y enviada al SAT.
- `PaqueteSolicitud`: paquete reportado o descargado para una solicitud.
- `LogSolicitud`: eventos relevantes de una solicitud.
- `ConfiguracionApp`: preferencias locales de la aplicacion.

Cada `PerfilSat` tiene una sola credencial activa, reemplazable cuando el usuario actualiza e.firma. `ConfiguracionApp` se maneja como un registro unico local.

Metadata minima de `SolicitudMasiva`:

- Perfil SAT.
- Operacion SAT usada.
- RFC solicitante.
- Tipo: emitidos o recibidos.
- Fechas solicitadas.
- Filtros enviados.
- Clave local anti-duplicados.
- Identificador SAT de solicitud.
- Estatus actual.
- Codigo y mensaje de creacion de solicitud.
- Estado, codigo y mensaje de verificacion.
- Numero de CFDI reportados, si aplica.
- Fecha de creacion.
- Fecha de inicio de envio.
- Fecha de envio.
- Ultima verificacion.
- Ultimo error, si existe.
- Verificacion pendiente, si existe.
- Descarga pendiente, si existe.
- Fecha de accion manual pendiente, si existe.

El numero de CFDI reportados por SAT puede venir vacio, nulo o en cero aun cuando existan paquetes. No debe usarse como unico indicador de exito o fracaso de una solicitud.

Metadata minima de `PaqueteSolicitud`:

- Solicitud asociada.
- Identificador de paquete SAT.
- Estatus de descarga.
- Ruta local del archivo descargado.
- Fecha de descarga.
- Codigo y mensaje SAT de descarga, si existen.
- Error de descarga, si existe.
- Fecha de vencimiento estimada, si se puede inferir de la respuesta SAT o del momento en que se reporto disponible.

## 11. Almacenamiento local

La aplicacion debe usar almacenamiento local.

Debe existir:

- Base de datos local para solicitudes, perfiles, paquetes y logs.
- Carpeta local para paquetes descargados.
- Logs locales para diagnostico.

Los paquetes se guardan por defecto en:

```text
~/SAT-CFDI-Downloader/paquetes/{rfc}/{yyyy-mm}/{solicitud_id}/
```

Donde:

- `{rfc}` es el RFC solicitante.
- `{yyyy-mm}` es el mes de la fecha inicial solicitada.
- `{solicitud_id}` es el identificador SAT de la solicitud.

La carpeta de una solicitud se crea cuando la aplicacion ya conoce el identificador SAT.

La ruta local de paquetes queda fija en el MVP. Una pantalla de configuracion para cambiarla puede agregarse despues.

El MVP no ejecuta limpieza automatica de paquetes ZIP. El usuario administra la carpeta local; la accion de eliminar solicitud local no borra ZIPs automaticamente.

## 12. Seguridad

La aplicacion debe tratar e.firma, llaves privadas, contrasenas, tokens SAT y paquetes descargados como informacion sensible.

Requerimientos:

- Importar/copiar `.cer` y `.key` al almacenamiento controlado por la aplicacion al registrar e.firma.
- No guardar contrasenas en texto plano.
- No registrar secretos en logs.
- Proteger credenciales mediante `SecretStore`; en macOS el adaptador inicial usa Keychain y archivos controlados/cifrados por la aplicacion.
- No persistir tokens SAT. Solo pueden mantenerse en memoria del proceso hasta expirar, fallar autenticacion, terminar el ciclo en curso o cerrar la app.
- No guardar tokens SAT en logs ni en texto plano.
- Guardar peticiones/respuestas SAT en `LogSolicitud` solo despues de sanitizar token, firma, llave privada, contrasena y cualquier otro secreto.
- No guardar en `LogSolicitud` el contenido `Paquete` de descarga ni base64/bytes del ZIP.
- Permitir operar sin backend remoto.

## 13. Requerimientos funcionales

### RF-001 Administrar perfiles SAT

La aplicacion debe permitir crear y editar perfiles SAT locales.

### RF-002 Registrar e.firma

La aplicacion debe permitir registrar certificado, llave privada y contrasena de e.firma para un perfil SAT.

Al registrar e.firma, la aplicacion debe importar/copiar `.cer` y `.key` al almacenamiento controlado por la aplicacion.

Antes de marcar el perfil como listo para crear solicitudes, la aplicacion debe validar que la contrasena abre la llave privada, que certificado y llave corresponden, que el RFC del certificado corresponde al perfil y que la e.firma esta vigente.

### RF-003 Crear solicitud masiva

La aplicacion debe permitir crear una solicitud de descarga masiva usando un perfil SAT y filtros soportados por el SAT.

Antes de llamar al SAT, la aplicacion debe rechazar solicitudes locales duplicadas con los mismos parametros relevantes. Esta defensa evita gastar cupos o topar errores SAT por solicitudes duplicadas.

La comparacion anti-duplicados debe usar la clave normalizada definida en `docs/design/operational-rules.md`. Las solicitudes en curso bloquean duplicados; las solicitudes terminales o eliminadas no bloquean automaticamente, pero deben mostrarse como advertencia antes de reenviar.

### RF-004 Guardar solicitud localmente

La aplicacion debe guardar la metadata de cada solicitud creada, incluyendo parametros, identificador SAT, codigos, mensajes y fechas relevantes.

### RF-005 Listar solicitudes

La aplicacion debe mostrar una lista de solicitudes masivas creadas con su estatus actual.

### RF-006 Consultar estatus

La aplicacion debe consultar el estatus de una solicitud ante el SAT y actualizar la base local.

### RF-007 Monitorear en segundo plano

La aplicacion debe monitorear solicitudes pendientes mediante un worker local mientras el proceso este ejecutandose, aunque la ventana principal no este visible.

### RF-008 Descargar paquetes

La aplicacion debe descargar paquetes ZIP disponibles para solicitudes terminadas.

La aplicacion debe escribir cada ZIP primero a un archivo temporal y marcarlo como descargado solo despues de renombrarlo a la ruta final.

### RF-009 Ver detalle

La aplicacion debe permitir abrir el detalle de una solicitud desde la lista.

### RF-010 Registrar logs

La aplicacion debe registrar eventos basicos de cada solicitud: creacion, envio, verificacion, descarga, error y cambios de estado.

El cierre normal de la aplicacion no debe registrar `LogSolicitud` por cada solicitud activa. Solo debe agregarse un `LogSolicitud` cuando una operacion concreta de solicitud o paquete sea interrumpida por la salida.

### RF-011 Manejar errores SAT

La aplicacion debe guardar codigo y mensaje SAT cuando ocurra un error o rechazo, separando origen de creacion de solicitud, verificacion de solicitud y descarga de paquete.

### RF-012 Evitar duplicar descargas

La aplicacion debe detectar paquetes ya descargados para no descargarlos otra vez sin accion explicita del usuario.

### RF-013 Inicio automatico y apertura manual

La aplicacion debe permitir habilitar o deshabilitar el inicio automatico con la sesion de macOS. Cuando este habilitado, la aplicacion debe iniciar en segundo plano con el icono de menu bar, sin abrir la ventana principal.

Cuando el usuario abra manualmente la aplicacion, la ventana principal debe mostrarse aunque el proceso ya exista en el menu bar.

### RF-014 Icono de menu bar

La aplicacion debe mostrar un icono en el menu bar de macOS para acceso rapido al estado del worker y acciones principales.

### RF-015 Ejecutar con ventana oculta

La aplicacion debe poder seguir monitoreando y descargando solicitudes aunque la ventana principal este cerrada u oculta, siempre que el proceso local siga ejecutandose.

### RF-016 Notificaciones del sistema operativo

La aplicacion debe emitir notificaciones nativas de macOS cuando una solicitud termine, una descarga concluya o una solicitud pase a error, rechazada o vencida.

### RF-017 Comportamiento al cerrar ventana

La aplicacion debe ocultarse al menu bar de macOS cuando el usuario cierre la ventana principal. Salir de la aplicacion requiere una accion explicita.

Al salir de la aplicacion, se guarda estado de cierre a nivel aplicacion. No se generan logs de solicitud salvo que exista una verificacion o descarga en curso que deba marcarse como interrumpida.

### RF-018 Acciones manuales sobre solicitud

La aplicacion debe permitir ejecutar acciones manuales sobre una solicitud: verificar ahora, reintentar descarga y eliminar solicitud local.

Reintentar descarga solo aplica a paquetes pendientes o fallidos. Si el monitoreo esta pausado, la accion queda pendiente hasta reanudar.

Eliminar solicitud local no modifica SAT ni borra paquetes ZIP automaticamente en el MVP. La eliminacion local es virtual: marca como eliminados `SolicitudMasiva`, `PaqueteSolicitud` y `LogSolicitud`.

### RF-019 Retencion local

La aplicacion no debe borrar paquetes ZIP automaticamente. La limpieza de archivos queda bajo control del usuario.

## 14. Requerimientos no funcionales

### RNF-001 Simplicidad

La aplicacion debe mantenerse enfocada en crear, listar, monitorear y detallar solicitudes masivas.

### RNF-002 Local-first

La aplicacion debe funcionar sin backend remoto. Solo requiere internet para comunicarse con SAT.

### RNF-003 Trazabilidad

La aplicacion debe conservar suficiente informacion para saber que se solicito, cuando se solicito, que respondio SAT y que paquetes se descargaron.

### RNF-004 Seguridad local

La aplicacion debe proteger credenciales y evitar exponer secretos en archivos, base de datos o logs.

### RNF-005 Recuperacion basica

Si la app se cierra y vuelve a abrir, debe poder continuar monitoreando solicitudes pendientes desde la informacion guardada localmente.

### RNF-006 Escala inicial

La aplicacion debe soportar alrededor de 10,000 CFDI como escala inicial de uso personal.

### RNF-007 Integracion con el sistema operativo

La aplicacion debe integrarse con el mecanismo normal de inicio automatico, menu bar y notificaciones nativas de macOS.

### RNF-008 Control explicito del usuario

El usuario debe poder saber si la aplicacion esta corriendo en segundo plano y debe poder pausar, reanudar o salir desde el icono de menu bar.

## 15. Criterios de aceptacion

### CA-001 Crear perfil SAT

Dado que el usuario captura los datos de un perfil SAT, cuando guarda el perfil, entonces la aplicacion lo muestra disponible para crear solicitudes.

### CA-002 Crear solicitud

Dado un perfil SAT valido, filtros soportados y sin una solicitud activa equivalente, cuando el usuario crea una solicitud, entonces la aplicacion la envia al SAT y guarda su metadata localmente.

Dado que ya existe una solicitud activa equivalente, cuando el usuario intenta crearla otra vez, entonces la aplicacion muestra la solicitud existente y no llama al SAT.

### CA-003 Listar solicitudes

Dado que existen solicitudes guardadas, cuando el usuario abre la pantalla principal, entonces ve la lista de solicitudes con su estatus actual.

### CA-004 Monitorear solicitud

Dado que existe una solicitud pendiente, cuando el worker local corre, entonces consulta el SAT y actualiza su estatus en la base local.

### CA-005 Ver detalle

Dado que existe una solicitud en la lista, cuando el usuario abre su detalle, entonces ve parametros, estatus, codigos SAT, mensajes, paquetes y logs basicos.

### CA-006 Descargar paquetes

Dado que una solicitud tiene paquetes disponibles, cuando el worker o el usuario inicia la descarga, entonces la aplicacion guarda los paquetes localmente con archivo temporal, renombra a la ruta final y solo entonces actualiza su estado a descargado.

### CA-007 Registrar error

Dado que SAT responde con error, rechazo o vencimiento, cuando la aplicacion procesa la respuesta, entonces guarda codigo, mensaje, origen y fecha del evento.

### CA-008 Continuar despues de cerrar

Dado que la aplicacion se cerro con solicitudes pendientes, cuando se vuelve a abrir, entonces muestra esas solicitudes y puede continuar monitoreandolas.

### CA-009 Inicio automatico

Dado que el usuario habilito el inicio automatico, cuando inicia sesion en macOS, entonces la aplicacion inicia en segundo plano, muestra el icono de menu bar sin abrir la ventana principal y continua monitoreando solicitudes pendientes.

### CA-010 Icono de menu bar

Dado que la aplicacion esta ejecutandose, cuando el usuario revisa el menu bar de macOS, entonces ve un icono que permite abrir la app, revisar estado general del worker, pausar/reanudar monitoreo y salir.

### CA-011 Ventana cerrada con worker activo

Dado que la ventana principal esta cerrada u oculta pero la aplicacion sigue activa en el menu bar de macOS, cuando existen solicitudes pendientes, entonces el worker continua consultando el SAT y descargando paquetes disponibles.

### CA-012 Notificacion del sistema

Dado que una solicitud termina, una descarga concluye o una solicitud falla, cuando la aplicacion procesa el cambio, entonces emite una notificacion nativa de macOS.

### CA-013 Acciones manuales

Dado que el usuario abre una solicitud, cuando ejecuta una accion manual disponible, entonces la aplicacion verifica ahora, reintenta una descarga fallida o marca la solicitud local como eliminada segun corresponda.

Si el monitoreo esta pausado, verificar ahora y reintentar descarga quedan pendientes hasta reanudar.

### CA-014 Pausa persistente

Dado que el usuario pausa el worker desde el menu bar, cuando cierra y vuelve a abrir la aplicacion, entonces el monitoreo permanece pausado hasta que el usuario lo reanude.

### CA-015 Retencion de ZIP

Dado que existen paquetes ZIP descargados, cuando la aplicacion monitorea, descarga o elimina una solicitud local, entonces no borra esos ZIPs automaticamente.

## 16. Preguntas abiertas

- No bloqueante: que nivel de detalle debe tener el log visible en la pantalla de detalle?
