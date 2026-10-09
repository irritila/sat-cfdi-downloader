# T013: Rediseno visual de la UI

## Estado

Completada (2026-10-09). Refinada e implementada en la rama `t013-rediseno-visual`.

## Prioridad y tamano

- Prioridad: Alta.
- Tamano: Grande.

## Objetivo

Aplicar a la app el rediseno de UI/UX entregado por Claude Design (cambios `UX-01`..`UX-42`): tema claro y oscuro, componentes, las cuatro pantallas, dialogos, menu bar, notificaciones, textos y accesibilidad, sin funcionalidad nueva salvo el nombre del perfil en la lista (UX-09).

## Contexto

El manual de T012 mostro la UI actual; con el se encargo un rediseno a Claude Design (sistema de diseno https://claude.ai/artifact/54Wo54qnQkxoqdH6wj2Bwg). El usuario reviso la entrega el 2026-10-09: los 42 cambios `UX-NN` entran en esta tarea y las 11 sugerencias aceptadas (`SUG-NN`) van en T014. Registro: `docs/meetings/UI-UX-claude-design/revision-sugerencias.md`.

## Decisiones

| ID | Decision | Estado | Fuente y motivo |
| --- | --- | --- | --- |
| D1 | Estilo de Qt Quick Controls `Basic` en toda la app, personalizado con los tokens. Menu bar y `FileDialog` siguen nativos. | Aprobada por usuario | `qt-architecture-lead`: Basic permite controlar color, foco, seleccion y geometria del diseno. |
| D2 | La entrega se versiona en `docs/design/ui-ux-v2/`: `README.md`, `traspaso.md`, `tokens.json`, `design-system.json` y los 46 mockups PNG (claro y oscuro). Es la referencia de esta tarea; los mockups no se empaquetan en la app. | Aprobada por usuario | Refinamiento 2026-10-09. |
| D3 | `Theme.qml` es un singleton del modulo QML con todos los tokens (colores claro/oscuro, alias por estado, tipografia, espaciado, radios, alturas). Sigue `Qt.styleHints.colorScheme`. Los valores se copian de `tokens.json` a mano, sin generacion en build; una prueba comprueba que coinciden. | Recomendacion tecnica | `qt-architecture-lead`: mas revisable y proporcional; la prueba evita que diverjan. |
| D4 | Iconos: los SVG de linea del grupo Iconos de la entrega, como recursos del modulo en `src/presentation/qml/assets/icons/`, cargados por un componente `Icono.qml` que los tine con el token del contexto. No se usan SF Symbols desde QML. | Recomendacion tecnica | `qt-architecture-lead`. |
| D5 | Fechas visibles en formato legible `es_MX` (`3 sep 2026`, `1 oct 2026, 10:03`, rangos `1-30 sep 2026` con guion largo); la captura sigue en `AAAA-MM-DD`. Las fechas sin hora se formatean sin conversion de zona. | Recomendacion tecnica | Pregunta 4 de Claude Design; `qt-architecture-lead`. |
| D6 | Textos con ortografia completa (acentos y n) en UTF-8, en QML (`qsTr`) y en los textos de presentacion C++ (`tr()` o `QCoreApplication::translate`), incluidos `Etiquetas.js`, `CatalogoMensajes` y los textos del menu bar y de notificaciones (UX-01, UX-37, UX-39). Los textos del dominio y de logs no cambian. | Recomendacion tecnica | Pregunta 1 de Claude Design; no hay regla que lo impida. |
| D7 | Un solo estado visible en el resumen del detalle; el estado local aparece solo en la pestana Datos. | Aprobada por usuario | Pregunta 5 de Claude Design. |
| D8 | La lista muestra RFC y, debajo, el nombre del perfil (UX-09): nuevo rol `perfilNombre` en el modelo de la lista, propagado desde la consulta. Es el unico cambio de modelo de la tarea. | Aprobada por usuario | Pregunta 8 de Claude Design. |
| D9 | La herramienta de T012 agrega una opcion de tema oscuro (`--tema oscuro`) para generar las mismas pantallas en oscuro en una carpeta de evidencia no versionada; el manual sigue en claro y se regenera al final. | Aprobada por usuario | `qt-quality-engineer`: las pruebas `offscreen` no prueban fidelidad visual. |
| D10 | Los `objectName` que usan las pruebas se conservan; si un control desaparece o cambia de papel, sus pruebas se actualizan en el mismo corte. El comportamiento funcional (acciones, transiciones, validaciones, mensajes del catalogo) no cambia. | Recomendacion tecnica | `qt-architecture-lead`, `qt-quality-engineer`. |
| D11 | Cortes integrables: (1) estilo Basic, `Theme`, `Icono`, formato de fechas y copia de la entrega; (2) componentes; (3) pantallas en orden lista, nueva solicitud, detalle, perfiles y dialogos; (4) menu bar y notificaciones; (5) textos restantes, regeneracion del manual y evidencia oscura. | Recomendacion tecnica | `qt-architecture-lead`. |

## Alcance

### Incluye

- Los cambios `UX-01`..`UX-42` del traspaso (`docs/design/ui-ux-v2/traspaso.md`), con sus componentes, pantallas, estados y textos.
- Los componentes nuevos del traspaso (`AvisoEnLinea`, `CampoArchivo`, `CampoFormulario`, `EstadoVacio`, `EventoHistorial`, `FilaPaquete`, `FilaPerfil`, `FilaSolicitud`, `ResumenEstado`, `SelectorSegmentado`) y la actualizacion de los existentes.
- El rol `perfilNombre` (D8) y la opcion de tema oscuro de la herramienta (D9).
- La regeneracion del manual de T012 con la UI nueva y la actualizacion de sus textos donde cambie la UI.

### No incluye

- Las sugerencias `SUG-NN` (T014): busqueda y filtros, progreso de descarga en la lista, atajos de periodo, notificaciones accionables, icono con estado, primer uso guiado, reintento por paquete, aviso de vencimiento, ver solicitud existente, atajos de teclado y copiar identificadores. Si un mockup muestra alguna, se implementa sin ella.
- Cambios de comportamiento, de estados o de reglas del dominio, la aplicacion o el SAT.
- Fuentes descargadas o iconos de SF Symbols.

## Dependencias

- T001-T012 (en `main`, `f629db9`).
- ADR 0011, 0012.

## Trabajo esperado

1. Copiar la entrega a `docs/design/ui-ux-v2/` (D2) y los SVG necesarios a los recursos (D4).
2. Corte 1: estilo Basic, `Theme.qml`, `Icono.qml`, formateo de fechas y su prueba de coincidencia con `tokens.json`.
3. Corte 2: componentes del traspaso con sus estados, medidas y accesibilidad.
4. Corte 3: pantallas y dialogos segun `## Pantallas` del traspaso, con D7 y D8.
5. Corte 4: textos del menu bar y de notificaciones (UX-37, UX-39) en la definicion compartida y en `ServicioNotificaciones`.
6. Corte 5: textos restantes, `--tema oscuro` en la herramienta, regeneracion del manual y comparacion con los mockups.

## Criterios de aceptacion

### Tema y tokens

- [x] Dado `tokens.json`, cuando corre la prueba de tema, entonces cada color, tamano, espaciado y radio de `Theme.qml` coincide con su token en claro y en oscuro.
- [x] Dado el modo claro u oscuro de macOS, cuando se abre o se cambia mientras la app corre, entonces todas las pantallas y dialogos usan los tokens de ese modo, sin colores del otro modo ni literales fuera de `Theme`.
- [x] Dados los pares texto/fondo del traspaso, cuando se miden en ambos temas, entonces el texto cumple 4.5:1 y el foco y los bordes de control 3:1.

### Estados y componentes

- [x] Dado cada estado de solicitud, de paquete y de e.firma, cuando aparece en la lista, el detalle, perfiles o una notificacion, entonces se muestra con `EstadoBadge` (icono, texto y tono del alias del estado) y nunca solo con color.
- [x] Dado cada componente del traspaso, cuando se renderiza en sus estados (normal, hover, presionado, foco, deshabilitado, error), entonces respeta sus medidas y tokens.

### Pantallas

- [x] Dada cada pantalla en sus estados (cargando, vacio, error, con datos y especiales del traspaso), cuando se compara con su mockup claro, entonces coinciden estructura, jerarquia, acciones visibles y textos, salvo lo que pertenezca a T014.
- [x] Dada una solicitud, cuando se abre el detalle, entonces el resumen muestra un solo estado con titular y descripcion (D7), las pestanas Paquetes, Datos e Historial (Paquetes por omision si hay paquetes), una sola accion principal y `Eliminar...` separado con `Cancelar` por omision en su dialogo.
- [x] Dada la lista de solicitudes, cuando hay datos, entonces se ve como tabla con columnas Estado, Contribuyente (RFC y nombre del perfil, D8), Tipo, Periodo, Paquetes y Creada, ordenada de la mas reciente a la mas antigua.
- [x] Dadas las acciones existentes (crear, enviar, verificar, reintentar, eliminar, Finder, perfiles, e.firma), cuando se usan con la UI nueva, entonces producen el mismo efecto que antes y las pruebas funcionales de presentacion e integracion pasan.

### Textos y fechas

- [x] Dados los textos de la UI, el menu bar y las notificaciones, cuando se inspeccionan, entonces usan acentos y n, y coinciden con `## Textos` del traspaso.
- [x] Dadas fechas con y sin hora, cuando se muestran, entonces usan el formato de D5, y un dia sin hora no cambia por la zona horaria.

### Accesibilidad y teclado

- [x] Dada cada pantalla, cuando se recorre con Tab y Shift+Tab, entonces el foco es visible y sigue el orden del traspaso; las flechas recorren listas, pestanas y el selector segmentado; Escape cierra dialogos y el foco vuelve al control que los abrio.
- [x] Dado cada control, fila, badge e icono accionable, cuando se inspecciona, entonces tiene nombre y rol accesibles; los errores y avisos se anuncian.

### Evidencia y regresion

- [x] Dado `manual_capturas`, cuando se ejecuta en claro y con `--tema oscuro`, entonces genera las pantallas sin errores; las claras actualizan `docs/manual/img/` y el manual se ajusta a la UI nueva, con `satcfdi_manual_enlaces` en verde.
- [x] `ctest --test-dir build` pasa completo; los `objectName` se conservan o sus pruebas se actualizaron en el mismo corte (D10).

## Verificacion

1. `ctest --test-dir build --output-on-failure` en cada corte; prueba de coincidencia de tokens.
2. `manual_capturas` en claro y oscuro; comparacion captura por captura contra `docs/design/ui-ux-v2/` (estructura, estados, textos, jerarquia).
3. Checklist manual del usuario en el build firmado: claro/oscuro y cambio en caliente, Tab/flechas/Enter/Escape, VoiceOver, textos con acentos, menu bar, notificacion real y ausencia de regresiones en el flujo de T009.

## Resultado

- Entrega de Claude Design versionada en `docs/design/ui-ux-v2/` (traspaso, tokens, README, indice, 46 mockups, 43 iconos).
- Estilo Basic global (`fijarEstiloBasico`), `Theme.qml` singleton (tokens claro/oscuro, `estado()`, `estadoEFirma()`) con prueba de
  coincidencia contra `tokens.json`, `Icono` sobre `IconoSvg` (Qt SVG, color exacto), `FormatoFechas` (corrigio el desplazamiento de un `QDate`
  en QML).
- 16 componentes (10 nuevos) y las cuatro pantallas, dialogos, menu bar (textos e iconos plantilla) y notificaciones en dos lineas, segun
  UX-01..UX-42; un solo estado visible (D7); RFC y nombre del perfil en la lista (D8, unido en `SolicitudesServicePersistido::listar`).
- Decisiones del usuario durante el desarrollo: Registrar e.firma exige `.cer`, `.key` y contrasena; tras un error se limpia la seleccion.
- Ortografia completa en todo texto visible (presentacion, application y domain); logs y tipos de evento sin cambio.
- Herramienta de capturas con `--tema oscuro` y target `manual_capturas_oscuro`; manual reescrito y 12 imagenes regeneradas.
- Carrera de `TestMonitoreo` corregida; `ctest` 14/14; revision Codex de calidad aprobada con condiciones; prueba manual del usuario completa
  (2026-10-09).
- Riesgos aceptados: notificaciones sin `translate` (no hay traducciones); prueba de cambio de tema en caliente se omite en offscreen (verificada
  en cocoa y por el usuario); ilustraciones solo en claro; Qt SVG debe incluirse al empaquetar (T011).

## Riesgos y notas

- Tamano grande: la tarea se entrega por cortes y cada uno debe dejar la app usable y las pruebas en verde.
- Los mockups son HTML de referencia; algunos detalles (sombras, metricas de fuente) diferiran en QML. La comparacion es de estructura y jerarquia, no de pixeles.
- Cambiar textos del catalogo puede romper pruebas que comparan cadenas; se migran en el mismo corte.

## Referencias

- `docs/design/ui-ux-v2/` (tras el corte 1) y el artifact de Claude Design
- `docs/meetings/UI-UX-claude-design/revision-sugerencias.md`
- `docs/meetings/T013-refinamiento/` (bitacora y rondas)
- `docs/tasks/T012-manual-usuario.md`
