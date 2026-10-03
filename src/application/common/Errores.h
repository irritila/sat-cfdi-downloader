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
// porque listar/eliminar/listarActivos lo devuelven directamente. T003 lo movio
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

// Error de PerfilesSatService::crearPerfilSimulado().
struct ErrorCrearPerfil {
    enum class Tipo {
        Validacion,   // `validaciones` no vacia
        Integridad,   // RFC vigente duplicado (ux_perfil_sat_rfc_vigente) u
                      // otra restriccion; `causa`
        Persistencia, // `causa`
    };

    enum class CodigoValidacion {
        RfcInvalido,
        RazonSocialRequerida,
    };

    Tipo tipo = Tipo::Validacion;
    QList<CodigoValidacion> validaciones;
    std::optional<ErrorPersistencia> causa;
    QString mensaje;
};

} // namespace satcfdi
