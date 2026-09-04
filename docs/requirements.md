# Requerimientos - SAT CFDI Downloader

Estado: borrador inicial para revision iterativa. Incluye retroalimentacion operativa contable.

## 1. Contexto de producto

SAT CFDI Downloader es una aplicacion de escritorio local-first para gestionar descargas masivas de CFDI usando los servicios web del SAT.

La aplicacion esta pensada para uso personal: un unico usuario actuando como contador desde su propio equipo. No se esta disenando como SaaS, portal multiusuario, producto comercial ni herramienta para clientes ficticios o potenciales.

Esta restriccion debe guiar las decisiones de alcance: si una funcionalidad solo tiene sentido para vender el producto, administrar usuarios externos, colaborar en linea o soportar organizaciones, queda fuera del alcance actual.

## 2. Objetivo

Permitir que el usuario registre perfiles SAT de contribuyentes/RFC, cree solicitudes de descarga masiva de CFDI conforme a los filtros soportados por el SAT, consulte el estado de dichas solicitudes, descargue los paquetes disponibles, extraiga los XML y consulte localmente los CFDI descargados.

El objetivo operativo no es construir un ERP ni un sistema fiscal completo. La aplicacion debe ser primero un descargador confiable, trazable y seguro. Cualquier funcion posterior, como conciliacion, validacion de vigencia, exportacion avanzada o integracion contable, debe evaluarse contra el uso personal real antes de entrar al alcance.

## 3. Alcance inicial

El alcance inicial incluye:

- Aplicacion de escritorio.
- Operacion local en una sola computadora.
- Un unico usuario de la aplicacion.
- Administracion de multiples perfiles SAT de contribuyentes/RFC.
- Almacenamiento local de credenciales SAT de forma segura.
- Creacion de solicitudes de descarga masiva para CFDI emitidos o recibidos.
- Captura de filtros reales soportados por el SAT, como rango de fechas, RFC contraparte, tipo de comprobante, complemento y tipo de solicitud, cuando apliquen.
- Manejo de solicitudes de tipo CFDI/XML y metadata cuando el servicio lo soporte y se confirme su uso en el flujo MVP.
- Particion automatica de solicitudes cuando los parametros excedan topes o rangos aceptados por el SAT.
- Consulta manual o automatica del estado de solicitudes.
- Descarga de paquetes cuando el SAT los deje disponibles.
- Extraccion de XML desde paquetes descargados.
- Persistencia local de solicitudes, paquetes, XML, metadatos y logs.
- Consulta local basica de CFDI descargados.
- Cola visible de solicitudes y paquetes con estado operativo.

## 4. Fuera de alcance actual

Queda fuera del alcance actual:

- Backend remoto.
- Sincronizacion en la nube.
- Multiusuario.
- Roles, permisos por organizacion o administracion de equipos.
- Portal para contribuyentes externos.
- Facturacion, pagos, licenciamiento o onboarding comercial.
- Colaboracion entre contadores.
- Operacion desatendida en servidor.
- Aplicacion movil.
- Integraciones contables externas.
- Conciliacion avanzada.
- Validacion fiscal exhaustiva de CFDI.
- Consulta masiva de vigencia/cancelacion de CFDI como requisito bloqueante del MVP.
- Exportaciones avanzadas, salvo que se definan como necesarias para el uso personal.

Nota: que la vigencia/cancelacion no sea bloqueante del MVP no significa que se ignore. Debe documentarse como limite conocido: los XML descargados no prueban por si solos que un CFDI siga vigente en una fecha posterior.

## 5. Actores

### Usuario

Persona que usa la aplicacion localmente y actua como contador.

Responsabilidades:

- Registrar perfiles SAT de contribuyentes/RFC.
- Proporcionar credenciales SAT validas.
- Crear solicitudes de descarga.
- Revisar estados y resultados.
- Consultar los CFDI descargados.

### SAT

Sistema externo que expone los servicios de autenticacion, solicitud, verificacion y descarga masiva.

Responsabilidades externas:

- Autenticar mediante e.firma vigente.
- Recibir solicitudes de descarga.
- Procesar solicitudes de forma asincrona.
- Devolver estados de solicitud.
- Entregar identificadores de paquetes cuando existan.
- Permitir la descarga de paquetes autorizados.

## 6. Supuestos y restricciones

- La aplicacion corre en el equipo propio del usuario.
- La informacion sensible no debe depender de un servidor remoto.
- La fuente de verdad operativa es local.
- El servicio SAT puede fallar, tardar, rechazar solicitudes, marcar duplicados o expirar paquetes.
- Los filtros disponibles deben limitarse a los aceptados por el web service del SAT.
- La operacion debe distinguir emitidos y recibidos porque corresponden a solicitudes y usos contables distintos.
- La aplicacion debe considerar dos modalidades operativas: backfill historico y corte mensual/rutinario.
- Los CFDI pueden tardar en aparecer en servicios SAT despues de emitirse; la aplicacion debe evitar presentar una descarga reciente como cierre fiscal definitivo.
- La zona horaria de negocio debe fijarse explicitamente a hora del centro de Mexico para evitar errores en rangos por fecha.
- La aplicacion debe limitar frecuencia de consultas/descargas para reducir riesgo de bloqueo temporal, throttling o errores por saturacion del SAT.
- La e.firma vencida o revocada bloquea la operacion del perfil SAT correspondiente.
- La documentacion local actual cubre con detalle la verificacion de solicitudes; los parametros exactos para creacion y descarga deben confirmarse contra documentacion oficial completa o referencia tecnica confiable.
- La documentacion SAT revisada indica e.firma vigente como prerrequisito. Por ahora no debe modelarse CSD como credencial de descarga masiva salvo que se confirme oficialmente.
- Las URL incluidas en ejemplos SAT no deben asumirse como endpoints productivos; deben tomarse de la publicacion vigente del SAT.

## 7. Flujos principales

### 7.0 Modalidades operativas

La aplicacion debe reconocer al menos dos formas de trabajo:

- Backfill historico: descarga de periodos anteriores, potencialmente grandes, ejecutada una o pocas veces por perfil SAT.
- Corte mensual/rutinario: descarga recurrente de periodos recientes, con tolerancia para CFDI que aparecen tarde o solicitudes que deben reintentarse.

Ambas modalidades usan el mismo flujo tecnico, pero tienen expectativas distintas de tiempo, particion, volumen y revision posterior.

### 7.1 Registrar perfil SAT

1. El usuario crea un perfil para un contribuyente/RFC.
2. El usuario registra certificado, llave privada y contrasena.
3. La aplicacion valida que los archivos tengan formato legible.
4. La aplicacion puede probar autenticacion contra el SAT antes de marcar las credenciales como operativas.
5. La aplicacion almacena las credenciales o referencias de forma segura.
6. El perfil queda disponible para generar solicitudes.

Resultado esperado:

- Perfil SAT local registrado.
- Credenciales protegidas localmente.
- Evento registrado en logs.

### 7.2 Crear solicitud de descarga

1. El usuario selecciona un perfil SAT.
2. El usuario elige si busca CFDI emitidos o recibidos.
3. El usuario elige tipo de solicitud, por ejemplo CFDI/XML o metadata si el SAT lo soporta para el caso.
4. El usuario captura filtros soportados por el servicio: rango de fechas, RFC contraparte, tipo de comprobante, complemento u otros confirmados.
5. La aplicacion valida los filtros antes de enviar.
6. La aplicacion parte automaticamente el trabajo en varias solicitudes SAT cuando el rango o volumen estimado exceda limites conocidos.
7. La aplicacion autentica contra SAT.
8. La aplicacion envia cada solicitud requerida.
9. La aplicacion persiste el identificador de cada solicitud y respuesta inicial.

Resultado esperado:

- Trabajo de descarga registrado localmente.
- Una o mas solicitudes SAT registradas localmente.
- Estado inicial registrado.
- Mensaje/codigo SAT persistido.

### 7.3 Consultar estado de solicitud

1. La aplicacion selecciona una solicitud pendiente.
2. La aplicacion autentica o reutiliza token valido.
3. La aplicacion llama a `VerificaSolicitudDescarga`.
4. La aplicacion guarda `EstadoSolicitud`, `CodigoEstadoSolicitud`, `CodEstatus`, `Mensaje`, `NumeroCFDIs` e `IdsPaquetes` si existen.
5. La UI muestra el estado actualizado.

Resultado esperado:

- Estado local sincronizado con la respuesta SAT.
- Si el estado es terminado, los paquetes quedan listos para descargarse.
- Si hay error, rechazo o vencimiento, queda visible para el usuario.

### 7.4 Descargar paquetes

1. La aplicacion identifica solicitudes terminadas con paquetes disponibles.
2. La aplicacion solicita al SAT la descarga de cada paquete.
3. La aplicacion valida la integridad del paquete segun la informacion disponible. Si el SAT no entrega hash utilizable, al menos debe validar que el archivo se pueda abrir y extraer correctamente.
4. La aplicacion guarda el paquete en carpeta local.
5. La aplicacion registra resultado de descarga por paquete.

Resultado esperado:

- Paquetes guardados localmente.
- Estado de paquete actualizado.
- Errores registrados con codigo, mensaje y fecha.

### 7.5 Extraer XML

1. La aplicacion detecta paquetes descargados no extraidos.
2. La aplicacion extrae los XML en una carpeta local controlada.
3. La aplicacion indexa metadatos minimos para consulta.
4. La aplicacion registra duplicados o archivos invalidos.

Resultado esperado:

- XML disponibles en almacenamiento local.
- Metadatos consultables desde la UI.
- Relacion entre solicitud, paquete y XML preservada.

### 7.6 Consultar CFDI descargados

1. El usuario abre la vista de CFDI.
2. El usuario filtra por perfil SAT, fechas y metadatos disponibles.
3. La aplicacion consulta la base local.
4. La aplicacion muestra resultados y permite abrir el XML local.
5. La aplicacion indica claramente cuando no ha verificado vigencia/cancelacion actual del CFDI.

Resultado esperado:

- El usuario puede encontrar CFDI previamente descargados sin consultar al SAT.
- El usuario entiende que la consulta local muestra lo descargado, no necesariamente el estatus fiscal vigente al dia de consulta.

## 8. Requerimientos funcionales

### RF-001 Perfil SAT

La aplicacion debe permitir crear, editar, desactivar y consultar perfiles SAT de contribuyentes/RFC.

### RF-002 Credenciales SAT

La aplicacion debe permitir registrar certificado, llave privada y contrasena de e.firma asociados a un perfil SAT.

### RF-003 Almacenamiento seguro de credenciales

La aplicacion debe proteger localmente las credenciales SAT usando cifrado o almacenamiento seguro del sistema operativo.

### RF-004 Validacion basica de credenciales

La aplicacion debe validar que los archivos de certificado y llave privada puedan leerse antes de guardarlos como credenciales activas. Cuando sea posible, debe probar autenticacion contra SAT antes de marcar el perfil como operativo.

### RF-005 Autenticacion SAT

La aplicacion debe autenticarse contra el servicio SAT usando e.firma vigente y WS-Security conforme a la documentacion tecnica disponible.

### RF-006 Crear solicitud

La aplicacion debe permitir crear solicitudes de descarga masiva usando un perfil SAT y filtros soportados por el SAT.

El formulario de solicitud debe distinguir:

- CFDI emitidos o recibidos.
- Rango de fechas.
- RFC contraparte cuando aplique.
- Tipo de comprobante cuando aplique: ingreso, egreso, traslado, nomina o pago.
- Complemento cuando aplique.
- Tipo de solicitud: CFDI/XML o metadata, sujeto a confirmacion del servicio.

### RF-007 Validar filtros

La aplicacion debe impedir el envio de solicitudes con filtros no soportados o incompletos para el tipo de solicitud seleccionado.

Los filtros deben validarse con base en reglas documentadas del SAT, no con expectativas de busqueda local o preferencias de UI.

### RF-008 Persistir solicitud

La aplicacion debe persistir cada solicitud enviada con su perfil SAT, parametros, fecha de envio, identificador SAT, codigos y mensaje de respuesta.

### RF-009 Consultar estado

La aplicacion debe permitir consultar el estado de una solicitud mediante el servicio de verificacion del SAT.

### RF-010 Worker local

La aplicacion debe contar con un worker local que pueda consultar periodicamente solicitudes pendientes y actualizar su estado.

### RF-011 Estados SAT

La aplicacion debe representar los estados SAT documentados: aceptada, en proceso, terminada, error, rechazada y vencida.

### RF-012 Paquetes disponibles

Cuando una solicitud termine exitosamente, la aplicacion debe registrar los identificadores de paquetes devueltos por SAT.

### RF-013 Descargar paquetes

La aplicacion debe descargar paquetes disponibles y guardarlos en almacenamiento local.

### RF-014 Evitar descargas duplicadas

La aplicacion debe detectar paquetes ya descargados para evitar descargas repetidas innecesarias.

### RF-015 Extraer XML

La aplicacion debe extraer los XML contenidos en los paquetes descargados.

### RF-016 Indexar metadatos minimos

La aplicacion debe guardar metadatos minimos de cada XML descargado para consulta local.

Metadatos minimos propuestos:

- UUID.
- RFC emisor.
- RFC receptor.
- Fecha de emision.
- Tipo de comprobante.
- Uso CFDI, si esta disponible.
- Metodo de pago, si esta disponible.
- Forma de pago, si esta disponible.
- Subtotal, si esta disponible.
- IVA u otros impuestos trasladados principales, si estan disponibles.
- Total.
- Moneda, si esta disponible.
- Complementos presentes.
- Estado local del archivo.
- Ruta local del XML.
- Perfil SAT asociado.
- Solicitud y paquete origen.

### RF-017 Consulta local

La aplicacion debe permitir consultar CFDI descargados desde la base local.

### RF-018 Logs funcionales

La aplicacion debe registrar eventos relevantes: autenticacion, solicitud, verificacion, descarga, extraccion, errores y reintentos.

### RF-019 Reintentos controlados

La aplicacion debe reintentar operaciones temporales fallidas sin duplicar solicitudes ni descargas ya registradas.

### RF-020 Manejo de codigos SAT

La aplicacion debe guardar y mostrar los codigos/mensajes SAT relevantes para que el usuario entienda el resultado de cada operacion.

### RF-021 Particion automatica

La aplicacion debe partir automaticamente trabajos de descarga en varias solicitudes SAT cuando el rango, filtros o volumen estimado puedan exceder topes del servicio.

### RF-022 Cola visible

La aplicacion debe mostrar una cola operativa donde el usuario pueda ver trabajos, solicitudes y paquetes con estados entendibles.

Estados internos sugeridos:

- Borrador.
- Solicitada.
- Aceptada.
- En proceso.
- Terminada.
- Descargando.
- Descargada.
- Extrayendo.
- Extraida.
- Sin informacion.
- Error.
- Rechazada.
- Vencida.

### RF-023 Traduccion operativa de errores

La aplicacion debe traducir codigos SAT a mensajes operativos accionables, sin ocultar el codigo original.

### RF-024 Integridad de paquetes

La aplicacion debe validar que los paquetes descargados sean utilizables. Si el servicio proporciona hash o informacion de integridad, debe usarse; si no, la aplicacion debe validar al menos que el ZIP sea legible y que la extraccion termine correctamente.

### RF-025 Organizacion predecible de archivos

La aplicacion debe guardar XML y paquetes en una estructura navegable sin depender de la app.

Estructura sugerida:

```text
{base}/{rfc}/{anio}/{mes}/{emitidos|recibidos}/{uuid}.xml
```

La estructura final debe revisarse antes de implementarse, especialmente para metadata, paquetes ZIP y solicitudes partidas.

### RF-026 Deduplicacion por UUID

La aplicacion debe detectar CFDI repetidos por UUID cuando existan rangos superpuestos o paquetes duplicados.

### RF-027 Avisos de e.firma

La aplicacion debe detectar y mostrar vigencia de e.firma cuando pueda leerla del certificado. Debe alertar con anticipacion razonable antes de que caduque.

### RF-028 Limite conocido de vigencia fiscal

La aplicacion debe indicar cuando un CFDI descargado no tiene verificacion reciente de vigencia/cancelacion. Esta verificacion no bloquea el MVP, pero la UI y la documentacion no deben presentar la descarga local como prueba definitiva de vigencia fiscal.

## 9. Requerimientos no funcionales

### RNF-001 Local-first

La aplicacion debe funcionar sin backend remoto. Solo requiere internet para comunicarse con SAT.

### RNF-002 Seguridad local

La aplicacion debe tratar certificados, llaves privadas, contrasenas, tokens y XML como informacion sensible.

### RNF-003 Trazabilidad

La aplicacion debe conservar historial suficiente para entender que se solicito, cuando, con que parametros, que respondio SAT y que paquetes/XML se obtuvieron.

### RNF-004 Idempotencia operativa

Las operaciones repetibles deben evitar duplicados: solicitudes con mismos parametros, descargas del mismo paquete y XML con mismo UUID.

### RNF-005 Resiliencia

La aplicacion debe tolerar errores temporales de red, expiracion de token, indisponibilidad del SAT y respuestas intermedias.

### RNF-006 Escala inicial

La aplicacion debe soportar al menos 10,000 CFDI descargados e indexados localmente sin degradacion notable para uso personal.

### RNF-007 Observabilidad local

Los logs deben permitir diagnosticar errores sin exponer innecesariamente secretos o contrasenas.

### RNF-008 Portabilidad

La arquitectura debe permitir distribuir la aplicacion como app de escritorio instalable. La plataforma exacta se definira despues.

### RNF-009 Zona horaria de negocio

La aplicacion debe manejar rangos de fecha usando una zona horaria de negocio explicita, inicialmente hora del centro de Mexico.

### RNF-010 Control de frecuencia

El worker debe aplicar limites de frecuencia y backoff para consultas y descargas, evitando ciclos agresivos contra el SAT.

### RNF-011 Recuperacion ante trabajos parciales

La aplicacion debe poder retomar trabajos incompletos despues de cerrar la app, reiniciar el equipo o fallar una operacion.

### RNF-012 Alertas por estado incompleto

La aplicacion debe mostrar claramente cuando un trabajo quedo parcial, con paquetes pendientes, XML no extraidos, errores o respuestas inconclusas.

### RNF-013 Respaldo local

La arquitectura debe permitir respaldar la base de datos local, configuracion y archivos descargados. La politica concreta de respaldo se definira despues.

## 10. Modelo conceptual inicial

Entidades propuestas:

- `PerfilSat`: contribuyente/RFC gestionado localmente.
- `CredencialSat`: certificado, llave privada y material sensible protegido.
- `TrabajoDescarga`: intencion operativa del usuario, por ejemplo descargar recibidos de enero 2026.
- `SolicitudDescarga`: solicitud enviada al SAT.
- `ConsultaSolicitud`: cada verificacion de estado realizada.
- `PaqueteDescarga`: paquete SAT asociado a una solicitud.
- `CfdiXml`: XML extraido e indexado.
- `LogOperacion`: evento funcional o tecnico relevante.

Relaciones principales:

- Un `PerfilSat` tiene cero o mas `CredencialSat`.
- Un `PerfilSat` tiene muchos `TrabajoDescarga`.
- Un `TrabajoDescarga` puede generar una o muchas `SolicitudDescarga`.
- Una `SolicitudDescarga` tiene muchas `ConsultaSolicitud`.
- Una `SolicitudDescarga` puede tener muchos `PaqueteDescarga`.
- Un `PaqueteDescarga` puede contener muchos `CfdiXml`.
- Un `CfdiXml` debe poder rastrearse hasta su paquete, solicitud, trabajo y perfil SAT.

## 11. Estados de solicitud

Estados documentados por SAT para `EstadoSolicitud`:

| Valor | Estado local sugerido | Significado |
| --- | --- | --- |
| 1 | Aceptada | SAT recibio la solicitud. |
| 2 | En proceso | SAT esta procesando la solicitud. |
| 3 | Terminada | SAT termino y puede devolver paquetes. |
| 4 | Error | SAT reporto error en la solicitud. |
| 5 | Rechazada | SAT rechazo la solicitud. |
| 6 | Vencida | La solicitud o paquetes vencieron. |

Notas:

- La documentacion indica que la solicitud vence 72 horas despues de generado el paquete de descarga.
- Solo el estado terminado debe habilitar descarga de paquetes.
- Error, rechazada y vencida deben considerarse estados terminales para el worker, salvo accion manual del usuario.

Estados internos adicionales de la aplicacion:

| Estado interno | Uso |
| --- | --- |
| Borrador | Trabajo capturado pero no enviado. |
| Solicitada | Solicitud enviada al SAT, esperando primera verificacion util. |
| Descargando | Paquetes en proceso de descarga. |
| Descargada | Paquetes guardados localmente. |
| Extrayendo | XML en proceso de extraccion/indexacion. |
| Extraida | XML extraidos e indexados. |
| Sin informacion | SAT no encontro informacion para los filtros. |
| Parcial | Hay paquetes, XML o solicitudes pendientes dentro del trabajo. |

Los estados internos no deben reemplazar los codigos originales SAT; deben complementarlos para que el usuario entienda el avance real.

## 12. Codigos SAT relevantes

Codigos de verificacion documentados:

| Codigo | Manejo esperado |
| --- | --- |
| 300 Usuario No Valido | Mostrar error de autenticacion/usuario. |
| 301 XML Mal Formado | Marcar solicitud/consulta con error tecnico de request. |
| 302 Sello Mal Formado | Marcar error de firma. |
| 303 Sello no corresponde con RfcSolicitante | Marcar error de credencial/RFC. |
| 304 Certificado Revocado o Caduco | Marcar credencial como no valida para operar. |
| 305 Certificado Invalido | Marcar error de credencial. |
| 5000 Solicitud recibida con exito | Continuar flujo normal. |
| 5003 Tope maximo | Indicar que se deben acotar filtros/rango. |
| 5004 No se encontro la informacion | Marcar resultado sin paquetes/informacion. |
| 5011 Limite de descargas por folio por dia | Posponer descarga/verificacion segun aplique. |

Codigos de solicitud documentados:

| Codigo | Manejo esperado |
| --- | --- |
| 5000 Solicitud recibida con exito | Guardar solicitud y continuar con verificacion. |
| 5002 Se agotaron solicitudes de por vida | Evitar reintento automatico con los mismos parametros. |
| 5003 Tope maximo | Sugerir reducir rango o filtros. |
| 5004 No se encontro informacion | Guardar resultado sin paquetes. |
| 5005 Solicitud duplicada | Asociar o mostrar que ya existe una solicitud vigente con los mismos parametros. |
| 404 Error no controlado | Registrar y permitir reintento manual/controlado. |

## 13. Criterios de aceptacion iniciales

### CA-001 Registrar perfil

Dado que el usuario tiene un RFC y credenciales validas, cuando registra un perfil SAT, entonces la aplicacion guarda el perfil y protege sus credenciales localmente.

### CA-002 Crear solicitud

Dado un perfil SAT valido y filtros soportados por SAT, cuando el usuario crea una solicitud, entonces la aplicacion envia la solicitud, guarda el identificador SAT y registra la respuesta inicial.

### CA-003 Consultar solicitud

Dado que existe una solicitud registrada, cuando la aplicacion consulta su estado, entonces guarda la respuesta SAT con estado, codigo, mensaje, numero de CFDI e identificadores de paquetes si existen.

### CA-004 Detectar solicitud terminada

Dado que SAT responde `EstadoSolicitud = 3`, cuando la aplicacion procesa la respuesta, entonces registra los paquetes disponibles y habilita su descarga.

### CA-005 Descargar paquete

Dado un paquete disponible, cuando la aplicacion lo descarga correctamente, entonces guarda el archivo localmente y actualiza el estado del paquete.

### CA-006 Extraer XML

Dado un paquete descargado, cuando la aplicacion lo extrae, entonces guarda los XML localmente e indexa sus metadatos minimos.

### CA-007 Consulta local

Dado que existen CFDI indexados, cuando el usuario filtra en la UI, entonces la aplicacion muestra resultados desde la base local sin consultar al SAT.

### CA-008 Manejo de errores SAT

Dado que SAT responde con error, rechazo, vencimiento o limite, cuando la aplicacion procesa la respuesta, entonces guarda el codigo/mensaje y muestra un estado entendible para el usuario.

### CA-009 Particion de trabajo

Dado un trabajo de descarga que excede limites conocidos del SAT, cuando el usuario lo confirma, entonces la aplicacion genera varias solicitudes SAT rastreables bajo el mismo trabajo.

### CA-010 Deduplicacion

Dado que un XML ya fue descargado previamente, cuando otro paquete contiene el mismo UUID, entonces la aplicacion no debe crear un duplicado logico y debe preservar el rastro de origen.

### CA-011 Paquete invalido o incompleto

Dado que un paquete descargado no se puede abrir o extraer, cuando la aplicacion lo procesa, entonces debe marcarlo como error, conservar evidencia en logs y permitir reintento controlado.

### CA-012 Vigencia no verificada

Dado que la aplicacion no ha consultado vigencia/cancelacion de un CFDI, cuando lo muestra en la consulta local, entonces debe indicar que el estatus fiscal vigente no ha sido verificado por este flujo.

### CA-013 Trabajo parcial

Dado que un trabajo tiene solicitudes, paquetes o XML pendientes, cuando el usuario revisa la cola, entonces la aplicacion debe mostrarlo como parcial o pendiente, no como completado.

## 14. Riesgos

- La documentacion disponible en el repositorio cubre principalmente verificacion; falta confirmar todos los parametros y reglas del servicio de solicitud y descarga.
- El manejo de WS-Security, firmas XML y RSA-SHA1 puede ser sensible a detalles de canonicalizacion y estructura SOAP.
- SAT puede cambiar endpoints, certificados, reglas o disponibilidad del servicio.
- El almacenamiento de llaves privadas y contrasenas requiere una decision cuidadosa de seguridad local.
- La duplicidad de solicitudes debe manejarse bien para evitar topar limites SAT.
- Los paquetes vencen; el worker debe priorizar descargas disponibles.
- XML mal formados, duplicados o con variantes de version pueden complicar la indexacion.
- El usuario puede confiar de mas en XML descargados localmente aunque no se haya verificado vigencia/cancelacion actual.
- Cancelaciones en proceso pueden requerir un modelo mas rico que vigente/cancelado si se agrega verificacion fiscal en una fase posterior.
- CFDI de nomina, pago, traslado, ingreso y egreso tienen usos contables distintos; la UI debe evitar mezclarlos de forma confusa.
- CFDI 3.3 historicos pueden aparecer en backfills y el parser debe tolerarlos si se soporta descarga historica.
- SAT puede degradarse en periodos de alta carga; el worker debe tolerar caidas prolongadas sin perder trazabilidad.
- Una descarga parcial no detectada puede provocar errores de trabajo contable. La aplicacion debe preferir estados incompletos visibles sobre cierres silenciosos.

## 15. Preguntas abiertas

- Cual sera la primera plataforma soportada: macOS, Windows o ambas?
- Se usara solo e.firma? La documentacion revisada exige e.firma vigente y no se debe asumir CSD para descarga masiva.
- Cuales son exactamente los filtros soportados por el servicio de solicitud para CFDI y metadata?
- El MVP descargara siempre CFDI XML, usara metadata primero o permitira ambos tipos de solicitud?
- El primer flujo sera backfill historico, corte mensual o ambos?
- La app sera descargador o fuente de verdad fiscal? Si sera fuente de verdad despues, cuando entra vigencia/cancelacion?
- Cuantos RFC reales y que volumen esperado por RFC/mes se usaran en la primera version?
- La consulta local necesita exportacion CSV/Excel desde el inicio?
- Se requiere password maestro de la aplicacion o basta con almacenamiento seguro del sistema operativo?
- El worker correra solo mientras la app esta abierta o tambien en segundo plano al iniciar sesion?
- Como se organizaran fisicamente los archivos: por RFC, anio/mes, solicitud o paquete?
- Que politica se usara para limpiar paquetes ZIP despues de extraer XML?
- Que nivel de detalle debe tener el log visible al usuario frente al log tecnico?
- Que estrategia minima de respaldo local se acepta para base de datos, XML y logs?
- Se marcara un mes como cerrado o siempre quedara sujeto a re-descarga/revision por CFDI tardios?
