# Requerimientos - Archivo Fiscal

Estado: borrador simple para revision iterativa.

## 1. Contexto

Archivo Fiscal es una aplicacion de escritorio para uso personal. El usuario sera una sola persona actuando como contador desde su propio equipo.

La aplicacion no se esta disenando como producto comercial, SaaS, portal multiusuario ni herramienta para clientes hipoteticos. Las decisiones deben mantenerse simples y enfocadas en el uso real actual.

## 2. Objetivo

Permitir ver las facturas (CFDI) emitidas y recibidas de cada perfil SAT por mes y descargar las que falten mediante el servicio de descarga masiva del SAT, guardarlas localmente, monitorear las descargas en segundo plano y revisar su avance desde una interfaz de escritorio (T023).

Las solicitudes de descarga masiva y sus paquetes ZIP son el mecanismo interno: la descarga guiada de Facturas los crea y el worker los continua. El usuario no los ve en el recorrido normal (Facturas, Historial, Perfiles SAT, Respaldo); solo en Diagnostico (T023 D3, D10; T024 D3).

La aplicacion debe poder mantenerse ejecutandose en segundo plano, iniciar automaticamente con la sesion de macOS y ofrecer acceso rapido desde el menu bar de macOS.

El MVP guarda los ZIP del SAT tal como llegan; para mostrar las facturas los lee localmente (T015, T023), sin validacion, conciliacion ni procesamiento fiscal.

## 3. Alcance del MVP

El MVP incluye:

- Aplicacion desktop para macOS.
- Un solo usuario local.
- Perfiles SAT de contribuyentes/RFC.
- Registro seguro de e.firma por perfil SAT.
- Pantalla Facturas: facturas emitidas y recibidas de un perfil por mes, estado del mes y una accion para descargar las que falten ("Descargar el mes", "Actualizar mes" o "Completar mes"); internamente se crean las solicitudes SAT necesarias (T023).
- Guardar localmente la metadata de cada solicitud.
- Historial de descargas por perfil y mes.
- Mostrar el estado de cada mes: Sin descargar, Preparando, Disponibles, Disponibles parcialmente o Requiere atencion (T023 D8).
- Monitorear solicitudes en segundo plano mediante un worker local.
- Iniciar automaticamente con la sesion de macOS, si el usuario lo habilita.
- Mostrar un icono en el menu bar de macOS para acceso rapido.
- Descargar los archivos del SAT (paquetes ZIP) cuando el SAT los deje disponibles.
- Guardar los archivos descargados localmente.
- Diagnostico (Ir > Diagnostico): lista tecnica de solicitudes y su detalle, con parametros enviados, respuesta SAT, estatus, paquetes y logs basicos.
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
- Carpeta local por defecto: `~/Archivo-Fiscal/paquetes/{rfc}/{yyyy-mm}/{solicitud_id}/`. Desde T019 la app migra al arrancar la carpeta anterior `~/SAT-CFDI-Downloader/paquetes` (renombrandola, sin copiar ni borrar ZIP); ver ADR 0004.
- En la ruta local, `{yyyy-mm}` corresponde al mes de la fecha inicial solicitada y `{solicitud_id}` corresponde al identificador SAT de la solicitud.
- Retencion local: el MVP no borra paquetes ZIP automaticamente.

## 5. Fuera de alcance actual

Queda fuera del MVP:

- Backend remoto. Un respaldo cifrado a una carpeta elegida por el usuario (que su proveedor de nube sincroniza) no es un backend propio y no contradice este punto (T025, ADR 0021).
- Multiusuario.
- Roles y permisos.
- Portal para terceros.
- Dashboards contables.
- Conciliacion.
- Validacion de vigencia/cancelacion de CFDI.
- Extraccion, parseo o indexacion de XML.
- Consulta local de CFDI por campos internos del XML.
- Solicitudes de metadata como flujo alterno.
- Exportaciones avanzadas (resumenes CSV o XLSX, formatos contables, plantillas, filtros por campos del XML o envio a otros sistemas). La exportacion simple a un ZIP local de RF-022 es una ampliacion aceptada (T026 D6) y no las incluye.
- Integraciones con sistemas contables.
- Timbrado o cancelacion de CFDI.
- Procesamiento fiscal avanzado.
- Servicio remoto o daemon multiusuario independiente de la sesion local.

## 6. Flujo principal

1. El usuario registra o selecciona un perfil SAT y registra su e.firma.
2. En Facturas elige el mes (por omision, el anterior al actual) y ve sus facturas emitidas y recibidas y el estado del mes.
3. Si faltan facturas, pulsa la accion del mes ("Descargar el mes", "Actualizar mes" o "Completar mes"). La aplicacion crea internamente una solicitud SAT por direccion con el tramo que falta (T023 D2, D7, D12, D17); no hay filtros SAT en este recorrido.
4. La aplicacion envia las solicitudes al SAT y guarda localmente su metadata.
5. El worker local consulta periodicamente al SAT y descarga los archivos del SAT disponibles.
6. La franja del mes muestra "Preparando" mientras hay trabajo y, al terminar, el mes pasa a Disponibles. Una notificacion avisa por direccion (seccion 7.6).
7. Historial muestra cada descarga por perfil y mes. Si algo requiere atencion, "Ver detalles tecnicos" abre Diagnostico, donde se revisan estatus, metadata, paquetes y logs de cada solicitud.

Cuando el inicio automatico este habilitado, la aplicacion debe iniciar con la sesion de macOS, mostrar solo el icono de menu bar y continuar el monitoreo sin abrir automaticamente la ventana principal.

Al cerrar la ventana principal, la aplicacion debe permanecer activa en el menu bar de macOS. Para detener el worker y cerrar el proceso, el usuario debe ejecutar una accion explicita de salida.

## 7. Pantallas del MVP

### 7.1 Perfiles SAT

Pantalla para administrar los RFC que el usuario usara para operar ante el SAT.

Debe permitir:

- Crear perfil SAT.
- Editar nombre descriptivo del perfil.
- Registrar certificado, llave privada y contrasena de e.firma.
- Ver si el perfil esta listo para descargar facturas ("Disponible para descargas").

### 7.2 Diagnostico: solicitudes

Vista tecnica fuera del recorrido normal (Ir > Diagnostico; T023 D10). La pantalla principal es Facturas (seccion 7.7). Conserva el vocabulario tecnico (solicitudes, paquetes, verificacion).

Debe mostrar una lista de solicitudes masivas creadas. Vacia, muestra "No hay solicitudes" con "Ver facturas"; la guia de primer uso vive solo en Facturas (T024.1 D11).

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

- Actualizar estatus.
- Abrir detalle.
- Descargar paquetes disponibles.
- Reintentar descarga fallida.
- Eliminar solicitud local.

La accion de descargar o reintentar descarga solo aplica cuando existen paquetes pendientes, fallidos o no descargados por pausa del worker.

### 7.3 Nueva solicitud (retirada)

No hay formulario de nueva solicitud: salio de la navegacion en T023.5 (D10) y se elimino en T024.1 (D11). Las descargas se inician desde Facturas por perfil y mes, sin filtros SAT (T023 D6); la descarga guiada crea una solicitud por direccion y tramo.

El mapeo a las operaciones SAT `SolicitaDescargaEmitidos` y `SolicitaDescargaRecibidos` queda definido en `docs/adrs/0013-sat-request-operations-v15.md`.

Antes de enviar una solicitud al SAT, la aplicacion valida localmente que no exista otra solicitud activa/no eliminada para el mismo perfil SAT con los mismos parametros relevantes. La descarga guiada reutiliza la existente o pide confirmacion por direccion (T023 D12), sin llamar al SAT.

Para el MVP, `EstadoComprobante` no se expone en UI. La aplicacion lo fija internamente en `Vigente` para solicitudes CFDI/XML.

### 7.4 Detalle de solicitud

Vista de Diagnostico para revisar una solicitud especifica. Se abre desde la lista de Diagnostico, desde Historial, desde "Ver detalles tecnicos" de un mes que requiere atencion y desde las notificaciones de solicitudes previas o con filtros.

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

Si una solicitud queda vencida sin haber descargado todos sus paquetes, el detalle debe mostrar un aviso visible. La aplicacion no debe recrear solicitudes por si sola; el usuario puede volver a descargar el mes desde Facturas (Descargar o Completar mes), que crea las solicitudes necesarias.

Si un paquete esta marcado como descargado pero el ZIP ya no existe en la ruta local, el detalle debe mostrarlo como archivo no encontrado sin cambiar automaticamente el estado persistido del paquete.

### 7.5 Icono de menu bar

La aplicacion debe mostrar un icono de acceso rapido en el menu bar de macOS.

Debe permitir:

- Ver el estado del monitoreo: "Monitoreo activo", "Monitoreo en pausa" o "Trabajando: pidiendo facturas al SAT…", "Trabajando: consultando al SAT…" o "Trabajando: descargando facturas…" ("Monitoreo" conserva su nombre).
- Abrir la ventana principal ("Mostrar ventana") y ir a Facturas ("Ver facturas").
- Ver cuantas descargas siguen pendientes ("Pendientes: N") y, en el icono, si algo requiere atencion ("Archivo Fiscal: trabajando", "en pausa" o "requiere atención").
- Pausar o reanudar el monitoreo.
- Configurar el inicio automatico y las notificaciones (permiso y notificacion de prueba).
- Salir de la aplicacion.

El menu no tiene "Abrir carpeta de paquetes" (T024.1 D5); esa accion sigue en Diagnostico.

Si el usuario pausa el monitoreo desde el menu bar, la pausa debe persistir aunque cierre y vuelva a abrir la aplicacion.

### 7.6 Notificaciones del sistema

La aplicacion emite notificaciones nativas de macOS segun la matriz de T024.1 (D4). Hay a lo mas una por direccion de cada descarga: un mes guiado con archivos en ambas direcciones produce exactamente dos. Nunca hay notificacion por paquete.

| Evento | Titulo |
| --- | --- |
| Descarga completa | Facturas descargadas |
| SAT termino con archivos | (no se notifica; avisa "Facturas descargadas") |
| SAT termino sin archivos o sin informacion (5004) | Sin facturas en el periodo |
| Error del SAT | No se pudieron descargar las facturas |
| Rechazada (salvo 5004) | El SAT no aceptó preparar las facturas |
| Vencida | Facturas no descargadas |

El cuerpo tiene dos lineas: el resultado con la direccion ("emitidas" o "recibidas") y "{periodo} · RFC ***XYZ" (RFC enmascarado: solo los 3 ultimos caracteres). Sin Ids, rutas, Mensaje SAT ni las palabras solicitud o paquete. Las notificaciones de e.firma (no disponible, por vencer) no cambian salvo vocabulario.

Al pulsarla, una notificacion de descarga guiada abre Facturas en su perfil y mes; la de una solicitud previa o con filtros abre su detalle (T023.5). Las nuevas no llevan la accion "Mostrar en Finder"; las ya entregadas con esa accion siguen abriendo su destino y su carpeta. Reintentar o reiniciar no duplica una notificacion (dedupe por solicitud y tipo).

### 7.7 Facturas

Pantalla principal (T023.4). Muestra, por perfil y mes natural, las facturas emitidas y recibidas que ya estan en el equipo (solo la contraparte) y una franja con el estado del mes y una sola accion: descargar, actualizar o completar el mes, reintentar, registrar la e.firma o "Ver detalles tecnicos" cuando requiere atencion. Sin perfiles muestra la guia de primer uso. Desde aqui se exporta (RF-022) y se abre el PDF de una factura.

### 7.8 Historial

Lista las descargas guiadas por perfil y mes con su resultado (T023.5). Lleva al mes en Facturas o a Diagnostico. Las solicitudes previas a T023 aparecen como "Descarga anterior", solo con diagnostico.

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

El worker no debe crear solicitudes nuevas por decision propia. Solo debe continuar el monitoreo y descarga de solicitudes creadas por una accion del usuario (la descarga guiada de Facturas; T023 D12). Las solicitudes y paquetes que maneja son mecanismo interno: el usuario ve su efecto en Facturas e Historial y su detalle en Diagnostico.

El worker debe iniciar automaticamente al iniciar la sesion de macOS cuando el usuario haya habilitado esa preferencia. Este comportamiento pertenece a la sesion local del usuario, no a un servicio remoto ni a un proceso compartido entre usuarios.

El intervalo inicial de verificacion es de 10 minutos por solicitud pendiente. Si una solicitud no cambia despues de tres verificaciones consecutivas, el intervalo para esa solicitud sube a 30 minutos.

Si el monitoreo esta pausado, el worker no debe consultar ni descargar hasta que el usuario lo reanude. La pausa debe guardarse localmente y sobrevivir al reinicio de la aplicacion.

Si el monitoreo esta pausado, las acciones manuales que impliquen verificar estatus o descargar paquetes deben quedar pendientes hasta que el usuario reanude el monitoreo.

Para mantener simple el MVP, las acciones manuales pendientes se guardan como dos intenciones idempotentes en `SolicitudMasiva`: verificacion pendiente y descarga pendiente. No existe una entidad separada para cola de acciones.

## 9. Estados

La aplicacion debe mostrar estados simples y entendibles. En el recorrido normal la UI muestra el estado del mes (Sin descargar, Preparando, Disponibles, Disponibles parcialmente o Requiere atencion; T023 D8, D11). Los estados de solicitud y paquete de esta seccion son internos y solo se ven en Diagnostico.

El modelo persiste por separado el ciclo local de envio y el estado devuelto por SAT; la UI puede presentar un resumen derivado.

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

Una solicitud en `Envio fallido` no tiene `IdSolicitud` SAT y es terminal en el MVP. Una solicitud en `Envio incierto` tampoco tiene `IdSolicitud`; la aplicacion no debe reenviarla porque no sabe si SAT alcanzo a registrar la solicitud. El usuario puede volver a descargar el mes desde Facturas (Descargar o Completar mes), que crea una solicitud nueva.

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

Cada `PerfilSat` puede existir sin credencial y tiene como maximo una credencial vigente, reemplazable cuando el usuario actualiza e.firma. `ConfiguracionApp` se maneja como un registro unico local.

Un perfil inactivo no puede usarse para crear solicitudes nuevas, pero no detiene el worker sobre solicitudes existentes, no invalida su credencial y no libera el RFC. Solo la eliminacion logica del perfil libera el RFC para recrearlo.

El MVP permite eliminar perfiles solo cuando no tengan solicitudes no eliminadas en curso. La eliminacion conserva historial local y elimina la credencial vigente y sus secretos desde la capa de aplicacion.

Metadata minima de `SolicitudMasiva`:

- Perfil SAT.
- Operacion SAT usada.
- RFC solicitante.
- Tipo: emitidos o recibidos.
- Fechas solicitadas.
- Filtros enviados.

La deduplicacion de solicitudes equivalentes usa RFC solicitante y filtros visibles para SAT, no la identidad local del perfil. Si un perfil se elimina y se recrea con el mismo RFC, las solicitudes historicas o en curso del perfil anterior siguen contando para las reglas de duplicados.
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
~/Archivo-Fiscal/paquetes/{rfc}/{yyyy-mm}/{solicitud_id}/
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

Antes de marcar el perfil como listo para descargar facturas, la aplicacion debe validar que la contrasena abre la llave privada, que certificado y llave corresponden, que el RFC del certificado corresponde al perfil y que la e.firma esta vigente.

### RF-003 Crear solicitud masiva

La aplicacion debe crear solicitudes de descarga masiva usando un perfil SAT y parametros soportados por el SAT. En el recorrido normal las crea la descarga guiada de Facturas: una por direccion y tramo del mes (T023 D2, D12); no hay formulario de nueva solicitud ni filtros SAT en la UI (T024.1 D11).

Antes de llamar al SAT, la aplicacion debe rechazar solicitudes locales duplicadas con los mismos parametros relevantes. Esta defensa evita gastar cupos o topar errores SAT por solicitudes duplicadas.

La comparacion anti-duplicados debe usar la clave normalizada definida en `docs/design/operational-rules.md`. Las solicitudes en curso bloquean duplicados; las solicitudes terminales o eliminadas no bloquean automaticamente, pero deben mostrarse como advertencia antes de reenviar.

### RF-004 Guardar solicitud localmente

La aplicacion debe guardar la metadata de cada solicitud creada, incluyendo parametros, identificador SAT, codigos, mensajes y fechas relevantes.

### RF-005 Listar solicitudes

La aplicacion debe mostrar en Diagnostico una lista de solicitudes masivas creadas con su estatus actual. El recorrido normal muestra en su lugar el Historial de descargas por perfil y mes.

### RF-006 Consultar estatus

La aplicacion debe consultar el estatus de una solicitud ante el SAT y actualizar la base local.

### RF-007 Monitorear en segundo plano

La aplicacion debe monitorear solicitudes pendientes mediante un worker local mientras el proceso este ejecutandose, aunque la ventana principal no este visible.

### RF-008 Descargar paquetes

La aplicacion debe descargar paquetes ZIP disponibles para solicitudes terminadas.

La aplicacion debe escribir cada ZIP primero a un archivo temporal y marcarlo como descargado solo despues de renombrarlo a la ruta final.

### RF-009 Ver detalle

La aplicacion debe permitir abrir el detalle de una solicitud desde la lista de Diagnostico, desde Historial y desde "Ver detalles tecnicos" de un mes que requiere atencion.

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

La aplicacion debe mostrar un icono en el menu bar de macOS para acceso rapido al estado del worker y acciones principales (seccion 7.5). El menu no ofrece "Abrir carpeta de paquetes"; los estados usan el vocabulario del recorrido normal (T024.1 D5).

### RF-015 Ejecutar con ventana oculta

La aplicacion debe poder seguir monitoreando y descargando solicitudes aunque la ventana principal este cerrada u oculta, siempre que el proceso local siga ejecutandose.

### RF-016 Notificaciones del sistema operativo

La aplicacion debe emitir notificaciones nativas de macOS segun la matriz de la seccion 7.6 (T024.1 D4): "Facturas descargadas", "Sin facturas en el periodo", "No se pudieron descargar las facturas", "El SAT no aceptó preparar las facturas" y "Facturas no descargadas", con direccion, periodo y RFC enmascarado en el cuerpo. "Terminada" con archivos no notifica.

### RF-017 Comportamiento al cerrar ventana

La aplicacion debe ocultarse al menu bar de macOS cuando el usuario cierre la ventana principal. Salir de la aplicacion requiere una accion explicita.

Al salir de la aplicacion, se guarda estado de cierre a nivel aplicacion. No se generan logs de solicitud salvo que exista una verificacion o descarga en curso que deba marcarse como interrumpida.

### RF-018 Acciones manuales sobre solicitud

La aplicacion debe permitir ejecutar acciones manuales sobre una solicitud desde Diagnostico: verificar ahora, reintentar descarga y eliminar solicitud local. En el recorrido normal, la accion del mes en Facturas reintenta o completa la descarga.

Reintentar descarga solo aplica a paquetes pendientes o fallidos. Si el monitoreo esta pausado, la accion queda pendiente hasta reanudar.

Eliminar solicitud local no modifica SAT ni borra paquetes ZIP automaticamente en el MVP. La eliminacion local es virtual: marca como eliminados `SolicitudMasiva`, `PaqueteSolicitud` y `LogSolicitud`.

### RF-019 Retencion local

La aplicacion no debe borrar paquetes ZIP automaticamente. La limpieza de archivos queda bajo control del usuario.

### RF-020 Restaurar un respaldo

El usuario puede restaurar un respaldo cifrado (T025, ADR 0021 y ADR 0022) en la misma u otra Mac:

- La app verifica todo el archivo antes de tocar nada (D5). Rechaza un respaldo de una version de la base mas nueva que la app y migra uno anterior (D13).
- La restauracion sustituye los datos locales; no los fusiona. La sustitucion ocurre en el siguiente arranque, despues de que la app se detiene y se vuelve a abrir.
- Los datos anteriores se conservan en carpetas `pre-restauracion-<fecha>`, en la carpeta de datos y junto a la carpeta de paquetes. Quedan hasta que el usuario los descarte.
- La e.firma no se restaura (D3): los perfiles restaurados requieren registrarla de nuevo y no se hacen llamadas SAT hasta entonces.
- Mientras existan datos previos sin descartar, la e.firma anterior se conserva en el almacen seguro del equipo (D22).
- La configuracion del respaldo de este equipo (seccion `respaldo`, id de instalacion y registro de copias publicadas) no se sustituye (D14, D20).
- Antes de confirmar, la pantalla Respaldo muestra la fecha y el identificador autenticados del respaldo y su contenido; al confirmar, la app se cierra y deberia abrirse sola (si no, el usuario la abre). Al arrancar se informa si la restauracion se aplico, se descarto o se revirtio, y un aviso persistente ofrece "Descartar datos anteriores" mientras existan datos previos (T025.6).

### RF-021 Respaldo cifrado

El usuario respalda sus datos desde la pantalla Respaldo (Ir > Respaldo, ⌘4; T025, ADR 0021, T025.6):

- Elige una carpeta con el dialogo del sistema (por ejemplo, su carpeta de iCloud Drive o Dropbox); la app escribe ahi una copia cifrada y el proveedor la sincroniza. La UI dice "copiado a la carpeta", nunca "subido".
- Crea una contrasena de al menos 12 caracteres (D23). Al crearla, la app muestra una clave de recuperacion que se puede copiar (solo a este equipo, se borra del portapapeles a los 90 s) o imprimir, y que no se vuelve a mostrar despues de confirmar que se guardo. Cambiar la contrasena conserva la clave (D16).
- "Respaldar ahora" y respaldo diario opcional (D4) con progreso, resultado y advertencias (archivos del SAT faltantes, copias que hay que borrar a mano, resultado no guardado). Se conservan las 5 copias propias mas recientes (D6, D17).
- La e.firma, los logs y los temporales nunca se respaldan (D3, D7).

### RF-022 Exportar facturas

El usuario exporta a un solo archivo ZIP, en la carpeta que elige, las facturas que ya estan en el equipo (T026, ADR 0023):

- Alcance: el mes que esta viendo en Facturas ("Exportar este mes"), un rango de meses o facturas elegidas en un modo de seleccion con casilla por fila (D3). El rango rechaza meses futuros, rangos invertidos y meses fuera del horizonte navegable de Facturas, y acepta el mismo mes en ambos extremos. La seleccion se conserva al cambiar de pestana, filtro o recarga y se vacia al cambiar de perfil (D14).
- Para mes o rango se elige Emitidas, Recibidas o Ambas (por omision, la pestana actual); con facturas elegidas no hay selector y se exporta exactamente lo elegido (D8).
- Contenido: el XML original de cada CFDI en `emitidas/AAAA-MM/<UUID>.xml` o `recibidas/AAAA-MM/<UUID>.xml` y, opcionalmente, su PDF generado localmente junto al XML (D2, D7). Sin resumen CSV o XLSX.
- Antes de exportar, el dialogo muestra siempre el aviso fijo de D10 (el archivo no esta cifrado y puede copiarse fuera del equipo) y cuantas facturas se exportaran; con 0, "Exportar" queda deshabilitado con "No hay facturas para exportar" y no se crea un ZIP vacio (D5, D10, D16).
- Una factura cuyo ZIP falta, esta en descarga, es ilegible o cambio desde que se indexo no se exporta; el resultado muestra por separado las exportadas, las exportadas sin PDF y las omitidas, y el ZIP lleva `omitidas.txt` con UUID y motivo, mas una linea por mes con descargas pendientes o cobertura incompleta (D4, D9, D17).
- El nombre sugerido no lleva RFC y se puede editar; la app nunca sobrescribe: si el nombre ya existe pide otro. Cancelar o fallar no deja archivo publicado ni `.partial` (D12, D15).
- Progreso por fase con conteo, cancelar y resultado en la ventana. Logs y notificaciones solo llevan fases, conteos y codigos; el nombre del archivo creado solo se muestra en la ventana (D20).
- No llama al SAT ni modifica estados persistidos ni los ZIP de paquetes; corre en su propio hilo, una a la vez (D11).

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

Dado que el usuario captura los datos de un perfil SAT, cuando guarda el perfil, entonces la aplicacion lo muestra disponible para descargas.

### CA-002 Descargar el mes

Dado un perfil SAT valido y un mes sin descargar, cuando el usuario elige Descargar en Facturas, entonces la aplicacion crea las solicitudes al SAT necesarias por direccion (mecanismo interno) y guarda su metadata localmente.

Dado que ya existe una solicitud activa equivalente, cuando se intenta crearla otra vez, entonces la aplicacion reutiliza la existente y no llama al SAT.

### CA-003 Ver facturas por mes

Dado un perfil con meses descargados, cuando el usuario abre la pantalla principal (Facturas), entonces ve el mes con su estado de disponibilidad y sus facturas. La lista de solicitudes con su estatus queda en Diagnostico.

### CA-004 Monitorear solicitud

Dado que existe una solicitud pendiente, cuando el worker local corre, entonces consulta el SAT y actualiza su estatus en la base local.

### CA-005 Ver detalle

Dado que existe una solicitud en la lista, cuando el usuario abre su detalle, entonces ve parametros, estatus, codigos SAT, mensajes, paquetes y logs basicos.

### CA-006 Descargar paquetes (mecanismo interno)

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

Dado que la descarga de un mes concluye, no tiene facturas, falla, es rechazada o vence en una direccion, cuando la aplicacion procesa el cambio, entonces emite una notificacion nativa de macOS por direccion segun la matriz de §7.6. Una solicitud terminada con archivos pendientes de descarga no notifica.

### CA-013 Acciones manuales

Dado que el usuario abre una solicitud, cuando ejecuta una accion manual disponible, entonces la aplicacion verifica ahora, reintenta una descarga fallida o marca la solicitud local como eliminada segun corresponda.

Si el monitoreo esta pausado, verificar ahora y reintentar descarga quedan pendientes hasta reanudar.

### CA-014 Pausa persistente

Dado que el usuario pausa el worker desde el menu bar, cuando cierra y vuelve a abrir la aplicacion, entonces el monitoreo permanece pausado hasta que el usuario lo reanude.

### CA-015 Retencion de ZIP

Dado que existen paquetes ZIP descargados, cuando la aplicacion monitorea, descarga o elimina una solicitud local, entonces no borra esos ZIPs automaticamente.

## 16. Preguntas abiertas

- No bloqueante: que nivel de detalle debe tener el log visible en la pantalla de detalle?
