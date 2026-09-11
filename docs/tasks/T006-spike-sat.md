# T006: Spike de integracion SAT

## Estado

Pendiente

## Prioridad y tamano

- Prioridad: Critica tecnica.
- Tamano: Grande.

## Objetivo

Obtener evidencia controlada de que la app puede firmar y consumir las operaciones SAT requeridas por el MVP antes de implementar el `SatGateway` productivo.

## Contexto

El SAT requiere firma XML, WS-Security, autenticacion y operaciones SOAP con contratos que deben coincidir con los WSDL productivos. Esta tarea reduce el riesgo tecnico sin integrar todavia el flujo real a la UI, al worker o a la base de datos.

## Alcance

### Incluye

- Inspeccion de los WSDL productivos y SOAP actions.
- Seleccion documentada de librerias para XML, firma y SOAP.
- Importacion de material de e.firma de prueba o proporcionado para el spike.
- Firma XML y construccion de WS-Security.
- Autenticacion SAT.
- Una prueba controlada de `SolicitaDescargaEmitidos` o `SolicitaDescargaRecibidos`.
- Verificacion mediante `VerificaSolicitudDescarga`.
- Descarga mediante `Descargar` si existe un paquete disponible.
- Manejo de respuestas, SOAP Faults, timeouts y codigos SAT.
- Registro de evidencia sanitizada.

### No incluye

- Integracion definitiva con la UI.
- Worker o ejecutor serial productivo.
- Persistencia de solicitudes o tokens.
- Solicitudes historicas amplias o repetitivas.
- Parsing, extraccion o validacion del ZIP/XML.
- Implementacion final de `SatGateway`.
- Uso de CSD para timbrado.

## Dependencias

- `T002-shell-qt-qml.md` para los contratos iniciales de `satcfdi_ports`.
- `docs/web-service.md`.
- ADR 0005 y ADR 0013.
- Un material de e.firma controlado para pruebas, o una decision explicita de que la prueba real queda bloqueada por falta de credenciales.

Esta tarea no depende de `T005`. Puede usar un fixture criptografico independiente; `T005` implementa la custodia productiva de credenciales.

## Reglas de seguridad y operacion

- No registrar tokens, contrasenas, llaves privadas, firmas completas, certificados completos ni contenido ZIP.
- No dejar credenciales o tokens en el repositorio, variables persistentes o archivos de evidencia.
- Usar el rango de fechas minimo necesario para una prueba controlada.
- No repetir solicitudes con los mismos filtros sin justificacion, para evitar limites o duplicados SAT.
- No ejecutar el spike automaticamente al abrir la app.
- Si SAT no ofrece un entorno de prueba equivalente, documentar claramente que operaciones fueron reales y cuales fueron simuladas.

## Trabajo esperado

1. Obtener o inspeccionar los WSDL de solicitud y descarga usados por el MVP.
2. Confirmar nombres de operaciones, SOAP actions, namespaces, tipos XSD y forma de respuestas.
3. Implementar un programa o test aislado que construya la autenticacion y firma, usando el contrato `LogSanitizer` para la evidencia.
4. Probar autenticacion con material controlado y registrar el resultado sanitizado.
5. Probar una solicitud de emitidos o recibidos con filtros minimos.
6. Probar verificacion y documentar la transicion de estado observada.
7. Probar descarga si SAT devuelve un `IdPaquete` utilizable.
8. Probar respuestas de error sin reintentos peligrosos.
9. Crear `docs/design/sat-spike-results.md` con evidencia, discrepancias y recomendacion de contrato.

## Decisiones que debe cerrar esta tarea

- Libreria de XML, firma y WS-Security.
- Forma de representar material de firma que recibira `SatGateway`.
- Forma de transportar el token en llamadas posteriores.
- Parseo minimo de respuestas SOAP y SOAP Faults.
- Como distinguir timeout antes del envio, despues del envio y respuesta SAT explicita.
- Si `SatGateway` debe conservar respuestas completas en memoria o solo DTOs sanitizados.

## Criterios de aceptacion

- [ ] Los WSDL inspeccionados y sus versiones/URLs quedan registrados.
- [ ] Las operaciones de solicitud, verificacion y descarga usan los endpoints y SOAP actions vigentes.
- [ ] Se confirma la forma concreta de `SolicitaDescargaEmitidos` y `SolicitaDescargaRecibidos`, aunque solo una se ejecute de extremo a extremo.
- [ ] La autenticacion produce un resultado interpretable o queda documentado el bloqueo exacto.
- [ ] La firma generada es aceptada o se documenta el rechazo con evidencia suficiente para corregirla.
- [ ] Una solicitud controlada produce una respuesta interpretable o queda documentado el codigo y contexto del rechazo.
- [ ] La verificacion distingue `CodEstatus`, `EstadoSolicitud` y `CodigoEstadoSolicitud`.
- [ ] La descarga identifica como se obtienen `CodEstatus`, `Mensaje` y `Paquete`, o documenta que no pudo probarse por falta de paquete.
- [ ] Se documentan SOAP Faults, timeouts y errores de autenticacion observados.
- [ ] Ningun secreto, token, firma completa, ZIP o payload sensible aparece en logs o evidencia.
- [ ] El resultado contiene una recomendacion concreta para implementar `SatGateway`.
- [ ] El resultado clasifica cada operacion como `Viable`, `Requiere ajuste` o `Bloqueada`.
- [ ] Si una operacion critica queda `Bloqueada`, `T009` no puede iniciar esa parte del flujo hasta resolver el bloqueo y actualizar la evidencia.

## Verificacion

1. Ejecutar inspeccion de WSDL y comparar con `docs/web-service.md`.
2. Ejecutar pruebas aisladas con material de prueba controlado.
3. Conservar solo request/response sanitizados, codigos, timestamps y referencias de operacion.
4. Confirmar que no quedaron secretos en el directorio de trabajo.
5. Revisar `docs/design/sat-spike-results.md` antes de crear tareas de integracion.

## Definicion de terminado

- Existe evidencia reproducible y sanitizada del spike.
- Las discrepancias entre WSDL, documentacion SAT y comportamiento real estan documentadas.
- Las decisiones de librerias y DTOs para `SatGateway` estan justificadas.
- Se sabe que parte del flujo puede implementarse con certeza y que parte requiere una prueba adicional.
- `T009` puede iniciar la integracion sin inventar firmas SOAP ni nombres de respuesta.

## Resultado

Pendiente.

## Riesgos y notas

- El SAT puede no ofrecer un entorno aislado; una prueba real debe ser pequena y controlada.
- Una prueba exitosa no garantiza que todos los rangos, filtros o estados funcionen igual.
- Esta tarea no autoriza guardar e.firma, tokens o paquetes dentro del repositorio.

## Referencias

- `docs/web-service.md`
- `docs/architecture.md`
- ADR 0005
- ADR 0013
