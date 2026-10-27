# T028: Zona horaria de las fechas de Respaldo

## Estado

Pendiente (sin refinar; prioridad baja). Detectada en T024.4 (2026-10-26).

## Objetivo

Que las fechas y horas que muestra Respaldo usen la misma zona horaria que el resto del recorrido (Historial y Facturas), para que el usuario vea horas coherentes entre pantallas.

## Contexto

Historial muestra sus horas en hora Centro (`America/Mexico_City`, T023 D19) con `fechaCentro` (`src/presentation/viewmodels/HistorialViewModel.cpp`), independiente de la zona del equipo. Respaldo formatea "Último respaldo correcto" con `FormatoFechas::fechaHora` (`src/presentation/viewmodels/RespaldoViewModel.cpp`, `textoUltimoExito`), que usa la zona del proceso. En un Mac fuera de la zona Centro, o con `TZ=UTC` como en la herramienta del video (T017 D8), las dos pantallas muestran horas distintas para el mismo instante. En el video de T024.4 se esquivo con el dato sintetico, sin cambiar la app. Revision de calidad de T024.4: hallazgo informativo.

## Alcance

### Incluye

- Decidir la zona de Respaldo (hora Centro, como Historial, o la del equipo en ambas) y aplicarla a todas las fechas de Respaldo (ultimo exito, pendiente diario, restauracion si muestra hora).
- Prueba que fije la zona del proceso distinta de Centro y compruebe el texto.

### No incluye

- Cambiar el formato de fecha ni el contenido del respaldo.

## Dependencias

- T023 (D19), T025.

## Trabajo esperado

- `src/presentation/viewmodels/RespaldoViewModel.cpp` (y `FormatoFechas` si se comparte la conversion), pruebas de presentacion.

## Criterios de aceptacion

- [ ] Con la zona del proceso distinta de `America/Mexico_City`, Respaldo y Historial muestran la misma hora para el mismo instante.
- [ ] Las capturas del manual y el video siguen pasando sus comprobaciones (regenerar solo si cambian).
- [ ] Build `-Werror`, `ctest` y `git diff --check` pasan.

## Verificacion

Prueba con zona forzada, `ctest`, capturas del manual.

## Resultado

Pendiente.

## Referencias

- `docs/tasks/T024.4-video-tutorial-nuevo.md`, `docs/meetings/T024.4-desarrollo/respuestas/05-calidad.md`, `docs/tasks/T023.2-catalogo-mensual-y-disponibilidad.md`.
