# Roles de ingenieria

Este directorio contiene las definiciones canonicas y portables de los roles de
ingenieria usados para revisar y desarrollar el proyecto. Son instrucciones de
trabajo, no agentes registrados en una herramienta especifica.

Todos los roles comparten una base tecnica obligatoria: C++, Qt 6, QML, Qt Quick
Controls y CMake. Cada archivo agrega una cualidad principal distinta, pero
ningun rol debe analizar el proyecto ignorando ese stack.

## Uso

Selecciona un solo rol para una tarea y lee su archivo antes de comenzar:

```text
Usa el rol definido en arx/agents/especialista-qt-qml-cpp.md.
Lee primero AGENTS.md, README.md y la documentacion relevante de docs/.
Reporta "Contexto leido" con los archivos consultados antes de trabajar.
```

Las definiciones completas estan en `arx/agents/`. Las integraciones concretas
de cada herramienta deben ser adaptadores pequenos que apunten a estos archivos,
sin copiar su contenido.

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
