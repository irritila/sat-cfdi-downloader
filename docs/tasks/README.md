# Tareas de implementacion

Este directorio contiene una tarea por archivo Markdown. Las tareas convierten las decisiones de arquitectura en incrementos pequenos, verificables y ordenados.

## Convencion de nombres

Usar el formato:

```text
TNNN-descripcion-corta.md
```

Ejemplo: `T001-modelo-fisico-sqlite.md`.

## Lista inicial

| Tarea | Estado | Resultado |
| --- | --- | --- |
| [T001](T001-modelo-fisico-sqlite.md) | Completada | Modelo fisico SQLite |
| [T002](T002-shell-qt-qml.md) | Completada | Shell Qt/QML ejecutable |
| [T003](T003-persistencia-local.md) | Pendiente | Persistencia local de solicitudes |
| [T004](T004-ciclo-vida-macos.md) | Pendiente | Ciclo de vida macOS y menu bar |
| [T005](T005-secret-store-keychain.md) | Pendiente | SecretStore con Keychain |
| [T005.1](T005.1-ui-perfiles-sat.md) | Pendiente | UI de perfiles SAT y e.firma |
| [T006](T006-spike-sat.md) | Pendiente | Spike de integracion SAT |
| [T007](T007-worker-ejecutor-serial.md) | Pendiente | Worker y ejecutor serial |
| [T008](T008-almacenamiento-zip.md) | Pendiente | Almacenamiento local de paquetes ZIP |
| [T009](T009-flujo-sat-integrado.md) | Pendiente | Flujo SAT integrado |
| [T010](T010-pruebas-aceptacion-mvp.md) | Pendiente | Pruebas de aceptacion del MVP |

## Estados

- `Pendiente`: definida, pero no iniciada.
- `En progreso`: trabajo activo.
- `Bloqueada`: requiere una decision o dependencia externa.
- `Completada`: criterios de aceptacion verificados.
- `Cancelada`: ya no aplica al MVP.

## Reglas

- Una tarea debe producir un resultado concreto y revisable.
- Los criterios de aceptacion deben poder comprobarse sin interpretar intenciones.
- Una tarea no debe introducir alcance fuera del MVP personal.
- Las decisiones permanentes pertenecen a un ADR, no solo a la tarea.
- Al terminar, actualizar el estado y agregar la evidencia de verificacion.
