#pragma once

#include "domain/cfdi/MesCalendario.h"

namespace satcfdi::horizonte_facturas {

// T026.3 (C3): horizonte navegable de la pantalla Facturas, compartido por
// FacturasViewModel (navegacion de meses) y ExportacionFacturasViewModel
// (rango de meses a exportar). Reglas puras: el mes en curso (hora Centro) lo
// captura el llamador con su reloj.

// Primer anio navegable: anio en curso - 6.
inline constexpr int kAniosAtras = 6;

// Mes no posterior al mes en curso y no anterior a enero de (anio en curso - 6).
bool navegable(MesCalendario mes, MesCalendario enCurso);

enum class ErrorRango {
    Ninguno,
    Futuro,            // algun extremo es posterior al mes en curso
    FueraDelHorizonte, // algun extremo es anterior al primer mes navegable
    Invertido,         // `desde` es posterior a `hasta`
};

// Rango inclusivo [desde, hasta]: ambos extremos navegables y en orden. El
// mismo mes en ambos extremos es valido. Si hay varias faltas se informa la
// primera en el orden de ErrorRango.
ErrorRango validarRango(MesCalendario desde, MesCalendario hasta, MesCalendario enCurso);

} // namespace satcfdi::horizonte_facturas
