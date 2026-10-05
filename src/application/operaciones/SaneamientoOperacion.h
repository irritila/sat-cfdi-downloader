#pragma once

#include "domain/operaciones/FallaOperacion.h"
#include "domain/operaciones/ResultadosOperacion.h"

#include <QString>
#include <QStringView>

#include <optional>

namespace satcfdi::saneamiento {

// Defensa en profundidad (T009) para lo que OperacionesSat devuelve y el
// ejecutor PERSISTE (ultimo_error, codigos, mensajes, logs). Independiente de
// la lista permitida del adaptador SAT: se aplica a cualquier puerto.

inline constexpr qsizetype kLimiteDetalle = 300;
inline const QString kCodigoNoSeguro = QStringLiteral("codigo_no_seguro");

// Codigo persistible: [A-Za-z0-9:._-]{1,40}, sin nada con forma de RFC y, si
// lleva ':', un solo prefijo de la lista {s, soap, a, wsse, wsu, credencial};
// si no, kCodigoNoSeguro. Recorta espacios.
QString codigoSeguro(QStringView codigo);
std::optional<QString> codigoSeguro(const std::optional<QString>& codigo);

// Texto libre persistible: RegexLogSanitizer (token, password, firma,
// certificado, PEM, Paquete, base64 largo), mas RFC -> "[RFC]" y corridas
// opacas de 40+ caracteres sin forma hexadecimal -> "[REDACTED:token]";
// limite `limite` (>= 32) con [TRUNCATED:<n>].
QString textoSeguro(QStringView texto, qsizetype limite = kLimiteDetalle);

// Aplican lo anterior a codigo/codEstatus, diagnostico y mensajes.
FallaOperacion fallaSegura(FallaOperacion falla);
ResultadoEnvio envioSeguro(ResultadoEnvio r);
ResultadoVerificacion verificacionSegura(ResultadoVerificacion r);
ResultadoDescarga descargaSegura(ResultadoDescarga r);

} // namespace satcfdi::saneamiento
