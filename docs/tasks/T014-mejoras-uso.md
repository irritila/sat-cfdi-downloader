# T014: Mejoras de uso (epica)

## Estado

Pendiente (refinada 2026-10-09). Epica dividida en T014.1-T014.4.

## Objetivo

Implementar las once sugerencias de Claude Design aceptadas por el usuario (SUG-02, 03, 04, 06, 07, 08, 09, 10, 11, 12, 13), en cuatro tareas independientes y ordenadas por riesgo.

## Contexto

Origen: `docs/design/ui-ux-v2/traspaso.md` (`## Sugerencias`) y `docs/meetings/UI-UX-claude-design/revision-sugerencias.md`. T013 aplico el rediseno visual. Pospuestas: SUG-01 (barra lateral), SUG-05 (lotes) y el calendario de SUG-04.

## Decisiones

| ID | Decision | Estado | Fuente |
| --- | --- | --- | --- |
| D1 | Division en T014.1 (pantalla), T014.2 (reintento por paquete), T014.3 (primer uso y vencimiento de e.firma) y T014.4 (macOS), en ese orden; cada una se refina, implementa y cierra por separado. | Aprobada por usuario | `qt-architecture-lead`; refinamiento 2026-10-09. |

## Tareas

| Tarea | Sugerencias |
| --- | --- |
| [T014.1](T014.1-mejoras-pantalla.md) | SUG-02, 03, 04, 11, 12, 13 |
| [T014.2](T014.2-reintento-por-paquete.md) | SUG-09 |
| [T014.3](T014.3-primer-uso-vencimiento-efirma.md) | SUG-08, 10 |
| [T014.4](T014.4-notificaciones-accionables-icono.md) | SUG-06, 07 |

## Criterios de aceptacion

- [ ] T014.1, T014.2, T014.3 y T014.4 completadas.

## Resultado

Pendiente.

## Referencias

- `docs/meetings/T014-refinamiento/` (bitacora y rondas)
