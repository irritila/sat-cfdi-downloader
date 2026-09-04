# Documentación para la implementación del Servicio Web de Verificación de Descarga Masiva de CFDI y CFDI de retenciones

**SAT / SHCP — Diciembre 2023, Versión 1.2**

---

## Índice

1. [Introducción](#1-introducción)
2. [Prerrequisitos](#2-prerrequisitos)
3. [Modo de uso para servicios](#3-modo-de-uso-para-servicios)
4. [Autenticación para servicios](#4-autenticación-para-servicios)
5. [Servicio de Verificación de Descarga Masiva](#5-servicio-de-verificación-de-descarga-masiva)
6. [Control de cambios](#6-control-de-cambios)

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

## 5. Servicio de Verificación de Descarga Masiva

Permite verificar el estatus de las solicitudes de descarga realizadas previamente a través del Servicio de Solicitud de Descarga Masiva. Si la solicitud tiene estatus de terminado, devuelve los identificadores de los paquetes que conforman la solicitud.

### 5.1 VerificaSolicitudDescarga

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

### 5.2 Códigos de respuesta

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

## 6. Control de cambios

| # | Cambio realizado | Fecha |
|---|-----------------|-------|
| 1 | Introducción: se precisa que la documentación contiene recomendaciones que pueden aplicarse siempre y cuando se utilice un equipo propio que no comprometa la información. | 14/08/2018 |
| 2 | Autenticación para Servicios. Servicio de autenticación: Se precisa que la e.firma no debe almacenarse en el repositorio de llaves criptográficas si no se está utilizando un equipo propio, a fin de no comprometer la información. | 14/08/2018 |
| 3 | Se incorpora la descripción referente a los códigos de evento 5003 y 5011 del Servicio de Verificación de Descarga Masiva. | 29/11/2023 |
