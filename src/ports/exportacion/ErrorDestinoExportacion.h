#pragma once

// T026.1 (D12, D20): errores del destino de una exportacion de facturas.
// Tipados por codigo; nunca llevan rutas, nombres de archivo ni texto del
// sistema. `errnoSistema` conserva el errno POSIX de la falla (0 si no hubo),
// que es lo unico que puede ir a un log junto con el codigo.

#include <QString>

namespace satcfdi::exportacion {

struct ErrorDestinoExportacion {
    enum class Codigo {
        CarpetaNoDisponible, // la carpeta destino no existe o su volumen no esta montado
        NoEsCarpeta,         // la ruta destino existe pero no es un directorio
        CarpetaNoPermitida,  // la carpeta destino es el directorio de datos de la app o esta dentro de el
        NombreInvalido,      // nombre final vacio, con '/', oculto, de control o > 255 bytes
        DestinoExistente,    // ya hay una entrada con el nombre final (al reservar o al publicar)
        SinPermiso,          // EACCES / EPERM / EROFS
        EspacioInsuficiente, // ENOSPC / EDQUOT
        Creacion,            // otra falla al crear la carpeta privada o el .partial
        Registro,            // no se pudo registrar el .partial en otro volumen (antes de crearlo)
        Escritura,           // otra falla de E/S al escribir o truncar
        Cierre,              // close() del .partial fallo
        Sincronizacion,      // F_FULLFSYNC / fsync del .partial fallo
        ExclusivoNoSoportado, // el sistema de archivos no admite RENAME_EXCL
        Publicacion,         // el rename fallo por otra causa
        Estado,              // uso fuera de orden (publicar tras descartar, etc.)
        Interno,
    };

    Codigo codigo = Codigo::Interno;
    int errnoSistema = 0;

    static ErrorDestinoExportacion de(Codigo c, int errnoSistema = 0)
    {
        ErrorDestinoExportacion e;
        e.codigo = c;
        e.errnoSistema = errnoSistema;
        return e;
    }

    bool operator==(const ErrorDestinoExportacion&) const = default;
};

inline QString claveEstable(ErrorDestinoExportacion::Codigo c)
{
    using C = ErrorDestinoExportacion::Codigo;
    switch (c) {
    case C::CarpetaNoDisponible: return QStringLiteral("carpeta_no_disponible");
    case C::NoEsCarpeta: return QStringLiteral("no_es_carpeta");
    case C::CarpetaNoPermitida: return QStringLiteral("carpeta_no_permitida");
    case C::NombreInvalido: return QStringLiteral("nombre_invalido");
    case C::DestinoExistente: return QStringLiteral("destino_existente");
    case C::SinPermiso: return QStringLiteral("sin_permiso");
    case C::EspacioInsuficiente: return QStringLiteral("espacio_insuficiente");
    case C::Creacion: return QStringLiteral("creacion");
    case C::Registro: return QStringLiteral("registro");
    case C::Escritura: return QStringLiteral("escritura");
    case C::Cierre: return QStringLiteral("cierre");
    case C::Sincronizacion: return QStringLiteral("sincronizacion");
    case C::ExclusivoNoSoportado: return QStringLiteral("exclusivo_no_soportado");
    case C::Publicacion: return QStringLiteral("publicacion");
    case C::Estado: return QStringLiteral("estado");
    case C::Interno: return QStringLiteral("interno");
    }
    return QStringLiteral("interno");
}

} // namespace satcfdi::exportacion
