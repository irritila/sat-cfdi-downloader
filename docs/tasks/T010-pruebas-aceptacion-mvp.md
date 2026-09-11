# T010: Pruebas de aceptacion del MVP

## Estado

Pendiente

## Prioridad y tamano

- Prioridad: Critica.
- Tamano: Grande.

## Objetivo

Verificar que el MVP personal funciona de extremo a extremo en una build empaquetada de macOS y que se recupera de los fallos previsibles definidos en la arquitectura.

## Contexto

Esta tarea es el cierre de la primera version utilizable. La app sera usada por una sola persona durante varios anos; la trazabilidad, la recuperacion y la ausencia de perdida silenciosa de datos importan mas que agregar funciones fuera del alcance.

## Alcance

### Incluye

- Pruebas automatizadas de dominio, aplicacion, repositorios y adaptadores fake.
- Checklist manual de UI y ciclo de vida macOS.
- Flujo controlado de crear solicitud, verificar, registrar paquetes y descargar ZIP.
- Reinicio durante envio, verificacion y descarga.
- Pausa, reanudacion, backoff y acciones pendientes.
- Estados SAT, estados locales y estados de paquetes.
- Keychain, Login Item, menu bar y notificaciones.
- Archivos finales, temporales y huerfanos.
- Revision de logs y ausencia de secretos.

### No incluye

- Pruebas de multiusuario, roles o clientes.
- Pruebas Windows.
- Validacion fiscal del contenido XML.
- Extraccion, parsing o indexacion XML.
- Solicitudes de metadata.
- Consulta de vigencia o cancelacion CFDI.
- Pruebas de rendimiento de un sistema distribuido.

## Dependencias

- `T004-ciclo-vida-macos.md`.
- `T005-secret-store-keychain.md`.
- `T006-spike-sat.md`.
- `T007-worker-ejecutor-serial.md`.
- `T008-almacenamiento-zip.md`.
- `T009-flujo-sat-integrado.md`.
- `docs/requirements.md`.

## Trabajo esperado

1. Convertir los criterios `CA-001` a `CA-015` en casos de prueba o checklist.
2. Ejecutar pruebas automatizadas en una base y carpeta temporales.
3. Ejecutar pruebas manuales de UI sobre una build empaquetada.
4. Ejecutar un flujo SAT controlado con credenciales autorizadas.
5. Simular errores, pausas, cierres y reinicios sin destruir datos del usuario.
6. Revisar base local, logs, notificaciones y carpeta de ZIPs.
7. Registrar evidencia y defectos restantes en este archivo o en un reporte relacionado.
8. Emitir un veredicto `Aprobado`, `Aprobado con riesgos` o `No aprobado`.

## Niveles de verificacion

- `Automatizada`: tests repetibles sin sistema SAT ni credenciales reales.
- `Manual local`: UI, bundle, menu bar, Login Item, Keychain y archivos locales.
- `Smoke SAT`: flujo real pequeno y controlado; no sustituye pruebas exhaustivas del SAT.

## Reglas de seguridad y operacion

- Usar credenciales y rangos SAT autorizados para pruebas.
- No guardar credenciales, tokens, firmas, ZIPs ni payloads sensibles como evidencia.
- Sanitizar logs antes de conservarlos.
- No ejecutar pruebas que repitan solicitudes equivalentes innecesariamente.
- No probar con una carpeta de paquetes que contenga archivos importantes del usuario.
- Los defectos que impliquen perdida silenciosa de solicitud, paquete o trazabilidad bloquean la aprobacion.

## Criterios de aceptacion

- [ ] La app compilada abre como bundle macOS.
- [ ] La apertura manual muestra la ventana principal.
- [ ] El cierre de ventana oculta la app y conserva el proceso.
- [ ] El menu bar permite abrir, pausar/reanudar y salir explicitamente.
- [ ] El Login Item esta apagado por defecto y el arranque automatico no abre la ventana.
- [ ] Las notificaciones funcionan con permiso concedido y el flujo principal continua con permiso denegado.
- [ ] Las solicitudes sobreviven al reinicio y conservan su metadata.
- [ ] El worker respeta pausa, backoff y acciones pendientes.
- [ ] Los estados SAT, locales y de paquetes se muestran sin mezclarse.
- [ ] Crear una solicitud valida evita duplicados locales antes de tocar SAT.
- [ ] Una solicitud terminada registra sus paquetes sin duplicarlos.
- [ ] La descarga usa temporal, rename y estado final en el orden definido.
- [ ] Las interrupciones de envio y descarga se recuperan segun las reglas documentadas.
- [ ] Los ZIP finales, temporales y huerfanos se reconcilian sin perdida silenciosa.
- [ ] Un ZIP ausente se muestra en el detalle sin cambiar automaticamente el estado persistido.
- [ ] Los errores SAT conservan codigo, mensaje, origen y fecha.
- [ ] Los paquetes con error solo se reintentan por accion manual.
- [ ] La eliminacion local no modifica SAT ni borra ZIPs fisicos.
- [ ] No aparecen secretos en SQLite, logs, mensajes, archivos temporales ni evidencia.
- [ ] La UI no contiene funciones fuera de alcance del MVP.
- [ ] `ctest --test-dir build` pasa.

## Verificacion

1. Ejecutar `ctest --test-dir build`.
2. Ejecutar el checklist manual sobre la build empaquetada.
3. Probar reinicio con solicitudes en estados `Creada`, `Enviando`, `Enviada`, `Descargando` y con paquetes disponibles.
4. Probar pausa persistente, reanudacion y acciones pendientes.
5. Probar Login Item, menu bar, cierre, reapertura, salida y notificaciones.
6. Probar Keychain con credencial valida, invalida y acceso denegado.
7. Ejecutar el smoke test SAT con rango controlado.
8. Revisar SQLite, logs sanitizados y carpeta temporal de paquetes.
9. Registrar resultados, riesgos aceptados y defectos bloqueantes.

## Definicion de terminado

- Todos los criterios de aceptacion estan marcados con evidencia.
- Las pruebas automatizadas pasan en una build reproducible.
- El smoke test SAT tiene resultado y evidencia sanitizada.
- Los escenarios de reinicio y recuperacion no pierden datos.
- Los defectos restantes estan clasificados como bloqueantes o riesgos aceptados.
- Existe un veredicto final de aprobacion del MVP.

## Resultado

Pendiente.

## Veredicto

Pendiente: `Aprobado`, `Aprobado con riesgos` o `No aprobado`.

## Riesgos y notas

- Una aprobacion no convierte la app en fuente de verdad fiscal ni valida el contenido de los XML.
- El comportamiento del SAT puede variar por disponibilidad, limites y respuestas no cubiertas por el smoke test.
- La app sigue siendo personal, local y exclusiva para macOS en esta version.

## Referencias

- `docs/requirements.md`
- `docs/architecture.md`
- `docs/design/operational-rules.md`
- `T004-ciclo-vida-macos.md`
- `T005-secret-store-keychain.md`
- `T006-spike-sat.md`
- `T007-worker-ejecutor-serial.md`
- `T008-almacenamiento-zip.md`
- `T009-flujo-sat-integrado.md`
