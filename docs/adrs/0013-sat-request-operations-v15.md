# ADR 0013: Usar operaciones SAT v1.5 separadas para crear solicitudes

## Estado

Accepted

## Reemplaza

- `ADR 0008`

## Contexto

El ADR 0008 fijo un mapeo inicial entre la UI y `SolicitaDescarga`, tomando como base la documentacion SAT previa y referencias secundarias.

Al contrastar contra el WSDL productivo vigente de `SolicitaDescargaService.svc`, el servicio publica operaciones separadas:

- `SolicitaDescargaEmitidos`
- `SolicitaDescargaRecibidos`
- `SolicitaDescargaFolio`

El MVP solo permite solicitudes por rango de fechas para CFDI/XML emitidos o recibidos. La solicitud por folio queda fuera de alcance.

## Decision

La UI conserva el campo simple `tipo_cfdi` con valores `emitidos` o `recibidos`, pero `SatGateway.crearSolicitud()` debe resolverlo a la operacion SOAP correspondiente.

Mapeo del MVP:

| UI | Operacion SAT | Atributos SAT |
| --- | --- | --- |
| `emitidos` | `SolicitaDescargaEmitidos` | `RfcEmisor = PerfilSat.rfc`, `RfcSolicitante = PerfilSat.rfc` |
| `emitidos` + RFC contraparte | `SolicitaDescargaEmitidos` | Agrega `RfcReceptores/RfcReceptor = RFC contraparte` |
| `recibidos` | `SolicitaDescargaRecibidos` | `RfcReceptor = PerfilSat.rfc`, `RfcSolicitante = PerfilSat.rfc` |
| `recibidos` + RFC contraparte | `SolicitaDescargaRecibidos` | Agrega `RfcEmisor = RFC contraparte` |

Reglas adicionales:

- `TipoSolicitud` queda fijo en `CFDI`.
- `EstadoComprobante` queda fijo internamente en `Vigente` para solicitudes CFDI/XML del MVP.
- `SolicitaDescargaFolio` queda fuera del MVP.
- `Metadata` queda fuera del MVP.
- `RfcACuentaTerceros` queda fuera del MVP.
- `TipoComprobante` y `Complemento` siguen siendo filtros opcionales si el SAT los acepta para la operacion elegida.
- Los RFC se normalizan en mayusculas y sin espacios antes de firmar, persistir filtros o calcular duplicados.
- La clave local anti-duplicados se calcula despues de normalizar a operacion SAT y atributos efectivos.

Regla de autoridad para discrepancias:

- Para la forma SOAP concreta, nombres de operaciones, acciones SOAP, tipos XSD y respuestas, manda el WSDL productivo vigente.
- Para reglas funcionales, limites y codigos SAT, manda la documentacion oficial SAT vigente.
- Si WSDL y PDF oficial difieren, se documenta la discrepancia en `docs/web-service.md` y se valida con spike tecnico antes de implementar flujo productivo.

## Consecuencias

- La UI no se complica con nombres internos del SAT.
- El modelo local debe guardar la operacion SAT efectiva usada en la solicitud.
- `EstadoComprobante = Vigente` evita pedir XML cancelados dentro del MVP y mantiene fuera de alcance la consulta de vigencia/cancelacion.
- La comparacion anti-duplicados debe considerar `operacion_sat`, fechas normalizadas, RFC solicitante, emisor, receptor, tipo solicitud, estado comprobante, tipo comprobante y complemento.
- Cualquier soporte futuro para folio, metadata, cancelados o retenciones requiere ADR nuevo.

## Referencias

- `docs/web-service.md`
- `docs/requirements.md`
- `docs/architecture.md`
- WSDL productivo solicitud CFDI: https://cfdidescargamasivasolicitud.clouda.sat.gob.mx/SolicitaDescargaService.svc?singleWsdl
- SAT: Web service de solicitud de descargas para CFDI y retenciones: https://www.sat.gob.mx/cs/Satellite?blobcol=urldata&blobkey=id&blobtable=MungoBlobs&blobwhere=1461175195160&ssbinary=true
