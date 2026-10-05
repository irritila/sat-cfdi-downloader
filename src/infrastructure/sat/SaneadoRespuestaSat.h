#pragma once

#include <QString>
#include <QStringView>
#include <QUrl>

#include <optional>

// Saneado de lo que una respuesta SAT puede llevar hacia el puerto
// SatGateway (T009, revision de seguridad). El parser de T006 devuelve texto
// crudo (la CLI lo enmascara para evidencia); el gateway NUNCA lo expone sin
// pasar por estas funciones. Puras y deterministas.
namespace satcfdi::sat::saneado {

inline const QString kFaultNoReconocido = QStringLiteral("fault_no_reconocido");
inline const QString kCodigoNoReconocido = QStringLiteral("codigo_no_reconocido");

// CodEstatus / CodigoEstadoSolicitud documentados (docs/web-service.md):
// 300-305, 404 y 5000-5012. Otro valor -> kCodigoNoReconocido (OperacionesSat
// lo trata como codigo no documentado).
QString codigoSatPermitido(QStringView codigo);
bool esCodigoSatDocumentado(QStringView codigo);

// faultcode: QName con nombre local de la lista permitida de SOAP 1.1/1.2 y
// WS-Security (Client, Server, VersionMismatch, MustUnderstand, Sender,
// Receiver, InvalidSecurity, InvalidSecurityToken, FailedAuthentication,
// FailedCheck, SecurityTokenUnavailable, UnsupportedSecurityToken,
// UnsupportedAlgorithm, MessageExpired, ActionNotSupported,
// InternalServiceFault, DestinationUnreachable): devuelve SOLO ese nombre
// local, sin prefijo (p. ej. "a:InvalidSecurity" -> "InvalidSecurity"); o un
// codigo SAT documentado. Otro valor -> kFaultNoReconocido.
QString faultcodePermitido(QStringView faultcode);

// Mensaje SAT para el puerto: sin controles, enmascarado (RFC, UUID, Ids,
// WRAP access_token, Base64 >= 200), secuencias opacas de 32+ caracteres ->
// "[oculto]", y truncado a 200 caracteres.
QString mensajePermitido(QStringView mensaje);

// Formatos de identificadores devueltos por el SAT (T006).
bool esIdSolicitud(QStringView id);
bool esIdPaquete(QStringView id);

// Endpoints productivos validos: https, puerto implicito o 443, sin
// credenciales y host *.clouda.sat.gob.mx.
bool esEndpointOficial(const QUrl& url);

} // namespace satcfdi::sat::saneado
