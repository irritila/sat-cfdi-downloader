# T022: Intermitencia de las pruebas graficas del video en modo rapido

## Estado

Pendiente (sin refinar).

## Objetivo

Que las pruebas graficas de `tools/video-tutorial` en `--rapido` pasen de forma repetible.

## Contexto

En el cierre de T017, alrededor del 5 % de las corridas aisladas de `satcfdi_video_rapido`, `pdf_error`, `pdf_timeout` y `composicion` fallan con "no se encontro un control visible con objectName ..." (`campoRfcPerfil`, `botonEFirma`). La espera acotada por condicion agregada en `mover` no lo corrige: el diagnostico muestra que se pierde el efecto de un clic anterior (no abre el formulario o no da foco), aunque press y release se envian sobre el control. En el cuadro del press el hit-test llega a `paginaPerfilesSat`. Solo ocurre en `--rapido`; la grabacion normal no lo ha mostrado.

## Alcance

### Incluye

- Diagnostico con `<salida>/<nombre>.fallo.png` y la causa real.
- Correccion propuesta: comprobar tras el press que hubo agarre del raton (`mouseGrabberItem()` o `pressed` del control) y, si no, repetir el press tras procesar eventos con el reloj congelado; o esperar a que la geometria se estabilice antes del clic.

### No incluye

- Cambios en `src/` salvo que la causa este en la app.

## Dependencias

- T017.

## Trabajo esperado

- Cambios en `tools/video-tutorial/Grabador.cpp` y sus pruebas.

## Criterios de aceptacion

- [ ] Las cuatro pruebas graficas pasan 25 veces seguidas cada una, aisladas.
- [ ] Tres `ctest` completos seguidos pasan.
- [ ] El video normal no cambia (D16 OK).

## Verificacion

Bucle de pruebas aisladas, `ctest` completo y `video_tutorial_verificar`.

## Resultado

Pendiente.

## Referencias

- `docs/tasks/T017-video-tutorial.md`, `docs/meetings/T017-desarrollo/respuestas/09-calidad-cierre.md`.
