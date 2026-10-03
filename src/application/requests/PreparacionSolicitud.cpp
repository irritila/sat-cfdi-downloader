#include "application/requests/PreparacionSolicitud.h"

#include <utility>

namespace satcfdi {

Resultado<SolicitudCanonica, ErrorCrear>
prepararSolicitud(const NuevaSolicitudRequest& request,
                  const std::optional<QString>& rfcPerfilActivo)
{
    using R = Resultado<SolicitudCanonica, ErrorCrear>;
    using C = ErrorValidacion::Codigo;
    using D = ErrorSolicitudCanonica::Codigo;

    QList<ErrorValidacion> validaciones;
    if (request.perfilId.esNulo()) {
        validaciones.append({C::PerfilRequerido, QStringLiteral("Selecciona un perfil SAT.")});
    } else if (!rfcPerfilActivo) {
        validaciones.append({C::PerfilInexistente,
                             QStringLiteral("El perfil SAT seleccionado no existe o no esta activo.")});
    }

    EntradaSolicitudCanonica entrada;
    entrada.tipoDescarga = request.tipoDescarga;
    entrada.rfcPerfil = rfcPerfilActivo.value_or(QString());
    entrada.fechaInicial = request.fechaInicial;
    entrada.fechaFinal = request.fechaFinal;
    if (request.rfcContraparte) {
        entrada.rfcContrapartes.append(*request.rfcContraparte);
    }
    entrada.tipoComprobante = request.tipoComprobante;
    entrada.complemento = request.complemento;

    auto canonica = SolicitudCanonica::normalizar(entrada);
    if (canonica.esExito() && validaciones.isEmpty()) {
        return R::exito(std::move(canonica).valor());
    }

    QList<ErrorSolicitudCanonica> filtros;
    if (!canonica.esExito()) {
        for (const ErrorSolicitudCanonica& e : canonica.error()) {
            switch (e.codigo) {
            case D::RfcSolicitanteInvalido:
                // Sin perfil valido ya se reporto arriba; con perfil valido es
                // un perfil persistido incoherente.
                if (rfcPerfilActivo) {
                    validaciones.append({C::PerfilInexistente, e.mensaje});
                }
                break;
            case D::FechaInicialRequerida:
                validaciones.append({C::FechaInicialRequerida, e.mensaje});
                break;
            case D::FechaFinalRequerida:
                validaciones.append({C::FechaFinalRequerida, e.mensaje});
                break;
            case D::RangoFechasInvalido:
                validaciones.append({C::RangoFechasInvalido, e.mensaje});
                break;
            case D::RfcContraparteInvalido:
            case D::ContraparteMultipleNoPermitida:
            case D::TipoComprobanteInvalido:
            case D::CaracterReservado:
                filtros.append(e);
                break;
            }
        }
    }

    if (!validaciones.isEmpty()) {
        ErrorCrear error = ErrorCrear::validacion(std::move(validaciones));
        error.filtros = std::move(filtros);
        return R::fallo(std::move(error));
    }
    return R::fallo(ErrorCrear::filtroInvalido(std::move(filtros)));
}

} // namespace satcfdi
