# T027: Clic perdido por cambio de app en las pruebas graficas del video

## Estado

Pendiente (sin refinar; prioridad baja). Revisar solo si vuelve a afectar una grabacion o las pruebas.

## Objetivo

Que cambiar de app mientras corren las pruebas graficas o una grabacion de `tools/video-tutorial` no haga perder clics.

## Contexto

T022 encontro que, si el usuario cambia de app entre el press y el release de un clic sintetico, Qt Quick suelta el agarre del boton y el release no lo alcanza. El usuario confirmo que cambia de app durante las corridas. T022 agrego un filtro que descarta `WindowDeactivate` y una comprobacion antes del release que hace fallar la prueba con mensaje claro ("perdio el agarre del raton antes del release") y PNG.

El filtro no basta: en la verificacion de T022 (2026-10-26), 11 de 75 corridas (`satcfdi_video_rapido` 3 de 25, `pdf_error` 8 de 25) fallaron con ese mensaje al cambiar de app de verdad; evidencia en `build-video/t022/final/`. La perdida real del agarre llega por otra via que la inyeccion de T022 (`handleFocusWindowChanged(nullptr)`) no imita. El usuario decidio cerrar T022 sin investigarla.

## Alcance

### Incluye

- Con `SATCFDI_VIDEO_DIAGNOSTICO_CLIC=1` y un cambio de app real del usuario, identificar que eventos sueltan el agarre (candidatos: `ApplicationStateChange`, `ActivationChange`, `focusOutEvent` de la ventana, `QGuiApplication::focusWindowChanged`, ungrab).
- Ajustar la inyeccion de diagnostico para que imite esa via y corregir el grabador.

### No incluye

- Cambios en la app (`src/`), salvo causa demostrada en ella.

## Dependencias

- T022.

## Trabajo esperado

- `tools/video-tutorial/EntornoQml.{h,cpp}`, `Grabador.cpp`, pruebas de desactivacion y README.

## Criterios de aceptacion

- [ ] La inyeccion reproduce la falla real y la prueba pasa con la correccion.
- [ ] Las cuatro pruebas graficas pasan 25 veces seguidas aisladas mientras el usuario cambia de app varias veces.
- [ ] Build `-Werror`, `ctest` y `git diff --check` pasan.

## Verificacion

Corridas aisladas con cambios de app del usuario, `ctest` del video y evidencia en `build-video/`.

## Resultado

Pendiente.

## Referencias

- `docs/tasks/T022-intermitencia-pruebas-graficas-video.md`, `docs/meetings/T022-desarrollo/bitacora.md`, `tools/video-tutorial/README.md`.
