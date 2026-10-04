# T006: Spike de integracion SAT

## Estado

Pendiente (refinada 2026-10-04; lista para implementar).

## Prioridad y tamano

- Prioridad: Critica tecnica.
- Tamano: Grande.

## Objetivo

Obtener evidencia controlada y sanitizada de que la app puede firmar y consumir contra produccion SAT las operaciones del MVP (autenticacion, solicitud, verificacion y descarga), y dejar una recomendacion concreta de contrato para `SatGateway`, antes de implementarlo en `T009`.

## Contexto

El SAT requiere WS-Security, firma XML, token de autenticacion y operaciones SOAP cuya forma debe coincidir con los WSDL productivos (`ADR 0005`, `ADR 0013`). `docs/web-service.md` documenta el contrato, pero la canonicalizacion, el orden de atributos, `KeyInfo`, el token y la forma real de respuestas solo se confirman contra SAT. No existe entorno SAT de pruebas: la prueba real usa produccion y los codigos `5002` (solicitudes de por vida agotadas) y `5005` (duplicada) hacen costosa cualquier repeticion.

`T005` y `T005.1` ya estan completadas. El spike no usa `SecretStore` (ver D4), pero sus bloques consumen el mismo `MaterialFirma` para que `T009` los conecte sin cambiarlos.

## Decisiones

| ID | Decision | Estado | Fuente y motivo |
| --- | --- | --- | --- |
| D1 | La prueba real se ejecuta contra produccion SAT con la e.firma del usuario, manualmente por el usuario. Alcance minimo: 1 solicitud de rango corto, verificacion y descarga si hay paquete. | Aprobada por usuario | Refinamiento 2026-10-04. Sin entorno SAT de pruebas. |
| D2 | XML y firma: libxml2 del SDK de macOS para C14N (exclusiva e inclusiva), OpenSSL 3 para SHA-1 y RSA PKCS#1 v1.5, `QXmlStreamReader` para parsear respuestas. Sin xmlsec1. | Recomendacion tecnica | `qt-architecture-lead`. OpenSSL no canonicaliza XML; xmlsec1 agrega empaquetado, rpaths y notarizacion. libxml2 con `c14n.h` existe en el SDK. |
| D3 | Codigo: bloques reutilizables en `src/infrastructure/sat/` (sobres SOAP/WS-Security, C14N+digest+firma, parser de respuestas y Faults, enmascarado de evidencia) y CLI delgada en `tools/sat_spike/`, compilada solo con `SATCFDI_BUILD_SAT_SPIKE=ON` (OFF por defecto), fuera del bundle y no enlazada por la app. | Recomendacion tecnica | `qt-architecture-lead`. El codigo que sobrevive es evidencia comprobable, no integracion accidental. |
| D4 | Material de firma: la CLI lee `.cer` y `.key` desde rutas fuera del repositorio y pide la contrasena por prompt sin eco. Valida y descifra reutilizando `satcfdi::crypto::validarEFirma()` y `descifrarLlavePkcs8()`; no llama a `prepararEFirma()`. Los bloques consumen `const MaterialFirma&`. `SecretStore` se conecta en `T009`. | Recomendacion tecnica (delegada por el usuario) | `especialista-sat-seguridad` y `qt-architecture-lead`. El data protection keychain exige un ejecutable firmado con entitlements del bundle; una CLI ad-hoc recibe `errSecMissingEntitlement` (`KeychainApiMacOS.h`, `docs/development.md`). |
| D5 | HTTP: `QNetworkAccessManager` con `QCoreApplication` y un deadline con `QTimer`, operaciones seriales. Las fallas se clasifican en `AntesDeEnvio`, `DespuesDeEnvio` (se observo `requestSent`) y `RespuestaExplicita` (HTTP completo, con resultado SOAP o Fault). Un timeout `DespuesDeEnvio` en una solicitud es resultado incierto y nunca se reenvia. | Recomendacion tecnica | `qt-architecture-lead`. Es la misma tecnologia del adaptador futuro. |
| D6 | La evidencia versionada enmascara RFC, `IdSolicitud`, `IdPaquete`, serial e issuer X509. Los valores reales quedan solo en un archivo local fuera del repositorio. El enmascarado es propio de la evidencia y no reemplaza a `RegexLogSanitizer`. | Recomendacion tecnica | `especialista-sat-seguridad`. `RegexLogSanitizer` preserva RFC e Ids a proposito y no cubre explicitamente `AutenticaResult` ni `BinarySecurityToken`. |
| D7 | Si la corrida real no devuelve un SOAP Fault, el parser se prueba con un fixture SOAP 1.1 marcado `sintetico`, y la evidencia registra el Fault real como `no observado`. No se provocan Faults reales. | Recomendacion tecnica | Coordinador, ante el riesgo senalado por `qt-quality-engineer`. |
| D8 | Contrato recomendado para `T009`, que T006 no implementa: `MaterialFirma` move-only por operacion; token opaco que vive solo en memoria y cuyo header `Authorization: WRAP access_token="..."` construye el adaptador; DTOs semanticos y diagnostico sanitizado, sin retener el SOAP crudo. | Recomendacion tecnica, a confirmar con evidencia | `qt-architecture-lead`, `ADR 0010`. |

## Alcance

### Incluye

- Inspeccion de los WSDL productivos (URL, fecha y hash) y comparacion con `docs/web-service.md`.
- Bloques de `src/infrastructure/sat/` descritos en D3, con sus pruebas automaticas sin red.
- CLI manual `tools/sat_spike/` con estos modos: `--dry-run` (sin red), autenticacion, solicitud de emitidos o recibidos, verificacion y descarga.
- Carga de la e.firma descrita en D4.
- Prueba real controlada que ejecuta el usuario (D1), conforme a las reglas de operacion.
- Clasificacion de respuestas, SOAP Faults, timeouts y codigos SAT.
- `docs/design/sat-spike-results.md` con evidencia sanitizada, discrepancias y recomendacion de contrato.
- Actualizar `docs/web-service.md` con las discrepancias confirmadas (`ADR 0005`).
- `.gitignore` para `*.cer`, `*.key`, `*.pfx`, `*.zip` y el directorio de salida local de la CLI.

### No incluye

- Metodos o DTOs publicos en `src/ports/SatGateway.h`. El archivo no cambia.
- El adaptador `SatGateway` final, la integracion con la UI, el worker, el ejecutor serial o el composition root.
- Usar `SecretStore` o Keychain desde el spike.
- Persistir solicitudes, tokens o paquetes en SQLite. Guardar ZIP en la carpeta de paquetes (`T008`).
- Mapear de forma definitiva codigos y timeouts a estados locales, politica de reintentos o TTL y reuso del token (`T009`).
- Solicitudes historicas amplias, repetitivas, por folio o de metadata.
- Extraer, parsear o validar ZIP/XML.
- Usar CSD.

## Dependencias

- `T002` (`satcfdi_ports`, estructura de capas) y `T005` (`EFirmaOpenSsl`, `MaterialFirma`, `BufferSecreto`). Ambas completadas.
- `docs/web-service.md`, `ADR 0005`, `ADR 0006`, `ADR 0010`, `ADR 0012`, `ADR 0013`.
- Que la e.firma del usuario este vigente y disponible como `.cer`/`.key` fuera del repositorio.
- Precondicion de la corrida real, que decide el usuario al ejecutarla: un dia cerrado de hace mas de 7 dias, con entre 1 y 50 CFDI conocidos, que no se haya solicitado antes por otro medio (portal, otro software).

## Reglas de seguridad y operacion

- Solo el usuario ejecuta las operaciones reales, en su Mac. La IA o el coordinador nunca ejecutan la corrida real ni manejan la e.firma.
- La contrasena se pide solo por prompt sin eco; nunca por argv ni por variable de entorno. La llave nunca se escribe en claro a disco. La CLI desactiva los core dumps (`RLIMIT_CORE=0`).
- Cada operacion real muestra la operacion, el RFC enmascarado y el rango, y exige una confirmacion escrita (`yes`). Ninguna operacion se ejecuta automaticamente al abrir la app.
- Orden de la corrida: `--dry-run`, `Autentica`, una `SolicitaDescargaEmitidos` (o `Recibidos` si el usuario emite poco), `VerificaSolicitudDescarga` manual (de 15 a 30 minutos entre llamadas, como maximo unas 10), `Descargar` una vez por paquete.
- La solicitud usa solo los filtros obligatorios (`TipoSolicitud=CFDI`, `EstadoComprobante=Vigente`): sin contraparte, `TipoComprobante` ni `Complemento`.
- Tope del spike: 3 solicitudes reales. Antes de cada solicitud, la CLI escribe en el archivo local la operacion, el rango y el hash del sobre, y se niega a reenviar un sobre con el mismo criterio ya registrado.
- Un timeout `DespuesDeEnvio` no se reenvia. Un rechazo `301` a `305` obliga a usar otro dia en el siguiente intento.
- Nunca se registran tokens, contrasenas, llaves, firmas completas, certificados completos, SOAP crudo ni contenido ZIP en la salida de consola, la evidencia versionada o los logs. El archivo local fuera del repositorio puede guardar RFC e Ids reales, pero no secretos.
- La evidencia distingue que operaciones fueron reales y cuales solo `dry-run` o fixture.

## Trabajo esperado

1. Descargar e inspeccionar los WSDL de solicitud, verificacion, descarga y autenticacion. Registrar URL, fecha y hash, y confirmar operaciones, SOAPAction, namespaces, tipos XSD y forma de las respuestas.
2. Agregar los targets: una biblioteca SAT en `src/infrastructure/sat/` que enlace directamente `LibXml2::LibXml2`, `OpenSSL::Crypto` y `Qt6::Network`, y `tools/sat_spike/` tras `SATCFDI_BUILD_SAT_SPIKE`. Documentar la opcion en `docs/development.md`.
3. Implementar C14N, digest y firma para el `Timestamp` de WS-Security (exc-c14n, `BinarySecurityToken`, `SecurityTokenReference`) y la firma enveloped del nodo de peticion (`X509IssuerSerial` con el formato de issuer y el serial en decimal).
4. Construir los sobres de `Autentica`, `SolicitaDescargaEmitidos`, `SolicitaDescargaRecibidos`, `VerificaSolicitudDescarga` y `Descargar`, omitiendo los atributos vacios.
5. Implementar el parser de respuestas y Faults, que mantiene separados `CodEstatus`, `Mensaje`, `EstadoSolicitud`, `CodigoEstadoSolicitud`, `NumeroCFDIs`, `IdsPaquetes` y `Paquete`.
6. Implementar el cliente HTTP con la clasificacion de fases de D5 y el header `WRAP` del token.
7. Implementar el enmascarado de evidencia (D6) y la CLI con las reglas de operacion.
8. Crear `satcfdi_sat_spike_tests` con el label `sat-spike` mas `unit` o `infrastructure`, en la suite por defecto. La CLI real no se registra en `ctest`.
9. El usuario ejecuta la corrida real. Con la salida sanitizada se redacta `docs/design/sat-spike-results.md` y se actualiza `docs/web-service.md`.

## Puntos de firma que la corrida real debe confirmar explicitamente

- C14N exclusiva en `Autentica` frente a la inclusiva del ejemplo de `VerificaSolicitudDescarga`: cual acepta SAT por operacion.
- El nodo sobre el que se calcula el digest enveloped y si SAT exige un orden de atributos propio (`docs/web-service.md` §5.6).
- `Timestamp`: UTC con milisegundos y `Z`, ventana de 5 minutos, desfase del reloj local.
- `KeyInfo` por operacion: `SecurityTokenReference` o `X509IssuerSerial` y `X509Certificate`.
- RSA-SHA1 bajo OpenSSL 3 sin bloqueo de politica.
- Header `Authorization: WRAP access_token="..."` exacto; TTL observado del token (`Created`/`Expires`) sin reusarlo despues de expirar.
- e.firma aceptada (no CSD).

## Criterios de aceptacion

### Automaticos (sin red ni e.firma real)

- [ ] Dado un vector XML conocido, cuando se canonicaliza en modo exclusivo y en modo inclusivo, entonces los bytes y el digest SHA-1 coinciden exactamente con los esperados.
- [ ] Dada una llave RSA temporal generada en la prueba, cuando se firman el `Timestamp` y cada nodo de peticion, entonces OpenSSL verifica la firma, la referencia apunta al nodo esperado y el serial X509 va en decimal.
- [ ] Dado cada sobre construido (autenticacion, emitidos, recibidos, verificacion, descarga), cuando se sanea, entonces coincide con su golden sanitizado, no contiene atributos vacios y no contiene firma, certificado ni token recuperables.
- [ ] Dados los fixtures de respuesta exitosa de cada operacion, de verificacion con `IdsPaquetes` y de SOAP Fault (`sintetico` si no hubo uno real), cuando se parsean, entonces producen un resultado o error tipado sin confundir `CodEstatus`, `EstadoSolicitud` y `CodigoEstadoSolicitud`.
- [ ] Dado un servidor HTTP local, cuando la conexion falla o vence el deadline antes de `requestSent`, entonces se clasifica como `AntesDeEnvio`; cuando se corta o vence el deadline despues de `requestSent`, entonces se clasifica como `DespuesDeEnvio`. Dado un HTTP completo con resultado o Fault, entonces se clasifica como `RespuestaExplicita`.
- [ ] Dada evidencia con `AutenticaResult`, `BinarySecurityToken`, `SignatureValue`, `X509Certificate`, `Paquete`, header `WRAP`, RFC o Ids, cuando se enmascara, entonces no queda ningun valor recuperable y enmascarar dos veces da el mismo resultado.
- [ ] Dada una contrasena incorrecta, un `.cer`/`.key` que no forman pareja o un certificado que no es e.firma, cuando la CLI carga el material, entonces termina con el error de `validarEFirma()` correspondiente, sin red y sin escribir material en disco.
- [ ] Con `SATCFDI_BUILD_SAT_SPIKE=OFF` (por defecto), la app y la suite compilan sin la CLI, y `satcfdi_app` no enlaza la biblioteca SAT.

### Corrida real y evidencia

- [ ] `docs/design/sat-spike-results.md` registra la URL, fecha y hash de cada WSDL inspeccionado y confirma la operacion, SOAPAction y namespace de autenticacion, emitidos, recibidos, verificacion y descarga, aunque solo una de emitidos o recibidos se ejecute.
- [ ] Dada la e.firma vigente del usuario, cuando ejecuta `Autentica`, entonces se obtiene un token (registrado solo por su formato y TTL) o queda documentado el rechazo exacto (HTTP, Fault, codigo).
- [ ] Dado un dia que cumple la precondicion, cuando el usuario confirma una solicitud, entonces la evidencia registra la fase, `CodEstatus`, `Mensaje` y un `IdSolicitud` enmascarado, o el codigo y contexto del rechazo.
- [ ] Cuando se verifica la solicitud, entonces la evidencia registra la secuencia de `EstadoSolicitud`, `CodigoEstadoSolicitud`, `CodEstatus` y `NumeroCFDIs` observada, con sus timestamps.
- [ ] Si hay un paquete, `Descargar` registra `CodEstatus`, `Mensaje` y el tamano en bytes de `Paquete`, sin su contenido. Si no hay paquete, la descarga queda como `No probada` con su motivo, no como `Viable`.
- [ ] La evidencia registra el numero total de solicitudes reales (3 como maximo) y declara que ningun timeout `DespuesDeEnvio` se reenvio.
- [ ] Cada operacion queda clasificada como `Viable`, `Requiere ajuste`, `Bloqueada` o `No probada`, y se senala cual fue real y cual solo `dry-run` o fixture.
- [ ] Los puntos de firma de la seccion anterior quedan resueltos o marcados como pendientes con su evidencia. Las discrepancias con `docs/web-service.md` quedan corregidas en ese documento.
- [ ] El resultado incluye una recomendacion concreta de contrato para `SatGateway` que confirma o ajusta D8.
- [ ] Un escaneo antes del commit (`git grep` de patrones de secretos, del RFC y los Ids reales, de base64 de 200 caracteres o mas, y de archivos `.cer`, `.key` o `.zip`) sobre el arbol y el staging da resultado vacio.
- [ ] Si una operacion critica queda `Bloqueada` o `No probada`, `docs/tasks/T009-flujo-sat-integrado.md` lo refleja como precondicion pendiente de esa parte del flujo.

## Verificacion

1. `cmake --build build && ctest --test-dir build -L sat-spike --output-on-failure` (los labels `sat-spike` tambien corren con `ctest` sin filtro).
2. Configurar con `-DSATCFDI_BUILD_SAT_SPIKE=ON` y ejecutar `--dry-run` con la e.firma del usuario: sin trafico de red y con el sobre saneado en consola.
3. El usuario ejecuta la corrida real conforme a las reglas y entrega solo la salida sanitizada.
4. Revisar `docs/design/sat-spike-results.md` y el diff de `docs/web-service.md`.
5. Ejecutar el escaneo de secretos del criterio correspondiente y confirmar que no quedan archivos temporales fuera de `.gitignore`.

## Definicion de terminado

- Existe evidencia reproducible y sanitizada del spike, separando lo real de lo simulado.
- Las discrepancias entre WSDL, documentacion SAT y comportamiento real estan documentadas.
- Las decisiones de librerias y contrato de `SatGateway` estan justificadas con evidencia.
- `T009` puede iniciar sin inventar firmas SOAP ni nombres de respuesta, o sabe exactamente que parte esta bloqueada.

## Resultado

Pendiente.

## Riesgos y notas

- Los vectores y fixtures prueban la implementacion, no la compatibilidad con SAT; solo la corrida real la confirma.
- Una prueba exitosa no garantiza que todos los rangos, filtros o estados se comporten igual.
- Si un rechazo `301` a `305` sin explicacion agota los 3 intentos, el resultado es `Bloqueada` con evidencia. No se amplia el tope sin una nueva decision del usuario.
- Que un rechazo de firma no registre la solicitud en SAT se infiere; no esta documentado.
- Esta tarea no autoriza guardar e.firma, tokens o paquetes dentro del repositorio.

## Referencias

- `docs/web-service.md`
- `docs/architecture.md` §9 y §12.1
- `ADR 0005`, `ADR 0006`, `ADR 0010`, `ADR 0012`, `ADR 0013`
- `src/infrastructure/crypto/EFirmaOpenSsl.h`, `src/ports/secrets/SecretStoreTypes.h`, `src/ports/LogSanitizer.h`
- `docs/meetings/T006-refinamiento/` (bitacora y rondas del refinamiento)
- https://github.com/phpcfdi/sat-ws-descarga-masiva (referencia secundaria)
