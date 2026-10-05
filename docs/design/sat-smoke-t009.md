# Prueba de humo SAT de T009

Evidencia sanitizada de la prueba real (D2) del flujo integrado de T009. La ejecuto el usuario en su Mac con el build firmado; el coordinador
solo consulto en modo de solo lectura la base local (estados, codigos y conteos) y la carpeta de paquetes (permisos y tamanos). No se registran
RFC, `IdSolicitud`, `IdPaquete`, rutas personales, tokens ni contenido de los ZIP.

## Resumen

| Campo | Valor |
| --- | --- |
| Fecha | 2026-10-05 |
| Build | `build-signed` (identidad Apple Development, hardened runtime, Keychain data protection) |
| Operacion | `SolicitaDescargaEmitidos` (solo filtros obligatorios, rango cerrado distinto al de T006) |
| Solicitudes reales | 1 (tope D2: 2) |
| CFDI reportados por el SAT | 2 |
| Paquetes | 1 |
| Resultado | Flujo completo correcto |

## Estados recorridos

| Hora local | Evento | Origen | Resultado |
| --- | --- | --- | --- |
| 18:08:23 | `solicitud_creada` | usuario | `Creada`, sin trafico SAT |
| 18:08:23 | `envio_iniciado` | usuario | `Enviando` |
| 18:08:24 | `solicitud_enviada` | usuario | `CodEstatus` 5000, `Enviada` con `IdSolicitud` |
| 18:09:08 | `verificacion_realizada` | usuario (`Verificar ahora`) | `CodEstatus` 5000, SAT aun en proceso |
| 18:45:00 | `verificacion_realizada` | worker | `CodEstatus` 5000, `EstadoSolicitud` Terminada, `CodigoEstadoSolicitud` 5000, 2 CFDI |
| 18:45:00 | `paquetes_registrados` | worker | 1 paquete `Disponible` |
| 18:45:00 | `descarga_iniciada` | worker | descarga automatica |
| 18:45:01 | `paquete_descargado` | worker | `CodEstatus` 5000, paquete `Descargado` |

Sin `ultimo_error` en la solicitud ni en el paquete.

## Notificaciones observadas por el usuario

- `Terminada` con 1 paquete.
- `Descarga completa: 1 de 1`.

## Paquetes

| Paquete | Tamano | Permisos del archivo | Permisos de la carpeta `paquetes` |
| --- | --- | --- | --- |
| `<paquete-1>.zip` | 6,610 bytes | `600` | `700` |

## Reapertura

El usuario cerro la app desde el menu bar y la reabrio: la solicitud y el paquete conservaron su estado.

## Revision de datos tras el humo

- SQLite: sin token, sin valores base64 de 200 o mas caracteres, sin RFC en `mensaje_solicitud_sat`, `mensaje_verificacion_sat` ni
  `ultimo_error`.
- `log_solicitud`: el evento `solicitud_creada` guarda los filtros normalizados (incluido el RFC) en `payload_resumen_json`, como define el
  modelo fisico (`sqlite-physical-model.md`). No es un mensaje ni una notificacion; se mantiene como esta.
- Escaneo del arbol (incluidos archivos sin seguimiento, excluido `docs/meetings`): 0 coincidencias del RFC, `IdSolicitud` e `IdPaquete`
  reales; 0 rutas personales; 0 tokens; 0 cadenas base64 de 200 o mas; sin archivos `.cer`, `.key`, `.pfx`, `.p12` ni `.zip`. Las 2
  coincidencias de cabeceras PEM son centinelas sinteticos preexistentes de pruebas (`TestSqliteCentinelas.cpp`, `TestRegexLogSanitizer.cpp`).

## Limites

- `SolicitaDescargaRecibidos` sigue `No probada` contra el SAT real. El usuario decidio cerrar T009 con la prueba de emitidos
  (2026-10-05); recibidos se valida en T010 y, mientras tanto, la UI la sigue ofreciendo.
- Fault SOAP, token rechazado y codigos 5003/5008 en produccion no se observaron; estan cubiertos con fixtures sinteticos.
