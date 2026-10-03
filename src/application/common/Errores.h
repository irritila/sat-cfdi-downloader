#pragma once

#include "domain/solicitudes/Duplicados.h"
#include "domain/solicitudes/SolicitudCanonica.h"
#include "ports/persistence/ErrorPersistencia.h"

#include <QList>
#include <QString>

#include <optional>
#include <utility>

namespace satcfdi {

// Errores de los servicios de aplicacion consumidos por presentacion.
// `mensaje` es texto descriptivo sin secretos; la presentacion decide el texto
// visible a partir de tipos y codigos.
//
// ErrorPersistencia (ports/persistence/ErrorPersistencia.h) se reexpone aqui
// porque listar/eliminar/listarNoEliminados lo devuelven directamente. T003 lo movio
// desde este archivo a ports; conserva el campo `mensaje`.

struct ErrorObtener {
    enum class Tipo {
        NoEncontrada, // id nulo, inexistente o eliminado logicamente
        Persistencia, // `causa` describe el fallo
    };

    Tipo tipo = Tipo::NoEncontrada;
    QString mensaje;
    std::optional<ErrorPersistencia> causa;
};

// Validacion de formulario de NuevaSolicitudRequest. Catalogo CONGELADO en
// T003 corte 1 (presentacion hace switch exhaustivo); los errores de filtros
// SAT van en ErrorCrear::filtros.
struct ErrorValidacion {
    enum class Codigo {
        PerfilRequerido,      // perfilId nulo
        PerfilInexistente,    // perfil desconocido, eliminado o inactivo (ErrorPerfilInvalido)
        FechaInicialRequerida,
        FechaFinalRequerida,
        RangoFechasInvalido,  // fechaFinal < fechaInicial
    };

    Codigo codigo = Codigo::PerfilRequerido;
    QString mensaje;
};

// Error de evaluarDuplicado()/crear()/crearLocal().
struct ErrorCrear {
    enum class Tipo {
        Validacion,           // `validaciones` no vacia (perfil, fechas)
        Persistencia,         // `causa` describe el fallo de almacenamiento
        FiltroInvalido,       // `filtros` no vacia (RFC contraparte, tipo de
                              // comprobante, complemento, caracteres reservados)
        DedupBloqueado,       // `duplicado` con clasificacion Bloqueado
        RequiereConfirmacion, // `duplicado` con clasificacion RequiereConfirmacion;
                              // reintentar con ConfirmacionDuplicado::Confirmada
        Integridad,           // restriccion de esquema inesperada; `causa`
    };

    Tipo tipo = Tipo::Validacion;
    QList<ErrorValidacion> validaciones;
    QList<ErrorSolicitudCanonica> filtros;
    std::optional<EvaluacionDuplicado> duplicado;
    std::optional<ErrorPersistencia> causa;
    QString mensaje;

    static ErrorCrear validacion(QList<ErrorValidacion> errores)
    {
        ErrorCrear e;
        e.tipo = Tipo::Validacion;
        e.mensaje = errores.isEmpty() ? QString() : errores.constFirst().mensaje;
        e.validaciones = std::move(errores);
        return e;
    }

    static ErrorCrear filtroInvalido(QList<ErrorSolicitudCanonica> errores)
    {
        ErrorCrear e;
        e.tipo = Tipo::FiltroInvalido;
        e.mensaje = errores.isEmpty() ? QString() : errores.constFirst().mensaje;
        e.filtros = std::move(errores);
        return e;
    }

    static ErrorCrear persistencia(QString mensaje)
    {
        ErrorCrear e;
        e.tipo = Tipo::Persistencia;
        e.causa = ErrorPersistencia::de(ErrorPersistencia::Tipo::Interno, mensaje);
        e.mensaje = std::move(mensaje);
        return e;
    }

    static ErrorCrear persistencia(ErrorPersistencia causa)
    {
        ErrorCrear e;
        e.tipo = Tipo::Persistencia;
        e.mensaje = causa.mensaje;
        e.causa = std::move(causa);
        return e;
    }

    static ErrorCrear integridad(ErrorPersistencia causa)
    {
        ErrorCrear e;
        e.tipo = Tipo::Integridad;
        e.mensaje = causa.mensaje;
        e.causa = std::move(causa);
        return e;
    }

    static ErrorCrear dedupBloqueado(EvaluacionDuplicado evaluacion)
    {
        ErrorCrear e;
        e.tipo = Tipo::DedupBloqueado;
        e.mensaje = QStringLiteral("Ya existe una solicitud equivalente.");
        e.duplicado = std::move(evaluacion);
        return e;
    }

    static ErrorCrear requiereConfirmacion(EvaluacionDuplicado evaluacion)
    {
        ErrorCrear e;
        e.tipo = Tipo::RequiereConfirmacion;
        e.mensaje = QStringLiteral("Existe una solicitud equivalente; confirma para crear otra.");
        e.duplicado = std::move(evaluacion);
        return e;
    }
};

// Validacion por campo de perfiles (T005.1, DA1).
enum class CodigoValidacionPerfil {
    RfcInvalido,     // campo rfc
    NombreRequerido, // campo nombre
};

enum class CampoPerfil {
    Rfc,
    Nombre,
};

inline CampoPerfil campoDe(CodigoValidacionPerfil codigo)
{
    return codigo == CodigoValidacionPerfil::RfcInvalido ? CampoPerfil::Rfc : CampoPerfil::Nombre;
}

inline bool tieneErrorEn(const QList<CodigoValidacionPerfil>& validaciones, CampoPerfil campo)
{
    for (CodigoValidacionPerfil c : validaciones) {
        if (campoDe(c) == campo) {
            return true;
        }
    }
    return false;
}

// Error de PerfilesSatService::crear() (T005.1, DA1): Validacion (RfcInvalido
// -> rfc, NombreRequerido -> nombre), RfcDuplicado (SOLO Unicidad de
// ux_perfil_sat_rfc_vigente; el RFC queda reservado aunque el perfil este
// inactivo) o Persistencia (resto, incluidas otras restricciones).
// `mensaje` es texto fijo sin el RFC capturado; la UI traduce por tipo/campo.
struct ErrorCrearPerfil {
    enum class Tipo {
        Validacion,   // `validaciones` no vacia
        RfcDuplicado, // `causa` = Unicidad ux_perfil_sat_rfc_vigente
        Persistencia, // `causa`
    };
    using CodigoValidacion = CodigoValidacionPerfil;

    Tipo tipo = Tipo::Validacion;
    QList<CodigoValidacionPerfil> validaciones;
    std::optional<ErrorPersistencia> causa;
    QString mensaje;

    bool tieneErrorEn(CampoPerfil campo) const { return satcfdi::tieneErrorEn(validaciones, campo); }

    static ErrorCrearPerfil validacion(QList<CodigoValidacionPerfil> codigos)
    {
        ErrorCrearPerfil e;
        e.tipo = Tipo::Validacion;
        e.validaciones = std::move(codigos);
        e.mensaje = QStringLiteral("Revisa los datos del perfil.");
        return e;
    }

    // Traduccion de un error de escritura de perfil_sat.
    static ErrorCrearPerfil desdePersistencia(ErrorPersistencia causa)
    {
        ErrorCrearPerfil e;
        if (causa.tipo == ErrorPersistencia::Tipo::Unicidad
            && causa.restriccion == QStringLiteral("ux_perfil_sat_rfc_vigente")) {
            e.tipo = Tipo::RfcDuplicado;
            e.mensaje = QStringLiteral("Ya existe un perfil con ese RFC.");
        } else {
            e.tipo = Tipo::Persistencia;
            e.mensaje = QStringLiteral("No se pudo guardar el perfil.");
        }
        e.causa = std::move(causa);
        return e;
    }
};

// Error de PerfilesSatService::actualizarNombre() (T005.1, DA1): Validacion
// (solo NombreRequerido), PerfilInexistente o Persistencia. Nunca RFC.
struct ErrorActualizarPerfil {
    enum class Tipo {
        Validacion,        // `validaciones` = {NombreRequerido}
        PerfilInexistente, // id nulo, desconocido o eliminado
        Persistencia,      // `causa`
    };

    Tipo tipo = Tipo::Validacion;
    QList<CodigoValidacionPerfil> validaciones;
    std::optional<ErrorPersistencia> causa;
    QString mensaje;

    bool tieneErrorEn(CampoPerfil campo) const { return satcfdi::tieneErrorEn(validaciones, campo); }

    static ErrorActualizarPerfil nombreRequerido()
    {
        ErrorActualizarPerfil e;
        e.tipo = Tipo::Validacion;
        e.validaciones = {CodigoValidacionPerfil::NombreRequerido};
        e.mensaje = QStringLiteral("Indica el nombre del perfil.");
        return e;
    }

    static ErrorActualizarPerfil inexistente()
    {
        ErrorActualizarPerfil e;
        e.tipo = Tipo::PerfilInexistente;
        e.mensaje = QStringLiteral("El perfil no existe.");
        return e;
    }

    static ErrorActualizarPerfil persistencia(ErrorPersistencia causa)
    {
        ErrorActualizarPerfil e;
        e.tipo = Tipo::Persistencia;
        e.mensaje = QStringLiteral("No se pudo guardar el perfil.");
        e.causa = std::move(causa);
        return e;
    }
};

} // namespace satcfdi
