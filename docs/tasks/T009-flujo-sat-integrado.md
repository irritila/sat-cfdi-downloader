# T009: Flujo SAT integrado

## Estado

Pendiente

## Prioridad y tamano

- Prioridad: Critica.
- Tamano: Grande.

## Objetivo

Conectar la UI, persistencia, `SecretStore`, worker, ejecutor serial, `SatGateway` y almacenamiento ZIP para completar el flujo real del MVP.

## Contexto

Esta tarea integra componentes previamente definidos y probados. No debe utilizarse para descubrir simultaneamente el contrato SAT, el modelo de datos, la custodia de credenciales y la navegacion de la UI.

El resultado sigue limitado a uso personal: crear solicitudes masivas, monitorear su estado y guardar paquetes ZIP localmente.

## Alcance

### Incluye

- Crear solicitudes reales de CFDI/XML emitidos y recibidos.
- Mapear filtros UI a `SolicitaDescargaEmitidos` y `SolicitaDescargaRecibidos`.
- Verificar estados mediante `VerificaSolicitudDescarga`.
- Registrar paquetes devueltos por SAT.
- Descargar paquetes ZIP mediante `Descargar`.
- Actualizar estados SAT y locales con codigos separados.
- Registrar logs sanitizados.
- Ejecutar verificaciones y descargas desde worker o acciones manuales.
- Emitir notificaciones nativas para eventos definidos.

### No incluye

- Solicitudes de metadata o folio.
- Extraccion, parsing o indexacion XML.
- Consulta de vigencia o cancelacion de CFDI.
- Reenvio de solicitudes `EnvioFallido` o `EnvioIncierto`.
- Particion automatica de rangos.
- Exportaciones contables.
- Backend remoto, multiusuario o Windows.

## Dependencias

- `T002-shell-qt-qml.md`.
- `T003-persistencia-local.md`.
- `T004-ciclo-vida-macos.md`.
- `T005-secret-store-keychain.md`.
- `T005.1-ui-perfiles-sat.md`.
- `T006-spike-sat.md`.
- `T007-worker-ejecutor-serial.md`.
- `T008-almacenamiento-zip.md`.
- ADR 0013 y ADR 0014.

## Precondiciones

- El spike SAT clasifico las operaciones requeridas como viables o con ajustes ya resueltos.
- Existe al menos un `PerfilSat` con e.firma valida.
- La base local, el ejecutor serial y el almacenamiento ZIP pasan sus pruebas aisladas.
- Los endpoints y SOAP actions se obtienen del contrato vigente, no de ejemplos historicos.

## Trabajo esperado

1. Implementar `SatGateway` productivo conforme al resultado de `T006`.
2. Conectar `ServicioSolicitudes` para validar filtros, deduplicar y crear solicitudes.
3. Conectar `OperacionExecutor` para autenticar y firmar cada operacion que lo requiera.
4. Conectar verificacion SAT con actualizacion transaccional de solicitud, paquetes y log.
5. Conectar descarga SAT con `PackageStorage` y rename del archivo temporal.
6. Conectar errores SAT a estados locales y acciones disponibles.
7. Conectar eventos terminales con `OSIntegration.notificar`.
8. Ejecutar pruebas unitarias con fakes y pruebas de humo controladas contra SAT.

## Reglas de integracion

- La UI nunca llama directamente a SAT ni decide transiciones de estado.
- El worker no crea solicitudes; solo verifica solicitudes existentes y descarga paquetes registrados.
- `Crear solicitud` guarda primero el registro local y despues intenta enviarlo.
- `CodEstatus` de creacion, `EstadoSolicitud`, `CodigoEstadoSolicitud` y codigos de descarga se conservan separadamente.
- La solicitud terminada y sus paquetes se registran en una misma transaccion local.
- El paquete se marca `Descargando` despues de obtener token valido.
- El paquete solo se marca `Descargado` despues de escribir y promover el ZIP final.
- `EnvioFallido` y `EnvioIncierto` son terminales y no se reenvian en el MVP.
- Los paquetes `Descargado` se ignoran en ciclos posteriores.
- Los errores de paquete requieren reintento manual.
- Una solicitud eliminada localmente no recibe resultados de una operacion que termine despues.
- Ninguna respuesta SAT sin sanitizar se persiste.

## Decisiones que debe cerrar esta tarea

- DTOs definitivos entre `SatGateway`, servicios y dominio.
- Mapeo exacto de cada error SAT a estado local y accion disponible.
- Politica de timeout de red por operacion.
- Mensajes visibles para errores de autenticacion, solicitud, verificacion y descarga.
- Condiciones de una prueba de humo SAT repetible sin consumir innecesariamente limites.

## Criterios de aceptacion

- [ ] Una solicitud emitida desde la UI se persiste antes de llamar a SAT.
- [ ] Los filtros emitidos y recibidos generan la operacion SAT correspondiente.
- [ ] La deduplicacion local evita enviar solicitudes equivalentes activas.
- [ ] Una respuesta exitosa de creacion conserva `IdSolicitud`, `CodEstatus` y mensaje de creacion.
- [ ] La verificacion conserva `EstadoSolicitud`, `CodigoEstadoSolicitud` y mensaje de verificacion por separado.
- [ ] Una respuesta terminada registra todos los `IdPaquete` sin duplicarlos.
- [ ] Un paquete disponible puede descargarse y queda asociado a una ruta local final.
- [ ] Los errores de autenticacion, solicitud, verificacion y descarga actualizan el origen correcto.
- [ ] `EnvioFallido` y `EnvioIncierto` no ofrecen reenvio automatico ni manual.
- [ ] Un error de paquete permite reintento manual sin duplicar el paquete.
- [ ] El worker no crea solicitudes por su cuenta.
- [ ] Las notificaciones se emiten para terminada, descarga concluida, error, rechazo y vencimiento.
- [ ] Las notificaciones no contienen tokens, firmas, contrasenas ni payloads sensibles.
- [ ] Los logs no contienen secretos, bytes ni base64 del ZIP.
- [ ] El flujo funciona despues de cerrar y reabrir la app.
- [ ] La prueba de humo usa un rango controlado y deja evidencia sanitizada.
- [ ] No se agrega parsing XML, metadata SAT ni funcionalidad comercial.

## Verificacion

1. Ejecutar pruebas unitarias con `FakeSatGateway`, `FakeSecretStore` y `FakePackageStorage`.
2. Probar emitidos y recibidos con filtros validos e invalidos.
3. Probar duplicado local antes de tocar SAT.
4. Simular respuestas de creacion, verificacion, descarga, timeout y errores SAT.
5. Ejecutar una prueba de humo controlada contra SAT con credenciales autorizadas.
6. Revisar SQLite, logs, notificaciones y carpeta de paquetes.
7. Cerrar y reabrir la app con solicitudes pendientes y paquetes disponibles.
8. Ejecutar `ctest --test-dir build`.

## Definicion de terminado

- El flujo real de solicitud, verificacion, registro de paquetes y descarga funciona.
- La UI muestra estados y errores sin conocer detalles SOAP.
- El worker y las acciones manuales usan la misma ruta de ejecucion serial.
- Los ZIP quedan fuera de SQLite y los logs quedan sanitizados.
- La prueba de humo y las pruebas con fakes tienen evidencia registrada.
- `T010` puede ejecutar las pruebas de aceptacion del MVP sin dependencias faltantes.

## Resultado

Pendiente.

## Riesgos y notas

- Una prueba SAT exitosa no garantiza todos los rangos o filtros posibles.
- Las credenciales y limites del SAT deben tratarse como recursos reales del usuario.
- Esta tarea no convierte la app en fuente de verdad fiscal ni agrega consulta de vigencia CFDI.

## Referencias

- `T005-secret-store-keychain.md`
- `T006-spike-sat.md`
- `T007-worker-ejecutor-serial.md`
- `T008-almacenamiento-zip.md`
- `docs/web-service.md`
- `docs/design/operational-rules.md`
- ADR 0013
- ADR 0014
