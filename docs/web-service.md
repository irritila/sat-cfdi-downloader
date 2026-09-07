# Especificación local para la implementación del Servicio Web de Descarga Masiva de CFDI

**Compilación local basada en documentación oficial SAT / SHCP**

---

> Nota de alcance local: este archivo resume la especificacion necesaria para el MVP personal. La fuente canonica queda definida en `docs/adrs/0005-sat-web-service-contract-source.md`. Los servicios de CFDI de retenciones quedan fuera del MVP.

## Índice

1. [Introducción](#1-introducción)
2. [Prerrequisitos](#2-prerrequisitos)
3. [Modo de uso para servicios](#3-modo-de-uso-para-servicios)
4. [Autenticación para servicios](#4-autenticación-para-servicios)
5. [Servicio de Solicitud de Descarga Masiva](#5-servicio-de-solicitud-de-descarga-masiva)
6. [Servicio de Verificación de Descarga Masiva](#6-servicio-de-verificación-de-descarga-masiva)
7. [Servicio de Descarga Masiva](#7-servicio-de-descarga-masiva)
8. [Control de cambios](#8-control-de-cambios)

## Fuentes oficiales usadas

- [Portal SAT: Consulta y recuperación de comprobantes](https://wwwmat.sat.gob.mx/cs/Satellite?c=ConsultaInfo&childpagename=SatTyR%2FConsultaInfo%2FSAT_LandingConsultaInformacion&cid=1462231542968&packedargs=d%3DTouch&pagename=TySWrapper).
- [SAT: URLs productivas del Web Service](https://wwwmat.sat.gob.mx/cs/Satellite?blobcol=urldata&blobkey=id&blobtable=MungoBlobs&blobwhere=1461174995058&ssbinary=true).
- [SAT: Web service de solicitud de descargas para CFDI y retenciones](https://wwwmat.sat.gob.mx/cs/Satellite?blobcol=urldata&blobkey=id&blobtable=MungoBlobs&blobwhere=1461175195160&ssbinary=true).
- [SAT: Web service de descarga de solicitudes exitosas](https://wwwmat.sat.gob.mx/cs/Satellite?blobcol=urldata&blobkey=id&blobtable=MungoBlobs&blobwhere=1461174995026&ssbinary=true).
- [SAT: Web service de verificación de descarga masiva](https://wwwmat.sat.gob.mx/cs/Satellite?blobcol=urldata&blobkey=id&blobtable=MungoBlobs&blobwhere=1461175779527&ssbinary=true).
- [WSDL productivo: SolicitaDescargaService](https://cfdidescargamasivasolicitud.clouda.sat.gob.mx/SolicitaDescargaService.svc?singleWsdl).
- [WSDL productivo: DescargaMasivaService](https://cfdidescargamasiva.clouda.sat.gob.mx/DescargaMasivaService.svc?singleWsdl).

---

## 1. Introducción

El Servicio Descarga Masiva de CFDI y retenciones está diseñado para que los contribuyentes en su calidad de emisores o receptores de CFDI puedan recuperar sus comprobantes que hayan emitido o recibido por las operaciones comerciales realizadas. Se implementó el Servicio Web (WS) que permite la descarga masiva de CFDI en sus propios equipos de cómputo.

El servicio permite:

- Generar solicitudes de descarga masiva de CFDI y CFDI de retenciones.
- Verificar el estatus de las solicitudes realizadas.
- Permitir la descarga de los archivos XML o metadatos generados en archivos compactados mediante las solicitudes procesadas de manera exitosa.

> **Nota de seguridad:** Algunas recomendaciones están enfocadas para realizarse en equipos de cómputo propios del contribuyente. De no ser así, se debe garantizar no poner en riesgo la información almacenándola en un equipo que no sea el propio.

---

## 2. Prerrequisitos

El contribuyente debe contar con el **certificado de e.firma vigente** para solicitar la información.

---

## 3. Modo de uso para servicios

Para utilizar los servicios Web descritos en este documento es necesario crear el cliente de servicios Web correspondiente a partir de:

- La **URL del Servicio**, o
- La **URL del WSDL**

de acuerdo con las instrucciones de la plataforma desde la que se vaya a consumir el servicio web.

Una vez creado el cliente, el siguiente paso es verificar el tipo de certificado a enviar para realizar la autenticación y posterior consumo de los servicios.

---

## 4. Autenticación para servicios

La autenticación se realiza mediante el **certificado de e.firma vigente y su respectiva llave privada**.

El tipo de autenticación cumple con las especificaciones de **Web Services Security v1.0 (WS-Security 2004)**:
https://www.oasis-open.org/standards#wssv1.0

### 4.1 Servicio de autenticación

Se recomienda utilizar el almacén local de llaves criptográficas para almacenar y recuperar la llave. Esto debe realizarse siempre y cuando se esté utilizando el propio equipo de cómputo. De no ser así, se debe garantizar que la información referente a la e.firma no se almacene en el equipo de un tercero.

**Ejemplo en C# — Obtener certificado por thumbprint:**

```csharp
private static X509Certificate2 ObtenerKey(string thumbPrint)
{
    X509Store store = new X509Store(StoreName.My, StoreLocation.LocalMachine);
    store.Open(OpenFlags.ReadOnly);
    var certificates = store.Certificates;
    var certificateEnc = certificates.Find(X509FindType.FindByThumbprint, thumbPrint, false);
    if (certificateEnc.Count > 0)
    {
        X509Certificate2 certificate = certificateEnc[0];
        return certificate;
    }
    return null;
}
```

**Ejemplo — Enviar certificado y obtener token:**

```csharp
autenticacion.ClientCredentials.ClientCertificate.Certificate = certi[0];
string token = autenticacion.Autentica();
```

### 4.2 Petición SOAP al servicio de autenticación

**Endpoint:**
```
https://desktop-3fi24u7:444/Autenticacion/Autenticacion.svc
```

**SOAPAction:**
```
http://DescargaMasivaTerceros.gob.mx/IAutenticacion/Autentica
```

**Estructura de la petición:**

```xml
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"
            xmlns:u="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd">
  <s:Header>
    <o:Security s:mustUnderstand="1"
                xmlns:o="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd">
      <u:Timestamp u:Id="_0">
        <u:Created>2018-05-09T21:21:42.953Z</u:Created>
        <u:Expires>2018-05-09T21:26:42.953Z</u:Expires>
      </u:Timestamp>
      <o:BinarySecurityToken
          u:Id="uuid-572bbc7a-287d-4233-bdcb-75f92418becd-1"
          ValueType="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-x509-token-profile-1.0#X509v3"
          EncodingType="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-soap-message-security-1.0#Base64Binary">
        <!-- Certificado X.509 en Base64 -->
      </o:BinarySecurityToken>
      <Signature xmlns="http://www.w3.org/2000/09/xmldsig#">
        <SignedInfo>
          <CanonicalizationMethod Algorithm="http://www.w3.org/2001/10/xml-exc-c14n#" />
          <SignatureMethod Algorithm="http://www.w3.org/2000/09/xmldsig#rsa-sha1" />
          <Reference URI="#_0">
            <Transforms>
              <Transform Algorithm="http://www.w3.org/2001/10/xml-exc-c14n#" />
            </Transforms>
            <DigestMethod Algorithm="http://www.w3.org/2000/09/xmldsig#sha1" />
            <DigestValue>Ij+Epaya2U5D/sSncl6BHkkTRWo=</DigestValue>
          </Reference>
        </SignedInfo>
        <SignatureValue><!-- Valor de firma --></SignatureValue>
        <KeyInfo>
          <o:SecurityTokenReference>
            <o:Reference
                ValueType="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-x509-token-profile-1.0#X509v3"
                URI="#uuid-572bbc7a-287d-4233-bdcb-75f92418becd-1" />
          </o:SecurityTokenReference>
        </KeyInfo>
      </Signature>
    </o:Security>
    <To s:mustUnderstand="1"
        xmlns="http://schemas.microsoft.com/ws/2005/05/addressing/none">
      https://desktop-3fi24u7:444/Autenticacion/Autenticacion.svc
    </To>
    <Action s:mustUnderstand="1"
            xmlns="http://schemas.microsoft.com/ws/2005/05/addressing/none">
      http://DescargaMasivaTerceros.gob.mx/IAutenticacion/Autentica
    </Action>
  </s:Header>
  <s:Body>
    <Autentica xmlns="http://DescargaMasivaTerceros.gob.mx" />
  </s:Body>
</s:Envelope>
```

> **Importante:** Si existe algún error durante la autenticación y no se obtiene el token, no se podrán utilizar los demás servicios. Al consumir los servicios se validará el token; si no es válido se mandará una excepción de autenticación.

> **Nota:** El servicio de autenticación descrito en esta sección es el mismo a utilizar para los servicios de Solicitud de Descarga Masiva, Verificación Descarga Masiva y Descarga Masiva.

---

## 5. Servicio de Solicitud de Descarga Masiva

Permite crear una solicitud para descargar CFDI o metadata por rango de fechas, emisor/receptor y filtros soportados por SAT. En el MVP solo se usara para solicitar paquetes CFDI/XML.

Nota de version: el PDF oficial SAT enlazado desde el portal puede describir la operacion historica `SolicitaDescarga`. El WSDL productivo vigente publica operaciones separadas para emitidos, recibidos y folio. Para la forma SOAP concreta se usa el WSDL productivo; para reglas funcionales, limites y codigos se conserva la documentacion oficial SAT como fuente canonica.

### 5.1 Endpoint productivo para CFDI

```text
https://cfdidescargamasivasolicitud.clouda.sat.gob.mx/SolicitaDescargaService.svc
```

WSDL:

```text
https://cfdidescargamasivasolicitud.clouda.sat.gob.mx/SolicitaDescargaService.svc?singleWsdl
```

### 5.2 Operaciones publicadas por el WSDL productivo

| Operacion | Uso en MVP | SOAPAction |
| --- | --- | --- |
| `SolicitaDescargaEmitidos` | Crear solicitud CFDI/XML emitidos. | `http://DescargaMasivaTerceros.sat.gob.mx/ISolicitaDescargaService/SolicitaDescargaEmitidos` |
| `SolicitaDescargaRecibidos` | Crear solicitud CFDI/XML recibidos. | `http://DescargaMasivaTerceros.sat.gob.mx/ISolicitaDescargaService/SolicitaDescargaRecibidos` |
| `SolicitaDescargaFolio` | Fuera del MVP. | `http://DescargaMasivaTerceros.sat.gob.mx/ISolicitaDescargaService/SolicitaDescargaFolio` |

### 5.3 Parámetros comunes

| Parámetro | Tipo | Dirección | Uso en MVP | Observaciones |
| --- | --- | --- | --- | --- |
| Authorization | Header | Entrada | Obligatorio | Token obtenido por autenticación. |
| FechaInicial | DateTime | Entrada | Obligatorio | No se declara cuando la consulta es por folio fiscal. |
| FechaFinal | DateTime | Entrada | Obligatorio | No se declara cuando la consulta es por folio fiscal. |
| RfcSolicitante | String | Entrada | Obligatorio en MVP | RFC dueño de la e.firma que realiza la solicitud. En el MVP se envia siempre igual a `PerfilSat.rfc`. |
| TipoSolicitud | Enum | Entrada | Fijo en `CFDI` | SAT tambien soporta otros valores en WSDL; quedan fuera del MVP. |
| EstadoComprobante | String | Entrada | Fijo en `Vigente` | Se fija internamente para solicitar XML vigentes y no exponer cancelados en el MVP. |
| TipoComprobante | String | Entrada | Opcional | `I`, `E`, `T`, `N`, `P` o sin filtro. |
| Complemento | String | Entrada | Opcional | Identificador de complemento SAT o sin filtro. |
| RfcACuentaTerceros | String | Entrada | Fuera del MVP | Filtro especifico del servicio SAT. |
| Signature | SignatureType | Entrada | Obligatorio | Firma XML de la petición con e.firma. |
| IdSolicitud | String | Salida | Persistir | Identificador SAT de la solicitud. |
| RfcSolicitante (respuesta) | String | Salida | Persistir | RFC solicitante devuelto por SAT si viene en respuesta. |
| CodEstatus | String | Salida | Persistir separado | Código de estatus de la creación de solicitud. No mezclar con `CodigoEstadoSolicitud` de verificación. |
| Mensaje | String | Salida | Persistir separado | Mensaje SAT asociado al estatus de creación. |

### 5.4 Operación `SolicitaDescargaEmitidos`

Uso:

- Crear solicitudes para CFDI emitidos por el RFC del perfil SAT.

Mapeo:

| UI | Atributo SAT | Regla MVP |
| --- | --- | --- |
| Perfil SAT | `RfcEmisor` | Igual a `PerfilSat.rfc`. |
| Perfil SAT | `RfcSolicitante` | Igual a `PerfilSat.rfc`. |
| RFC contraparte | `RfcReceptores/RfcReceptor` | Opcional; el MVP permite un receptor contraparte. El WSDL permite arreglo. |

### 5.5 Operación `SolicitaDescargaRecibidos`

Uso:

- Crear solicitudes para CFDI recibidos por el RFC del perfil SAT.

Mapeo:

| UI | Atributo SAT | Regla MVP |
| --- | --- | --- |
| Perfil SAT | `RfcReceptor` | Igual a `PerfilSat.rfc`. |
| Perfil SAT | `RfcSolicitante` | Igual a `PerfilSat.rfc`. |
| RFC contraparte | `RfcEmisor` | Opcional; se usa cuando el usuario filtra por emisor. |

### 5.6 Operación `SolicitaDescargaFolio`

El WSDL productivo publica `SolicitaDescargaFolio`, pero queda fuera del MVP. La UI no permite crear solicitudes por UUID/Folio.

#### Orden de atributos para firma

SAT documenta que la petición debe ordenarse para validar correctamente la firma. El orden historico documentado para los atributos de solicitud es:

1. `Complemento`
2. `EstadoComprobante`
3. `FechaInicial`
4. `FechaFinal`
5. `Folio`
6. `RfcACuentaTerceros`
7. `RfcEmisor`
8. `RfcSolicitante`
9. `TipoComprobante`
10. `TipoSolicitud`

La implementacion debe validar el orden exacto por operacion durante el spike SAT, usando WSDL productivo y prueba real, antes de conectar `SatGateway` productivo.

#### Respuesta esperada

Cuando SAT acepta la solicitud, la respuesta de la operacion elegida contiene:

- `IdSolicitud`.
- `RfcSolicitante`, si viene en respuesta.
- `CodEstatus`.
- `Mensaje`.

La aplicacion debe persistir esos valores en `SolicitudMasiva`, separando los campos de creacion de solicitud de los campos de verificacion.

#### Códigos de respuesta

| Código | Mensaje | Observaciones |
| --- | --- | --- |
| 300 | Usuario No Válido | |
| 301 | XML Mal Formado | Request con información inválida, por ejemplo RFC receptor no válido. |
| 302 | Sello Mal Formado | |
| 303 | Sello no corresponde con RfcSolicitante | |
| 304 | Certificado Revocado o Caduco | Certificado inválido por tipo, vigencia u otra condición. |
| 305 | Certificado Inválido | Certificado inválido por tipo, vigencia u otra condición. |
| 5000 | Solicitud de descarga recibida con éxito | Solicitud aceptada por SAT. |
| 5001 | Tercero no autorizado | El solicitante no tiene autorización de descarga. |
| 5002 | Se han agotado las solicitudes de por vida | Se alcanzó el límite con el mismo criterio. |
| 5005 | Ya se tiene una solicitud registrada | Ya existe una solicitud activa con los mismos criterios. |
| 5006 | Error interno en el proceso | Error SAT. |

#### Reglas para el MVP

- Solo CFDI regulares, no retenciones.
- `TipoSolicitud` fijo en `CFDI`.
- `EstadoComprobante` fijo internamente en `Vigente`.
- No se implementa solicitud de metadata.
- No se implementa solicitud por `Folio/UUID`, aunque el WSDL publique `SolicitaDescargaFolio`.
- No se expone `RfcACuentaTerceros`.
- No se expone `EstadoComprobante` en la UI; al solicitar XML/CFDI, el MVP conserva `Vigente` como constante interna.
- La UI debe evitar crear solicitudes locales duplicadas antes de llamar a SAT.
- La equivalencia local anti-duplicados debe considerar `operacion_sat`, perfil/RFC solicitante, fechas, emisor, receptor/receptores, tipo de solicitud, estado de comprobante, tipo de comprobante y complemento.
- La traduccion UI "emitidos/recibidos" queda definida por `ADR 0013`.
- Los filtros vacios se omiten del XML; no deben enviarse cadenas vacias salvo que una prueba contra SAT demuestre que el contrato lo exige.

---

## 6. Servicio de Verificación de Descarga Masiva

Permite verificar el estatus de las solicitudes de descarga realizadas previamente a través del Servicio de Solicitud de Descarga Masiva. Si la solicitud tiene estatus de terminado, devuelve los identificadores de los paquetes que conforman la solicitud.

### 6.1 VerificaSolicitudDescarga

Verifica el estatus de una solicitud de descarga masiva realizada previamente.

#### Parámetros

| Parámetro | Tipo de dato | Descripción | Tipo |
|-----------|-------------|-------------|------|
| Authorization | Header | Token de autenticación. Formato: `WRAP access_token="Token"` | Entrada |
| IdSolicitud | String | Identificador de la solicitud a consultar | Entrada |
| RfcSolicitante | String | RFC del solicitante que generó la solicitud | Entrada |
| Signature | SignatureType | Firma de la petición con el certificado de e.firma | Entrada |
| IdsPaquetes | Lista\<String\> | Identificadores de los paquetes. Solo se devuelve cuando el estado es **Terminado** | Salida |
| EstadoSolicitud | Int | Estado de la solicitud de descarga | Salida |
| CodigoEstadoSolicitud | String | Código de estado: `5000`, `5001`, `5002` o `5005` | Salida |
| NumeroCFDIs | Int | Número de CFDI que conforman la solicitud | Salida |
| CodEstatus | String | Código de estatus de la petición de verificación | Salida |
| Mensaje | String | Descripción del código de la petición de verificación | Salida |

**Estados de la solicitud (EstadoSolicitud):**

| Valor | Estado |
|-------|--------|
| 1 | Aceptada |
| 2 | En Proceso |
| 3 | Terminada |
| 4 | Error |
| 5 | Rechazada |
| 6 | Vencida* |

> *La solicitud vence 72 horas después de que se generó el paquete de descarga.

#### Ejemplo de petición

```http
POST https://srvsolicituddescargamaster.cloudapp.net/VerificaSolicitudDescargaService.svc HTTP/1.1
Accept-Encoding: gzip,deflate
Content-Type: text/xml;charset=UTF-8
SOAPAction: "http://DescargaMasivaTerceros.sat.gob.mx/IVerificaSolicitudDescargaService/VerificaSolicitudDescarga"
Authorization: WRAP access_token="eyJhbGci..."
Content-Length: 4641
Host: srvsolicituddescargamaster.cloudapp.net
Connection: Keep-Alive
User-Agent: Apache-HttpClient/4.1.1 (java 1.5)
```

```xml
<soapenv:Envelope
    xmlns:soapenv="http://schemas.xmlsoap.org/soap/envelope/"
    xmlns:des="http://DescargaMasivaTerceros.sat.gob.mx"
    xmlns:xd="http://www.w3.org/2000/09/xmldsig#">
  <soapenv:Header/>
  <soapenv:Body>
    <des:VerificaSolicitudDescarga>
      <!--Optional:-->
      <des:solicitud
          IdSolicitud="4E80345D-917F-40BB-A98F-4A73939343C5"
          RfcSolicitante="AXT940727FP8">
        <!--Optional:-->
        <Signature xmlns="http://www.w3.org/2000/09/xmldsig#">
          <SignedInfo>
            <CanonicalizationMethod Algorithm="http://www.w3.org/TR/2001/REC-xml-c14n-20010315"/>
            <SignatureMethod Algorithm="http://www.w3.org/2000/09/xmldsig#rsa-sha1"/>
            <Reference URI="">
              <Transforms>
                <Transform Algorithm="http://www.w3.org/2000/09/xmldsig#enveloped-signature"/>
              </Transforms>
              <DigestMethod Algorithm="http://www.w3.org/2000/09/xmldsig#sha1"/>
              <DigestValue>leZ4dK/Q/RNbckYkY7WOOnCjK5Q=</DigestValue>
            </Reference>
          </SignedInfo>
          <SignatureValue><!-- Valor de firma RSA-SHA1 --></SignatureValue>
          <KeyInfo>
            <X509Data>
              <X509IssuerSerial>
                <X509IssuerName>OID.1.2.840.113549.1.9.2=Responsable: ACDMA,
                  OID.2.5.4.45=SAT970701NN3, L=Coyoacán, S=Distrito Federal,
                  C=MX, PostalCode=06300, CN=A.C. 2 de pruebas(4096)
                </X509IssuerName>
                <X509SerialNumber>29223316287020600175976619842587949050930609075057</X509SerialNumber>
              </X509IssuerSerial>
              <X509Certificate><!-- Certificado en Base64 --></X509Certificate>
            </X509Data>
          </KeyInfo>
        </Signature>
      </des:solicitud>
    </des:VerificaSolicitudDescarga>
  </soapenv:Body>
</soapenv:Envelope>
```

La petición va dividida en dos partes:
1. El **Header**, que contiene el token de autenticación (ver sección 4).
2. El **Body**, que contiene la petición con los parámetros establecidos. Esta operación solo puede ser usada una vez autenticado exitosamente y con un token válido.

#### Ejemplo de respuesta

```http
HTTP/1.1 200 OK
Content-Type: text/xml; charset=utf-8
Content-Encoding: gzip
Server: Microsoft-IIS/10.0
X-Powered-By: ASP.NET
```

```xml
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Body xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
          xmlns:xsd="http://www.w3.org/2001/XMLSchema">
    <VerificaSolicitudDescargaResponse xmlns="http://DescargaMasivaTerceros.sat.gob.mx">
      <VerificaSolicitudDescargaResult
          CodEstatus="5000"
          EstadoSolicitud="3"
          CodigoEstadoSolicitud="5000"
          NumeroCFDIs="0"
          Mensaje="Solicitud Aceptada">
        <IdsPaquetes>4e80345d-917f-40bb-a98f-4a73939343c5_01</IdsPaquetes>
        <IdsPaquetes>4e80345d-917f-40bb-a98f-4a73939343c5_02</IdsPaquetes>
        <IdsPaquetes>4e80345d-917f-40bb-a98f-4a73939343c5_03</IdsPaquetes>
        <IdsPaquetes>4e80345d-917f-40bb-a98f-4a73939343c5_04</IdsPaquetes>
        <IdsPaquetes>4e80345d-917f-40bb-a98f-4a73939343c5_05</IdsPaquetes>
        <IdsPaquetes>4e80345d-917f-40bb-a98f-4a73939343c5_06</IdsPaquetes>
      </VerificaSolicitudDescargaResult>
    </VerificaSolicitudDescargaResponse>
  </s:Body>
</s:Envelope>
```

### 6.2 Códigos de respuesta

> **Nota importante:** Las URL integradas en esta documentación son solo referencia para la correcta interpretación de los ejemplos. Las URL válidas para la implementación del Web Service están publicadas en la sección **Consulta y Recuperación de Comprobantes**, del apartado de Factura Electrónica en el Portal del SAT.

#### Mensajes de la operación VerificaSolicitudDescarga

| Código | Mensaje | Observaciones |
|--------|---------|---------------|
| 300 | Usuario No Válido | |
| 301 | XML Mal Formado | El request contiene información inválida (ej: RFC de receptor no válido) |
| 302 | Sello Mal Formado | |
| 303 | Sello no corresponde con RfcSolicitante | |
| 304 | Certificado Revocado o Caduco | El certificado fue revocado o la fecha de vigencia expiró |
| 305 | Certificado Inválido | Puede ser inválido por tipo, codificación incorrecta, entre otros |
| 5000 | Solicitud recibida con éxito | |
| 5003 | Tope máximo de elementos de la consulta | La solicitud sobrepasa el máximo de resultados por tipo de solicitud (Metadata y CFDI) |
| 5004 | No se encontró la información | No se encontró la solicitud de descarga que se pretende verificar |
| 5011 | Límite de descargas por folio por día | Se ha alcanzado o sobrepasado el límite de descargas diarias por folio |

#### Códigos de Solicitud de Descarga Masiva (CodigoEstadoSolicitud)

| Código | Mensaje | Observaciones |
|--------|---------|---------------|
| 5000 | Solicitud recibida con éxito | Indica que la solicitud de descarga fue aceptada |
| 5002 | Se agotó las solicitudes de por vida | Para descarga tipo CFDI, se tiene un límite máximo con los mismos parámetros (Fecha inicial, Fecha final, RfcEmisor, RfcReceptor) |
| 5003 | Tope máximo | Se está superando el tope máximo de CFDI o Metadata por solicitud de descarga masiva |
| 5004 | No se encontró la información | La solicitud de descarga no generó paquetes por falta de información |
| 5005 | Solicitud duplicada | Si existe una solicitud vigente con los mismos parámetros (Fecha inicial, Fecha final, RfcEmisor, RfcReceptor, TipoSolicitud), no se permitirá generar una nueva |
| 404 | Error no controlado | Error genérico. Si persiste, levantar un RMA |

---

## 7. Servicio de Descarga Masiva

Permite descargar un paquete especifico asociado a una solicitud terminada. El paquete se obtiene usando un identificador devuelto por `VerificaSolicitudDescarga`.

### 7.1 Endpoint productivo para CFDI

```text
https://cfdidescargamasiva.clouda.sat.gob.mx/DescargaMasivaService.svc
```

WSDL:

```text
https://cfdidescargamasiva.clouda.sat.gob.mx/DescargaMasivaService.svc?singleWsdl
```

SOAPAction:

```text
http://DescargaMasivaTerceros.sat.gob.mx/IDescargaMasivaTercerosService/Descargar
```

### 7.2 Operación `Descargar`

La operación recibe una petición firmada con `IdPaquete` y `RfcSolicitante`. Si la descarga es exitosa, devuelve el contenido del paquete compactado.

#### Parámetros

| Parámetro | Tipo | Dirección | Uso en MVP | Observaciones |
| --- | --- | --- | --- | --- |
| Authorization | Header | Entrada | Obligatorio | Token obtenido por autenticación. |
| IdPaquete | String | Entrada | Obligatorio | Identificador de paquete devuelto por verificación. |
| RfcSolicitante | String | Entrada | Obligatorio | RFC dueño de la e.firma que realiza la descarga. |
| Signature | SignatureType | Entrada | Obligatorio | Firma XML de la petición con e.firma. |
| CodEstatus | String | Salida | Persistir | Código de estatus de la descarga. |
| Mensaje | String | Salida | Persistir | Mensaje SAT asociado al estatus. |
| Paquete | Stream/Base64 | Salida | Guardar como ZIP | Contenido del paquete descargado. |

#### Respuesta esperada

Cuando SAT acepta la descarga, la respuesta contiene:

- `CodEstatus`.
- `Mensaje`.
- `Paquete`.

La aplicacion debe guardar el paquete como archivo ZIP en la carpeta local definida por la solicitud y actualizar `PaqueteSolicitud`.

#### Códigos de respuesta

| Código | Mensaje | Observaciones |
| --- | --- | --- |
| 300 | Usuario No Válido | |
| 301 | XML Mal Formado | Request con información inválida. |
| 302 | Sello Mal Formado | |
| 303 | Sello no corresponde con RfcSolicitante | |
| 304 | Certificado Revocado o Caduco | El certificado fue revocado o expiró. |
| 305 | Certificado Inválido | Puede deberse al tipo, codificación u otra condición. |
| 5000 | Solicitud de descarga recibida con éxito | Descarga aceptada por SAT. |
| 5004 | No se encontró la información | No se encontró la información del paquete solicitado. |
| 5007 | No existe el paquete solicitado | Los paquetes solo tienen periodo de vida limitado. |
| 5008 | Máximo de descargas permitidas | El paquete ya alcanzó el máximo de descargas. |
| 404 | Error no controlado | Error genérico. Si persiste, levantar un RMA. |

#### Reglas para el MVP

- Solo descargar paquetes ya registrados en `PaqueteSolicitud`.
- Marcar el paquete como `Descargando` despues de obtener token valido.
- Guardar el ZIP sin extraer, parsear ni indexar XML.
- No validar que el ZIP pueda abrirse antes de marcarlo como `Descargado`.
- Si SAT devuelve `5007`, marcar el paquete como `Vencido`.
- Si SAT devuelve `5008`, marcar el paquete como `Error` y requerir intervencion manual.
- No reintentar automaticamente paquetes en `Error`.

---

## 8. Control de cambios

| # | Cambio realizado | Fecha |
|---|-----------------|-------|
| 1 | Introducción: se precisa que la documentación contiene recomendaciones que pueden aplicarse siempre y cuando se utilice un equipo propio que no comprometa la información. | 14/08/2018 |
| 2 | Autenticación para Servicios. Servicio de autenticación: Se precisa que la e.firma no debe almacenarse en el repositorio de llaves criptográficas si no se está utilizando un equipo propio, a fin de no comprometer la información. | 14/08/2018 |
| 3 | Se incorpora la descripción referente a los códigos de evento 5003 y 5011 del Servicio de Verificación de Descarga Masiva. | 29/11/2023 |
