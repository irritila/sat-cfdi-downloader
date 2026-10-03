# Diseno tecnico

Este directorio contiene decisiones y especificaciones de diseno tecnico listas para guiar la implementacion.

Los documentos aqui no amplian el alcance del MVP. Deben mantenerse alineados con:

- `docs/requirements.md`
- `docs/architecture.md`
- `docs/adrs/README.md`

## Indice

| Documento | Proposito |
| --- | --- |
| [Estructura Qt/CMake](qt-project-structure.md) | Define carpetas, targets CMake, reglas de dependencia y frontera QML/C++. |
| [Reglas operativas](operational-rules.md) | Define estados, codigos, transacciones, recuperacion y ejecucion serial. |
| [Modelo fisico SQLite](sqlite-physical-model.md) | Define tablas, restricciones, indices, contrato de migraciones y transacciones de la base local. |
