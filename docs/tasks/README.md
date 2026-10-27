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
| [T003](T003-persistencia-local.md) | Completada | Persistencia local de solicitudes |
| [T004](T004-ciclo-vida-macos.md) | Completada | Ciclo de vida macOS y menu bar |
| [T005](T005-secret-store-keychain.md) | Completada | SecretStore con Keychain |
| [T005.1](T005.1-ui-perfiles-sat.md) | Completada | UI de perfiles SAT y e.firma |
| [T006](T006-spike-sat.md) | Completada | Spike de integracion SAT |
| [T007](T007-worker-ejecutor-serial.md) | Completada | Worker y ejecutor serial |
| [T008](T008-almacenamiento-zip.md) | Completada | Almacenamiento local de paquetes ZIP |
| [T009](T009-flujo-sat-integrado.md) | Completada | Flujo SAT integrado |
| [T009.1](T009.1-acceso-paquetes.md) | Completada | Acceso a paquetes descargados |
| [T012](T012-manual-usuario.md) | Completada | Manual de usuario con capturas |
| [T013](T013-rediseno-visual.md) | Completada | Rediseno visual de la UI |
| [T014](T014-mejoras-uso.md) | Completada | Mejoras de uso (sugerencias de Claude Design) |
| [T014.1](T014.1-mejoras-pantalla.md) | Completada | Mejoras de pantalla |
| [T014.2](T014.2-reintento-por-paquete.md) | Completada | Reintento por paquete |
| [T014.3](T014.3-primer-uso-vencimiento-efirma.md) | Completada | Primer uso guiado y vencimiento de e.firma |
| [T014.4](T014.4-notificaciones-accionables-icono.md) | Completada | Notificaciones accionables e icono con estado |
| [T011](T011-empaquetado-firma-local.md) | Pendiente | Empaquetado y firma local (previa a T010) |
| [T010](T010-pruebas-aceptacion-mvp.md) | Pendiente | Pruebas de aceptacion del MVP |
| [T015](T015-explorador-cfdi.md) | Completada | Explorador de CFDI con previsualizacion PDF (M2, epica) |
| [T015.1](T015.1-lector-zip-parser-cfdi.md) | Completada | Lector de ZIP seguro y parser de CFDI |
| [T015.2](T015.2-indice-cfdi.md) | Completada | Indice SQLite de CFDI |
| [T015.3](T015.3-explorador-por-solicitud.md) | Completada | Explorador de CFDI por solicitud |
| [T015.4](T015.4-pdf-qr-visor.md) | Completada | PDF con QR, visor y acciones |
| [T016](T016-manual-t014-3-t014-4.md) | Completada | Manual de usuario al dia con T014.3 y T014.4 |
| [T018](T018-roles-produccion-video.md) | Completada | Roles de guion, audio y motion design (previa a T017) |
| [T017](T017-video-tutorial.md) | Completada | Video tutorial automatizado con voz, subtitulos y musica |
| [T019](T019-renombrar-archivo-fiscal.md) | Completada | Renombrar la app a "Archivo Fiscal" |
| [T020](T020-logo.md) | Completada | Logo e iconos en SVG |
| [T021](T021-manual-capturas-al-dia.md) | Absorbida por T024 | Manual y capturas al dia con la app |
| [T022](T022-intermitencia-pruebas-graficas-video.md) | Completada con limitaciones | Intermitencia de pruebas graficas del video |
| [T023](T023-rediseno-ui-caso-de-uso.md) | Completada | Rediseno de la UI centrado en las facturas del perfil fiscal (epica) |
| [T023.1](T023.1-prototipo-y-adr-cobertura.md) | Completada | Prototipo navegable y ADR de cobertura mensual |
| [T023.2](T023.2-catalogo-mensual-y-disponibilidad.md) | Completada | Catalogo mensual y regla de disponibilidad |
| [T023.3](T023.3-descarga-guiada.md) | Completada | Descarga guiada por mes y tramos |
| [T023.4](T023.4-pantalla-facturas.md) | Completada | Pantalla Facturas |
| [T023.5](T023.5-historial-diagnostico-notificaciones.md) | Completada | Historial, diagnostico, notificaciones y menu bar |
| [T023.6](T023.6-sin-facturas-5004.md) | Completada | Periodo sin facturas (Rechazada 5004) no es error |
| [T024](T024-vocabulario-y-materiales-flujo-facturas.md) | Completada | Vocabulario y materiales del flujo centrado en facturas (epica) |
| [T024.1](T024.1-vocabulario-y-notificaciones.md) | Completada | Vocabulario de la app y notificaciones |
| [T024.2](T024.2-fakes-sinteticos-materiales.md) | Completada | Fakes sinteticos comunes para capturas y video |
| [T024.3](T024.3-manual-y-capturas.md) | Completada | Manual y capturas al dia |
| [T024.4](T024.4-video-tutorial-nuevo.md) | Completada | Video tutorial nuevo |
| [T025](T025-respaldo-en-la-nube.md) | Completada | Respaldo cifrado en la nube del usuario (epica) |
| [T025.1](T025.1-formato-y-cifrado-respaldo.md) | Completada | Formato y cifrado del respaldo |
| [T025.2](T025.2-captura-consistente.md) | Completada | Captura consistente de los datos |
| [T025.3](T025.3-respaldo-manual.md) | Completada | Respaldo manual a la carpeta elegida |
| [T025.4](T025.4-respaldo-diario.md) | Completada | Configuracion y respaldo diario |
| [T025.5](T025.5-restauracion.md) | Completada | Restauracion transaccional |
| [T025.6](T025.6-ui-y-documentacion-respaldo.md) | Completada | UI y documentacion del respaldo |
| [T026](T026-exportar-facturas.md) | Completada | Exportar facturas (epica) |
| [T026.1](T026.1-plan-y-escritura-zip.md) | Completada | Plan de exportacion y escritura atomica del ZIP |
| [T026.2](T026.2-servicio-exportacion.md) | Completada | Servicio de exportacion |
| [T026.3](T026.3-ui-exportacion.md) | Completada | UI y documentacion de la exportacion |
| [T027](T027-clic-perdido-cambio-de-app-video.md) | Pendiente (sin refinar, baja) | Clic perdido por cambio de app en pruebas del video |

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
