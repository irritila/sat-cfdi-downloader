#pragma once

#include <QString>
#include <QStringView>

namespace satcfdi::uuid {

// Forma canonica de los identificadores locales: 36 caracteres, hexadecimal en
// minusculas, guiones en las posiciones 8, 13, 18 y 23, sin llaves. Coincide con
// el CHECK de los campos `id` del modelo fisico (T001).
bool esCanonico(QStringView texto);

// Genera un UUID aleatorio (v4) en forma canonica.
QString generarCanonico();

} // namespace satcfdi::uuid
