#pragma once

#include "ports/LogSanitizer.h"

#include <QString>
#include <QStringList>
#include <QStringView>

namespace satcfdi {

// Implementacion de referencia de LogSanitizer (T003 "LogSanitizer") con
// QRegularExpression (Qt Core). Sin SQL ni E/S; reentrante.
//
// Reglas sobre texto libre (mensajeSat, mensaje, detalle):
// - Recorta la entrada a 1 MiB antes de aplicar expresiones.
// - Redacta bloques XML SignatureValue, ds:Signature, X509Certificate y
//   Paquete; bloques PEM; pares clave/valor sensibles (token, access token,
//   authorization, password, passwd, contrasena, contraseña, clave, pin,
//   secret, api key); cabeceras Authorization completas; base64 huerfano de
//   64+ caracteres que no sea hexadecimal puro.
// - Preserva RFC, UUID/IdSolicitud, IdPaquete, codigos SAT, fechas,
//   dedup_key v1 y SHA-256 hex.
// - Limites: mensaje/mensajeSat 500, detalle 8192 caracteres, sin partir
//   pares sustitutos UTF-16; agrega [TRUNCATED:<charsRecortados>].
// - Idempotente.
//
// Idempotencia: un valor que ya es exactamente un marcador del catalogo
// cerrado, sin nada pegado antes del delimitador, se conserva. Un parecido
// ("[REDACTED:x-secreto]") o un marcador con sufijo ("[REDACTED:token]S3cr3t")
// se redacta completo (fail-closed). Aplica a pares clave/valor, elementos XML
// sensibles, Authorization (valor hasta fin de linea) y Bearer.
//
// Limitaciones conocidas: un secreto codificado en hexadecimal puro no se
// redacta (para preservar hashes); un valor sin comillas despues de una clave
// sensible se redacta hasta el primer espacio o separador (, ; & < > [ ] { }
// ( ) comillas), salvo que vaya entre corchetes; Authorization se redacta
// hasta fin de linea. Una corrida de 64+ caracteres del alfabeto base64 que no
// sea hex (p. ej. una ruta larga sin espacios) tambien se redacta. Los limites
// se miden en unidades UTF-16 (QChar) sin partir pares sustitutos; el limite
// de entrada es 1 MiB = 1048576 unidades.
class RegexLogSanitizer final : public LogSanitizer {
public:
    static constexpr qsizetype kLimiteEntrada = 1024 * 1024;
    static constexpr qsizetype kLimiteMensaje = 500;
    static constexpr qsizetype kLimiteDetalle = 8192;

    // Texto saneado y reporte de lo aplicado (para pruebas y diagnostico).
    struct TextoSaneado {
        QString texto;
        QStringList marcadores;          // marcadores aplicados, en orden
        qsizetype caracteresRecortados = 0;
    };

    // Sanea un texto libre con el limite indicado. Precondicion: limite >= 32
    // (cabe el marcador [TRUNCATED:<n>] con n de hasta 7 digitos y algo de
    // texto); con limites menores la salida puede excederlo.
    static TextoSaneado sanearTexto(QStringView texto, qsizetype limite);

    LogEntradaSaneada sanitizar(const LogEntradaCruda& entrada) const override;
};

} // namespace satcfdi
