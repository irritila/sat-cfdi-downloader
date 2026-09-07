# ADR 0005: Usar fuentes oficiales SAT y WSDL productivos como autoridad de contrato

## Estado

Accepted

## Contexto

Al revisar la documentacion inicial, `docs/web-service.md` solo documentaba autenticacion y `VerificaSolicitudDescarga`. Para implementar el MVP tambien se requerian contratos trazables para:

- `SolicitaDescargaEmitidos` y `SolicitaDescargaRecibidos`.
- `DescargaMasiva`.

La arquitectura ya modelaba `SatGateway.crearSolicitud()` y `SatGateway.descargarPaquete()`, por lo que era necesario decidir una fuente canonica antes de fijar firma, payload, codigos, filtros y comportamiento real.

En el portal del SAT de consulta y recuperacion de comprobantes existen documentos oficiales separados para URLs productivas, solicitud, descarga y verificacion. Tambien existen endpoints WSDL publicados para los servicios.

## Decision

Usar fuentes oficiales SAT como base canonica, separando forma tecnica y reglas funcionales.

Orden de autoridad:

1. WSDL productivos publicados por SAT para nombres de operaciones, SOAPActions, tipos XSD, endpoints efectivos y estructura SOAP concreta.
2. Documentos oficiales SAT enlazados desde el portal de consulta y recuperacion de comprobantes para reglas funcionales, limites, codigos y comportamiento operativo.
3. `phpcfdi/sat-ws-descarga-masiva` como referencia secundaria de implementacion, ejemplos y compatibilidad, no como fuente normativa.

Si el WSDL productivo y un PDF oficial difieren, la discrepancia debe documentarse en `docs/web-service.md` y validarse con spike tecnico antes de conectar un flujo productivo. No se debe fijar contrato C++ definitivo sobre firmas asumidas.

Para el MVP inicial se consumira el servicio productivo de CFDI regulares:

- Autenticacion: `https://cfdidescargamasivasolicitud.clouda.sat.gob.mx/Autenticacion/Autenticacion.svc`
- Solicitud: `https://cfdidescargamasivasolicitud.clouda.sat.gob.mx/SolicitaDescargaService.svc`
- Verificacion: `https://cfdidescargamasivasolicitud.clouda.sat.gob.mx/VerificaSolicitudDescargaService.svc`
- Descarga: `https://cfdidescargamasiva.clouda.sat.gob.mx/DescargaMasivaService.svc`

Los servicios de retenciones quedan fuera del MVP, aunque se documentan como separacion existente del SAT.

## Consecuencias

- Se cierra la regla de autoridad de fuentes, pero la interoperabilidad real queda sujeta al spike tecnico SAT.
- `docs/web-service.md` contiene la especificacion local vigente con endpoints, operaciones, parametros, codigos y reglas MVP.
- La especificacion local debe mantenerse trazable a los documentos oficiales SAT.
- Los WSDL controlan nombres de servicios, endpoints y contratos generables; la implementacion debe respetar tambien las reglas funcionales del PDF oficial.
- `phpcfdi/sat-ws-descarga-masiva` puede usarse para contrastar comportamiento real y casos conocidos, especialmente cuando la documentacion oficial sea ambigua.

## Referencias

- `docs/web-service.md`
- `docs/architecture.md`
- Portal SAT: https://wwwmat.sat.gob.mx/cs/Satellite?c=ConsultaInfo&childpagename=SatTyR%2FConsultaInfo%2FSAT_LandingConsultaInformacion&cid=1462231542968&packedargs=d%3DTouch&pagename=TySWrapper
- URLs productivas SAT: https://wwwmat.sat.gob.mx/cs/Satellite?blobcol=urldata&blobkey=id&blobtable=MungoBlobs&blobwhere=1461174995058&ssbinary=true
- Servicio de solicitud SAT: https://wwwmat.sat.gob.mx/cs/Satellite?blobcol=urldata&blobkey=id&blobtable=MungoBlobs&blobwhere=1461175195160&ssbinary=true
- Servicio de descarga SAT: https://wwwmat.sat.gob.mx/cs/Satellite?blobcol=urldata&blobkey=id&blobtable=MungoBlobs&blobwhere=1461174995026&ssbinary=true
- Servicio de verificacion SAT: https://wwwmat.sat.gob.mx/cs/Satellite?blobcol=urldata&blobkey=id&blobtable=MungoBlobs&blobwhere=1461175779527&ssbinary=true
- WSDL solicitud CFDI: https://cfdidescargamasivasolicitud.clouda.sat.gob.mx/SolicitaDescargaService.svc?singleWsdl
- WSDL descarga CFDI: https://cfdidescargamasiva.clouda.sat.gob.mx/DescargaMasivaService.svc?singleWsdl
- Referencia secundaria: https://github.com/phpcfdi/sat-ws-descarga-masiva
