#pragma once

// T024.2: valores de "resultado" de la barrera esperarExportacion. Solo datos
// (sin presentacion), para que el guion los valide sin enlazar view models.
// Son las variantes de ExportacionFacturasViewModel::varianteResultado y
// "esperandoNombre" (el ZIP ya existe y se pide otro nombre).

#include <QString>
#include <QStringList>

namespace video {

inline QStringList resultadosExportacion()
{
    return {QStringLiteral("exito"), QStringLiteral("advertencia"), QStringLiteral("neutro"),
            QStringLiteral("error"), QStringLiteral("esperandoNombre")};
}

inline QString resultadoExportacionPorOmision() { return QStringLiteral("exito"); }

} // namespace video
