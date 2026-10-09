# SAT CFDI Downloader: manual de usuario

SAT CFDI Downloader es una aplicación de escritorio para macOS. Sirve para
solicitar al SAT la descarga masiva de CFDI de los contribuyentes que
administras, darle seguimiento a cada solicitud y guardar en tu equipo los
paquetes ZIP que entrega el SAT. Todo se guarda localmente: perfiles,
solicitudes, historial y paquetes. La e.firma se guarda cifrada y su contraseña
queda en el llavero de macOS.

Las imágenes de este manual usan solo datos ficticios: RFC genéricos del SAT
(`XAXX010101000`, `XEXX010101000`), identificadores inventados, archivos
`demo.cer` y `demo.key` y rutas bajo `/Users/usuario/...`. Las imágenes
marcadas como **Ilustración** son dibujos simplificados de elementos de macOS
(menu bar, notificaciones, Finder y selector de archivos). Muestran los mismos
textos que la app, aunque su aspecto puede variar respecto de macOS.

La app muestra las fechas en formato legible, por ejemplo "3 sep 2026" o
"1 oct 2026, 10:35". Las fechas del formulario de nueva solicitud se escriben
en formato AAAA-MM-DD.

Contenido:

1. [Perfiles SAT y e.firma](#1-perfiles-sat-y-efirma)
2. [Crear una nueva solicitud y resolver duplicados](#2-crear-una-nueva-solicitud-y-resolver-duplicados)
3. [Envío y monitoreo](#3-envío-y-monitoreo)
4. [Seguimiento en la lista de solicitudes](#4-seguimiento-en-la-lista-de-solicitudes)
5. [Detalle: resumen, paquetes, datos e historial](#5-detalle-resumen-paquetes-datos-e-historial)
6. [Paquetes en Finder](#6-paquetes-en-finder)
7. [Menu bar](#7-menu-bar)
8. [Notificaciones de macOS](#8-notificaciones-de-macos)
9. [Mensajes frecuentes y cómo resolverlos](#9-mensajes-frecuentes-y-cómo-resolverlos)

## 1. Perfiles SAT y e.firma

Un perfil SAT representa a un contribuyente: su RFC y un nombre descriptivo.
Para que un perfil pueda usarse en solicitudes debe estar activo y tener su
e.firma registrada y vigente. Abre la pantalla con el botón **Perfiles SAT**,
que está en la lista de solicitudes y en Nueva solicitud. **‹ Solicitudes** (o
la tecla Escape) regresa a la lista.

La pantalla tiene dos paneles: a la izquierda la lista de perfiles y a la
derecha el perfil seleccionado. Al abrirla queda seleccionado el primer perfil.
Puedes cambiar el ancho de los paneles arrastrando la línea que los separa.

![Pantalla Perfiles SAT con dos paneles: a la izquierda XAXX010101000 con la etiqueta e.firma lista, Disponible para solicitudes y Vigente hasta 2029-01-01, y XEXX010101000 con Sin e.firma y No disponible para solicitudes; a la derecha, el grupo Perfil con el RFC de solo lectura y el grupo e.firma con Lista para solicitudes y el botón Reemplazar e.firma…](img/01-perfiles.png)

### Crear o editar un perfil

1. Pulsa **Nuevo perfil**.
2. Escribe el **RFC** y un **Nombre descriptivo**, y pulsa **Guardar** en el
   pie del grupo **Perfil**. **Descartar** abandona los cambios. Los dos botones
   se habilitan cuando hay cambios.
3. Selecciona un perfil de la lista para editarlo. En edición solo cambia el
   nombre: el RFC aparece con un candado y la ayuda "El RFC identifica al perfil
   y no se puede cambiar."

Los avisos del formulario aparecen bajo su campo: "Ya existe un perfil con ese
RFC." (la app conserva lo que escribiste), "El RFC no es válido." e "Indica un
nombre para el perfil."

Si todavía no hay perfiles, el panel derecho dice "Aún no hay perfiles SAT" y
ofrece **Nuevo perfil**.

### Estado de la e.firma

Cada perfil de la lista muestra su RFC, su nombre, una etiqueta con el estado
de la e.firma y una línea de disponibilidad. La etiqueta usa color e icono, pero
el estado siempre está escrito.

| Etiqueta | Significado |
| --- | --- |
| Verificando | La app está comprobando la e.firma guardada. |
| Sin e.firma | El perfil no tiene e.firma registrada. |
| e.firma lista | La e.firma está registrada y vigente. |
| e.firma vencida | La e.firma ya no es vigente; hay que reemplazarla. |
| e.firma aún no vigente | La e.firma todavía no entra en vigor. |
| e.firma incompleta en el llavero / e.firma dañada en el llavero | No se pudo leer la e.firma guardada; vuelve a registrarla. |
| Estado no disponible | No se pudo consultar el estado (por ejemplo, con el llavero bloqueado). Usa **Reintentar** en la fila, **Reintentar estado** en el grupo e.firma o la tecla R sobre la fila. |

La línea de disponibilidad dice "Disponible para solicitudes", "No disponible
para solicitudes" o, si el perfil está inactivo, "Inactivo: no disponible para
solicitudes". Si la e.firma tiene vigencia conocida, también aparece "Vigente
hasta AAAA-MM-DD".

En el panel derecho, el grupo **e.firma** repite la etiqueta y explica el
estado: "Lista para solicitudes · Vigente hasta 2029-01-01" o "Este perfil no
tiene e.firma registrada.", seguido de "La e.firma se guarda cifrada y su
contraseña queda en el llavero de macOS." o "Regístrala para poder usar este
perfil en solicitudes."

### Registrar la e.firma

1. Selecciona el perfil. Si tienes cambios sin guardar, primero guarda el
   perfil ("Guarda el perfil antes de gestionar su e.firma.").
2. En el grupo **e.firma**, pulsa **Registrar e.firma…**.
3. El diálogo **Registrar e.firma** muestra el RFC y el nombre del perfil, y la
   instrucción "Elige el certificado (.cer), la llave privada (.key) y escribe
   la contraseña de la llave." Pulsa **Elegir…** junto a **Certificado (.cer)**
   y junto a **Llave privada (.key)**. Cada botón abre el selector de archivos
   de macOS. En el campo solo se muestra el nombre del archivo, nunca su ruta;
   sin archivo dice "Ningún archivo".
4. Escribe la **Contraseña** ("Se guarda en el llavero de macOS.").
5. **Registrar** se habilita solo cuando elegiste el certificado, la llave y
   escribiste la contraseña. Mientras tanto, el pie del diálogo dice qué falta:
   "Elige el certificado (.cer).", "Elige la llave privada (.key)." o "Escribe
   la contraseña de la llave privada." Pulsa **Registrar** (o Enter en el campo
   de contraseña).
6. Mientras se valida aparece "Validando e.firma…" y los controles se
   deshabilitan. Al terminar aparece "e.firma registrada." con "`<RFC>` ya está
   disponible para solicitudes." y el botón **Cerrar**.

![Diálogo Registrar e.firma de XEXX010101000 · Proveedor de ejemplo con demo.cer y demo.key elegidos, el campo Contraseña vacío, el aviso Escribe la contraseña de la llave privada y el botón Registrar deshabilitado junto a Cancelar](img/02-efirma.png)

![Ilustración del selector de archivos de macOS en la carpeta efirma, con demo.cer seleccionado y demo.key en la lista](img/03-selector-archivos.png)

Si la validación falla, el mensaje aparece bajo el campo que lo causó (por
ejemplo, la contraseña) y el foco va a ese campo. Por seguridad, la app olvida
la selección: los campos vuelven a "Ningún archivo", la contraseña se vacía y
aparece "Vuelve a elegir los archivos y escribe la contraseña."

El campo de contraseña se vacía al enviar, al cancelar, al cerrar el diálogo,
al cambiar de perfil, al salir de la pantalla y al ocultar la ventana.

### Reemplazar la e.firma

Si el perfil ya tiene e.firma, el botón dice **Reemplazar e.firma…**; es el
botón principal cuando la e.firma está vencida, aún no es vigente o no se pudo
leer. Antes de capturar los archivos, la app pide confirmación con "¿Reemplazar
la e.firma de `<RFC>`?" y "La e.firma registrada se reemplazará solo si la
nueva se valida. Si falla, la actual sigue registrada sin cambios." Pulsa
**Continuar** y sigue los mismos pasos que en el registro; el diálogo se llama
**Reemplazar e.firma** y el botón final dice **Reemplazar**. Si el reemplazo
falla, el mensaje termina con "La e.firma anterior sigue registrada sin
cambios." y la lista vuelve a mostrar el estado real de la e.firma anterior.

Los errores al registrar o reemplazar están en la [sección 9](#9-mensajes-frecuentes-y-cómo-resolverlos).

## 2. Crear una nueva solicitud y resolver duplicados

Pulsa **Nueva solicitud** en la lista o **Nueva solicitud…** en el menu bar.

![Formulario Nueva solicitud con el grupo Solicitud (Perfil SAT XAXX010101000 — Contribuyente de ejemplo, Tipo de descarga Emitidos o Recibidos, Periodo 2026-09-01 a 2026-09-30), el grupo Filtros opcionales (RFC contraparte, Tipo de comprobante Todos, Complemento) y el pie con Cancelar y Crear solicitud](img/04-nueva-solicitud.png)

El formulario tiene dos grupos:

1. **Solicitud**
   - **Perfil SAT**: elige el contribuyente. "Solo aparecen perfiles activos
     con la e.firma lista." Si no hay ninguno, la pantalla dice "No hay perfiles
     SAT listos para solicitudes" y "Un perfil necesita estar activo y tener su
     e.firma registrada y vigente.", y ofrece **Administrar perfiles SAT**.
   - **Tipo de descarga**: elige **Emitidos** o **Recibidos** en el selector. Al
     abrir el formulario está en Emitidos.
   - **Periodo**: fecha inicial "a" fecha final, en formato AAAA-MM-DD ("Formato
     AAAA-MM-DD. Incluye ambos días."). Al abrir el formulario se propone del
     primer día del mes actual a hoy.
2. **Filtros opcionales**: **RFC contraparte** ("Cualquiera" si lo dejas
   vacío), **Tipo de comprobante** (Todos, I - Ingreso, E - Egreso, T -
   Traslado, N - Nómina, P - Pago) y **Complemento**.

El pie de la pantalla queda siempre visible: "Al crearla, la app la envía al
SAT con la e.firma del perfil.", con **Cancelar** y **Crear solicitud**. Enter
en un campo de texto también crea la solicitud. **Cancelar**, **‹ Solicitudes**
o Escape vuelven a la lista sin crear nada.

Los avisos aparecen bajo el campo correspondiente: "Selecciona un perfil
SAT.", "Indica una fecha inicial válida (AAAA-MM-DD).", "Indica una fecha final
válida (AAAA-MM-DD).", "La fecha final debe ser igual o posterior a la fecha
inicial." y "El perfil SAT seleccionado ya no está disponible."

Al crearla, la solicitud se guarda en el equipo, se abre su detalle y la app
se encarga del envío al SAT (ver la [sección 3](#3-envío-y-monitoreo)).

### Solicitudes equivalentes

Antes de crear, la app compara los filtros con las solicitudes que ya tienes.

- **Bloqueado**: si ya hay una solicitud equivalente que impide crear otra,
  aparece sobre el formulario "No se puede crear: ya existe una solicitud
  equivalente." seguido del motivo, y el botón **Ver solicitud existente** abre
  su detalle. Motivos: "Hay una solicitud equivalente en curso.", "Hay una
  solicitud equivalente terminada con paquetes pendientes de descargar." y "Ya
  existe una solicitud equivalente terminada y descargada."
- **Requiere confirmación**: el diálogo "¿Crear otra solicitud con los mismos
  filtros?" muestra el motivo y "Puedes crear otra solicitud con los mismos
  filtros o cancelar." **Cancelar** es el botón por omisión (Enter o Escape) y
  no crea nada; **Crear de todos modos** crea la solicitud con los mismos
  filtros. Motivos: "Existe una solicitud equivalente terminada cuyos paquetes
  vencieron.", "Existe una solicitud equivalente terminada sin paquetes.",
  "Existe una solicitud equivalente con envío incierto: el SAT pudo haberla
  recibido.", "Existe una solicitud equivalente que no tuvo éxito." y "Existe
  una solicitud equivalente que eliminaste localmente."

![Diálogo ¿Crear otra solicitud con los mismos filtros? con el motivo Existe una solicitud equivalente terminada sin paquetes, el texto Puedes crear otra solicitud con los mismos filtros o cancelar, y los botones Cancelar (por omisión) y Crear de todos modos](img/05-duplicado.png)

## 3. Envío y monitoreo

Mientras la app está abierta, aunque la ventana esté oculta, un proceso local
hace el trabajo con el SAT en este orden:

1. **Envío**: al crear una solicitud, la app la envía al SAT con la e.firma del
   perfil. Mientras tanto la solicitud aparece como "Enviando".
2. **Verificación**: una vez enviada, consulta periódicamente su estado en el
   SAT (Aceptada por SAT, En proceso SAT, Terminada…).
3. **Descarga**: cuando el SAT termina y hay paquetes, los descarga y los
   guarda en la carpeta de paquetes (ver la [sección 6](#6-paquetes-en-finder)).

Pasos manuales en el resumen del detalle (solo aparece el que aplica):

- **Enviar**: aparece en solicitudes "Creada", por ejemplo si el envío no pudo
  iniciarse. Solo se habilita si la e.firma del perfil está lista; si no, se
  muestra el motivo (por ejemplo "Este perfil no tiene e.firma registrada.
  Regístrala para continuar."). Al pulsarlo se lee "Envío solicitado."
- **Verificar ahora**: aparece en solicitudes enviadas que aún no tienen un
  estado final en el SAT. Muestra "Verificación solicitada. Si el monitoreo
  está pausado, queda pendiente."
- **Reintentar descarga**: aparece si algún paquete está "Disponible" o con
  "Error de descarga". Muestra "Descarga solicitada. Si el monitoreo está
  pausado, queda pendiente."

El monitoreo se pausa y se reanuda desde el menu bar ([sección 7](#7-menu-bar)).
Con el monitoreo en pausa no se trabaja con el SAT: las acciones pedidas se
guardan y el menu bar muestra cuántas hay pendientes ("Pendientes: N").

Una solicitud que terminó en "Envío fallido" o "Envío incierto" no se reenvía.
Si necesitas repetirla, crea una nueva; la app te pedirá confirmar el
duplicado.

## 4. Seguimiento en la lista de solicitudes

La pantalla **Solicitudes** muestra junto al título cuántas solicitudes hay y
las ordena de la más reciente a la más antigua. La tabla tiene las columnas
**Estado**, **Contribuyente** (RFC y nombre del perfil), **Tipo**, **Periodo**,
**Paquetes** y **Creada**. En ventanas angostas se oculta la columna Creada.

![Lista Solicitudes 4 con columnas Estado, Contribuyente, Tipo, Periodo, Paquetes y Creada; cuatro filas de XAXX010101000 · Contribuyente de ejemplo en los estados Error SAT, Terminada con 3 paquetes, En proceso SAT y Creada, y los botones Abrir carpeta de paquetes, Perfiles SAT y Nueva solicitud](img/06-lista.png)

Acciones de la lista:

- Haz clic en una fila, o usa las flechas y Enter o Espacio, para abrir el
  detalle.
- **Abrir carpeta de paquetes** (abre en Finder la carpeta donde se guardan los
  paquetes), **Perfiles SAT** y **Nueva solicitud**.

Estados que puede mostrar la etiqueta:

| Estado | Significado |
| --- | --- |
| Creada | Guardada en el equipo; aún no se envía al SAT. |
| Enviando | Se está enviando al SAT. |
| Enviada | El SAT recibió la solicitud; aún no hay estado del SAT. |
| Envío fallido | El SAT rechazó el envío; no se registró en el SAT. |
| Envío incierto | No se sabe si el SAT registró la solicitud; no se reenvía automáticamente. |
| Aceptada por SAT | El SAT aceptó la solicitud y la está atendiendo. |
| En proceso SAT | El SAT está preparando los paquetes. |
| Terminada | El SAT terminó; los paquetes están listos o ya se descargaron. |
| Error SAT | El SAT reportó un error en la solicitud. |
| Rechazada por SAT | El SAT rechazó la solicitud. |
| Vencida | La solicitud venció en el SAT. |

Si no hay solicitudes, la lista dice "No hay solicitudes" y "Crea una solicitud
para descargar del SAT los CFDI emitidos o recibidos de un contribuyente.", con
el botón **Nueva solicitud**.

![Lista vacía con el texto No hay solicitudes, la explicación Crea una solicitud para descargar del SAT los CFDI emitidos o recibidos de un contribuyente y el botón Nueva solicitud](img/07-lista-vacia.png)

Si la lista no se puede leer, aparece "No se pudo cargar la lista de
solicitudes.", "Tus solicitudes siguen guardadas en este equipo. Intenta
cargarlas de nuevo." y el botón **Reintentar**. Si las notificaciones de macOS
están desactivadas, sobre la tabla aparece el aviso "Las notificaciones están
desactivadas." con "Actívalas en Ajustes del Sistema para recibir avisos cuando
una solicitud termine o falle."

## 5. Detalle: resumen, paquetes, datos e historial

El encabezado del detalle dice **Solicitud** con el RFC, el tipo y el periodo
(por ejemplo "XAXX010101000 · Recibidos · 3 sep 2026"). Tiene **‹ Solicitudes**
para volver, **Abrir carpeta de la solicitud** y, separado, **Eliminar…**.

Debajo, el **resumen** muestra un solo estado (el mismo de la lista) con un
titular y una descripción, por ejemplo "El SAT terminó la solicitud" y "1 de 3
paquetes descargados · 1 disponible · 1 vencido". También muestra
Contribuyente, Tipo · periodo, CFDI reportados y Última verificación, y la
acción principal que aplica: **Enviar**, **Verificar ahora** o **Reintentar
descarga** (ver la [sección 3](#3-envío-y-monitoreo)). Cuando todos los
paquetes están descargados, la acción es **Abrir carpeta de la solicitud**.

| Estado | Titular |
| --- | --- |
| Creada | La solicitud aún no se envía al SAT |
| Enviando | Enviando la solicitud al SAT… |
| Enviada | El SAT recibió la solicitud |
| Aceptada por SAT | El SAT aceptó la solicitud |
| En proceso SAT | El SAT está preparando los paquetes |
| Terminada | El SAT terminó la solicitud / Todos los paquetes están descargados / Hay un paquete con error de descarga / El SAT terminó sin paquetes |
| Envío fallido | El SAT rechazó el envío |
| Envío incierto | No se sabe si el SAT registró la solicitud |
| Error SAT | El SAT reportó un error en la solicitud |
| Rechazada por SAT | El SAT rechazó la solicitud |
| Vencida | La solicitud venció en el SAT |

Bajo el resumen hay tres pestañas:

- **Paquetes**: uno por paquete del SAT, con su estado de descarga, sus fechas
  y un mensaje cuando hace falta. Es la pestaña inicial si hay paquetes.
- **Datos**: secciones **Estado** (Resumen, Estado local, Estado SAT y Último
  error; si el SAT aún no responde dice "Sin respuesta del SAT"), **SAT** (Id
  solicitud SAT, Código de solicitud SAT, Código de verificación SAT, CFDI
  reportados, Enviada y Última verificación), **Solicitud** (Identificador
  local, Perfil, Tipo de descarga y Creada) y **Filtros** (Periodo, RFC
  contraparte, Tipo de comprobante y Complemento).
- **Historial**: los eventos de la solicitud con fecha, descripción y origen
  (Usuario, Automático o Recuperación).

![Detalle de una solicitud Terminada: resumen con El SAT terminó la solicitud, 1 de 3 paquetes descargados · 1 disponible · 1 vencido, 12 CFDI reportados y el botón Reintentar descarga; pestaña Paquetes con un paquete Descargado con Archivo local presente y Mostrar en Finder, uno Disponible con La app lo descargará automáticamente y uno Vencido con El paquete ya no existe en el SAT (vencido)](img/08-detalle-terminada.png)

Estados de un paquete:

| Estado | Significado |
| --- | --- |
| Disponible | El SAT lo tiene listo: "La app lo descargará automáticamente." |
| Descargando | Se está descargando. |
| Descargado | Ya está guardado en el equipo. |
| Error de descarga | No se pudo descargar ("No se pudo descargar el paquete. Puedes reintentar." o "El paquete alcanzó el máximo de descargas permitidas."; en este último caso no aparece **Reintentar descarga**). |
| Vencido | "El paquete ya no existe en el SAT (vencido). Crea una solicitud nueva para el mismo periodo." |

En los paquetes descargados, la app comprueba el archivo local y muestra
"Archivo local presente", "Archivo local no encontrado: el ZIP se movió o se
borró fuera de la app." o "No se pudo comprobar el archivo local". **Mostrar en
Finder** solo se habilita si el archivo está presente.

![Detalle con incidencias: resumen con Hay un paquete con error de descarga y 1 de 2 paquetes descargados · 1 con error; un paquete en Error de descarga con No se pudo descargar el paquete. Puedes reintentar, y un paquete Descargado con Archivo local no encontrado y Mostrar en Finder deshabilitado](img/09-detalle-incidencia.png)

**Eliminar…** pide confirmación: "¿Eliminar esta solicitud?", "La solicitud se
eliminará de esta aplicación. No se modifica nada en el SAT." y "Los ZIP ya
descargados se quedan en su carpeta." **Cancelar** es el botón por omisión.
Después de eliminarla vuelves a la lista. Si abres una solicitud que ya no
existe, el detalle dice "Solicitud no encontrada" y "La solicitud no existe o
fue eliminada."

## 6. Paquetes en Finder

Los paquetes se guardan como ZIP en la carpeta `SAT-CFDI-Downloader/paquetes`
de tu carpeta de usuario, organizados por RFC, mes de la solicitud e
identificador local. La app no abre ni modifica los ZIP.

Hay tres formas de llegar a ellos:

- **Mostrar en Finder**, en un paquete Descargado de la pestaña Paquetes, abre
  Finder con el ZIP seleccionado.
- **Abrir carpeta de la solicitud**, en el detalle, abre la carpeta con los
  paquetes de esa solicitud.
- **Abrir carpeta de paquetes**, en la lista o en el menu bar, abre la carpeta
  principal de paquetes.

![Ilustración de Finder en la carpeta de una solicitud dentro de paquetes, XAXX010101000 y 2026-09, con el archivo ZIP del paquete seleccionado](img/10-finder.png)

Si el destino no existe o Finder no responde, la app lo avisa con "Archivo
local no encontrado", "Carpeta de paquetes no encontrada" o "No se pudo abrir
Finder". Estas acciones no cambian ningún estado.

## 7. Menu bar

La app pone un icono en la barra de menús de macOS. Cerrar la ventana solo la
oculta: la app y el monitoreo siguen activos.

![Ilustración del menú de la app en la barra de menús con Mostrar ventana, Nueva solicitud…, Abrir carpeta de paquetes, Monitoreo activo, Pendientes: 1, Pausar monitoreo, Abrir al iniciar sesión marcado, Estado en macOS: habilitado, Notificaciones: permitidas, Enviar notificación de prueba y Salir de SAT CFDI Downloader](img/11-menu-bar.png)

| Opción | Para qué sirve |
| --- | --- |
| Mostrar ventana | Muestra y trae al frente la ventana principal. |
| Nueva solicitud… | Muestra la ventana en el formulario de nueva solicitud. |
| Abrir carpeta de paquetes | Abre en Finder la carpeta de paquetes. |
| Monitoreo activo / Monitoreo en pausa / Trabajando: enviando… / Trabajando: verificando… / Trabajando: descargando… | Línea informativa con lo que hace el monitoreo. |
| Pendientes: N | Acciones guardadas durante una pausa; solo aparece si hay alguna. |
| Pausar monitoreo / Reanudar monitoreo | Detiene o reanuda el trabajo con el SAT. La pausa se recuerda al volver a abrir la app. |
| Abrir al iniciar sesión | Abre la app al iniciar sesión en macOS, sin mostrar la ventana. |
| Estado en macOS: … | Lo que macOS reporta para ese inicio automático: habilitado, deshabilitado, pendiente de aprobación, rechazado o no disponible. Si está pendiente o rechazado aparece **Abrir ajustes de inicio de sesión…** |
| Notificaciones: … | Permiso de notificaciones: sin permiso solicitado, permitidas, deshabilitadas o no disponibles. Según el caso aparecen **Solicitar permiso de notificaciones**, **Enviar notificación de prueba** o **Abrir ajustes de notificaciones…** |
| Salir de SAT CFDI Downloader | Termina la app de forma ordenada: espera la operación en curso y cierra. |

## 8. Notificaciones de macOS

Con el permiso concedido, la app avisa con notificaciones de macOS de estos
cambios. Cada notificación tiene un título y un texto:

- **Solicitud terminada**: "El SAT terminó la solicitud con 3 paquetes."
- **Descarga completa**: "1 de 1 paquetes descargados."
- **Error en el SAT**: "El SAT reportó un error en la solicitud."
- **Solicitud rechazada**: "El SAT rechazó la solicitud."
- **Solicitud vencida**: "La solicitud venció en el SAT; los paquetes pueden ya
  no estar disponibles."
- **e.firma no disponible**: la e.firma de un perfil dejó de estar lista. El
  texto explica la causa, termina con "El monitoreo de ese perfil está en
  pausa." y en otra línea indica qué hacer: "Reemplázala en Perfiles SAT." o
  "Regístrala en Perfiles SAT."

Las notificaciones de solicitudes agregan una segunda línea con el tipo de
descarga, el periodo y el RFC abreviado a sus últimos tres caracteres, por
ejemplo "Recibidos · 3 sep 2026 · RFC \*\*\*000".

![Ilustración de una notificación de macOS con el título Descarga completa, el texto 1 de 1 paquetes descargados. y la segunda línea Recibidos · 3 sep 2026 · RFC ***000](img/12-notificacion.png)

Para comprobar las notificaciones usa **Enviar notificación de prueba** en el
menu bar. Si las desactivas en Ajustes del Sistema, la app sigue funcionando
igual; solo deja de avisar y la lista muestra un aviso.

## 9. Mensajes frecuentes y cómo resolverlos

### e.firma (al registrar o reemplazar)

Tras cualquiera de estos errores, vuelve a elegir los archivos y a escribir la
contraseña.

| Mensaje | Qué hacer |
| --- | --- |
| No se pudo leer el certificado o la llave. / El certificado o la llave no tienen un formato válido. | Elige otra vez el `.cer` o la `.key`; el foco va al archivo con problema. |
| La contraseña de la llave privada es incorrecta. | Escribe de nuevo la contraseña. |
| El certificado y la llave privada no corresponden. | Usa el `.cer` y la `.key` de la misma e.firma. |
| El RFC del certificado no coincide con el del perfil. | Usa la e.firma del contribuyente de ese perfil. |
| El certificado no es una e.firma admisible. | Usa el certificado de e.firma del contribuyente. |
| La e.firma está vencida. / La e.firma aún no es vigente. | Usa una e.firma vigente. |
| El llavero está bloqueado; intenta de nuevo tras desbloquear la sesión. / Se denegó el acceso al llavero. / La operación se canceló. | Desbloquea tu sesión o permite el acceso en el aviso de macOS e intenta de nuevo. |
| El perfil está inactivo; no se puede registrar su e.firma. | Solo los perfiles activos pueden registrar e.firma. |

### Envío y verificación

| Mensaje | Qué hacer |
| --- | --- |
| Este perfil no tiene e.firma registrada. Regístrala para continuar. | Registra la e.firma en Perfiles SAT y pulsa **Enviar**. |
| La e.firma de este perfil está vencida. Reemplázala para continuar. | Reemplaza la e.firma y pulsa **Enviar**. |
| No se pudo leer la e.firma guardada. Vuelve a importarla. | Vuelve a registrar la e.firma del perfil. |
| El llavero de macOS está bloqueado. Desbloquea tu sesión e intenta de nuevo. | Desbloquea la sesión e intenta de nuevo. |
| El SAT no aceptó la autenticación con esta e.firma. | Revisa que la e.firma sea la correcta y esté vigente. |
| No se pudo conectar con el SAT para autenticar. Intenta más tarde. / No se pudo conectar con el SAT; la solicitud no se envió. | Revisa tu conexión; la solicitud queda en Creada y puedes pulsar **Enviar** después. |
| El SAT rechazó la solicitud (código N). No se registró en el SAT. | Revisa los filtros y crea una solicitud nueva si hace falta. |
| No estás autorizado para descargar estos CFDI (5001). | La e.firma no tiene permiso sobre esos CFDI. |
| El SAT ya no acepta solicitudes con este mismo criterio. (5002) | Cambia los filtros. |
| La consulta supera el máximo de CFDI; usa un rango más corto. (5003) | Divide el periodo. |
| El SAT ya tiene una solicitud activa con este criterio. (5005) | Espera a la solicitud existente. |
| Se alcanzó el límite diario del SAT. Intenta mañana. (5011) | Intenta al día siguiente. |
| No se sabe si el SAT registró la solicitud. No se reenviará automáticamente; revisa antes de crear otra. | Revisa con el SAT antes de crear otra; la app pedirá confirmar el duplicado. |
| No se pudo consultar el estado; se reintentará automáticamente. | No hace falta hacer nada; también puedes usar **Verificar ahora**. |
| El SAT no encontró esta solicitud. La verificación automática se detuvo. | Crea una solicitud nueva si todavía la necesitas. |

### Paquetes y archivos

| Mensaje | Qué hacer |
| --- | --- |
| No se pudo descargar el paquete. Puedes reintentar. | Pulsa **Reintentar descarga**. |
| El paquete alcanzó el máximo de descargas permitidas. | El SAT ya no permite descargarlo de nuevo. |
| El paquete ya no existe en el SAT (vencido). Crea una solicitud nueva para el mismo periodo. | Crea una solicitud nueva para el mismo periodo. |
| No hay espacio suficiente para guardar el paquete. | Libera espacio en disco y reintenta la descarga. |
| No se pudo escribir en la carpeta de paquetes; revisa los permisos. | Revisa los permisos de la carpeta de paquetes. |
| Ya existe un archivo con ese nombre en la carpeta del paquete. | Mueve o renombra el archivo existente y reintenta. |
| Archivo local no encontrado / Archivo local no encontrado: el ZIP se movió o se borró fuera de la app. | El ZIP se movió o se borró fuera de la app. El estado del paquete no cambia. |
| Carpeta de paquetes no encontrada / No se pudo abrir Finder | Revisa que la carpeta exista e intenta de nuevo. |

### Otros

- "No se pudo cargar la lista de solicitudes." o "No se pudo cargar la
  solicitud.": pulsa **Reintentar**.
- "No se pudo eliminar la solicitud. Intenta de nuevo.": intenta eliminarla otra vez.
- "No se pudieron cargar los perfiles SAT.": usa **Reintentar** en Perfiles SAT.
