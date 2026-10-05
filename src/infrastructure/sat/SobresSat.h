#pragma once

#include "domain/common/Resultado.h"
#include "infrastructure/sat/FirmaXml.h"
#include "infrastructure/sat/SatOperaciones.h"
#include "infrastructure/sat/XmlC14n.h"
#include "ports/secrets/SecretStoreTypes.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include <chrono>
#include <optional>

namespace satcfdi::sat {

// Sobres SOAP firmados de las cinco operaciones del spike (T006, D2/D3).
// Funciones puras salvo el reloj/ids del ContextoSobre (inyectables para
// goldens). Consumen `const MaterialFirma&`; el material no se copia al
// resultado salvo el certificado publico (Base64) que exige el protocolo.
//
// Forma (compacta, sin espacios entre elementos):
// - Autentica (doc §4.2): Header/o:Security con u:Timestamp u:Id="_0",
//   o:BinarySecurityToken (certificado) y Signature sobre "#_0" con
//   KeyInfo/o:SecurityTokenReference/o:Reference URI="#<idToken>". Cuerpo
//   vacio <Autentica xmlns="http://DescargaMasivaTerceros.gob.mx"/>.
// - Solicitudes, verificacion y descarga (doc §6.1): s:Header vacio y firma
//   enveloped dentro del nodo de peticion (des:solicitud o
//   des:peticionDescarga) con Reference URI="", transform enveloped-signature
//   (+ exc-c14n si la variante es exclusiva) y KeyInfo/X509Data con
//   X509IssuerSerial (issuer FormatoIssuer, serial decimal) y X509Certificate.
//   El digest se calcula sobre el NODO DE PETICION sin la firma (no sobre el
//   documento completo): punto de firma a confirmar en la corrida real.
// - Atributos vacios se omiten. Orden textual de atributos de solicitud segun
//   doc §5.6 (Complemento, EstadoComprobante, FechaInicial, FechaFinal,
//   RfcACuentaTerceros, RfcEmisor, [RfcReceptor], RfcSolicitante,
//   TipoComprobante, TipoSolicitud); C14N los reordena para el digest.

struct OpcionesFirma {
    // C14N usada para calcular digest y SignedInfo.
    VarianteC14n c14n = VarianteC14n::Inclusiva;
    // EXPERIMENTAL (solo diagnostico): C14N declarada en
    // CanonicalizationMethod/Transform; nullopt = `c14n` (camino por defecto,
    // conforme a XMLDSig). Si difiere de `c14n`, el sobre NO es conforme a
    // XMLDSig (un verificador que siga lo declarado no reproduce el digest ni
    // la firma); existe solo para comparar contra el comportamiento observado
    // de phpcfdi ("declara inclusiva, calcula equivalente a exclusiva"). La
    // evidencia debe marcar aparte cualquier corrida con esta opcion.
    std::optional<VarianteC14n> c14nDeclarada;
    FormatoIssuer formatoIssuer = FormatoIssuer::DocSat;
    // Solo Autentica (ignorado en las demas operaciones): agrega al s:Header,
    // despues de o:Security y SIN firmarlos, los headers WS-Addressing del
    // ejemplo de docs/web-service.md §4.2:
    //   <To s:mustUnderstand="1" xmlns="http://schemas.microsoft.com/ws/2005/05/addressing/none">endpoint</To>
    //   <Action s:mustUnderstand="1" xmlns="...addressing/none">SOAPAction</Action>
    // Desactivado por defecto: phpcfdi (referencia secundaria) no los envia y el
    // WSDL no los exige. La CLI lo expone como --ws-addressing por si el SAT
    // rechaza la autenticacion sin ellos.
    bool wsAddressing = false;

    VarianteC14n declarada() const { return c14nDeclarada.value_or(c14n); }
    // Variante por defecto del descriptor de la operacion.
    static OpcionesFirma porDefecto(Operacion operacion);
};

struct ContextoSobre {
    QDateTime ahoraUtc;                              // Timestamp/Created (se trunca a ms)
    std::chrono::seconds vigencia{300};              // Expires = Created + vigencia
    QString idToken;                                 // u:Id del BinarySecurityToken; vacio = uuid-<v4>-1
};

// Filtros de SolicitaDescargaEmitidos/Recibidos. Fechas: xs:dateTime sin zona
// ("yyyy-MM-ddTHH:mm:ss") tomando fecha y hora tal cual (sin conversion).
struct ParametrosSolicitud {
    QString rfcSolicitante;
    QDateTime fechaInicial;
    QDateTime fechaFinal;
    QString tipoSolicitud = QStringLiteral("CFDI");
    QString estadoComprobante = QStringLiteral("Vigente");
    QString tipoComprobante;     // opcional
    QString complemento;         // opcional
    QString rfcACuentaTerceros;  // opcional (fuera del MVP)
    QString rfcEmisor;           // emitidos: obligatorio (= solicitante); recibidos: contraparte opcional
    QString rfcReceptor;         // recibidos: obligatorio (= solicitante); emitidos: no aplica
    QStringList rfcReceptores;   // emitidos: contrapartes opcionales (des:RfcReceptores)
};

struct SobreSat {
    Operacion operacion = Operacion::Autentica;
    QByteArray xml;          // sobre completo UTF-8 (contiene certificado y firma: no registrar)
    QByteArray digestBase64; // DigestValue calculado (diagnostico)
};

struct ErrorSobre {
    enum class Tipo {
        Parametros, // validacion de entrada
        Xml,        // construccion o C14N
        Firma,      // certificado o RSA
    };
    Tipo tipo = Tipo::Parametros;
    QString diagnostico; // sin valores de entrada ni material
};

Resultado<SobreSat, ErrorSobre> construirAutentica(const MaterialFirma& material, const ContextoSobre& contexto,
                                                   const OpcionesFirma& opciones =
                                                       OpcionesFirma::porDefecto(Operacion::Autentica));

Resultado<SobreSat, ErrorSobre> construirSolicitudEmitidos(
    const ParametrosSolicitud& parametros, const MaterialFirma& material,
    const OpcionesFirma& opciones = OpcionesFirma::porDefecto(Operacion::SolicitaDescargaEmitidos));

Resultado<SobreSat, ErrorSobre> construirSolicitudRecibidos(
    const ParametrosSolicitud& parametros, const MaterialFirma& material,
    const OpcionesFirma& opciones = OpcionesFirma::porDefecto(Operacion::SolicitaDescargaRecibidos));

Resultado<SobreSat, ErrorSobre> construirVerificacion(
    const QString& idSolicitud, const QString& rfcSolicitante, const MaterialFirma& material,
    const OpcionesFirma& opciones = OpcionesFirma::porDefecto(Operacion::VerificaSolicitudDescarga));

Resultado<SobreSat, ErrorSobre> construirDescarga(
    const QString& idPaquete, const QString& rfcSolicitante, const MaterialFirma& material,
    const OpcionesFirma& opciones = OpcionesFirma::porDefecto(Operacion::Descargar));

// Formato de Timestamp: UTC "yyyy-MM-ddTHH:mm:ss.zzzZ".
QString textoTimestamp(const QDateTime& instante);

} // namespace satcfdi::sat
