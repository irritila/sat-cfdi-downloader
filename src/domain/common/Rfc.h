#pragma once

#include <QString>
#include <QStringView>

#include <optional>

namespace satcfdi::rfc {

// Normalizacion de RFC (operational-rules, dedup_key v1): trim, mayusculas y
// sin espacios internos. No valida.
QString normalizar(QStringView texto);

// Valida un RFC YA normalizado: 3-4 letras (A-Z, Ñ, &), 6 digitos de fecha y
// 3 caracteres alfanumericos de homoclave; 12 o 13 caracteres. Es mas estricto
// que el CHECK de SQLite (`[A-Z0-9&Ñ]{12,13}`), por lo que todo RFC valido aqui
// cumple el esquema.
bool esValido(QStringView rfcNormalizado);

// normalizar + esValido. nullopt si el resultado no es valido.
std::optional<QString> normalizarYValidar(QStringView texto);

} // namespace satcfdi::rfc
