# Roles de ingenieria

Este directorio contiene las definiciones canonicas y portables de los roles de
ingenieria usados para revisar y desarrollar el proyecto. Son instrucciones de
trabajo, no agentes registrados en una herramienta especifica.

Todos los roles comparten una base tecnica obligatoria: C++, Qt 6, QML, Qt Quick
Controls y CMake. Cada archivo agrega una cualidad principal distinta, pero
ningun rol debe analizar el proyecto ignorando ese stack.

Excepcion: los roles de produccion de video tienen una base minima del proyecto
en lugar de la base Qt completa. Conocen la aplicacion y sus pantallas, leen QML
y el guion, saben que se puede pedir a la herramienta de video de T017 y no
implementan C++. Cuando necesitan un cambio en la herramienta, lo piden al rol
Qt propietario.

## Uso

Selecciona un solo rol para una tarea y lee su archivo antes de comenzar:

```text
Usa el rol definido en arx/agents/qt-engineer.md.
Lee primero AGENTS.md, README.md y la documentacion relevante de docs/.
Reporta "Contexto leido" con los archivos consultados antes de trabajar.
```

Las definiciones completas estan en `arx/agents/`. Las integraciones concretas
de cada herramienta deben ser adaptadores pequenos que apunten a estos archivos,
sin copiar su contenido.

## Refinamiento de tareas

| Rol | Cuando invocarlo | Responsabilidad principal |
| --- | --- | --- |
| [refinement-lead](agents/refinement-lead.md) | Objetivos o alcance poco claros, alternativas por decidir o tareas sin criterios verificables. | Conducir la discusion con el usuario, explicitar decisiones y pendientes, y redactar una tarea lista para implementar. |

El refinamiento usa `docs/tasks/_template.md` y distingue decisiones tomadas,
supuestos y preguntas abiertas. El rol indica que falta cuando la tarea aun no
esta lista para implementarse.

## Ciclo de desarrollo por tarea

| Rol | Cuando invocarlo | Responsabilidad principal |
| --- | --- | --- |
| [development-cycle-lead](agents/development-cycle-lead.md) | Una tarea refinada requiere plan, implementacion coordinada, integracion y evidencia de cierre. | Coordinar especialistas, cortes verticales, propiedad de archivos, bitacora y validacion sin sustituir sus roles tecnicos. |

El ciclo crea una sesion en `docs/meetings/TNNN-desarrollo/` con un dossier de
contexto, plan, propiedad, bitacora y evidencia. El coordinador da la palabra a
los especialistas, limita las lecturas a su encargo y conserva una matriz de
criterios contra evidencia real.

## Skills

| Skill | Proposito | Invocacion |
| --- | --- | --- |
| [task-refinement](skills/task-refinement/SKILL.md) | Coordinar el refinamiento de una tarea con rondas de especialistas, decisiones y criterios verificables. | Codex: `$task-refinement`; Claude Code: `/task-refinement`. |
| [development-cycle](skills/development-cycle/SKILL.md) | Coordinar la implementacion, integracion y evidencia de una tarea refinada. | Codex: `$development-cycle`; Claude Code: `/development-cycle`. |

Los directorios canonicos incluyen los inicializadores y referencias usados por
ambos proveedores. `.claude/skills/` contiene adaptadores breves para invocarlos
como skills de proyecto en Claude Code.

## Roles Qt por responsabilidad

| Rol | Cuando invocarlo | Responsabilidad principal |
| --- | --- | --- |
| [qt-architecture-lead](agents/qt-architecture-lead.md) | Nuevos modulos, responsabilidades, contratos compartidos o dudas de arquitectura y estilo. | Definir arquitectura y convenciones; coordinar cambios entre modulos. |
| [qt-interface-engineer](agents/qt-interface-engineer.md) | Pantallas, componentes, navegacion e interaccion. | Componer la UI y verificar estados visuales, interaccion y accesibilidad. |
| [qt-core-engineer](agents/qt-core-engineer.md) | Logica de negocio, modelos, persistencia e integracion con servicios. | Implementar dominio y casos de uso, con contratos, ownership y concurrencia definidos. |
| [qt-platform-engineer](agents/qt-platform-engineer.md) | Build, dependencias, CI, tooling, integracion nativa y distribucion. | Mantener automatizaciones e instrucciones reproducibles. |
| [qt-quality-engineer](agents/qt-quality-engineer.md) | Estrategia de pruebas, validacion, regresiones y defectos dificiles de reproducir. | Producir pruebas y evidencia sobre criterios de aceptacion y riesgos pendientes. |

Cada definicion detalla su autonomia, limites, entregables y criterios de
finalizacion. Selecciona el rol segun la responsabilidad principal de la tarea.
Coordinar un cambio compartido implica identificar consumidores, acordar el
contrato y registrar su impacto; no activa otros roles automaticamente. Combina
roles solo cuando la tarea lo solicite explicitamente, como indica `AGENTS.md`.

## Roles de produccion de video

| Rol | Cuando invocarlo | Responsabilidad principal |
| --- | --- | --- |
| [especialista-guion-tutoriales](agents/especialista-guion-tutoriales.md) | Escribir o revisar el guion de un video tutorial: orden y duracion de escenas, textos en pantalla y hablados, coherencia con el manual. | Estructura didactica, presupuesto de cuadros, pronunciacion en el campo `voz` y regla de datos sinteticos. |
| [especialista-audio-voz](agents/especialista-audio-voz.md) | Elegir modelo, voz y tono del TTS; revisar pronunciacion, pausas, loudness y mezcla; preparar la muestra de voz. | Voz sintetica reproducible y uniforme, sin exponer `OPENAI_API_KEY` ni generar costos sin aprobacion del usuario. |
| [especialista-motion-design](agents/especialista-motion-design.md) | Cursor simulado, resaltados, transiciones, titulos y rotulos; ilustraciones de menu bar, selector, Finder y notificaciones. | Movimiento determinista en cuadros, legibilidad y contraste, rotulos que no tapan controles y rotulo "Ilustracion". |

Estos roles tienen la base minima descrita arriba y declaran su proveedor por
omision: guion y motion design en Claude Fable 5.1 (`claude-fable-5-1`); audio y
voz en Codex GPT-6-Astra (`gpt-6-astra`). Cuando trabajan en el video leen
ademas `docs/manual/usuario.md` y `docs/tasks/T017-video-tutorial.md`.

## Solicitud de especialistas y cruce de proveedor

Las skills de refinamiento y de ciclo de desarrollo fijan una ruta de proveedor
por omision para cada rol. Sobre esa ruta aplica esta regla:

- a. Todo especialista, sea de Claude o de Codex, puede cerrar su respuesta con
  una seccion `Especialistas solicitados` que indique rol, area, motivo,
  proveedor y modelo sugeridos, y que decidiria o revisaria ese rol.
- b. El coordinador decide si lo convoca y registra la decision en
  `sesiones.json` y en la bitacora de la sesion.
- c. Si el rol no existe, el coordinador puede convocarlo con un encargo acotado
  y etiquetado como rol provisional, y propone al usuario una tarea para
  crearlo en `arx/agents/`.
- d. La sugerencia de proveedor o modelo de un especialista no reemplaza la ruta
  por omision sin decision del coordinador, y esa decision se registra con su
  motivo.

## Integraciones

- **Codex:** invoca explicitamente el archivo del rol en el prompt de la tarea.
  `AGENTS.md` contiene las reglas comunes para el repositorio.
- **Claude Code:** usa los adaptadores en `.claude/agents/`, que referencian la
  definicion canonica correspondiente.

## Regla de contexto

Cada rol debe leer el contexto vigente del repositorio antes de analizar,
proponer o modificar algo. Si falta un archivo relevante, debe reportarlo como
limitacion. La documentacion del proyecto es la fuente de contexto; el rol no
debe sustituirla ni inventar decisiones ausentes.
