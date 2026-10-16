# T018: Roles especialistas de produccion de video

## Estado

Completada (2026-10-15).

## Prioridad y tamano

- Prioridad: Media (bloquea T017).
- Tamano: Pequeno (solo documentacion de roles y skills).

## Objetivo

Crear en `arx/agents/` y `.claude/agents/` los roles de guion, audio y voz sintetica, y motion design que pidio el refinamiento de T017, y dejar escrita en las skills la regla para que los especialistas pidan roles nuevos y crucen de proveedor.

## Contexto

En el refinamiento de T017, `qt-platform-engineer` y `qt-architecture-lead` pidieron especialistas que no existen. El usuario indico que los agentes pueden pedir especialistas nuevos, que Codex puede sugerir agentes Claude con modelos concretos y que Claude puede pedir a Codex. Hoy `arx/README.md` exige a todos los roles la base tecnica C++/Qt/QML/CMake, y las skills fijan la ruta de proveedor sin prever roles nuevos ni cruces.

## Decisiones

| ID | Decision | Estado | Fuente y motivo |
| --- | --- | --- | --- |
| D1 | Se crean tres roles: `especialista-guion-tutoriales` (guionista y disenador instruccional), `especialista-audio-voz` (audio y voz sintetica) y `especialista-motion-design`. Los nombres siguen el patron de `especialista-macos`. | Aprobada por usuario (los tres roles); nombres por recomendacion tecnica | Refinamientos de T017 y T018. |
| D2 | Proveedor y modelo por omision: guion y motion design en Claude Fable 5.1 (`claude-fable-5-1`); audio y voz en Codex GPT-6-Astra (`gpt-6-astra`), porque la voz de T017 usa la API TTS de OpenAI. | Aprobada por usuario | Refinamiento de T018 (modelos cambiados por el usuario el 2026-10-15). |
| D3 | Los roles de video tienen una base minima del proyecto en lugar de la base Qt completa: conocen la app y sus pantallas, leen QML y el guion, saben que se puede pedir a la herramienta de T017 y no implementan C++. `arx/README.md` documenta la excepcion. | Aprobada por usuario | Refinamiento de T018. Conserva la especialidad sin romper la regla para los roles Qt. |
| D4 | La regla de solicitud y cruce vive en la tabla de ruta de proveedor de `arx/skills/task-refinement/SKILL.md` y `arx/skills/development-cycle/SKILL.md`, y en `arx/README.md`. | Aprobada por usuario | Refinamiento de T018. |
| D5 | Contenido de la regla: (a) todo especialista, Claude o Codex, puede cerrar con una seccion "Especialistas solicitados" (rol, area, motivo, proveedor y modelo sugeridos, que decidiria o revisaria); (b) el coordinador decide si lo convoca y lo registra en `sesiones.json` y la bitacora; (c) si el rol no existe, el coordinador puede convocarlo con un encargo acotado y etiquetado como rol provisional, y propone al usuario una tarea para crearlo; (d) la sugerencia de proveedor o modelo de un especialista no reemplaza la ruta por omision sin decision del coordinador, y se registra con su motivo. | Recomendacion tecnica | Indicaciones del usuario en el refinamiento de T017. |
| D6 | La copia instalada en `~/.claude/skills/` esta fuera del repo y no forma parte de esta tarea; al cerrar, el coordinador avisa al usuario que debe sincronizarla. | Recomendacion tecnica | Esa copia solo difiere en la ruta del script; editarla no es un cambio versionado. |
| D7 | El ciclo de desarrollo de esta tarea usa GPT-6-Astra (`gpt-6-astra`) para los participantes de Codex y Claude Fable 5.1 (`claude-fable-5-1`) para los especialistas de Claude, igual que D2. | Aprobada por usuario | Refinamiento de T018. |

## Alcance

### Incluye

- `arx/agents/especialista-guion-tutoriales.md`: estructura didactica, orden y duracion de escenas, texto en pantalla y hablado, ritmo de lectura, pronunciacion de siglas y montos, coherencia con `docs/manual/usuario.md`, y la regla de datos sinteticos.
- `arx/agents/especialista-audio-voz.md`: modelo, voz e instrucciones de tono del TTS, pronunciacion, pausas, loudness, mezcla, cortes entre clips, costo y manejo de `OPENAI_API_KEY` (nunca en repo ni logs).
- `arx/agents/especialista-motion-design.md`: cursor simulado, resaltados, easing y velocidades, transiciones, legibilidad, contraste y posicion de rotulos para que no tapen controles, y el rotulo "Ilustracion".
- Cada rol sigue la estructura de los roles existentes (Rol, Mision, Cualidad principal, Base tecnica, Contexto obligatorio con `Contexto leido`, Autonomia y limites, Enfoque, Forma de trabajo, Entrega esperada) e indica su proveedor por omision.
- Adaptadores en `.claude/agents/` con la misma forma que los existentes.
- `arx/README.md`: tabla "Roles de produccion de video", excepcion de la base tecnica (D3) y regla de solicitud y cruce (D5).
- Tabla de ruta de proveedor y regla D5 en las dos skills de `arx/skills/`.

### No incluye

- Implementar T017 ni escribir su guion.
- Modificar `~/.claude/skills/` (D6) o la configuracion de Codex fuera del repo.
- Cambiar los roles existentes, salvo la frase de `arx/README.md` sobre la base tecnica.

## Dependencias

- Formato de `arx/agents/`, `.claude/agents/` y `arx/skills/`.
- T017 (D3, D5, D7, D13, D14) como fuente de las responsabilidades.

## Trabajo esperado

- Tres roles canonicos, tres adaptadores y cambios en `arx/README.md` y las dos `SKILL.md`. Sin cambios de codigo.

## Criterios de aceptacion

- [x] Existen los tres archivos de D1 en `arx/agents/` con todas las secciones del formato existente y su proveedor por omision segun D2.
- [x] Existen los tres adaptadores en `.claude/agents/` con `name` igual al nombre del archivo, una `description` de una linea y la referencia al rol canonico, sin copiar su contenido. Claude Code los lista como tipos de agente al iniciar una sesion nueva.
- [x] Cada rol de video declara la base minima de D3, dice que no implementa C++ y exige leer `docs/manual/usuario.md` y `docs/tasks/T017-video-tutorial.md` cuando trabaja en el video.
- [x] El rol de audio y voz prohibe escribir `OPENAI_API_KEY` en el repo, logs o evidencias y exige aprobacion del usuario antes de cualquier llamada con costo. El de guion exige datos sinteticos (`XAXX010101000` / `XEXX010101000`).
- [x] `arx/README.md` lista los tres roles con cuando invocarlos y su responsabilidad, documenta la excepcion D3 y la regla D5.
- [x] Las dos `SKILL.md` de `arx/skills/` incluyen los roles nuevos en la ruta de proveedor y la regla D5 con sus cuatro puntos.
- [x] Cada enlace Markdown agregado apunta a un archivo existente, y `git diff --check` pasa.

## Verificacion

1. Revision de estructura de cada rol contra `arx/agents/especialista-macos.md` y `arx/agents/qt-platform-engineer.md`.
2. Comprobacion de enlaces de `arx/README.md` y de las `SKILL.md` (que cada ruta exista) y `git diff --check`.
3. Revision de Codex (calidad) sobre coherencia con T017 y la regla D5.
4. Prueba de humo: en una sesion nueva, `especialista-guion-tutoriales` aparece como tipo de agente y su primera respuesta incluye `Contexto leido`.

## Resultado

- Roles `especialista-guion-tutoriales`, `especialista-audio-voz` y `especialista-motion-design` en `arx/agents/` con adaptadores en
  `.claude/agents/`; base minima (D3) y proveedor por omision (D2: Claude Fable 5.1 / GPT-6-Astra).
- `arx/README.md` con la tabla de roles de video, la excepcion de base tecnica y la regla de solicitud y cruce de proveedor (D5);
  las dos skills de `arx/skills/` con la ruta y la regla.
- Contrato de cache alineado con T017 D5 (solo se sintetizan hashes faltantes) y valor de la clave distinguido de su identificador.
- Revision de Codex GPT-6-Astra en dos rondas; prueba de humo en sesion nueva: los tres tipos aparecen y el guionista responde con
  `Contexto leido`.
- Pendiente fuera del repo (D6): sincronizar `~/.claude/skills/` con `arx/skills/`.

## Referencias

- `docs/tasks/T017-video-tutorial.md`, `docs/meetings/T017-refinamiento/bitacora.md`, `docs/meetings/T018-refinamiento/bitacora.md`.
- `arx/README.md`, `arx/agents/`, `arx/skills/`, `.claude/agents/`.
