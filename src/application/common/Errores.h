#pragma once

#include <QList>
#include <QString>

#include <utility>

namespace satcfdi {

// Errores minimos de aplicacion para T002. `mensaje` es texto descriptivo sin
// secretos; la presentacion decide el texto visible a partir de los codigos.

// Fallo de almacenamiento (en T002 el demo nunca lo produce; T003 si).
struct ErrorPersistencia {
    QString mensaje;
};

struct ErrorObtener {
    enum class Tipo {
        NoEncontrada,
        Persistencia,
    };

    Tipo tipo = Tipo::NoEncontrada;
    QString mensaje;
};

// Validacion superficial de NuevaSolicitudRequest.
struct ErrorValidacion {
    enum class Codigo {
        PerfilRequerido,      // perfilId nulo
        PerfilInexistente,    // perfil desconocido o inactivo
        FechaInicialRequerida,
        FechaFinalRequerida,
        RangoFechasInvalido,  // fechaFinal < fechaInicial
    };

    Codigo codigo = Codigo::PerfilRequerido;
    QString mensaje;
};

struct ErrorCrear {
    enum class Tipo {
        Validacion,   // `validaciones` contiene al menos un elemento
        Persistencia, // `mensaje` describe el fallo
    };

    Tipo tipo = Tipo::Validacion;
    QList<ErrorValidacion> validaciones;
    QString mensaje;

    static ErrorCrear validacion(QList<ErrorValidacion> errores)
    {
        ErrorCrear e;
        e.tipo = Tipo::Validacion;
        e.mensaje = errores.isEmpty() ? QString() : errores.constFirst().mensaje;
        e.validaciones = std::move(errores);
        return e;
    }

    static ErrorCrear persistencia(QString mensaje)
    {
        ErrorCrear e;
        e.tipo = Tipo::Persistencia;
        e.mensaje = std::move(mensaje);
        return e;
    }
};

} // namespace satcfdi
