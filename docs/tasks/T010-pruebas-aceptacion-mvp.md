# T010: Pruebas de aceptacion del MVP

## Estado

Pendiente (refinada 2026-10-04; lista para ejecutar cuando se cumplan sus dependencias).

## Prioridad y tamano

- Prioridad: Critica.
- Tamano: Grande.

## Objetivo

Verificar el MVP personal de extremo a extremo sobre el bundle definitivo, autocontenido y firmado (T011) e instalado en `/Applications`, y emitir un veredicto `Aprobado`, `Aprobado con riesgos` o `No aprobado` con evidencia trazable a `CA-001`..`CA-015`.

## Contexto

Es el cierre de la primera version utilizable. La app la usara una sola persona durante anos: importan mas la trazabilidad, la recuperacion y la ausencia de perdida silenciosa de datos que las funciones nuevas.

T004-T009 ya definen pruebas automatizadas de transiciones, agenda, recuperacion, almacenamiento, mapeo SAT y sanitizacion. T010 no las reimplementa: las ejecuta, las referencia y cubre los huecos y lo que solo puede verificarse sobre el bundle real.

## Decisiones

| ID | Decision | Estado | Fuente y motivo |
| --- | --- | --- | --- |
| D1 | T010 no crea solicitudes SAT. Sobre el bundle definitivo reutiliza la base con la solicitud de T009: `Verificar ahora` y descarga si quedan paquetes vigentes. Si ya vencieron, se acepta la evidencia de T009 y el resto del flujo se prueba con fakes. | Aprobada por usuario | Refinamiento 2026-10-04. No consume cupo SAT. |
| D2 | La aceptacion corre sobre el bundle definitivo de T011, instalado en `/Applications`. Las pruebas destructivas (recuperacion, archivos, escala) usan un `--data-dir` aislado y un perfil SAT de prueba. | Aprobada por usuario | Refinamiento 2026-10-04. Calidad proponia un bundle id de prueba y un usuario macOS dedicado; el usuario prefirio probar el artefacto real. |
| D3 | La firma es Developer ID (T011 D1). Un perfil gratuito de 7 dias no es aceptable para aprobar. | Aprobada por usuario | Refinamiento 2026-10-04. |
| D4 | Matriz de trazabilidad: cada `CA` se mapea a pruebas existentes (T004-T009), a casos dirigidos nuevos o al checklist del bundle. Los huecos se cubren con casos dirigidos (seccion Huecos). | Recomendacion tecnica | `qt-quality-engineer`. |
| D5 | Escala (RNF-006): base sintetica aislada con solicitudes y paquetes cuyo `NumeroCFDIs` sume 10,000, mas una variante de estres con muchas filas de paquetes, sin XML reales. Se mide con `QElapsedTimer` en tres corridas en frio y tres en caliente, en un Mac identificado. | Recomendacion tecnica | `qt-quality-engineer`. Los umbrales estan en los criterios. |
| D6 | Reporte en `docs/reports/T010-aceptacion-mvp-AAAA-MM-DD.md`, sanitizado, con build (commit, hash del bundle, firma, macOS y hardware), la matriz, `ctest` por label, el checklist del bundle, la parte SAT, los defectos y riesgos, y el veredicto. Cada caso registra ID, precondiciones, datos, accion, esperado, observado, evidencia y estado (`Pasa`, `Falla`, `Bloqueado` o `No probado`). | Recomendacion tecnica | `qt-quality-engineer`. |
| D7 | Veredicto: `No aprobado` si hay perdida silenciosa, un defecto de seguridad o trazabilidad, un flujo critico fallido o un criterio obligatorio sin verificar. `Aprobado con riesgos` solo con riesgos explicitos, no bloqueantes, mitigados y aceptados por el usuario. `Aprobado` si todo lo obligatorio pasa. Nada `No probado` cuenta como aprobado. | Recomendacion tecnica | `qt-quality-engineer`, version previa de T010. |
| D8 | Mitigacion de D2: antes de las pruebas se registra el estado del Login Item y de los permisos de notificacion, y al terminar se restaura. Las pruebas de Keychain usan solo el perfil SAT de prueba; nunca se borra ni reemplaza la e.firma del perfil real. | Recomendacion tecnica | Coordinador, ante el riesgo de aislamiento senalado por `qt-quality-engineer`. |

## Huecos que T010 cubre con casos dirigidos

- `CA-002`: crear un duplicado activo muestra la solicitud existente y no genera trafico hacia el puerto SAT.
- `CA-003`: la lista persistida muestra todas las solicitudes no eliminadas con su estado tras reiniciar.
- `CA-005`: el detalle muestra parametros, estados, codigos SAT separados, mensajes, paquetes con existencia del ZIP y logs basicos.
- `CA-015`: eliminar una solicitud local no genera trafico SAT y no borra ZIP fisicos.
- `CA-012`: notificacion real con permiso concedido (pendiente desde T004; se cierra en T011 y se reconfirma aqui).

## Alcance

### Incluye

- Ejecutar `ctest` completo (labels `unit`, `infrastructure`, `integration`, `presentation`, `secrets`, `sat-spike`) y referenciar sus resultados.
- Los casos dirigidos de los huecos y el checklist manual del bundle: ciclo de vida, menu bar y estados del worker, Login Item, Keychain, notificaciones, reinicios, archivos y mensajes.
- La parte SAT segun D1.
- La medicion de escala (D5).
- La revision de SQLite, logs, notificaciones, carpeta de paquetes y evidencia en busca de secretos.
- El reporte y el veredicto (D6, D7).

### No incluye

- Crear el bundle ni firmarlo (`T011`).
- Reimplementar pruebas de T004-T009.
- Solicitudes SAT nuevas.
- Multiusuario, Windows, validacion fiscal, parsing XML, metadata o consulta de vigencia CFDI.

## Dependencias

- `T004`-`T009` implementadas, con sus pruebas aprobadas y la evidencia de T006 y T009.
- `T011` completada: bundle autocontenido, Developer ID e instalacion en `/Applications`.
- `docs/requirements.md` §15.

## Reglas de seguridad y operacion

- No se guardan credenciales, tokens, firmas, ZIP ni payloads sensibles como evidencia; RFC e Ids van enmascarados (T006 D6).
- Las pruebas destructivas nunca usan la base ni la carpeta de paquetes reales.
- Los defectos de perdida silenciosa de solicitud, paquete o trazabilidad bloquean la aprobacion.

## Criterios de aceptacion

### Base automatizada

- [ ] `ctest --test-dir build --output-on-failure` pasa en una build reproducible del commit candidato. El reporte registra el resultado por label.
- [ ] La matriz cubre `CA-001`..`CA-015` y cada criterio de este archivo con una evidencia existente o un caso de T010. Ninguna fila queda sin evidencia.

### Bundle y ciclo de vida (checklist sobre `/Applications`)

- [ ] El bundle de T011 abre por apertura manual con la ventana principal; cerrar la ventana conserva el proceso en el menu bar; `Salir` termina el proceso.
- [ ] El menu bar muestra los estados del worker de T007 D2 (`Monitoreo activo`, `Monitoreo pausado`, `Trabajando...`, `Pendientes: N`) y permite abrir, pausar o reanudar y salir.
- [ ] El Login Item esta apagado por defecto. Al habilitarlo y volver a iniciar sesion, la app arranca sin ventana y el worker monitorea (`CA-009`, `CA-011`). El estado original se restaura (D8).
- [ ] Las notificaciones se entregan con el permiso concedido. Con el permiso denegado, el flujo continua y la UI indica que estan deshabilitadas (`CA-012`).
- [ ] El Keychain guarda y lee la e.firma del perfil de prueba sin dialogos. Una contrasena incorrecta y un acceso denegado muestran los mensajes del catalogo de T009 (`CA-001`).

### Datos, recuperacion y archivos (`--data-dir` aislado)

- [ ] Los casos dirigidos de `CA-002`, `CA-003`, `CA-005` y `CA-015` pasan (seccion Huecos).
- [ ] Tras forzar la terminacion de la app con solicitudes en `Creada`, `Enviando`, `Enviada` y paquetes en `Descargando` (con y sin archivo final), al reabrir los estados quedan segun T007 y T008, sin perdida y con un log por interrupcion (`CA-008`).
- [ ] La pausa persiste tras reiniciar, y `Verificar ahora` y `Reintentar descarga` quedan pendientes hasta reanudar (`CA-013`, `CA-014`).
- [ ] Un ZIP borrado a mano aparece como "archivo no encontrado" en el detalle, sin cambiar el estado persistido. Los temporales y huerfanos sembrados se tratan segun T008 sin borrar archivos finales.

### SAT (D1)

- [ ] Sobre el bundle definitivo, `Verificar ahora` de la solicitud de T009 actualiza su estado y, si quedan paquetes vigentes, los descarga a la ruta de T008. Si no quedan, el reporte lo declara y referencia la evidencia de T009.

### Escala (D5)

- [ ] Con la base sintetica de 10,000 CFDI: primera lista visible y poblada con p95 de 2 s o menos; detalle con paquetes y logs con p95 de 500 ms o menos; ciclo interno del worker con `FakeOperacionesSat` con p95 de 2 s o menos; ninguna accion principal bloquea la UI mas de 100 ms.

### Seguridad y alcance

- [ ] El escaneo de secretos de T006 sobre SQLite (volcado), logs, notificaciones, temporales y evidencia da resultado vacio.
- [ ] La UI no ofrece funciones fuera del alcance del MVP.

### Cierre

- [ ] El reporte D6 existe con todos los casos y el veredicto segun D7. Los defectos quedan clasificados como bloqueantes o riesgos aceptados, con la aceptacion explicita del usuario para estos ultimos.

## Verificacion

1. `ctest --test-dir build --output-on-failure` en el commit candidato.
2. El checklist manual sobre `/Applications/SAT CFDI Downloader.app`.
3. Los casos dirigidos y de recuperacion con `--data-dir` aislado.
4. La parte SAT (D1), ejecutada por el usuario.
5. La medicion de escala con la base sintetica.
6. El escaneo de secretos y la redaccion del reporte.

## Definicion de terminado

- Todos los criterios tienen evidencia en el reporte.
- El veredicto final esta emitido con sus riesgos y defectos clasificados.

## Resultado

Pendiente.

## Veredicto

Pendiente: `Aprobado`, `Aprobado con riesgos` o `No aprobado`.

## Riesgos y notas

- Probar sobre el bundle definitivo (D2) toca el Keychain y el Login Item reales; la mitigacion D8 reduce el riesgo, pero no lo elimina.
- Si los paquetes de T009 ya vencieron, la descarga real sobre el bundle final no se reconfirma (D1).
- Una aprobacion no convierte la app en fuente de verdad fiscal. El SAT puede variar por disponibilidad y limites.
- La pregunta abierta de `requirements.md` §16 (nivel de detalle del log visible) no bloquea: se acepta el detalle definido por T009.

## Referencias

- `docs/requirements.md` §15-16 y RNF-006..008
- `docs/milestones/m1-app-desktop.md`
- `T004`-`T009`, `T011`
- `docs/meetings/T010-refinamiento/` (bitacora y rondas del refinamiento)
