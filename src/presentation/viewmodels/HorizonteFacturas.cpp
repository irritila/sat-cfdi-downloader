#include "HorizonteFacturas.h"

namespace satcfdi::horizonte_facturas {

bool navegable(MesCalendario mes, MesCalendario enCurso)
{
    return mes <= enCurso && mes.anio() >= enCurso.anio() - kAniosAtras;
}

ErrorRango validarRango(MesCalendario desde, MesCalendario hasta, MesCalendario enCurso)
{
    if (desde > enCurso || hasta > enCurso) {
        return ErrorRango::Futuro;
    }
    if (!navegable(desde, enCurso) || !navegable(hasta, enCurso)) {
        return ErrorRango::FueraDelHorizonte;
    }
    if (desde > hasta) {
        return ErrorRango::Invertido;
    }
    return ErrorRango::Ninguno;
}

} // namespace satcfdi::horizonte_facturas
