# T015: Explorador de CFDI con previsualizacion PDF (epica)

## Estado

Pendiente (refinada 2026-10-11). Epica dividida en T015.1-T015.4. Milestone M2, posterior al MVP.

## Objetivo

Listar las facturas CFDI de cada solicitud terminada y previsualizarlas como PDF generado localmente a partir del XML.

## Contexto

Cada solicitud existe para obtener CFDI, pero hoy la app solo guarda los paquetes ZIP y no muestra su contenido. El MVP deja fuera la extraccion, el parseo y la indexacion de XML; por decision del usuario (2026-10-11) esta funcion va en el milestone M2 sin cambiar el alcance del MVP (`docs/requirements.md` y `docs/architecture.md` siguen describiendo el MVP).

## Decisiones

| ID | Decision | Estado | Fuente y motivo |
| --- | --- | --- | --- |
| D1 | El explorador vive por solicitud: desde el detalle de una solicitud con paquetes descargados se abre la lista de sus CFDI. Una vista global queda fuera de T015. | Aprobada por usuario | Refinamiento T015. Navegacion simple. |
| D2 | Los XML se quedan dentro de los ZIP (no se extraen a disco). Un indice SQLite derivado (migracion nueva) guarda el resumen de cada CFDI, la huella de su ZIP (tamano, fecha de modificacion y SHA-256) y la version del parser. Se indexa en segundo plano al quedar `Descargado` un paquete y, una vez, los paquetes ya descargados. Si el ZIP falta, cambia o esta corrupto, sus CFDI quedan como "origen no disponible" o se reindexan, sin cambiar `paquete_solicitud`. La reconstruccion borra y regenera solo lo derivado del ZIP afectado. | Recomendacion tecnica | `qt-architecture-lead`. 10k CFDI sin descomprimir en cada consulta. |
| D3 | El indice contiene datos fiscales (RFC, nombres, montos) en la base local, junto a los ZIP de los que se deriva; no sale del equipo ni va a logs. | Recomendacion tecnica | `especialista-sat-seguridad`. Misma proteccion que la carpeta de datos. |
| D4 | Lector de ZIP con `libzip` (API publica). Nunca usa nombres internos como rutas; rechaza entradas cifradas, enlaces, rutas absolutas o con `..`; cuenta bytes reales al descomprimir. Por entrada (se omite con motivo y el resto continua): nombre con `..`, absoluto o enlace, cifrada, no XML de CFDI, mas de 16 MiB o razon de compresion mayor a 200:1 con mas de 1 MiB descomprimido. Por ZIP (el paquete completo queda "paquete ilegible" y no se indexa nada de el): no existe, directorio central corrupto o truncado, mas de 20,000 entradas o mas de 1 GiB descomprimido acumulado. | Recomendacion tecnica | `qt-architecture-lead`, `especialista-sat-seguridad`. `QZipReader` es privado. |
| D5 | Parser con `QXmlStreamReader`: rechaza `<!DOCTYPE`, profundidad maxima 64, UTF-8 (BOM aceptado); montos con decimal exacto (nunca `double`). CFDI 4.0 y 3.3 completos; 3.2 u otros se listan con los datos minimos legibles y estado "version no soportada". Un error afecta solo a ese CFDI. | Recomendacion tecnica | `especialista-sat-seguridad`. |
| D6 | Complementos: TimbreFiscalDigital 1.1; Pagos 2.0 (y 1.0 en 3.3) basico: fecha, monto, moneda y documentos relacionados con parcialidad e importe pagado; Nomina 1.2 basico: periodo, fecha de pago, total de percepciones y deducciones. Otros complementos solo se nombran ("contiene complemento X, no representado"). | Aprobada por usuario | Refinamiento T015. |
| D7 | PDF generado bajo demanda con `QPdfWriter`/`QPainter` y visto dentro de la app con Qt Pdf (`QtPdfQuick`); cache regenerable en `QStandardPaths::CacheLocation`/`cfdi-pdf`, clave UUID + SHA-256 del XML + version del generador, limite de 200 MB con expulsion de los menos usados tras cada escritura; si la cache falta o no se puede escribir, el PDF se genera en un temporal y se muestra igual. Acciones: "Guardar PDF..." y "Abrir en Vista Previa". | Recomendacion tecnica | `qt-architecture-lead`. |
| D8 | El PDF incluye el QR de verificacion del Anexo 20 (`https://verificacfdi.facturaelectronica.sat.gob.mx/default.aspx?id=&re=&rr=&tt=&fe=`, `fe` = ultimos 8 caracteres del `Sello`, ISO/IEC 18004, al menos 2.75 cm), con una libreria de QR pequena (p. ej. qrcodegen, MIT) incluida en el repositorio. Se genera como el ejemplo del Anexo 20 (`tt` con 10 enteros rellenados con ceros y 6 decimales; `fe` sin codificar), usado como vector de prueba. Como el texto del Anexo es ambiguo, la regla se confirma escaneando el QR de un CFDI real del usuario; si el portal no lo acepta, se ajusta `tt`/`fe` dentro de T015.4 y se registra la regla validada. | Aprobada por usuario | Refinamiento T015; Anexo 20 (DOF 2022), seccion D. |
| D9 | Leyenda fija en el PDF: "Visualizacion generada localmente a partir del XML del CFDI. No es la representacion impresa emitida por el contribuyente ni valida sello, vigencia o cancelacion. El XML es el comprobante fiscal." No se valida sello, certificado, vigencia, cancelacion, XSD ni aritmetica. | Recomendacion tecnica | `especialista-sat-seguridad`. |
| D10 | Capas (ADR 0012): `domain/cfdi` (resumen, montos, UUID), puertos `CfdiArchiveReader`, `CfdiCatalogRepository` y `CfdiPdfRenderer`; implementaciones en `infrastructure/cfdi`; orquestacion en `application/cfdi`. Indexacion, lectura y PDF en un hilo de trabajo serial propio, separado del `OperacionExecutor` del SAT (nunca se encolan juntos); escrituras por `PersistenceDispatcher` en una transaccion por paquete (CFDI + estado "indexado" + huella), de modo que una cancelacion deja el paquete "pendiente" y se reindexa al arrancar; QML sin SQL, archivos ni XML. | Recomendacion tecnica | `qt-architecture-lead`. |
| D11 | Logs sin contenido XML, RFC, nombres, UUID completos, montos, sellos, rutas personales ni nombres de entradas del ZIP; solo conteos, Id de paquete enmascarado y tipo de error. | Recomendacion tecnica | `especialista-sat-seguridad`, reglas de logs del proyecto. |
| D12 | `libzip`, Qt Pdf, `QtPdfQuick` y la libreria de QR deben empaquetarse y firmarse en T011 (o en su sucesora si T015 se implementa antes). | Recomendacion tecnica | `qt-architecture-lead`. |

## Subtareas

| Tarea | Resultado | Depende de |
| --- | --- | --- |
| [T015.1](T015.1-lector-zip-parser-cfdi.md) | Lector de ZIP seguro y parser de CFDI (sin UI) | - |
| [T015.2](T015.2-indice-cfdi.md) | Indice SQLite de CFDI, indexacion y reconstruccion | T015.1 |
| [T015.3](T015.3-explorador-por-solicitud.md) | Explorador por solicitud (lista, busqueda y estados) | T015.2 |
| [T015.4](T015.4-pdf-qr-visor.md) | PDF con QR y complementos, visor y acciones | T015.1, T015.3 |

## Alcance

### Incluye

- Lo descrito en T015.1-T015.4.

### No incluye

- Vista global de CFDI, exportaciones, conciliacion, metadata, consulta de vigencia o cancelacion, validacion de sellos, edicion de CFDI.

## Dependencias

- T008 (ZIP), T009 (flujo SAT), T009.1 (acceso a paquetes), T013 (diseno visual); ADR 0004, 0012, 0016.

## Criterios de aceptacion

- [ ] T015.1-T015.4 completadas con sus criterios.

## Verificacion

Ver cada subtarea.

## Resultado

Pendiente.

## Referencias

- `docs/milestones/m2-explorador-cfdi.md`, `docs/meetings/T015-refinamiento/`
- Anexo 20 del SAT (DOF 13-ene-2022), seccion D (codigo bidimensional).
