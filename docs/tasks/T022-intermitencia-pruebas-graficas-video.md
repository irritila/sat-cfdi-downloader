# T022: Intermitencia de las pruebas graficas del video en modo rapido

## Estado

Completada con limitaciones (2026-10-26; cierre anticipado por decision del usuario, seguimiento en T027).

## Objetivo

Que las pruebas graficas de `tools/video-tutorial` en `--rapido` (`satcfdi_video_rapido`, `pdf_error`, `pdf_timeout` y `composicion`) pasen de forma repetible, con la causa del clic perdido demostrada y corregida.

## Contexto

En el cierre de T017, alrededor del 5 % de las corridas aisladas de esas pruebas fallan con "no se encontro un control visible con objectName ..." (`campoRfcPerfil`, `botonEFirma`). La espera acotada por condicion en `mover` (`esperarControl`) no lo corrige: se pierde el efecto de un clic anterior (no abre el formulario o no da foco), aunque press y release se envian sobre el control. En el cuadro del press el hit-test de diagnostico llega a `paginaPerfilesSat`. Solo ocurre en `--rapido`, donde las esperas se reducen al 10 %. Sesion: `docs/meetings/T022-refinamiento/`.

## Decisiones

| ID | Decision | Estado | Fuente |
|---|---|---|---|
| D1 | Hipotesis principal: carrera de geometria o estado en el cuadro del press (el control aun no esta estable bajo el cursor; `MouseMove` y press van en el mismo cuadro sin estabilizar). Doble clic (H2), hover que cambia el estado, item que se mueve entre press y release, grabber distinto y transicion congelada por el reloj de cuadros se registran y se descartan con evidencia, no por intuicion. | Recomendacion tecnica | Ronda 01 calidad |
| D2 | Primero se confirma la causa con instrumentacion de diagnostico apagada por defecto; despues se corrige. | Recomendacion tecnica | Ronda 01 calidad |
| D3 | Correccion: antes del press, con el reloj congelado, bombear y renderizar hasta que el objetivo sea visible, habilitado y con geometria estable en dos observaciones consecutivas, y recalcular el punto. Tras el press, exigir evidencia de agarre (`mouseGrabberItem()` o `pressed`/`down`); si no la hay, fallar con traza y PNG. Sin sleeps, sin cuadros extra arbitrarios y sin reintentos silenciosos. Si la precondicion consume cuadros, solo aplica en `--rapido`; el video normal no cambia (D16 de T017). | Recomendacion tecnica | Ronda 01 calidad |
| D4 | Si la instrumentacion muestra el press agarrado por el control correcto y el cambio de estado no se refleja, o una prueba de presentacion reproduce el fallo sin el grabador, la causa esta en la app y T022 la corrige en `src/` (qt-interface-engineer, con prueba de presentacion que reproduzca el caso). Un problema de contrato o ciclo de vida entre capas se consulta con qt-architecture-lead. | Aprobada por usuario | 2026-10-26 |
| D5 | Verificacion completa: 4 pruebas x 25 corridas aisladas, 3 `ctest` completos y 2 grabaciones normales con D16. | Aprobada por usuario | 2026-10-26 |
| D6 | Las 4 pruebas fallan de forma determinista desde T023 (la herramienta no inyecta fuente de Facturas y `guion-ejemplo.json` espera la lista de solicitudes). En T022: fuente de Facturas sintetica en la herramienta y ajuste minimo de `guion-ejemplo.json`; `guion-tutorial.json` queda para T024. | Aprobada por usuario | 2026-10-26 (desarrollo) |
| D7 | Causa demostrada: una desactivacion de la ventana entre press y release (otra app toma el foco de macOS) suelta el agarre del boton y el release no lo alcanza; H1 y H2 descartadas (710 clics instrumentados estables y agarrados). Correccion (ajusta D3): el filtro de entrada real descarta tambien `WindowDeactivate`; antes del release se exige que el objetivo siga agarrado o `pressed`, si no la prueba falla con traza y PNG. Sin precondicion de geometria. | Recomendacion tecnica | Corte 1 de desarrollo |
| D8 | Oraculo de D5 hasta T024: dos grabaciones normales de `guion-ejemplo.json` (diagnostico apagado y encendido) con cuadros y audio identicos; la verificacion D16 del tutorial pasa a T024, porque `guion-tutorial.json` esta roto desde T023. | Aprobada por usuario | 2026-10-26 (desarrollo) |

## Alcance

### Incluye

- Linea base: confirmar que las cuatro pruebas corren contra la app vigente (T023 a T026 cambiaron pantallas) y medir la tasa de fallo actual.
- Instrumentacion de diagnostico (opcional, apagada por defecto) en press y release: cuadro, cursor, objetivo, rectangulo en escena, `visible`/`enabled`/`opacity`, cadena de items bajo el punto, `activeFocusItem`, ventana activa, `mouseGrabberItem()`, `pressed`/`down`/`hovered` del objetivo, eventos recibidos con timestamp y estado semantico minimo (modo de perfiles, `hayFormulario`). Sin RFC, contrasenas ni otros secretos.
- Causa demostrada y documentada, y la correccion D3 (o D4).
- Pruebas del grabador para la precondicion y el fallo por falta de agarre.

### No incluye

- Actualizar el guion o el video a la UI nueva (T024); si la linea base muestra fallos deterministas por cambios de pantallas, se documentan y se acuerda con el usuario si se corrigen aqui o en T024.
- Cambios en `src/` sin causa demostrada (D4).

## Dependencias

- T017. Conviene antes de T024, que vuelve a grabar el video.

## Trabajo esperado

- `tools/video-tutorial/Grabador.{h,cpp}` y sus pruebas; `tools/video-tutorial/README.md` o `docs/development.md` si se agrega una opcion de diagnostico.
- Solo con D4: cambio en `src/presentation/` y prueba de presentacion.

## Criterios de aceptacion

- [x] Linea base registrada: tasa de fallo actual de cada prueba y si corren contra la app vigente.
- [ ] La causa queda demostrada con la traza de diagnostico de al menos una falla reproducida, o con el fallo de la prueba nueva sobre el codigo sin corregir; H2 y las demas hipotesis quedan descartadas o confirmadas con evidencia.
- [x] Un clic cuyo press no queda agarrado por el objetivo hace fallar la prueba con traza y `<salida>/<nombre>.fallo.png`, nunca un reintento silencioso.
- [ ] Cada una de las cuatro pruebas pasa 25 veces seguidas aislada (100 corridas), sin `SKIP` ni `.fallo.png`.
- [ ] Despues, tres `ctest --output-on-failure` completos seguidos pasan; un `SKIP` grafico no cuenta.
- [ ] Dos grabaciones normales de `guion-ejemplo.json`, con diagnostico apagado y encendido, dan cuadros y audio identicos (D8); D16 del tutorial queda para T024.
- [x] Build `-Werror` y `git diff --check` pasan.

## Verificacion

Linea base y bucle de 100 corridas aisladas (serial), tres `ctest` completos, dos grabaciones normales con `video_tutorial_verificar`, build `-Werror`, `git diff --check`. Ejecutar con `caffeinate -i -s` (ver `docs/development.md`).

## Resultado

La herramienta del video recorre la UI vigente (D6: fuentes sinteticas de Facturas e Historial, `guion-ejemplo.json` ajustado). Causa de la intermitencia: el usuario cambia de app durante la corrida y Qt Quick suelta el agarre del boton entre press y release (mecanismo reproducido por inyeccion; disparador confirmado por el usuario). El grabador descarta `WindowDeactivate` (mitigacion parcial) y, antes de cada release, exige que el objetivo conserve el agarre: si no, falla con mensaje claro y PNG, sin reintento (`Agarre.h`, pruebas `satcfdi_video_agarre` y de desactivacion). Diagnostico opcional `SATCFDI_VIDEO_DIAGNOSTICO_CLIC=1`. Build `-Werror`, `ctest` del video no grafico 26/26 y `ctest` 44/44. Limitaciones: con cambios de app reales siguen perdiendose clics por otra via (11 de 75 corridas en la verificacion detenida); la verificacion 4x25, los 3 `ctest` completos y D8 no se ejecutaron. Seguimiento en T027; D16 del tutorial en T024.

## Referencias

- `docs/tasks/T017-video-tutorial.md`, `docs/meetings/T017-desarrollo/respuestas/09-calidad-cierre.md`, `docs/meetings/T022-refinamiento/`.
