Sistema de diseño de **SAT CFDI Downloader**: una app de escritorio para macOS (Qt 6 / QML) con la que un contador descarga del SAT, de forma masiva, los CFDI de sus contribuyentes. Las vistas previas son HTML de referencia. La implementación es QML: cada token se traduce a una propiedad de un `Theme.qml` y cada componente, a un archivo QML con el mismo nombre. El documento de traspaso completo (tokens, componentes, pantallas, cambios UX-NN, textos, sugerencias y preguntas) es la sección **Traspaso UI/UX**.

## Principios

- **Herramienta de trabajo, no escaparate.** Grises de sistema, un solo acento (`acento`) para la acción principal, y color solo para comunicar estado.
- **El estado nunca va solo en color.** Todo estado se muestra con `EstadoBadge`: tono, icono SF Symbol y texto. Es el mismo badge en la lista, el detalle, los perfiles y las notificaciones.
- **Una acción principal por vista.** Va como `BotonAccion` primario en el extremo derecho del encabezado o dentro de `ResumenEstado`. Las destructivas se separan y piden confirmación con `Cancelar` por omisión.
- **Qué pasó y qué sigue.** Cada estado tiene un titular y una descripción en lenguaje claro. Los mensajes del SAT llevan su código entre paréntesis.

## Contenido y tono

- Español de México, con tuteo: «Elige», «Regístrala», «Puedes reintentar».
- Ortografía completa, con acentos y ñ: «Contraseña», «Código», «Envío».
- Mayúscula solo al inicio y en siglas: «Nueva solicitud», «Perfiles SAT», «e.firma» siempre en minúscula.
- Las acciones que abren un diálogo terminan en «…»: «Eliminar…», «Registrar e.firma…», «Elegir…».
- Fechas visibles como «3 sep 2026» y «1 oct 2026, 10:03». Los rangos del mismo mes, como «1–30 sep 2026». La captura sigue en AAAA-MM-DD.
- RFC e identificadores van completos. En las notificaciones el RFC se abrevia a sus últimos tres caracteres (`***000`).
- Sin emoji ni signos de exclamación. Solo datos ficticios en los ejemplos: `XAXX010101000`, `XEXX010101000`, `demo.cer`, `demo.key`.

## Color

- Ventana y encabezado en `fondo`; listas, tablas, grupos y tarjetas en `superficie`; diálogos, menús y notificaciones en `superficieElevada` con `sombra-dialogo`.
- Texto en `texto`; metadatos, etiquetas de columna y ayudas en `textoSecundario`. Ambos cumplen 4.5:1 sobre todas las superficies, en claro y en oscuro.
- `borde` (≥3:1) delimita campos de texto y combos. `separador` es solo decorativo: líneas entre filas y grupos.
- `acento` con `textoSobreAcento` es exclusivo del botón primario y del indicador de progreso. Los enlaces y el botón «‹ Solicitudes» usan `acentoTexto`.
- Fila seleccionada: `seleccion` con `texto` normal. Nunca texto negro sobre azul saturado.
- Foco: anillo sólido de 2 px en `foco`, con 2 px de separación. Cumple ≥3:1 en todas las superficies.
- Estados: cinco tonos (`tono-neutro`, `tono-progreso`, `tono-exito`, `tono-advertencia`, `tono-error`), cada uno con `-fondo`, `-texto` y `-borde`. Los tokens `estado-<Clave>-*` son alias de su tono; usa siempre el alias del estado, no el tono directo, para que el mapeo viva en un solo lugar.
- Mensajes sueltos (sin píldora) en `exito`, `advertencia`, `error` o `info`, siempre con icono.

## Tipografía

- Fuente del sistema (SF Pro); `mono` (SF Mono) solo para identificadores.
- Estilos de texto:
  - `titulo` (20/600): título de página;
  - `subtitulo` (15/600): titulares de resumen y de diálogo;
  - `cuerpo` (13/400): texto general;
  - `cuerpoFuerte` (13/600): RFC y valores clave;
  - `leyenda` (12/400): metadatos y ayudas;
  - `etiqueta` (11/600): badges y encabezados de columna o de grupo.
- No uses tamaños fuera de esta escala.

## Espacio, forma y medidas

- Espaciado en pasos de 4: `espacio-xs` 4, `espacio-s` 8, `espacio-m` 12, `espacio-l` 16, `espacio-xl` 24 y `espacio-xxl` 32. El margen lateral de página es `espacio-l`; la separación entre secciones, `espacio-xl`.
- Radios:
  - `radio-control` (6): botones y campos;
  - `radio-tarjeta` (10): grupos y tarjetas;
  - `radio-badge` (10): píldoras;
  - `radio-dialogo` (12): diálogos y menús.
- Alturas: controles de 28 (`alto-control`), badge de 20, encabezado de 52 y filas de 56.
- Ventana mínima de 960 × 640. El formulario se centra en una columna de 640 (`ancho-formulario`); el detalle y la lista usan todo el ancho.
- Sombra solo en `superficieElevada` (`sombra-dialogo`) y, en claro, una sutil en tarjetas (`sombra-tarjeta`). En oscuro, las tarjetas se separan con `separador`.
- Sin desenfoques, vidrio, degradados ni animaciones por elemento. Las transiciones de página son las de `StackView`.

## Iconografía

- SF Symbols en macOS. Cada estado declara su símbolo en `tokens.json` (en la nota de uso) y en el traspaso.
- El grupo de assets **Iconos** contiene equivalentes de línea propios (24 × 24, trazo de 1.6, puntas redondas) por si no se pueden usar SF Symbols. Están dibujados en tinta `#1D1D1F`: tíñelos con `icon.color` o con la colorización de `MultiEffect`.
- Tamaños de icono: 13 en badges, 16 en botones, 18 en avisos, 48 en estados vacíos.

## Componentes y pantallas

- Cada componente tiene su ficha: propósito, variantes, anatomía con tokens, estados, medidas y accesibilidad.
- Las pantallas (`SolicitudesPage`, `NuevaSolicitudPage`, `DetalleSolicitudPage`, `PerfilesSatPage`, el diálogo de e.firma, `MenuBar` y `Notificaciones`) se muestran en todos sus estados.
- El grupo de assets **Mockups** reúne los PNG de 1920 × 1280 en claro y oscuro, con el nombre `NN-<pantalla>-<estado>-<light|dark>.png`.
