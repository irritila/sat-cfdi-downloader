# Traspaso UI/UX

## Resumen

Dirección: herramienta de escritorio sobria y nativa de macOS, del tipo «lista + detalle». Grises de sistema, un solo acento azul para la acción principal y el color reservado para el estado. Tipografía SF Pro a tamaños de macOS (13 pt cuerpo). Claro y oscuro con contraste AA verificado.
Un solo `EstadoBadge` (color + icono SF Symbol + texto) representa cada estado de solicitud, paquete y e.firma, igual en la lista, el detalle y las notificaciones.
La lista de solicitudes pasa a tabla con columnas (Estado, Contribuyente, Tipo, Periodo, Paquetes, Creada); la selección deja de ser azul saturado con texto negro.
El detalle deja de ser una página larga: arriba, un `ResumenEstado` con titular, una sola acción principal por estado y cuatro datos clave; abajo, pestañas Paquetes / Datos / Historial. `Eliminar…` queda separado y con confirmación.
Nueva solicitud se ordena en dos grupos (Solicitud y Filtros opcionales), con selector segmentado Emitidos/Recibidos, periodo en una fila, errores junto al campo y acciones fijas al pie. El duplicado bloqueado se muestra como aviso en línea; el que requiere confirmación es un diálogo con `Cancelar` por omisión.
Perfiles SAT usa SplitView; la e.firma es un grupo propio con estado, vigencia y una acción clara (Registrar/Reemplazar). El diálogo de e.firma muestra los estados validando, error junto al campo y éxito.
Textos: acentos y ñ en toda la interfaz, fechas legibles («1 oct 2026, 10:40») y titulares por estado que dicen qué pasó y qué sigue.

## Tokens

```json
{
  "color": {
    "light": { "fondo": "#F0F0F2", "superficie": "#FFFFFF", "superficieElevada": "#FFFFFF", "texto": "#1D1D1F", "textoSecundario": "#5C5C63", "borde": "#85858C", "acento": "#0062D6", "textoSobreAcento": "#FFFFFF", "exito": "#18793A", "advertencia": "#975300", "error": "#C0271F", "info": "#0B5CC0" },
    "dark": { "fondo": "#1C1C1E", "superficie": "#252527", "superficieElevada": "#2F2F33", "texto": "#F2F2F5", "textoSecundario": "#A9A9B1", "borde": "#7A7A83", "acento": "#286CDB", "textoSobreAcento": "#FFFFFF", "exito": "#5BC57A", "advertencia": "#F0AA3E", "error": "#FF7069", "info": "#64A8FF" }
  },
  "estado": {
    "Creada": {
      "light": { "fondo": "#EEEEF1", "texto": "#3A3A41", "borde": "#9A9AA2" },
      "dark": { "fondo": "#36363B", "texto": "#E4E4EA", "borde": "#8A8A93" },
      "icono": "circle.dashed"
    },
    "Enviando": {
      "light": { "fondo": "#E7F0FD", "texto": "#0B4CA3", "borde": "#7FA8E3" },
      "dark": { "fondo": "#1A2C47", "texto": "#B0CFFF", "borde": "#4F7DBF" },
      "icono": "arrow.up.circle"
    },
    "Enviada": {
      "light": { "fondo": "#E7F0FD", "texto": "#0B4CA3", "borde": "#7FA8E3" },
      "dark": { "fondo": "#1A2C47", "texto": "#B0CFFF", "borde": "#4F7DBF" },
      "icono": "paperplane"
    },
    "EnvioFallido": {
      "light": { "fondo": "#FCE7E6", "texto": "#A11A16", "borde": "#E08079" },
      "dark": { "fondo": "#451A1B", "texto": "#FFB4B0", "borde": "#B9544F" },
      "icono": "xmark.octagon"
    },
    "EnvioIncierto": {
      "light": { "fondo": "#FCF0DA", "texto": "#7F4500", "borde": "#D29A40" },
      "dark": { "fondo": "#3B2B11", "texto": "#F6CB82", "borde": "#A8792C" },
      "icono": "questionmark.diamond"
    },
    "AceptadaSat": {
      "light": { "fondo": "#E7F0FD", "texto": "#0B4CA3", "borde": "#7FA8E3" },
      "dark": { "fondo": "#1A2C47", "texto": "#B0CFFF", "borde": "#4F7DBF" },
      "icono": "checkmark.seal"
    },
    "EnProcesoSat": {
      "light": { "fondo": "#E7F0FD", "texto": "#0B4CA3", "borde": "#7FA8E3" },
      "dark": { "fondo": "#1A2C47", "texto": "#B0CFFF", "borde": "#4F7DBF" },
      "icono": "clock"
    },
    "Terminada": {
      "light": { "fondo": "#E3F3E7", "texto": "#13652B", "borde": "#6BB282" },
      "dark": { "fondo": "#16321F", "texto": "#A2E0B4", "borde": "#428E5B" },
      "icono": "checkmark.circle"
    },
    "ErrorSat": {
      "light": { "fondo": "#FCE7E6", "texto": "#A11A16", "borde": "#E08079" },
      "dark": { "fondo": "#451A1B", "texto": "#FFB4B0", "borde": "#B9544F" },
      "icono": "exclamationmark.octagon"
    },
    "Rechazada": {
      "light": { "fondo": "#FCE7E6", "texto": "#A11A16", "borde": "#E08079" },
      "dark": { "fondo": "#451A1B", "texto": "#FFB4B0", "borde": "#B9544F" },
      "icono": "nosign"
    },
    "Vencida": {
      "light": { "fondo": "#FCF0DA", "texto": "#7F4500", "borde": "#D29A40" },
      "dark": { "fondo": "#3B2B11", "texto": "#F6CB82", "borde": "#A8792C" },
      "icono": "calendar.badge.exclamationmark"
    },
    "Disponible": {
      "light": { "fondo": "#E7F0FD", "texto": "#0B4CA3", "borde": "#7FA8E3" },
      "dark": { "fondo": "#1A2C47", "texto": "#B0CFFF", "borde": "#4F7DBF" },
      "icono": "arrow.down.circle"
    },
    "Descargando": {
      "light": { "fondo": "#E7F0FD", "texto": "#0B4CA3", "borde": "#7FA8E3" },
      "dark": { "fondo": "#1A2C47", "texto": "#B0CFFF", "borde": "#4F7DBF" },
      "icono": "arrow.down.circle.dotted"
    },
    "Descargado": {
      "light": { "fondo": "#E3F3E7", "texto": "#13652B", "borde": "#6BB282" },
      "dark": { "fondo": "#16321F", "texto": "#A2E0B4", "borde": "#428E5B" },
      "icono": "checkmark.circle"
    },
    "Error": {
      "light": { "fondo": "#FCE7E6", "texto": "#A11A16", "borde": "#E08079" },
      "dark": { "fondo": "#451A1B", "texto": "#FFB4B0", "borde": "#B9544F" },
      "icono": "exclamationmark.triangle"
    },
    "Vencido": {
      "light": { "fondo": "#FCF0DA", "texto": "#7F4500", "borde": "#D29A40" },
      "dark": { "fondo": "#3B2B11", "texto": "#F6CB82", "borde": "#A8792C" },
      "icono": "clock.badge.xmark"
    },
    "EFirmaVerificando": {
      "light": { "fondo": "#EEEEF1", "texto": "#3A3A41", "borde": "#9A9AA2" },
      "dark": { "fondo": "#36363B", "texto": "#E4E4EA", "borde": "#8A8A93" },
      "icono": "ellipsis.circle"
    },
    "SinEFirma": {
      "light": { "fondo": "#EEEEF1", "texto": "#3A3A41", "borde": "#9A9AA2" },
      "dark": { "fondo": "#36363B", "texto": "#E4E4EA", "borde": "#8A8A93" },
      "icono": "key.slash"
    },
    "EFirmaLista": {
      "light": { "fondo": "#E3F3E7", "texto": "#13652B", "borde": "#6BB282" },
      "dark": { "fondo": "#16321F", "texto": "#A2E0B4", "borde": "#428E5B" },
      "icono": "checkmark.shield"
    },
    "EFirmaVencida": {
      "light": { "fondo": "#FCF0DA", "texto": "#7F4500", "borde": "#D29A40" },
      "dark": { "fondo": "#3B2B11", "texto": "#F6CB82", "borde": "#A8792C" },
      "icono": "calendar.badge.exclamationmark"
    },
    "EFirmaNoVigente": {
      "light": { "fondo": "#FCF0DA", "texto": "#7F4500", "borde": "#D29A40" },
      "dark": { "fondo": "#3B2B11", "texto": "#F6CB82", "borde": "#A8792C" },
      "icono": "calendar.badge.clock"
    },
    "EFirmaIncompleta": {
      "light": { "fondo": "#FCE7E6", "texto": "#A11A16", "borde": "#E08079" },
      "dark": { "fondo": "#451A1B", "texto": "#FFB4B0", "borde": "#B9544F" },
      "icono": "exclamationmark.lock"
    },
    "EFirmaDanada": {
      "light": { "fondo": "#FCE7E6", "texto": "#A11A16", "borde": "#E08079" },
      "dark": { "fondo": "#451A1B", "texto": "#FFB4B0", "borde": "#B9544F" },
      "icono": "exclamationmark.lock"
    },
    "EFirmaEstadoNoDisponible": {
      "light": { "fondo": "#FCF0DA", "texto": "#7F4500", "borde": "#D29A40" },
      "dark": { "fondo": "#3B2B11", "texto": "#F6CB82", "borde": "#A8792C" },
      "icono": "questionmark.circle"
    }
  },
  "tipografia": {
    "titulo": { "size": 20, "weight": 600 },
    "subtitulo": { "size": 15, "weight": 600 },
    "cuerpo": { "size": 13, "weight": 400 },
    "etiqueta": { "size": 11, "weight": 600 },
    "mono": { "size": 12, "weight": 400 }
  },
  "espaciado": {
    "xs": 4,
    "s": 8,
    "m": 12,
    "l": 16,
    "xl": 24
  },
  "radio": {
    "control": 6,
    "tarjeta": 10,
    "badge": 10
  },
  "sombra": {
    "tarjeta": "Claro: 0 1px 2px #0000001A (offsetY 1, blur 2, negro 10%). Oscuro: sin sombra; la tarjeta se separa con un borde de 1px en `separador` (#3A3A3F)."
  }
}
```

Notas para implementar:

- `estado` incluye los 11 estados de solicitud (los del punto 3 del brief más `Enviando`, `AceptadaSat` y `EnProcesoSat`, que el manual también muestra), los 5 de paquete y los 8 de e.firma. Todos los estados de un mismo tono comparten colores; el icono y el texto los distinguen. Tonos: neutro (`Creada`, `SinEFirma`, `EFirmaVerificando`), progreso (`Enviando`, `Enviada`, `AceptadaSat`, `EnProcesoSat`, `Disponible`, `Descargando`), éxito (`Terminada`, `Descargado`, `EFirmaLista`), advertencia (`EnvioIncierto`, `Vencida`, `Vencido`, `EFirmaVencida`, `EFirmaNoVigente`, `EFirmaEstadoNoDisponible`) y error (`EnvioFallido`, `ErrorSat`, `Rechazada`, `Error`, `EFirmaIncompleta`, `EFirmaDanada`).
- La clave de paquete `Error` se muestra como «Error de descarga».
- Alto de línea: titulo 26, subtitulo 20, cuerpo 18, etiqueta 14, mono 16. Además se usan `cuerpoFuerte` (13/600) para RFC y titulares de alerta, y `leyenda` (12/400) para metadatos y ayudas.
- Familias: `font.family: Qt.application.font.family` (SF Pro en macOS) y, para `mono`, `"SF Mono"` con respaldo `Menlo`.
- Contraste comprobado (WCAG 2.1): `texto` y `textoSecundario` ≥4.5:1 sobre `fondo`, `superficie`, `superficieElevada`, `superficieSeccion` y `seleccion` en ambos temas; texto de cada tono ≥6.2:1 sobre su fondo; `textoSobreAcento` sobre `acento` 5.65:1 en claro y 4.93:1 en oscuro; `borde` ≥3.6:1 sobre `superficie`; `foco` ≥3:1 en todas las superficies.

Tokens auxiliares. No forman parte del esquema del brief, pero los componentes los necesitan. Van en el mismo `Theme.qml`:

| Token | Claro | Oscuro | Uso |
|---|---|---|---|
| separador | #E2E2E6 | #3A3A3F | Líneas de 1 px entre filas, bajo el encabezado y alrededor de grupos. Es decorativo y no delimita controles. |
| superficieSeccion | #F7F7F9 | #2A2A2D | Encabezados de sección en Datos, campo de solo lectura, nombre de archivo y fila con hover. |
| seleccion | #DCE9FB | #1E3A63 | Fila seleccionada. El texto conserva `texto`. |
| foco | #0062D6 | #5A9BFF | Anillo de foco de 2 px con 2 px de separación. |
| acentoTexto | #0059C2 | #7DB4FF | Botón «‹ Solicitudes» y enlaces. |
| acentoPresionado | #0050AE | #1D57B8 | Botón primario presionado. |
| controlFondo | #FFFFFF | #1F1F22 | Interior de TextField y ComboBox. |
| controlSecundario | #E9E9ED | #3A3A3F | Botones secundarios y destructivos, y carril del segmentado. |
| segmentoActivo | #FFFFFF | #5E5E66 | Segmento o pestaña activa. |
| velo | #0000004D | #00000080 | Fondo detrás de un diálogo modal. |
| sombra-dialogo | 0 12 32 #00000033 + borde 1 #0000001F | 0 12 32 #00000099 + borde 1 #FFFFFF26 | Diálogos. En QML: `MultiEffect` con `shadowEnabled`, o un `Rectangle` desplazado y con opacidad si se evita el efecto. |

Medidas fijas (pt): alto de control 28; badge 20 (grande 26); encabezado 52; fila de solicitud 56; pie de formulario 56; lista de perfiles 340 (mín. 280); diálogos 480; formulario 640 máx.; radio de diálogo 12.

## Componentes

### BotonAccion

- **Propósito:** botón estándar de la app para toda acción de página, fila o diálogo.
- **Variantes:**
  - `primario`: relleno `acento`, texto `textoSobreAcento`. Uno por vista como máximo.
  - `secundario` (por omisión): relleno `controlSecundario`, texto `texto`.
  - `destructivo`: relleno `controlSecundario`, texto e icono `error`.
  - `navegacion`: sin relleno, texto `acentoTexto` e icono `chevron.left`.
  - `icono`: cuadrado de 28 sin texto.
  - Tamaño `sm`: alto 24, texto 12, para acciones dentro de filas y avisos.
- **Anatomía:**
  - contenedor: `radio.control`, alto 28;
  - icono opcional de 16 pt, del mismo color que el texto;
  - etiqueta en `cuerpo` con peso 500;
  - gap entre icono y etiqueta: 6.
- **Estados:**
  - normal;
  - hover: relleno 4 % más oscuro (claro) o 15 % más claro (oscuro);
  - presionado: `acentoPresionado` en el primario y 10 % más oscuro en los demás;
  - foco: anillo `foco` de 2 px con separación de 2;
  - deshabilitado: opacidad 0.45. Cuando el motivo importa, se muestra en texto junto al botón o en un ToolTip;
  - cargando: `BusyIndicator` de 14 pt en lugar del icono, con el botón deshabilitado.
- **Medidas:** alto 28 (`sm` 24); padding horizontal `espaciado.m` (12); gap entre botones `espaciado.s` (8).
- **Accesibilidad:**
  - `Accessible.name` = la etiqueta; en la variante icono, un texto explícito (p. ej. «Eliminar solicitud»);
  - Espacio y Enter activan el botón;
  - el botón por omisión de un diálogo responde a Enter, y `Cancelar` a Escape;
  - un ToolTip muestra el atajo cuando existe.

### EstadoBadge

- **Propósito:** comunicar el estado de una solicitud, un paquete o una e.firma, siempre con texto, icono y color.
- **Variantes:** `normal` (alto 20) y `grande` (alto 26, en ResumenEstado y en el grupo de e.firma). Tono según `estado.<Clave>`.
- **Anatomía:**
  - píldora con fondo `estado.<Clave>.fondo` y borde de 1 px `estado.<Clave>.borde`;
  - icono SF Symbol `estado.<Clave>.icono` de 13 pt (16 en la variante grande) y color `estado.<Clave>.texto`;
  - texto en `etiqueta` (11/600), o en `cuerpo` 13/600 en la variante grande, y color `estado.<Clave>.texto`.
- **Estados:** estático. `Descargando` y `Enviando` pueden alternar la opacidad del icono (0.5–1) cada 1 s. Es opcional; no usar animaciones por elemento más complejas.
- **Medidas:** padding 6 a la izquierda y 8 a la derecha (8/10 en la variante grande); gap `espaciado.xs`; radio `radio.badge` (mitad del alto).
- **Accesibilidad:** `Accessible.role: Accessible.StaticText`, `Accessible.name: "Estado: " + etiqueta`. No recibe foco. El texto siempre está visible.

### EncabezadoPagina

- **Propósito:** barra superior de cada página con navegación, título y acciones.
- **Variantes:**
  - `raiz` (Solicitudes): título y contador, sin botón atrás;
  - `secundaria`: botón `navegacion` «‹ Solicitudes» y título;
  - `con subtitulo` (Detalle): título de 15/600 con una línea `leyenda` debajo.
- **Anatomía, de izquierda a derecha:**
  - BotonAccion `navegacion`, solo en la variante secundaria;
  - título en `tipografia.titulo`;
  - contador opcional en 15/400 `textoSecundario`;
  - espacio flexible;
  - acciones secundarias;
  - separador vertical opcional, antes de una acción destructiva;
  - acción primaria en el extremo derecho.
  - El fondo es `fondo`, con una línea inferior de 1 px en `separador`.
- **Estados:** normal. Si el ancho no alcanza, las etiquetas de las acciones secundarias se ocultan y queda solo el icono (con ToolTip y nombre accesible). La primaria nunca se oculta.
- **Medidas:** alto 52; padding horizontal `espaciado.l`; gap `espaciado.s`.
- **Accesibilidad:** el título usa `Accessible.role: Accessible.Heading`. Escape equivale a «‹ Solicitudes». El orden de foco va de izquierda a derecha.

### CampoDetalle

- **Propósito:** par etiqueta–valor de solo lectura en la pestaña Datos.
- **Variantes:** `texto`, `mono` (identificadores) y `estado` (el valor es un EstadoBadge o un texto atenuado como «Sin respuesta del SAT»).
- **Anatomía:**
  - fila en grid de 200 + 1fr;
  - etiqueta en `cuerpo` `textoSecundario`;
  - valor en `cuerpo` `texto`, o `mono` cuando es un identificador;
  - línea superior de 1 px `separador`, excepto en la primera fila.
- **Estados:** normal. Valor vacío: «—» en `textoSecundario`. Si el valor no cabe, se trunca al final con ToolTip del valor completo.
- **Medidas:** padding de 7 en vertical y `espaciado.l` en horizontal.
- **Accesibilidad:** el valor es un `TextEdit` de solo lectura con `selectByMouse: true`, para poder copiar el Id SAT. `Accessible.name` = «etiqueta: valor». Se alcanza con Tab solo si es seleccionable (variante mono).

### DialogoConfirmacion

- **Propósito:** confirmar una acción riesgosa (eliminar, duplicar, reemplazar e.firma).
- **Variantes:**
  - `advertencia` (duplicado, reemplazo): icono `exclamationmark.triangle` sobre `tono-advertencia`;
  - `destructivo` (eliminar): icono `trash` sobre `tono-error`, y el botón de acción en la variante `destructivo`.
- **Anatomía:**
  - contenedor `superficieElevada`, radio 12, `sombra-dialogo` y velo `velo`;
  - cabecera con un cuadro de icono de 40 × 40 (radio 10, colores del tono) y título en `subtitulo`;
  - cuerpo en `cuerpo`: motivo en 600 y consecuencia en `textoSecundario`;
  - botones alineados a la derecha: `Cancelar` y luego la acción.
- **Estados:**
  - abierto: el foco inicial está en `Cancelar`, que es el botón por omisión;
  - procesando: la acción muestra un BusyIndicator y ambos botones se deshabilitan;
  - error: un AvisoEnLinea `error` aparece sobre los botones (p. ej. «No se pudo eliminar la solicitud. Intenta de nuevo.»).
- **Medidas:** ancho 480; padding 20; gap `espaciado.m`; los botones están a `espaciado.s` entre sí.
- **Accesibilidad:**
  - `Dialog { modal: true }` con `Accessible.role: Accessible.Dialog` y el título como nombre;
  - Escape cancela; Enter activa el botón por omisión (`Cancelar`);
  - el foco queda atrapado dentro del diálogo y vuelve al control que lo abrió al cerrarse.

### EFirmaDialogo

- **Propósito:** registrar o reemplazar la e.firma de un perfil.
- **Variantes:**
  - `registrar`: título «Registrar e.firma», botón «Registrar»;
  - `reemplazar`: título «Reemplazar e.firma», botón «Reemplazar». Va precedido por el DialogoConfirmacion de reemplazo.
- **Anatomía:**
  - cabecera: icono `key` sobre `tono-progreso`, título y subtítulo «RFC · nombre» en `leyenda`;
  - instrucción;
  - tres filas CampoFormulario (etiqueta de 150): «Certificado (.cer)» y «Llave privada (.key)» como CampoArchivo con «Elegir…», y «Contraseña» como TextField con `echoMode: TextInput.Password` y la ayuda «Se guarda en el llavero de macOS.»;
  - pie con «Cancelar» y la acción primaria.
- **Estados:**
  - formulario: la acción primaria está deshabilitada hasta que haya .cer, .key y contraseña;
  - validando: BusyIndicator y «Validando e.firma…» a la izquierda del pie; todos los controles deshabilitados;
  - error: mensaje bajo el campo culpable, con borde `error`; el foco va a ese campo (archivo o contraseña) y la contraseña se vacía;
  - éxito: el formulario se reemplaza por un AvisoEnLinea `exito` («e.firma registrada.») y queda un único botón «Cerrar» primario;
  - error de reemplazo: el mensaje termina con «La e.firma anterior sigue registrada sin cambios.»
- **Medidas:** ancho 480; filas con padding vertical 6; gap entre el nombre de archivo y «Elegir…» `espaciado.s`.
- **Accesibilidad:**
  - orden de foco: Elegir certificado → Elegir llave → Contraseña → Cancelar → Registrar;
  - Enter en la contraseña envía; Escape cancela, salvo durante la validación;
  - el nombre accesible del archivo es «Certificado: demo.cer» o «Certificado: ninguno»;
  - la contraseña se vacía al enviar, cancelar, cerrar el diálogo, cambiar de perfil, salir de la pantalla y ocultar la ventana (comportamiento actual).

### AvisoEnLinea (nuevo)

- **Propósito:** mensaje persistente dentro de una página: bloqueo de duplicado, notificaciones desactivadas, error o éxito en un diálogo.
- **Variantes:** `progreso`, `exito`, `advertencia`, `error` y `neutro`, con los colores de `tono-*`.
- **Anatomía:**
  - contenedor `tono-X-fondo` con borde de 1 px `tono-X-borde` y radio 8;
  - icono de 18 pt en `tono-X-texto`;
  - título en `cuerpoFuerte` y descripción en `cuerpo`, ambos en `tono-X-texto`;
  - BotonAccion `sm` opcional a la derecha, con fondo `superficie`.
- **Estados:** estático. No se puede cerrar: el aviso desaparece cuando su causa se resuelve.
- **Medidas:** padding 10/12; gap 10.
- **Accesibilidad:** `Accessible.role: Accessible.AlertMessage` cuando aparece por una acción del usuario. Su botón entra en el orden de foco.

### EstadoVacio (nuevo)

- **Propósito:** pantalla o región sin datos, o con un error de carga.
- **Variantes:** `vacio` (icono `tray`), `error` (icono `exclamationmark.triangle`) y `sin perfiles` (icono `person.text.rectangle`).
- **Anatomía:**
  - icono de 48 pt en `textoSecundario`;
  - título en 15/600;
  - descripción en `cuerpo` `textoSecundario`, de 380 de ancho máximo;
  - BotonAccion opcional (primario o secundario).
- **Estados:** estático.
- **Medidas:** centrado; gap `espaciado.s`; `espaciado.m` antes del botón; padding `espaciado.xxl`.
- **Accesibilidad:** el título es Heading. El botón recibe el foco al mostrarse el estado de error.

### FilaSolicitud (nuevo)

- **Propósito:** una solicitud en la tabla de Solicitudes.
- **Variantes:** ninguna.
- **Anatomía (columnas):**
  - Estado: EstadoBadge, 176;
  - Contribuyente: RFC en `cuerpoFuerte` con el nombre del perfil debajo en `leyenda` `textoSecundario`, 1.5fr;
  - Tipo: icono `arrow.up.right` (Emitidos) o `arrow.down.left` (Recibidos) más el texto, 112;
  - Periodo, 1.1fr;
  - Paquetes: alineado a la derecha y tabular, 72;
  - Creada: en `textoSecundario`, 150;
  - `chevron.right`, 16.
- **Estados:**
  - normal: `superficie`;
  - hover: `superficieSeccion`;
  - seleccionada: `seleccion`;
  - foco: anillo `foco` interior de 2 px.
- **Medidas:** alto mínimo 56; padding horizontal `espaciado.l`; gap entre columnas `espaciado.m`; línea inferior `separador`.
- **Accesibilidad:**
  - `Accessible.name` = «Terminada, XAXX010101000 Contribuyente de ejemplo, Recibidos, 3 sep 2026, 3 paquetes, creada 1 oct 2026 10:03»;
  - flechas para moverse; Enter o Espacio para abrir.

### FilaPaquete (nuevo)

- **Propósito:** un paquete del SAT en la pestaña Paquetes.
- **Variantes:** ninguna.
- **Anatomía:**
  - columna de 150 con un EstadoBadge alineado a la izquierda;
  - bloque central con el nombre del paquete en `mono` 500, los metadatos en `leyenda` `textoSecundario` y un mensaje opcional con icono de 14 en `exito`, `advertencia`, `error` o `textoSecundario`;
  - acción a la derecha (BotonAccion `sm` «Mostrar en Finder»).
- **Estados por estado del paquete:**
  - `Disponible`: «La app lo descargará automáticamente.»;
  - `Descargando`: ProgressBar indeterminada en lugar del mensaje;
  - `Descargado`: comprobación del archivo local, con «Archivo local presente» (éxito), «Archivo local no encontrado: …» (advertencia) o «No se pudo comprobar el archivo local» (advertencia). «Mostrar en Finder» se habilita solo si el archivo está presente;
  - `Error`: mensaje de error;
  - `Vencido`: mensaje de vencido.
- **Medidas:** padding 12/16; gap `espaciado.m`; separador entre filas.
- **Accesibilidad:** `Accessible.name` = «Paquete _01, Descargado, archivo local presente». El botón tiene su propio foco. El nombre del paquete es seleccionable.

### FilaPerfil (nuevo)

- **Propósito:** un perfil en la lista izquierda de Perfiles SAT.
- **Variantes:** ninguna.
- **Anatomía:**
  - línea 1: RFC en `cuerpoFuerte` y EstadoBadge de e.firma a la derecha;
  - línea 2: nombre;
  - línea 3: icono de 13 con la disponibilidad en `leyenda` `textoSecundario`;
  - línea 4, opcional: `calendar` con «Vigente hasta AAAA-MM-DD».
  - Si el estado es `EFirmaEstadoNoDisponible`, aparece un BotonAccion `sm` «Reintentar» en la línea 3.
- **Estados:** normal, seleccionada (`seleccion`), foco e inactivo («Inactivo: no disponible para solicitudes»).
- **Medidas:** padding 10/16; gap entre líneas 2; separador inferior.
- **Accesibilidad:** el nombre accesible es la concatenación de las líneas. Flechas para moverse y R para reintentar el estado (atajo actual).

### ResumenEstado (nuevo)

- **Propósito:** cabecera del detalle que dice qué pasó, qué sigue y cuál es la acción principal.
- **Variantes:** una por estado de `Resumen` (ver la tabla en DetalleSolicitudPage).
- **Anatomía:**
  - tarjeta `superficie` con borde `separador`, radio `radio.tarjeta` y `sombra.tarjeta`;
  - a la izquierda, EstadoBadge grande, titular en 15/600 y descripción en `textoSecundario`;
  - a la derecha, la acción principal (BotonAccion primario o secundario) y, debajo, el mensaje de resultado de la acción como nota con icono (`info` o `error`);
  - abajo, una rejilla de 4 datos (etiqueta en `etiqueta` `textoSecundario`, valor en `cuerpo`): Contribuyente, Tipo · periodo, CFDI reportados y Última verificación.
- **Estados:** con acción, sin acción y acción deshabilitada con el motivo visible debajo.
- **Medidas:** padding `espaciado.l`; gap de columnas `espaciado.l`; la rejilla de datos lleva una línea superior `separador` y `espaciado.m` arriba.
- **Accesibilidad:** la región se llama «Resumen». El titular es Heading. El mensaje de resultado se anuncia con `Accessible.AlertMessage`. Primer foco de la página: la acción principal.

### CampoFormulario (nuevo)

- **Propósito:** fila etiqueta + control + ayuda + error de los formularios.
- **Variantes:** control `TextField`, `ComboBox`, `fecha` (TextField de 132 de ancho con `inputMask` o validador AAAA-MM-DD), `soloLectura` (fondo `superficieSeccion`, borde punteado e icono `lock`), segmentado y archivo.
- **Anatomía:**
  - grid de 168 + 1fr;
  - etiqueta en `cuerpo` `texto`;
  - control con alto 28, borde `borde`, radio `radio.control` y fondo `controlFondo`; el ComboBox lleva `chevron.up.chevron.down`;
  - ayuda en `leyenda` `textoSecundario`;
  - error en `leyenda` `error` con icono `exclamationmark.octagon`.
- **Estados:**
  - normal;
  - foco: borde y anillo `foco`;
  - error: borde de 2 px `error` y el mensaje bajo el control;
  - deshabilitado: opacidad 0.5;
  - solo lectura.
- **Medidas:** padding de fila 10/12; filas separadas por `separador`.
- **Accesibilidad:**
  - `Accessible.name` = la etiqueta y `Accessible.description` = la ayuda o el error;
  - al validar, el foco va al primer campo con error y el mensaje se anuncia;
  - Enter en un TextField envía el formulario (comportamiento actual).

### SelectorSegmentado (nuevo)

- **Propósito:** elegir una opción entre 2 o 3 (tipo de descarga) y servir de pestañas del detalle.
- **Variantes:** `opciones` (`ButtonGroup` con `Button { checkable: true }`) y `pestanas` (`TabBar` con `TabButton` estilizado igual; un contador opcional en `etiqueta` `textoSecundario`).
- **Anatomía:**
  - carril `controlSecundario` con radio 7 y padding 2;
  - segmentos de alto 24, padding 0/14 y radio 5;
  - el segmento activo lleva `segmentoActivo`, `sombra.tarjeta` y peso 600; el inactivo, peso 500;
  - icono opcional de 14.
- **Estados:** activo, inactivo, hover (inactivo con 50 % de `segmentoActivo`), foco (anillo alrededor del segmento) y deshabilitado.
- **Medidas:** gap 2 entre segmentos.
- **Accesibilidad:** el activo no depende solo del color: lleva peso 600 y fondo elevado. `Accessible.checked` en opciones; `Accessible.PageTab` en pestañas. Flechas izquierda y derecha cambian de segmento; Tab entra y sale del grupo.

### CampoArchivo (nuevo)

- **Propósito:** mostrar el archivo elegido (.cer o .key) y abrir el selector de macOS.
- **Variantes:** `vacio` («Ningún archivo» en `textoSecundario`), `elegido` (icono `doc` o `key`, el nombre del archivo y un `checkmark.circle` en `exito`) y `error` (borde `error`).
- **Anatomía:** caja `superficieSeccion` con borde `separador`, alto 28 y radio 6; a la derecha, BotonAccion «Elegir…». Solo se muestra el nombre, nunca la ruta (comportamiento actual).
- **Estados:** vacío, elegido, error y deshabilitado (durante la validación).
- **Medidas:** gap `espaciado.s`.
- **Accesibilidad:** solo el botón recibe foco, con nombre «Elegir certificado (.cer)» o «Elegir llave privada (.key)» y descripción igual al archivo actual.

### EventoHistorial (nuevo)

- **Propósito:** una línea del historial de la solicitud.
- **Variantes:** origen `Usuario` (icono `person`) u `Automático` (icono `gearshape`).
- **Anatomía:** grid de 150 + 1fr + auto, con fecha y hora en `textoSecundario`, la descripción en `cuerpo` y el origen con icono de 13 en `leyenda` `textoSecundario`.
- **Estados:** estático.
- **Medidas:** padding 8/16; separador entre eventos.
- **Accesibilidad:** nombre «1 oct 2026 10:35, Verificación realizada, origen Automático».

## Pantallas

### SolicitudesPage

- **Layout:**
  - EncabezadoPagina (`raiz`, 52);
  - AvisoEnLinea opcional, solo cuando las notificaciones están deshabilitadas, en una franja `superficie` con padding 12/16;
  - fila de encabezados de columna de 30, fija al hacer scroll;
  - `ListView` de FilaSolicitud que ocupa el resto, con scroll vertical.
  - Al redimensionar crecen las columnas Contribuyente y Periodo. Por debajo de 960 se oculta primero la columna Creada; su dato sigue disponible en el detalle.
- **Contenido, en orden de foco:**
  1. Título «Solicitudes» (`titulo`) y contador «7» (15/400 `textoSecundario`).
  2. «Abrir carpeta de paquetes»: BotonAccion secundario, icono `folder`.
  3. «Perfiles SAT»: secundario, icono `person.text.rectangle`.
  4. «Nueva solicitud»: primario, icono `plus`.
  5. Aviso (si aplica): «Las notificaciones están desactivadas.» y «Actívalas en Ajustes del Sistema para recibir avisos cuando una solicitud termine o falle.»
  6. Encabezados de columna (`etiqueta` `textoSecundario`): «Estado», «Contribuyente», «Tipo», «Periodo», «Paquetes», «Creada».
  7. La lista; la primera fila recibe el foco al entrar a la página.
- **Formatos:**
  - Periodo: un día «3 sep 2026»; un rango del mismo mes «1–30 sep 2026»; otros rangos «15 ago – 14 sep 2026».
  - Paquetes: el número; «—» si es 0 y la solicitud no está `Terminada`; «0» si está terminada.
  - Creada: «1 oct 2026, 10:03».
- **Estados de la pantalla:**
  - con datos: más reciente primero;
  - vacío: EstadoVacio con «No hay solicitudes», «Crea una solicitud para descargar del SAT los CFDI emitidos o recibidos de un contribuyente.» y el botón primario «Nueva solicitud»;
  - cargando: `BusyIndicator` centrado; solo se muestra si la carga tarda más de 300 ms;
  - error: EstadoVacio `error` con «No se pudo cargar la lista de solicitudes.», «Tus solicitudes siguen guardadas en este equipo. Intenta cargarlas de nuevo.» y «Reintentar» (con el foco);
  - notificaciones deshabilitadas: el aviso arriba de la tabla.
- **Acciones:** Nueva solicitud, Perfiles SAT, Abrir carpeta de paquetes, abrir detalle (clic, Enter o Espacio) y Reintentar.
- **Cambios respecto de la versión actual:** UX-01, UX-02, UX-03, UX-04, UX-05, UX-06, UX-07, UX-08, UX-09, UX-10, UX-11, UX-12, UX-13, UX-40, UX-41, UX-42.
- **Mockup:** `01-solicitudes-con-datos-{light,dark}.png`, `02-solicitudes-vacia-*.png`, `03-solicitudes-error-*.png`, `04-solicitudes-sin-notificaciones-*.png`.

### NuevaSolicitudPage

- **Layout:**
  - EncabezadoPagina `secundaria`;
  - área con scroll y fondo `fondo`, con una columna centrada de 640 de ancho máximo y padding 24/16;
  - grupo «Solicitud» y grupo «Filtros opcionales» (tarjetas `superficie`);
  - pie fijo de 56 con texto de ayuda a la izquierda y los botones a la derecha.
  - Al agrandar la ventana, la columna se queda centrada en 640.
- **Contenido, en orden de foco:**
  1. «‹ Solicitudes» (navegación, Escape).
  2. Título «Nueva solicitud».
  3. «Perfiles SAT» (secundario).
  4. Grupo «Solicitud»:
     - «Perfil SAT»: ComboBox «XAXX010101000 — Contribuyente de ejemplo», con la ayuda «Solo aparecen perfiles activos con la e.firma lista.»;
     - «Tipo de descarga»: SelectorSegmentado «Emitidos» (`arrow.up.right`) / «Recibidos» (`arrow.down.left`), con Emitidos por omisión;
     - «Periodo»: fecha inicial, el texto «a» y fecha final (TextField fecha), con la ayuda «Formato AAAA-MM-DD. Incluye ambos días.»
  5. Grupo «Filtros opcionales», con controles de 240 de ancho:
     - «RFC contraparte»: placeholder «Cualquiera»;
     - «Tipo de comprobante»: ComboBox con «Todos», «I - Ingreso», «E - Egreso», «T - Traslado», «N - Nómina» y «P - Pago»;
     - «Complemento»: placeholder «Cualquiera».
  6. Pie: «Al crearla, la app la envía al SAT con la e.firma del perfil.» (`leyenda` `textoSecundario`), «Cancelar» (secundario; equivale a Regresar) y «Crear solicitud» (primario, por omisión con Enter).
- **Estados de la pantalla:**
  - formulario: valores iniciales actuales (del primer día del mes a hoy);
  - error de validación: el mensaje va bajo el campo («Selecciona un perfil SAT.», «Indica una fecha inicial válida (AAAA-MM-DD).», «Indica una fecha final válida (AAAA-MM-DD).», «La fecha final debe ser igual o posterior a la fecha inicial.» o «El perfil SAT seleccionado ya no está disponible.»), el campo con borde de error y el foco en el primer campo inválido;
  - bloqueado por duplicado: AvisoEnLinea `error` arriba del primer grupo, con «No se puede crear: ya existe una solicitud equivalente.», el motivo y el botón «Ver solicitud existente»; «Crear solicitud» queda deshabilitado mientras los filtros sean equivalentes;
  - duplicado que requiere confirmación: ver DialogoConfirmacion;
  - sin perfiles: EstadoVacio en lugar del formulario, con «No hay perfiles SAT listos para solicitudes», «Un perfil necesita estar activo y tener su e.firma registrada y vigente.» y «Administrar perfiles SAT» (primario);
  - creando: «Crear solicitud» con BusyIndicator; al terminar se abre el detalle (comportamiento actual).
- **Acciones:** Regresar o Cancelar, Perfiles SAT, Crear solicitud, Ver solicitud existente, Crear de todos modos y Administrar perfiles SAT.
- **Cambios respecto de la versión actual:** UX-01, UX-05, UX-14, UX-15, UX-16, UX-17, UX-18, UX-19, UX-20, UX-21.
- **Mockup:** `05-nueva-solicitud-formulario-*.png`, `06-nueva-solicitud-error-validacion-*.png`, `07-nueva-solicitud-bloqueada-*.png`, `08-nueva-solicitud-duplicado-*.png`, `09-nueva-solicitud-sin-perfiles-*.png`.

### DetalleSolicitudPage

- **Layout:**
  - EncabezadoPagina con subtítulo: título «Solicitud» y subtítulo «XAXX010101000 · Recibidos · 3 sep 2026»;
  - área con padding 16 y gap 12, que contiene ResumenEstado, SelectorSegmentado `pestanas` y el panel de la pestaña activa (tarjeta `superficie`, scroll interno);
  - todo ocupa el ancho completo; la rejilla de datos del resumen pasa de 4 a 2 columnas por debajo de 760 de ancho.
- **Contenido, en orden de foco:**
  1. «‹ Solicitudes».
  2. «Abrir carpeta de la solicitud» (secundario, `folder`).
  3. «Eliminar…» (destructivo, `trash`, separado por una línea vertical; nombre accesible «Eliminar solicitud»).
  4. La acción principal del resumen.
  5. Las pestañas «Paquetes N» / «Datos» / «Historial N».
  6. El contenido de la pestaña.
- **Pestaña por omisión:** Paquetes si hay paquetes; si no, Datos.
- **ResumenEstado por estado (texto exacto):**

| Resumen | Titular | Descripción | Acción principal |
|---|---|---|---|
| Creada | La solicitud aún no se envía al SAT | Guardada en este equipo. Se enviará con la e.firma del perfil. | Enviar (primario). Si la e.firma no está lista, queda deshabilitado y se muestra el motivo debajo, p. ej. «Este perfil no tiene e.firma registrada. Regístrala para continuar.» |
| Enviando | Enviando la solicitud al SAT… | La app la está enviando con la e.firma del perfil. | — (BusyIndicator junto al titular) |
| Enviada | El SAT recibió la solicitud | Aún no hay respuesta del SAT. La app verificará automáticamente. | Verificar ahora (secundario) |
| Aceptada por SAT | El SAT aceptó la solicitud | La está atendiendo. La app verificará automáticamente. | Verificar ahora |
| En proceso SAT | El SAT está preparando los paquetes | La app verifica el estado periódicamente. No necesitas hacer nada. | Verificar ahora |
| Terminada (paquetes pendientes o con error reintentable) | El SAT terminó la solicitud (o «Hay un paquete con error de descarga») | «X de N paquetes descargados · Y disponibles · Z con error · W vencidos» (solo los conteos mayores que 0) | Reintentar descarga (primario) |
| Terminada (todo descargado) | Todos los paquetes están descargados | N de N paquetes en este equipo. | Abrir carpeta de la solicitud (primario; en ese caso se quita del encabezado) |
| Terminada (sin paquetes) | El SAT terminó sin paquetes | No hay paquetes que descargar para estos filtros. | — |
| Envío fallido | El SAT rechazó el envío | No se registró en el SAT. Si necesitas repetirla, crea una solicitud nueva. | — |
| Envío incierto | No se sabe si el SAT registró la solicitud | No se reenviará automáticamente; revisa antes de crear otra. | — |
| Error SAT | El SAT reportó un error en la solicitud | Último error con su código, p. ej. «La consulta supera el máximo de CFDI; usa un rango más corto. (5003)» | — (o Verificar ahora si aplica hoy) |
| Rechazada por SAT | El SAT rechazó la solicitud | Mensaje del SAT con su código, p. ej. «No estás autorizado para descargar estos CFDI (5001).» | — |
| Vencida | La solicitud venció en el SAT | Los paquetes pueden ya no estar disponibles. | Reintentar descarga, solo si hay paquetes Disponibles |

- El mensaje de resultado aparece bajo la acción como nota `info`: «Envío solicitado.», «Verificación solicitada. Si el monitoreo está pausado, queda pendiente.» o «Descarga solicitada. Si el monitoreo está pausado, queda pendiente.»
- **Datos del resumen:** Contribuyente (RFC), Tipo · periodo, CFDI reportados («—» si no hay) y Última verificación.
- **Pestaña Paquetes:** FilaPaquete por paquete, con estos textos de metadatos:
  - «Disponible 1 oct 2026, 10:40»;
  - «· Descargado 1 oct 2026, 10:45»;
  - «· Venció 4 oct 2026, 10:00»;
  - «· Código SAT 5007».
  - Mensajes: «Archivo local presente», «Archivo local no encontrado: el ZIP se movió o se borró fuera de la app.», «No se pudo comprobar el archivo local», «No se pudo descargar el paquete. Puedes reintentar.», «El paquete alcanzó el máximo de descargas permitidas.» (sin Reintentar), «El paquete ya no existe en el SAT (vencido). Crea una solicitud nueva para el mismo periodo.» y «La app lo descargará automáticamente.»
  - Sin paquetes: EstadoVacio pequeño con «El SAT aún no entrega paquetes.» o «Esta solicitud no tiene paquetes.»
- **Pestaña Datos:** CampoDetalle agrupado bajo encabezados de sección (`etiqueta` sobre `superficieSeccion`):
  - «Estado»: Resumen, Estado local, Estado SAT («Sin respuesta del SAT» si no hay) y Último error;
  - «SAT»: Id solicitud SAT (mono, seleccionable), Código de solicitud SAT («5000 · Solicitud aceptada»), Código de verificación SAT, CFDI reportados, Enviada y Última verificación;
  - «Solicitud»: Identificador local (mono), Perfil, Tipo de descarga y Creada;
  - «Filtros»: Periodo, RFC contraparte, Tipo de comprobante y Complemento.
- **Pestaña Historial:** EventoHistorial en orden cronológico, como hoy.
- **Estados de la pantalla:** uno por estado de Resumen (tabla). También:
  - cargando: BusyIndicator;
  - error: EstadoVacio con «No se pudo cargar la solicitud.» y «Reintentar»;
  - no encontrada: EstadoVacio con «Solicitud no encontrada» y «‹ Solicitudes»;
  - eliminar: DialogoConfirmacion destructivo.
- **Acciones:** Regresar, Enviar, Verificar ahora, Reintentar descarga, Abrir carpeta de la solicitud, Mostrar en Finder y Eliminar solicitud (con confirmación).
- **Cambios respecto de la versión actual:** UX-01, UX-03, UX-05, UX-22, UX-23, UX-24, UX-25, UX-26, UX-27, UX-28, UX-29, UX-30, UX-40.
- **Mockup:** `10-detalle-terminada-*.png`, `11-detalle-error-paquete-*.png`, `12-detalle-creada-sin-efirma-*.png`, `13-detalle-en-proceso-*.png`, `14-detalle-eliminar-*.png`.

### PerfilesSatPage

- **Layout:**
  - EncabezadoPagina `secundaria` con «Nuevo perfil» (primario, `plus`);
  - `SplitView` horizontal: a la izquierda, un `ListView` de FilaPerfil (340; mín. 280, máx. 420; fondo `superficie`); a la derecha, el detalle con scroll, padding 24/16 y una columna de 640 de ancho máximo.
- **Contenido del detalle, en orden de foco:**
  - Grupo «Perfil»:
    - «RFC»: CampoFormulario `soloLectura` con `lock` y la ayuda «El RFC identifica al perfil y no se puede cambiar.» En «Nuevo perfil» es un TextField editable;
    - «Nombre descriptivo»;
    - pie del grupo con «Descartar» y «Guardar» (primario), deshabilitados sin cambios.
  - Grupo «e.firma»:
    - EstadoBadge grande y una línea de estado: «Lista para solicitudes · Vigente hasta 2029-01-01» o «Este perfil no tiene e.firma registrada.»;
    - ayuda: «La e.firma se guarda cifrada y su contraseña queda en el llavero de macOS.» o «Regístrala para poder usar este perfil en solicitudes.»;
    - botón «Reemplazar e.firma…» (secundario) o «Registrar e.firma…» (primario).
    - Con cambios sin guardar, el botón se deshabilita y se muestra «Guarda el perfil antes de gestionar su e.firma.»
- **Estados de la pantalla:**
  - con datos: el primer perfil queda seleccionado;
  - sin perfiles: EstadoVacio en el panel derecho con «Aún no hay perfiles SAT» y «Nuevo perfil»;
  - perfil sin e.firma;
  - e.firma vencida o no vigente: badge de advertencia y «Reemplazar e.firma…» como primario;
  - e.firma dañada o incompleta: badge de error y «Registrar e.firma…» primario;
  - estado no disponible: badge de advertencia y «Reintentar estado» (también en la fila, y con la tecla R);
  - inactivo: el texto de disponibilidad «Inactivo: no disponible para solicitudes»;
  - validación del formulario: «Ya existe un perfil con ese RFC.», «El RFC no es válido.» e «Indica un nombre para el perfil.» bajo su campo;
  - error de carga: EstadoVacio con «No se pudieron cargar los perfiles SAT.» y «Reintentar».
- **Acciones:** Volver (Escape), Nuevo perfil, Guardar, Descartar, Registrar e.firma, Reemplazar e.firma (con confirmación) y Reintentar estado.
- **Cambios respecto de la versión actual:** UX-01, UX-05, UX-31, UX-32, UX-33, UX-34.
- **Mockup:** `15-perfiles-con-datos-*.png`, `16-perfiles-sin-efirma-*.png`.

### EFirmaDialogo

- **Layout:** modal de 480 centrado horizontalmente, a 104 del borde superior de la ventana, sobre `velo`. De arriba a abajo: cabecera, instrucción, tres filas de campos y pie.
- **Contenido, en orden de foco:**
  - textos: título «Registrar e.firma» (o «Reemplazar e.firma»), subtítulo «XEXX010101000 · Proveedor de ejemplo» e instrucción «Elige el certificado (.cer), la llave privada (.key) y escribe la contraseña de la llave.»;
  - foco: «Elegir…» del certificado → «Elegir…» de la llave → «Contraseña» → «Cancelar» → «Registrar».
- **Estados de la pantalla:**
  - formulario;
  - validando: «Validando e.firma…» con BusyIndicator y todo deshabilitado;
  - error: mensaje bajo el campo culpable; la tabla de mensajes de la sección 9 del manual se usa tal cual, con acentos;
  - éxito: AvisoEnLinea `exito` con «e.firma registrada.» y «XEXX010101000 ya está disponible para solicitudes. Vigente hasta 2029-01-01.», y el botón «Cerrar».
  - Reemplazo: antes de abrirse aparece el DialogoConfirmacion «¿Reemplazar la e.firma de XAXX010101000?» con «La e.firma registrada se reemplazará solo si la nueva se valida. Si falla, la actual sigue registrada sin cambios.» y los botones «Cancelar» / «Continuar».
- **Acciones:** Elegir certificado, Elegir llave privada, Registrar o Reemplazar, Cancelar y Cerrar.
- **Cambios respecto de la versión actual:** UX-01, UX-35, UX-36.
- **Mockup:** `17-efirma-formulario-*.png`, `18-efirma-validando-*.png`, `19-efirma-error-*.png`, `20-efirma-exito-*.png`.

### DialogoConfirmacion

- **Layout:** igual que EFirmaDialogo: 480 de ancho, cabecera con un icono de 40 y el título, cuerpo y botones a la derecha.
- **Contenido:**

| Caso | Tono | Título | Cuerpo | Botones (el por omisión va primero) |
|---|---|---|---|---|
| Duplicado | advertencia | ¿Crear otra solicitud con los mismos filtros? | **{motivo}** (p. ej. «Existe una solicitud equivalente terminada sin paquetes.») + «Puedes crear otra solicitud con los mismos filtros o cancelar.» | Cancelar · Crear de todos modos |
| Eliminar | error | ¿Eliminar esta solicitud? | La solicitud se eliminará de esta aplicación. No se modifica nada en el SAT. + «Los ZIP ya descargados se quedan en su carpeta.» | Cancelar · Eliminar solicitud (destructivo) |
| Reemplazar e.firma | advertencia | ¿Reemplazar la e.firma de {RFC}? | La e.firma registrada se reemplazará solo si la nueva se valida. Si falla, la actual sigue registrada sin cambios. | Cancelar · Continuar |

- **Estados de la pantalla:** abierto, procesando y error (AvisoEnLinea dentro del diálogo).
- **Acciones:** las de la tabla.
- **Cambios respecto de la versión actual:** UX-20, UX-25.
- **Mockup:** `08-nueva-solicitud-duplicado-*.png`, `14-detalle-eliminar-*.png`.
- **Pendiente de confirmar:** la frase «Los ZIP ya descargados se quedan en su carpeta.» describe lo que el manual implica («La app no abre ni modifica los ZIP»).

### MenuBar

- **Layout:** menú nativo (`Qt.labs.platform` `SystemTrayIcon` + `Menu`). Su aspecto lo dibuja macOS, así que solo se definen textos, orden, separadores, marcas e iconos de `MenuItem`.
- **Contenido, en orden:**
  1. Mostrar ventana
  2. Nueva solicitud…
  3. Abrir carpeta de paquetes
  4. —
  5. [info, deshabilitado, icono] «Monitoreo activo» (`checkmark.circle`), «Monitoreo en pausa» (`clock`) o «Trabajando: enviando…», «verificando…» o «descargando…» (`arrow.down.circle.dotted`)
  6. [info] «Pendientes: N» (`clock`), solo si N > 0
  7. «Pausar monitoreo» o «Reanudar monitoreo»
  8. —
  9. ✓ Abrir al iniciar sesión
  10. [info] «Estado en macOS: habilitado / deshabilitado / pendiente de aprobación / rechazado / no disponible»
  11. Abrir ajustes de inicio de sesión… (solo si está pendiente o rechazado)
  12. —
  13. [info] «Notificaciones: sin permiso solicitado / permitidas / deshabilitadas / no disponibles»
  14. «Solicitar permiso de notificaciones», «Enviar notificación de prueba» o «Abrir ajustes de notificaciones…», según el caso
  15. —
  16. Salir de SAT CFDI Downloader
- **Estados de la pantalla:** activo, en pausa (con pendientes) y trabajando.
- **Acciones:** las actuales, sin cambios.
- **Cambios respecto de la versión actual:** UX-01, UX-37, UX-38.
- **Mockup:** `21-menubar-activo-*.png`, `22-menubar-pausado-*.png` (ilustraciones).

### Notificaciones

- **Layout:** notificación nativa de macOS: título, cuerpo y, si la API lo permite, subtítulo. Si solo hay título y cuerpo, el cuerpo lleva dos líneas separadas por un salto de línea.
- **Contenido:**

| Evento | Título | Cuerpo, línea 1 | Línea 2 |
|---|---|---|---|
| Solicitud terminada | Solicitud terminada | El SAT terminó la solicitud con N paquetes. | {Tipo} · {periodo} · RFC ***000 |
| Descarga completa | Descarga completa | X de N paquetes descargados. | {Tipo} · {periodo} · RFC ***000 |
| Error en el SAT | Error en el SAT | El SAT reportó un error en la solicitud. | ídem |
| Solicitud rechazada | Solicitud rechazada | El SAT rechazó la solicitud. | ídem |
| Solicitud vencida | Solicitud vencida | La solicitud venció en el SAT; los paquetes pueden ya no estar disponibles. | ídem |
| e.firma | e.firma no disponible | {causa}. El monitoreo de ese perfil está en pausa. | Reemplázala en Perfiles SAT. |

- **Estados de la pantalla:** uno por evento.
- **Acciones:** las actuales (ninguna dentro de la notificación).
- **Cambios respecto de la versión actual:** UX-01, UX-39.
- **Mockup:** `23-notificaciones-varias-*.png` (ilustración).

## Cambios

| ID | Pantalla | Problema actual | Cambio propuesto | Tipo | Prioridad | Requiere logica nueva |
|---|---|---|---|---|---|---|
| UX-01 | Todas | Textos sin acentos ni ñ («Contrasena», «Codigo», «Ultima verificacion», «sesion»). | Usar ortografía completa en todos los textos (ver `## Textos`). Archivos fuente en UTF-8. | texto | alta | no |
| UX-02 | Todas | Solo hay tema claro, con grises planos; fondo y contenido no se distinguen. | Paleta de tokens claro/oscuro: `fondo` para la ventana, `superficie` para el contenido, `superficieElevada` para diálogos. Leer el tema del sistema (`Qt.styleHints.colorScheme`). | visual | alta | no |
| UX-03 | Todas | La etiqueta de estado solo varía por color; hay estados con el mismo color (Creada y Enviada en gris). | EstadoBadge con icono SF Symbol, texto y tono por estado, idéntico en lista, detalle, perfiles y notificaciones. | visual | alta | no |
| UX-04 | Todas | No se ve el foco del teclado. | Anillo `foco` de 2 px con separación de 2 en todo control focuseable; orden de foco definido por pantalla. | accesibilidad | alta | no |
| UX-05 | Todas | Botones del encabezado del mismo peso; «Regresar» parece una acción más. | EncabezadoPagina: «‹ Solicitudes» como navegación a la izquierda, título y acciones a la derecha, con la primaria en el extremo. | layout | alta | no |
| UX-06 | Solicitudes | Filas de texto corrido; difícil comparar estados y periodos. | Tabla con columnas Estado, Contribuyente, Tipo, Periodo, Paquetes y Creada, y encabezado fijo. | layout | alta | no |
| UX-07 | Solicitudes | La fila seleccionada es azul saturado con texto negro (contraste bajo). | Selección con `seleccion` y texto `texto`, más el anillo de foco. | visual | alta | no |
| UX-08 | Solicitudes, Detalle | Fechas ISO repetidas («2026-09-01 a 2026-09-01») y «0 paquete(s)». | Fechas «3 sep 2026», rangos «1–30 sep 2026» y fecha-hora «1 oct 2026, 10:03»; «—» si aún no hay paquetes. | texto | media | no |
| UX-09 | Solicitudes | Solo se ve el RFC; con varios contribuyentes cuesta reconocerlos. | Nombre del perfil en una segunda línea bajo el RFC. | interaccion | media | si: exponer el nombre del perfil en el modelo de la fila |
| UX-10 | Solicitudes | El estado vacío es solo texto. | EstadoVacio con icono, explicación y botón «Nueva solicitud». | visual | media | no |
| UX-11 | Solicitudes | El error de carga no tiene forma definida. | EstadoVacio de error con «Reintentar» enfocado. | visual | media | no |
| UX-12 | Solicitudes | El aviso de notificaciones deshabilitadas no tiene jerarquía. | AvisoEnLinea de advertencia con icono `bell.slash` sobre la tabla. | visual | media | no |
| UX-13 | Solicitudes | No se sabe cuántas solicitudes hay. | Contador junto al título. | visual | baja | no |
| UX-14 | Nueva solicitud | Formulario de ancho completo y sin agrupación; los opcionales se mezclan con los obligatorios. | Dos grupos («Solicitud», «Filtros opcionales») en una columna de 640, con etiquetas a la izquierda y ayudas bajo el control. | layout | alta | no |
| UX-15 | Nueva solicitud | «Tipo de descarga» es un combo para solo dos opciones. | SelectorSegmentado Emitidos/Recibidos con icono. | interaccion | media | no |
| UX-16 | Nueva solicitud | Las fechas ocupan dos columnas anchas. | Periodo en una fila «[fecha] a [fecha]» con la ayuda de formato. | layout | media | no |
| UX-17 | Nueva solicitud | «Crear solicitud» flota bajo el formulario, sin «Cancelar». | Pie fijo con texto de ayuda, «Cancelar» y «Crear solicitud» (por omisión). | layout | alta | no |
| UX-18 | Nueva solicitud | Los avisos de validación no tienen posición ni estilo definidos. | Mensaje bajo el campo, borde `error`, icono y foco en el primer campo inválido. | accesibilidad | alta | no |
| UX-19 | Nueva solicitud | El bloqueo por duplicado no tiene un patrón visual. | AvisoEnLinea de error arriba del formulario, con motivo y «Ver solicitud existente»; «Crear solicitud» deshabilitado. | interaccion | alta | no |
| UX-20 | Diálogo de duplicado | «Crear de todos modos» es el botón azul por omisión; el motivo no destaca. | Título en pregunta, motivo en negrita y «Cancelar» por omisión (Enter/Escape). | interaccion | alta | no |
| UX-21 | Nueva solicitud | «Sin perfiles listos» se muestra como un aviso más. | EstadoVacio con «Administrar perfiles SAT» como primario. | visual | media | no |
| UX-22 | Detalle | Estado local, Estado SAT y Resumen se apilan sin decir qué pasó ni qué sigue. | ResumenEstado: badge grande, titular y descripción por estado, conteo de paquetes y 4 datos clave. | layout | alta | no |
| UX-23 | Detalle | Una página larga con datos, paquetes e historial. | Pestañas Paquetes / Datos / Historial, con Paquetes por omisión si hay paquetes. | layout | alta | no |
| UX-24 | Detalle | Las acciones (Enviar, Verificar ahora, Reintentar descarga) están en el encabezado junto a las destructivas. | Una acción principal por estado dentro del resumen; si está deshabilitada se ve el motivo y el resultado aparece debajo. | interaccion | alta | no |
| UX-25 | Detalle | «Eliminar solicitud» está junto a «Reintentar descarga» con el mismo estilo. | «Eliminar…» destructivo, separado por una línea vertical; el diálogo pone «Cancelar» por omisión. | interaccion | alta | no |
| UX-26 | Detalle | Paquetes con badge a la derecha, mensaje rojo suelto y un «Mostrar en Finder» deshabilitado sin explicación. | FilaPaquete: badge a la izquierda, nombre en mono, metadatos, mensaje con icono y tono, y acción alineada; el motivo de deshabilitado queda visible. | visual | media | no |
| UX-27 | Detalle | «Vencido 2026-10-04 10:00» se lee como estado y no como fecha. | «Venció 4 oct 2026, 10:00»; «Código SAT 5007» se mantiene. | texto | media | no |
| UX-28 | Detalle | Etiquetas largas que se parten en dos líneas («Codigo de verificacion SAT»). | Pestaña Datos con secciones Estado / SAT / Solicitud / Filtros; columna de etiquetas de 200 y valores seleccionables en mono. | layout | media | no |
| UX-29 | Detalle | Historial con «origen Usuario» como texto plano. | EventoHistorial con icono de origen (persona o engrane). | visual | baja | no |
| UX-30 | Detalle | No hay texto que explique cada estado. | Titulares y descripciones por estado (tabla en DetalleSolicitudPage). | texto | media | no |
| UX-31 | Perfiles SAT | Lista y formulario sin divisor; la selección es azul con texto negro. | SplitView redimensionable; FilaPerfil con RFC, badge, nombre, disponibilidad y vigencia; selección con `seleccion`. | layout | alta | no |
| UX-32 | Perfiles SAT | El RFC no editable parece un campo normal. | Campo de solo lectura con candado, borde punteado y ayuda. | visual | media | no |
| UX-33 | Perfiles SAT | Descartar/Guardar flotan sin pertenecer a un grupo. | Dentro del grupo «Perfil», en su pie. | layout | media | no |
| UX-34 | Perfiles SAT | La e.firma es una franja gris con un botón; sin e.firma no está claro qué hacer. | Grupo «e.firma» con badge grande, explicación y acción: «Registrar e.firma…» primario si falta o está dañada, «Reemplazar e.firma…» si existe. | interaccion | alta | no |
| UX-35 | Diálogo e.firma | Botones de archivo de ancho distinto con el nombre suelto; sin confirmación visual. | Filas etiqueta / CampoArchivo / «Elegir…» alineadas, check de archivo elegido y ayuda del llavero. | layout | alta | no |
| UX-36 | Diálogo e.firma | Los estados validando, error y éxito no tienen diseño. | Validando con BusyIndicator y todo deshabilitado; error bajo el campo con foco; éxito con aviso y «Cerrar». | interaccion | alta | no |
| UX-37 | Menu bar | «Nueva solicitud» abre la ventana sin indicarlo; «Iniciar al iniciar sesion» es redundante; «Salir» no dice qué. | «Nueva solicitud…», «Abrir al iniciar sesión», «Salir de SAT CFDI Downloader» y «Monitoreo en pausa». | texto | media | no |
| UX-38 | Menu bar | Las líneas informativas son texto gris sin jerarquía. | Icono en las líneas informativas (`MenuItem.icon`). | visual | baja | no |
| UX-39 | Notificaciones | El cuerpo repite el título («Descarga completa: 1 de 1.») y mezcla resultado con contexto. | Título = evento; línea 1 = resultado; línea 2 = «Recibidos · 3 sep 2026 · RFC ***000». | texto | media | no |
| UX-40 | Todas | No hay nombres accesibles definidos para badges, filas ni botones de icono. | `Accessible.name` y rol definidos en cada componente (ver `## Componentes`). | accesibilidad | alta | no |
| UX-41 | Todas | No hay un orden de foco definido. | Orden por pantalla (ver `## Pantallas`); flechas en listas, pestañas y segmentado. | accesibilidad | media | no |
| UX-42 | Todas | Sin tema oscuro. | Tema oscuro completo con los tokens `dark`. | visual | media | no |

## Textos

| Clave | Texto actual | Texto propuesto | Donde aparece |
|---|---|---|---|
| global.acentos | Contrasena, Codigo, verificacion, Ultima, Ultimo, sesion, notificacion, Envio, Nomina, valido, esta | Contraseña, Código, verificación, Última, Último, sesión, notificación, Envío, Nómina, válido, está | Toda la app |
| lista.titulo | Solicitudes | Solicitudes {N} | Encabezado de Solicitudes |
| lista.vacia.descripcion | Crea una nueva solicitud para comenzar. | Crea una solicitud para descargar del SAT los CFDI emitidos o recibidos de un contribuyente. | Lista vacía |
| lista.error.descripcion | (sin texto) | Tus solicitudes siguen guardadas en este equipo. Intenta cargarlas de nuevo. | Error de carga de la lista |
| lista.notificaciones | Las notificaciones estan deshabilitadas. Activalas en Ajustes del Sistema para recibir avisos. | Las notificaciones están desactivadas. / Actívalas en Ajustes del Sistema para recibir avisos cuando una solicitud termine o falle. | Aviso en Solicitudes |
| lista.fila.resumen | Recibidos · 2026-09-03 a 2026-09-03 · 3 paquete(s) | Columnas: Recibidos · 3 sep 2026 · 3 | Fila de solicitud |
| lista.fila.creada | Creada 2026-10-01 10:03 | 1 oct 2026, 10:03 | Columna Creada |
| comun.regresar | Regresar | ‹ Solicitudes | Encabezado de Nueva solicitud y Detalle |
| perfiles.volver | Volver a solicitudes | ‹ Solicitudes | Encabezado de Perfiles SAT |
| nueva.perfil.ayuda | (sin texto) | Solo aparecen perfiles activos con la e.firma lista. | Nueva solicitud, Perfil SAT |
| nueva.fechas | Fecha inicial (AAAA-MM-DD) / Fecha final (AAAA-MM-DD) | Periodo: [fecha] a [fecha], con la ayuda «Formato AAAA-MM-DD. Incluye ambos días.» | Nueva solicitud |
| nueva.rfcContraparte | RFC contraparte (opcional) | RFC contraparte (placeholder «Cualquiera»; grupo «Filtros opcionales») | Nueva solicitud |
| nueva.tipoComprobante | Tipo de comprobante (opcional) | Tipo de comprobante | Nueva solicitud |
| nueva.complemento | Complemento (opcional) | Complemento (placeholder «Cualquiera») | Nueva solicitud |
| nueva.pie | (sin texto) | Al crearla, la app la envía al SAT con la e.firma del perfil. | Pie de Nueva solicitud |
| nueva.cancelar | Regresar | Cancelar | Pie de Nueva solicitud |
| duplicado.titulo | Solicitud posiblemente duplicada | ¿Crear otra solicitud con los mismos filtros? | Diálogo de duplicado |
| eliminar.titulo | (sin título) | ¿Eliminar esta solicitud? | Diálogo de eliminar |
| eliminar.extra | (sin texto) | Los ZIP ya descargados se quedan en su carpeta. | Diálogo de eliminar |
| detalle.eliminar | Eliminar solicitud | Eliminar… (nombre accesible «Eliminar solicitud») | Encabezado del Detalle |
| detalle.titulo | Detalle de solicitud | Solicitud + subtítulo «{RFC} · {Tipo} · {periodo}» | Encabezado del Detalle |
| detalle.paquete.vencido | Vencido 2026-10-04 10:00 | Venció 4 oct 2026, 10:00 | Metadatos de paquete |
| detalle.paquete.vencidoMsg | El paquete ya no existe en el SAT (vencido). | El paquete ya no existe en el SAT (vencido). Crea una solicitud nueva para el mismo periodo. | Paquete Vencido |
| detalle.paquete.noEncontrado | Archivo local no encontrado | Archivo local no encontrado: el ZIP se movió o se borró fuera de la app. | Paquete Descargado |
| detalle.paquete.disponible | (sin texto) | La app lo descargará automáticamente. | Paquete Disponible |
| detalle.codigoSolicitud | 5000 - Solicitud Aceptada | 5000 · Solicitud aceptada | Datos |
| detalle.resumen.* | (sin texto) | Titulares y descripciones de la tabla de DetalleSolicitudPage | ResumenEstado |
| efirma.instruccion | Elige el certificado (.cer), la llave privada (.key) y escribe su contrasena. | Elige el certificado (.cer), la llave privada (.key) y escribe la contraseña de la llave. | Diálogo e.firma |
| efirma.botones | Elegir certificado (.cer) / Elegir llave privada (.key) | Etiqueta «Certificado (.cer)» / «Llave privada (.key)» + botón «Elegir…» | Diálogo e.firma |
| efirma.contrasena | Contrasena de la llave privada | Contraseña (ayuda «Se guarda en el llavero de macOS.») | Diálogo e.firma |
| efirma.exito | e.firma registrada. | e.firma registrada. / XEXX010101000 ya está disponible para solicitudes. Vigente hasta 2029-01-01. | Diálogo e.firma |
| perfiles.efirma.lista | Listo para solicitudes | Lista para solicitudes · Vigente hasta {fecha} | Grupo e.firma |
| perfiles.efirma.ayuda | (sin texto) | La e.firma se guarda cifrada y su contraseña queda en el llavero de macOS. | Grupo e.firma |
| perfiles.efirma.registrar | Registrar e.firma | Registrar e.firma… | Grupo e.firma |
| perfiles.efirma.reemplazar | Reemplazar e.firma | Reemplazar e.firma… | Grupo e.firma |
| perfiles.reemplazo.titulo | (sin título) | ¿Reemplazar la e.firma de {RFC}? | Confirmación de reemplazo |
| estado.envioFallido | Envio fallido | Envío fallido | Badge |
| estado.envioIncierto | Envio incierto | Envío incierto | Badge |
| estado.efirmaNoVigente | e.firma aun no vigente | e.firma aún no vigente | Badge |
| estado.efirmaDanada | e.firma danada en el llavero | e.firma dañada en el llavero | Badge |
| menu.nueva | Nueva solicitud | Nueva solicitud… | Menu bar |
| menu.inicio | Iniciar al iniciar sesion | Abrir al iniciar sesión | Menu bar |
| menu.pausado | Monitoreo pausado | Monitoreo en pausa | Menu bar |
| menu.salir | Salir | Salir de SAT CFDI Downloader | Menu bar |
| notif.descarga.cuerpo | Descarga completa: 1 de 1. Recibidos del 2026-09-03 al 2026-09-03, RFC ***000. | 1 de 1 paquetes descargados. / Recibidos · 3 sep 2026 · RFC ***000 | Notificación |
| notif.efirma.titulo | (título actual de la notificación de e.firma) | e.firma no disponible | Notificación |

## Mockups

Todas las imágenes son PNG de 1920 × 1280 (960 × 640 a 2x), en tema claro y oscuro. Las marcadas como «Ilustración» dibujan elementos de macOS. Se renderizaron con Inter como sustituto de SF Pro y DejaVu Sans Mono en lugar de SF Mono; en macOS se verán con las fuentes del sistema.

- `01-solicitudes-con-datos-light.png` / `01-solicitudes-con-datos-dark.png`: lista con datos, fila Terminada seleccionada y con foco.
- `02-solicitudes-vacia-light.png` / `02-solicitudes-vacia-dark.png`: lista vacía.
- `03-solicitudes-error-light.png` / `03-solicitudes-error-dark.png`: error de carga con Reintentar.
- `04-solicitudes-sin-notificaciones-light.png` / `04-solicitudes-sin-notificaciones-dark.png`: aviso de notificaciones desactivadas.
- `05-nueva-solicitud-formulario-light.png` / `05-nueva-solicitud-formulario-dark.png`: formulario inicial, foco en Perfil SAT.
- `06-nueva-solicitud-error-validacion-light.png` / `06-nueva-solicitud-error-validacion-dark.png`: error de validación en fecha final.
- `07-nueva-solicitud-bloqueada-light.png` / `07-nueva-solicitud-bloqueada-dark.png`: duplicado bloqueado (aviso en línea).
- `08-nueva-solicitud-duplicado-light.png` / `08-nueva-solicitud-duplicado-dark.png`: diálogo de duplicado que requiere confirmación.
- `09-nueva-solicitud-sin-perfiles-light.png` / `09-nueva-solicitud-sin-perfiles-dark.png`: sin perfiles listos.
- `10-detalle-terminada-light.png` / `10-detalle-terminada-dark.png`: Terminada con paquetes Descargado, Disponible y Vencido.
- `11-detalle-error-paquete-light.png` / `11-detalle-error-paquete-dark.png`: paquete con error de descarga y archivo local no encontrado.
- `12-detalle-creada-sin-efirma-light.png` / `12-detalle-creada-sin-efirma-dark.png`: Creada con Enviar deshabilitado por falta de e.firma (pestaña Datos).
- `13-detalle-en-proceso-light.png` / `13-detalle-en-proceso-dark.png`: En proceso SAT (pestaña Historial).
- `14-detalle-eliminar-light.png` / `14-detalle-eliminar-dark.png`: confirmación de eliminar.
- `15-perfiles-con-datos-light.png` / `15-perfiles-con-datos-dark.png`: perfiles con e.firma lista.
- `16-perfiles-sin-efirma-light.png` / `16-perfiles-sin-efirma-dark.png`: perfil sin e.firma.
- `17-efirma-formulario-light.png` / `17-efirma-formulario-dark.png`: diálogo de e.firma.
- `18-efirma-validando-light.png` / `18-efirma-validando-dark.png`: validando.
- `19-efirma-error-light.png` / `19-efirma-error-dark.png`: contraseña incorrecta.
- `20-efirma-exito-light.png` / `20-efirma-exito-dark.png`: e.firma registrada.
- `21-menubar-activo-light.png` / `21-menubar-activo-dark.png`: menú del menu bar, monitoreo activo (ilustración).
- `22-menubar-pausado-light.png` / `22-menubar-pausado-dark.png`: monitoreo en pausa con pendientes (ilustración).
- `23-notificaciones-varias-light.png` / `23-notificaciones-varias-dark.png`: notificaciones de macOS (ilustración).

## Sugerencias

### SUG-01: Barra lateral con vistas de la lista

- **Problema u oportunidad:** la app navega entre páginas apiladas con un botón «Regresar». Con varios contribuyentes y decenas de solicitudes, la lista única crece sin forma de enfocarse en lo que requiere atención.
- **Propuesta:** ventana con `SplitView` de tres zonas:
  - barra lateral con «Requieren atención» (Error SAT, Rechazada, Envío fallido, Envío incierto, paquetes con error), «En curso», «Terminadas», «Todas» y, debajo, un elemento por perfil SAT;
  - lista en el centro;
  - detalle a la derecha, sin cambiar de página.
  - Perfiles SAT pasa a ser un elemento de la barra lateral.
- **Beneficio:** el contador ve de un vistazo qué falla y por qué contribuyente, sin abrir cada solicitud.
- **Costo estimado:** alto. Rehace la navegación y el layout de tres paneles a 960 de ancho.
- **Requiere logica nueva:** si: filtros por estado y por perfil en el modelo, y conteos por vista.
- **Riesgos o dudas:** a 960 × 640 el detalle queda estrecho; podría abrirse como hoja o en una ventana aparte.
- **Mockup opcional:** —

### SUG-02: Filtros locales y búsqueda en la lista

- **Problema u oportunidad:** no hay forma de encontrar «las de XAXX de agosto».
- **Propuesta:** un campo de búsqueda (RFC o nombre) y menús de filtro por Estado, Tipo y mes del periodo en el encabezado de Solicitudes. Los filtros se recuerdan por sesión.
- **Beneficio:** acceso rápido en listas largas.
- **Costo estimado:** medio. Requiere un proxy de filtrado sobre el modelo.
- **Requiere logica nueva:** si: modelo de filtrado local (`SortFilterProxyModel`).
- **Riesgos o dudas:** ninguno relevante.
- **Mockup opcional:** —

### SUG-03: Progreso de descarga en la lista

- **Problema u oportunidad:** «Terminada · 3» no dice si los paquetes ya están en el equipo.
- **Propuesta:** la columna Paquetes muestra «2/3» con una barra de 40 pt, o un check si todo está descargado. La fila de una solicitud Terminada con paquetes Disponibles o con Error lleva un indicador «Pendiente de descarga».
- **Beneficio:** distingue «terminada y lista» de «terminada pero incompleta» sin abrir el detalle.
- **Costo estimado:** bajo.
- **Requiere logica nueva:** si: exponer el conteo de paquetes por estado en el modelo de la lista.
- **Riesgos o dudas:** ninguno.
- **Mockup opcional:** —

### SUG-04: Atajos de periodo y selector de fecha

- **Problema u oportunidad:** escribir AAAA-MM-DD es propenso a errores, y la mayoría de las solicitudes de un contador son por mes.
- **Propuesta:** junto al periodo, botones «Mes actual», «Mes anterior» y «Mismo mes, año anterior», más un calendario emergente en cada campo de fecha. Los campos siguen aceptando texto.
- **Beneficio:** menos validaciones fallidas y altas más rápidas.
- **Costo estimado:** bajo para los atajos, medio para el calendario (Qt Quick no trae DatePicker).
- **Requiere logica nueva:** si: cálculo de rangos y un componente de calendario.
- **Riesgos o dudas:** el SAT limita los rangos que entregan muchos CFDI (5003); el atajo no debería proponer rangos que excedan lo que la app recomienda.
- **Mockup opcional:** —

### SUG-05: Solicitudes por lote (varios meses o ambos tipos)

- **Problema u oportunidad:** regularizar años anteriores implica crear 24 solicitudes (12 meses × Emitidos/Recibidos) una por una.
- **Propuesta:** en Nueva solicitud, la opción «Dividir por mes» y la casilla «Emitidos y Recibidos». Se previsualiza la lista de solicitudes que se crearán, se detectan duplicados por cada una y se crean juntas.
- **Beneficio:** ahorra decenas de altas y reduce el error 5003 al partir rangos largos.
- **Costo estimado:** alto. Afecta la creación, la detección de duplicados y la cola de envío.
- **Requiere logica nueva:** si: creación múltiple, previsualización y manejo del límite diario del SAT (5011).
- **Riesgos o dudas:** el límite diario puede dejar parte del lote sin enviar; hay que mostrar ese estado.
- **Mockup opcional:** —

### SUG-06: Notificaciones accionables

- **Problema u oportunidad:** al ver una notificación, el usuario tiene que buscar la solicitud.
- **Propuesta:** un clic en la notificación abre el detalle de esa solicitud. Las de «Descarga completa» llevan el botón «Mostrar en Finder»; las de e.firma, «Abrir Perfiles SAT».
- **Beneficio:** un paso menos en cada aviso.
- **Costo estimado:** medio. Requiere manejar la respuesta de la notificación en macOS.
- **Requiere logica nueva:** si: identificador de la solicitud en la notificación y manejo de las acciones.
- **Riesgos o dudas:** el soporte depende de la API de notificaciones que use la app.
- **Mockup opcional:** —

### SUG-07: Icono del menu bar con estado

- **Problema u oportunidad:** el icono es igual con el monitoreo activo, en pausa o con errores.
- **Propuesta:** icono plantilla con variantes: normal, en pausa (con barras), trabajando (flecha) y atención (punto) cuando hay solicitudes con error o una e.firma no lista.
- **Beneficio:** el estado se ve sin abrir el menú.
- **Costo estimado:** bajo.
- **Requiere logica nueva:** si: derivar el estado agregado y cambiar `SystemTrayIcon.icon`.
- **Riesgos o dudas:** ninguno.
- **Mockup opcional:** —

### SUG-08: Primer uso guiado

- **Problema u oportunidad:** una instalación nueva muestra «No hay solicitudes», pero no se puede crear ninguna sin un perfil con e.firma.
- **Propuesta:** el estado vacío de la lista muestra 3 pasos con check: 1) Crear un perfil SAT, 2) Registrar su e.firma y 3) Crear la primera solicitud. Cada paso abre su pantalla y se marca solo al cumplirse.
- **Beneficio:** evita el callejón sin salida de «No hay perfiles SAT listos».
- **Costo estimado:** bajo.
- **Requiere logica nueva:** si: consultar si existe algún perfil y alguna e.firma lista.
- **Riesgos o dudas:** ninguno.
- **Mockup opcional:** —

### SUG-09: Reintento por paquete

- **Problema u oportunidad:** «Reintentar descarga» actúa sobre toda la solicitud.
- **Propuesta:** un botón «Reintentar» en cada FilaPaquete con Error reintentable.
- **Beneficio:** más control cuando un solo paquete falla.
- **Costo estimado:** bajo.
- **Requiere logica nueva:** si: reintento de un paquete individual.
- **Riesgos o dudas:** el límite de descargas del SAT por paquete.
- **Mockup opcional:** —

### SUG-10: Aviso previo de vencimiento de e.firma

- **Problema u oportunidad:** hoy la e.firma avisa cuando ya dejó de estar lista.
- **Propuesta:** 30 y 7 días antes de «Vigente hasta», un badge de advertencia «Vence en N días» en Perfiles SAT y una notificación.
- **Beneficio:** evita que el monitoreo se detenga por vencimiento.
- **Costo estimado:** bajo.
- **Requiere logica nueva:** si: comparar la vigencia con la fecha actual y un nuevo tipo de notificación.
- **Riesgos o dudas:** ninguno.
- **Mockup opcional:** —

### SUG-11: «Ver solicitud existente» en el diálogo de duplicado

- **Problema u oportunidad:** el diálogo dice que existe una solicitud equivalente, pero no deja verla antes de decidir.
- **Propuesta:** un tercer botón «Ver solicitud existente», a la izquierda del diálogo.
- **Beneficio:** el usuario decide con información.
- **Costo estimado:** bajo.
- **Requiere logica nueva:** si: exponer el id de la solicitud equivalente también en el caso de confirmación (la eliminada localmente no tendría detalle).
- **Riesgos o dudas:** el caso «que eliminaste localmente» no tiene a dónde ir.
- **Mockup opcional:** —

### SUG-12: Atajos de teclado

- **Problema u oportunidad:** el uso diario es repetitivo.
- **Propuesta:** ⌘N Nueva solicitud, ⌘R Verificar ahora o Reintentar (la acción principal del resumen), ⌘⌫ Eliminar…, ⌘F buscar (con SUG-02), ⌘, Perfiles SAT, y ⌘[ o Escape para regresar. Se muestran en ToolTips y en el menú.
- **Beneficio:** velocidad para usuarios frecuentes.
- **Costo estimado:** bajo.
- **Requiere logica nueva:** si: `Shortcut` globales y su registro en el menú.
- **Riesgos o dudas:** conflictos con los atajos del sistema.
- **Mockup opcional:** —

### SUG-13: Copiar identificadores

- **Problema u oportunidad:** el Id solicitud SAT y el nombre del paquete se usan en reclamaciones al SAT.
- **Propuesta:** un botón de icono «Copiar» junto a cada identificador en Datos y en FilaPaquete.
- **Beneficio:** evita errores al transcribir.
- **Costo estimado:** bajo.
- **Requiere logica nueva:** si: acceso al portapapeles.
- **Riesgos o dudas:** ninguno.
- **Mockup opcional:** —

## Fuera de alcance

- **Visor o procesamiento de XML de los CFDI** (tablas de importes, validación, búsqueda dentro del ZIP): el producto solo descarga y la app no abre ni modifica los ZIP.
- **Reportes, conciliación o resúmenes fiscales:** implican procesar XML y salen del propósito de descarga.
- **Cuentas, inicio de sesión o sincronización en la nube:** todo es local por diseño, y la e.firma no debe salir del equipo.
- **Multiusuario, roles o permisos:** es una app personal de un solo usuario.
- **Licencias, planes, pagos o promociones dentro de la app:** no es comercial.
- **Integración con sistemas contables o envío de paquetes por correo:** requiere servicios externos y procesamiento de datos.
- **Descarga desde el portal web del SAT o por otros medios:** el producto usa el servicio de descarga masiva con e.firma.

## Preguntas abiertas

1. **Acentos:** ¿la ausencia de acentos y ñ en los textos actuales es deliberada (por ejemplo, por problemas de codificación en el pipeline de traducción)? UX-01 asume que se puede usar UTF-8.
2. **Estilo de Qt Quick Controls:** ¿se usará el estilo `macOS` nativo o `Basic` personalizado? Los badges, el segmentado y las filas se dibujan a mano en ambos casos, pero el estilo nativo limita el color de botones y campos. Recomiendo `Basic` + `Theme.qml` con estos tokens, y controles nativos solo en el menú y los selectores de archivo.
3. **Orden de la lista:** el manual dice «de la más reciente a la más antigua», pero la captura muestra 10:01 arriba y 10:04 abajo. Los mockups siguen al manual.
4. **Formato de fecha:** ¿se prefiere el formato legible («3 sep 2026») en lugar de ISO en toda la vista? Los campos de captura siguen en AAAA-MM-DD.
5. **Estado local:** ¿se necesita mostrarlo? En la captura dice «Enviada» mientras el Resumen dice «Terminada», lo que confunde. Lo dejé solo en la pestaña Datos.
6. **Código SAT en un paquete con error:** la captura muestra «Codigo SAT 5000» en un paquete con «Error de descarga». ¿Es el código de la descarga o el de la solicitud? Si no aporta, conviene ocultarlo.
7. **Icono de la app:** no tengo el icono real; las ilustraciones de notificación usan un marcador genérico.
8. **Nombre del perfil en la lista (UX-09):** ¿el modelo de la lista puede exponerlo sin costo, o se deja solo el RFC?
9. **Eliminar solicitud:** ¿confirmas que los ZIP ya descargados se conservan al eliminar? El diálogo propuesto lo afirma.
