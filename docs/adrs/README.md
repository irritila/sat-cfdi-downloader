# Architecture Decision Records

Este directorio registra decisiones de arquitectura del proyecto.

El objetivo es evitar que el diseno se reconstruya desde memoria cada vez que avance la implementacion. Cada ADR debe capturar contexto, decision y consecuencias.

## Estados

- `Proposed`: decision propuesta, pendiente de revision.
- `Accepted`: decision vigente para el MVP.
- `Superseded`: decision reemplazada por otro ADR.
- `Deprecated`: decision vigente en el pasado, pero ya no recomendada.

Un ADR aceptado no se edita para cambiar la decision central. Si una decision cambia, se crea un nuevo ADR que lo reemplaza.

## Indice

| ADR | Estado | Decision |
| --- | --- | --- |
| [0001](0001-record-architecture-decisions.md) | Accepted | Registrar decisiones de arquitectura como ADRs |
| [0002](0002-personal-local-first-macos-app.md) | Accepted | Mantener el MVP como app personal local-first para macOS |
| [0003](0003-single-macos-app-process.md) | Accepted | Ejecutar UI, menu bar y worker en un solo proceso de app macOS |
| [0004](0004-local-persistence-boundaries.md) | Accepted | Separar base local, almacenamiento de secretos y carpeta de paquetes ZIP |
| [0005](0005-sat-web-service-contract-source.md) | Accepted | Usar fuentes oficiales SAT y WSDL productivos como autoridad de contrato |
| [0006](0006-secret-store-contract.md) | Accepted | Proteger e.firma mediante contrato de secretos y logs sanitizados |
| [0007](0007-worker-scheduling-and-manual-actions.md) | Accepted | Monitorear con worker local y ejecutar acciones manuales bajo reglas de pausa |
| [0008](0008-map-ui-filters-to-sat-solicita-descarga.md) | Superseded | Mapear filtros de UI a `SolicitaDescarga` |
| [0009](0009-macos-activation-and-login-behavior.md) | Accepted | Usar activacion macOS por contexto |
| [0010](0010-keychain-secret-store-and-memory-token.md) | Accepted | Usar Keychain para secretos y token SAT solo en memoria |
| [0011](0011-qt-qml-application-stack.md) | Accepted | Usar Qt y QML como stack de aplicacion |
| [0012](0012-qt-layered-project-structure.md) | Accepted | Organizar el proyecto Qt por capas y targets CMake |
| [0013](0013-sat-request-operations-v15.md) | Accepted | Usar operaciones SAT v1.5 separadas para crear solicitudes |
| [0014](0014-serial-operation-executor-and-recovery.md) | Accepted | Serializar operaciones y definir recuperacion transaccional |
