@README.md

## Roles especializados

Las definiciones canonicas de roles viven en `arx/agents/`. Los adaptadores para
Claude Code viven en `.claude/agents/` y referencian esas definiciones para evitar
duplicarlas.

Al usar un subagente especializado, debe leer antes de trabajar:

1. `AGENTS.md`, `CLAUDE.md` y `README.md`.
2. La documentacion relevante de `docs/`.
3. El archivo canonico del rol en `arx/agents/`.

La primera respuesta del subagente debe incluir `Contexto leido` con los archivos
consultados. No se deben activar todos los roles para una misma tarea salvo que
se solicite una revision cruzada.
