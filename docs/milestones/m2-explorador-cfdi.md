# M2 - Explorador de CFDI

Estado: completado (2026-10-13). Posterior al MVP (M1); no lo bloquea.

## Objetivo

Que el usuario vea las facturas CFDI contenidas en los paquetes ZIP de sus solicitudes terminadas y las previsualice como PDF generado localmente a partir del XML.

## Alcance

- Explorador de CFDI por solicitud terminada: listado de las facturas de sus paquetes.
- Previsualizacion en PDF generado localmente con los datos del XML.

## Fuera de alcance de M2

- Lo que siga fuera del MVP y no pida este milestone (metadata, vigencia o cancelacion, exportaciones contables, conciliacion).
- Backend remoto y servicios de terceros para generar PDF.

## Tareas

- [T015](../tasks/T015-explorador-cfdi.md) Explorador de CFDI con previsualizacion PDF (epica T015.1-T015.4, completada).

## Referencias

- `docs/requirements.md` (fuera del MVP: extraccion, parseo e indexacion de XML).
- `docs/milestones/m1-app-desktop.md`
