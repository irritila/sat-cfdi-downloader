# Resultados del spike SAT (T006)

Fecha de la corrida real: 2026-10-05 (UTC). Ejecutada por el usuario en su Mac con su e.firma vigente, contra produccion SAT,
con la CLI `satcfdi_sat_spike` (`tools/sat_spike/`). Esta evidencia esta enmascarada: no contiene RFC, `IdSolicitud`,
`IdPaquete`, serial, issuer, token, firma, certificado ni contenido de paquetes.

## WSDL inspeccionados

Descargados el 2026-10-04T21:52:16Z (GET publico).

| Servicio | URL | HTTP | SHA-256 |
| --- | --- | --- | --- |
| Autenticacion | `https://cfdidescargamasivasolicitud.clouda.sat.gob.mx/Autenticacion/Autenticacion.svc?singleWsdl` | 200 | `fc279f4266a6e22b9d67ffb2b7778467f02fae5810610b044dbb4d2d148d1bfb` |
| Solicitud | `https://cfdidescargamasivasolicitud.clouda.sat.gob.mx/SolicitaDescargaService.svc?singleWsdl` | 200 | `f48bb36dc0d95e730f1ecaffe320f173b04f012d1732e5963e8a081f21d8c668` |
| Verificacion | `https://cfdidescargamasivasolicitud.clouda.sat.gob.mx/VerificaSolicitudDescargaService.svc?singleWsdl` | 200 | `a2eee875f33efa5c8d783a37b91ed5bae7f648cd9147518a024fbe00c2325973` |
| Descarga | `https://cfdidescargamasiva.clouda.sat.gob.mx/DescargaMasivaService.svc` (`?singleWsdl`, `?wsdl`, sin query) | 400 | - |

| Operacion | SOAPAction | Namespace | Fuente |
| --- | --- | --- | --- |
| `Autentica` | `http://DescargaMasivaTerceros.gob.mx/IAutenticacion/Autentica` | `http://DescargaMasivaTerceros.gob.mx` | WSDL |
| `SolicitaDescargaEmitidos` | `http://DescargaMasivaTerceros.sat.gob.mx/ISolicitaDescargaService/SolicitaDescargaEmitidos` | `http://DescargaMasivaTerceros.sat.gob.mx` | WSDL |
| `SolicitaDescargaRecibidos` | `http://DescargaMasivaTerceros.sat.gob.mx/ISolicitaDescargaService/SolicitaDescargaRecibidos` | `http://DescargaMasivaTerceros.sat.gob.mx` | WSDL |
| `VerificaSolicitudDescarga` | `http://DescargaMasivaTerceros.sat.gob.mx/IVerificaSolicitudDescargaService/VerificaSolicitudDescarga` | `http://DescargaMasivaTerceros.sat.gob.mx` | WSDL |
| `Descargar` | `http://DescargaMasivaTerceros.sat.gob.mx/IDescargaMasivaTercerosService/Descargar` | `http://DescargaMasivaTerceros.sat.gob.mx` | Doc §7 + phpcfdi; confirmado por la corrida |

## Operaciones ejecutadas

| Operacion | Tipo | Resultado observado | Clasificacion |
| --- | --- | --- | --- |
| `Autentica` | Real (3 veces) | `RespuestaExplicita`, HTTP 200, token opaco de 395 caracteres, `Created`/`Expires` con TTL de 300 s | Viable |
| `SolicitaDescargaEmitidos` | Real (1 solicitud, dia cerrado con 2 CFDI) | HTTP 200, `CodEstatus=5000`, `Mensaje="Solicitud Aceptada"`, `IdSolicitud` devuelto | Viable |
| `SolicitaDescargaRecibidos` | Solo `dry-run` y golden | Sobre construido con la misma firma que emitidos; no ejecutado | No probada (forma confirmada por WSDL) |
| `VerificaSolicitudDescarga` | Real (1 con Id valido) | HTTP 200, `CodEstatus=5000`, `EstadoSolicitud=3`, `CodigoEstadoSolicitud=5000`, `NumeroCFDIs=2`, 1 `IdPaquete` (unos 5 min despues de solicitar) | Viable |
| `VerificaSolicitudDescarga` con Id invalido | Real (accidental, con un texto no UUID) | HTTP 200, `CodEstatus=301`, `Mensaje="XML Mal Formado"`, `EstadoSolicitud=0` | Evidencia de rechazo |
| `Descargar` | Real (1 paquete) | HTTP 200, `CodEstatus=5000`, `Mensaje="Solicitud Aceptada"`, `Paquete` de 6610 bytes (ZIP guardado 0600 fuera del repo) | Viable |

Totales: 1 solicitud real de 3 permitidas; ningun timeout; ningun `DespuesDeEnvio`; ningun reenvio. Ningun SOAP Fault real
observado: el parser de Faults se valida con un fixture `sintetico` (D7).

## Puntos de firma

| Punto | Resultado |
| --- | --- |
| C14N por operacion | `Autentica`: exclusiva (`xml-exc-c14n`) en `CanonicalizationMethod` y `Transform`. Solicitud, verificacion y descarga: inclusiva (`REC-xml-c14n-20010315`). Ambas aceptadas. No se uso `--c14n-declarada`. |
| Nodo del digest enveloped | `Reference URI=""` con `enveloped-signature`, digest calculado sobre el nodo de peticion (`solicitud`/`peticionDescarga`) sin la firma. Aceptado. |
| Orden de atributos | Atributos en orden canonico (alfabetico) y sin atributos vacios. Aceptado. |
| `Timestamp` | UTC con milisegundos y `Z`, ventana de 5 minutos. Aceptado. |
| `KeyInfo` | `Autentica`: `SecurityTokenReference` al `BinarySecurityToken`. Demas: `X509IssuerSerial` (issuer en formato `docsat`, serial decimal) + `X509Certificate`. Aceptado. |
| WS-Addressing (`To`/`Action`) | No se envian; `Autentica` aceptada sin ellos. `--ws-addressing` no hizo falta. |
| RSA-SHA1 | Aceptado por el SAT; OpenSSL 3.6 lo permite localmente. |
| Header de token | `Authorization: WRAP access_token="<token>"` aceptado en solicitud, verificacion y descarga. |
| TTL del token | 300 s (`Expires - Created`). Cada proceso de la CLI autentica de nuevo; el token no se reutilizo despues de expirar. |
| e.firma frente a CSD | e.firma aceptada. La regla local `OU` no vacio => CSD (T005) no rechazo la e.firma del usuario. |

## Discrepancias con `docs/web-service.md`

1. El WSDL de descarga no se publica en el host productivo (HTTP 400 con cualquier query). La forma de `Descargar`
   (`PeticionDescargaMasivaTercerosEntrada/peticionDescarga` con `IdPaquete` y `RfcSolicitante`; respuesta con `Paquete`)
   se tomo de phpcfdi y quedo confirmada por la corrida.
2. El `Mensaje` de una descarga exitosa fue `"Solicitud Aceptada"`, no `"Solicitud de descarga recibida con exito"`.
3. El ejemplo de §4.2 transcribe mal el `DigestValue` (`...sSncl6...` en lugar de `...sSncI6...`).
4. El ejemplo de autenticacion incluye `To`/`Action` (WS-Addressing); el SAT acepta la autenticacion sin ellos.
5. Formatos observados: `IdSolicitud` es un UUID en minusculas; `IdPaquete` es un UUID en mayusculas con sufijo `_NN`.

## Recomendacion de contrato para `SatGateway` (T009)

Confirma D8 con ajustes:

- `MaterialFirma` move-only por operacion; el adaptador no conserva material entre operaciones.
- Token opaco solo en memoria con su `Expires`; reutilizable mientras falte margen (por ejemplo, mas de 60 s) dentro de un mismo
  ciclo del ejecutor; nunca persistido. El adaptador construye `Authorization: WRAP access_token="..."`.
- Operaciones: `autenticar()`, `solicitar(operacion, filtros)`, `verificar(idSolicitud)`, `descargar(idPaquete)`, con DTOs que
  separan `CodEstatus`, `Mensaje`, `EstadoSolicitud`, `CodigoEstadoSolicitud`, `NumeroCFDIs`, `IdsPaquetes` y el paquete como
  bytes que se entregan a `PackageStorage` sin loguear.
- Resultado de transporte con fase `AntesDeEnvio` | `DespuesDeEnvio` | `RespuestaExplicita`; una solicitud con `DespuesDeEnvio`
  pasa a `EnvioIncierto` y no se reenvia.
- Firma: exclusiva en `Autentica`, inclusiva en las demas, `X509IssuerSerial` con issuer `docsat`, sin WS-Addressing.
- Validar formato de `IdSolicitud` e `IdPaquete` antes de la red (un valor invalido produce `301`).
- Diagnostico sanitizado; el adaptador no retiene el SOAP crudo.
- Los bloques de `src/infrastructure/sat/` se reutilizan tal cual; `T009` agrega el adaptador y el mapeo a estados.

## Pendientes para T009

- `SolicitaDescargaRecibidos` no se ejecuto contra el SAT (`No probada`); su sobre usa la misma firma que emitidos.
  T009 (2026-10-05) tampoco la ejecuto: el humo integrado uso emitidos ([`sat-smoke-t009.md`](sat-smoke-t009.md)); queda para T010.
- SOAP Fault real no observado; el parser se valida con fixture sintetico.
- Codigos de error de solicitud (`5002`, `5005`), estados intermedios de verificacion (`1`, `2`) y vencimiento (`5007`) no
  observados en esta corrida.
