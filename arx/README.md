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
