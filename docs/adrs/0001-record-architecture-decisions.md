# ADR 0001: Registrar decisiones de arquitectura como ADRs

## Estado

Accepted

## Contexto

El proyecto ya tiene requerimientos y diagramas de arquitectura iniciales. La aplicacion se quiere mantener por varios anos, por lo que las decisiones no deben depender de memoria o de una conversacion aislada.

Tambien existe riesgo de scope creep: el proyecto es de uso personal, no comercial, y varias decisiones deben conservar ese limite.

## Decision

Registrar decisiones de arquitectura en archivos Markdown dentro de `docs/adrs`.

Cada ADR debe incluir:

- Estado.
- Contexto.
- Decision.
- Consecuencias.
- Referencias, cuando aplique.

Los ADRs aceptados representan la decision vigente. Si una decision cambia, se crea un ADR nuevo y el anterior se marca como `Superseded`.

## Consecuencias

- Las decisiones importantes quedan auditables.
- Las discusiones futuras pueden contrastarse contra decisiones existentes.
- No todas las decisiones pequenas necesitan ADR; solo las que afecten arquitectura, seguridad, integracion SAT, persistencia, plataforma o alcance.

## Referencias

- `README.md`
- `docs/requirements.md`
- `docs/architecture.md`

