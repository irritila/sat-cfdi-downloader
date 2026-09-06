# ADR 0008: Mapear filtros de UI a `SolicitaDescarga`

## Estado

Accepted

## Contexto

El MVP permite crear solicitudes de descarga masiva CFDI/XML desde una UI simple. La UI habla en terminos de uso: `emitidos`, `recibidos`, rango de fechas, RFC contraparte, tipo de comprobante y complemento.

El servicio SAT `SolicitaDescarga` no recibe directamente esos nombres de UI. Recibe atributos XML como `RfcSolicitante`, `RfcEmisor`, `RfcReceptor`, `RfcReceptores`, `TipoSolicitud`, `TipoComprobante`, `Complemento`, `FechaInicial` y `FechaFinal`.

La documentacion oficial SAT define los atributos soportados, pero la distincion operacional `emitidos/recibidos` debe quedar fija en la arquitectura para no depender de interpretaciones durante implementacion.

## Decision

Usar el siguiente mapeo para el MVP:

| UI | Atributo SAT | Regla |
| --- | --- | --- |
| Perfil SAT | `RfcSolicitante` | Siempre es el RFC del perfil/e.firma. |
| Tipo `emitidos` | `RfcEmisor` | Igual a `PerfilSat.rfc`. |
| Tipo `emitidos` + RFC contraparte | `RfcReceptores/RfcReceptor` | La contraparte se envia como receptor; si no hay contraparte, se omite. |
| Tipo `recibidos` | `RfcReceptor` | Igual a `PerfilSat.rfc`. |
| Tipo `recibidos` + RFC contraparte | `RfcEmisor` | La contraparte se envia como emisor; si no hay contraparte, se omite. |
| Fecha inicial | `FechaInicial` | Inicio del dia en hora Centro de Mexico. |
| Fecha final | `FechaFinal` | Fin del dia en hora Centro de Mexico. |
| Tipo de solicitud | `TipoSolicitud` | Fijo en `CFDI` para el MVP. |
| Tipo de comprobante | `TipoComprobante` | Opcional: `I`, `E`, `T`, `N`, `P`. Si no se captura, se omite. |
| Complemento | `Complemento` | Opcional. Si no se captura, se omite. |

Reglas adicionales:

- El MVP no expone solicitud por `Folio/UUID`.
- El MVP no expone `Metadata`.
- El MVP no expone `RfcACuentaTerceros`.
- El MVP no expone `EstadoComprobante`; al solicitar XML/CFDI, SAT documenta que solo se descargan XML vigentes.
- Los filtros vacios se omiten del XML. Si una prueba contra SAT demuestra que algun campo requiere cadena vacia, se documentara como excepcion tecnica.
- La clave local anti-duplicados se calcula despues de normalizar este mapeo a atributos SAT.

## Consecuencias

- La UI puede mantenerse simple sin exponer nombres internos del SAT.
- `SatGateway.crearSolicitud()` recibe filtros ya normalizados a un modelo de solicitud SAT.
- `emitidos` y `recibidos` no son solo etiquetas: cambian el atributo donde vive el RFC del perfil y el de la contraparte.
- La descarga de CFDI cancelados queda fuera del MVP, porque no se solicitara `Metadata` ni se modelara `EstadoComprobante`.
- Si en el futuro se agregan `Metadata`, `Folio/UUID`, `RfcACuentaTerceros` o retenciones, debe crearse un nuevo ADR o extenderse este con una decision posterior.

## Referencias

- `docs/web-service.md`
- `docs/requirements.md`
- `docs/architecture.md`
- SAT: Web service de solicitud de descargas para CFDI y retenciones: https://wwwmat.sat.gob.mx/cs/Satellite?blobcol=urldata&blobkey=id&blobtable=MungoBlobs&blobwhere=1461175195160&ssbinary=true
- Referencia secundaria: `phpcfdi/sat-ws-descarga-masiva`, `DownloadType` y `FielRequestBuilder`: https://github.com/phpcfdi/sat-ws-descarga-masiva
