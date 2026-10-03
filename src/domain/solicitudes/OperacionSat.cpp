#include "domain/solicitudes/OperacionSat.h"

namespace satcfdi {

QString claveEstable(OperacionSat operacion)
{
    switch (operacion) {
    case OperacionSat::SolicitaDescargaEmitidos:
        return QStringLiteral("SolicitaDescargaEmitidos");
    case OperacionSat::SolicitaDescargaRecibidos:
        return QStringLiteral("SolicitaDescargaRecibidos");
    }
    Q_UNREACHABLE_RETURN(QString());
}

std::optional<OperacionSat> operacionSatDesdeClave(QStringView clave)
{
    for (OperacionSat o : {OperacionSat::SolicitaDescargaEmitidos,
                           OperacionSat::SolicitaDescargaRecibidos}) {
        if (clave == claveEstable(o)) {
            return o;
        }
    }
    return std::nullopt;
}

OperacionSat operacionPara(TipoDescarga tipo)
{
    switch (tipo) {
    case TipoDescarga::Emitidos: return OperacionSat::SolicitaDescargaEmitidos;
    case TipoDescarga::Recibidos: return OperacionSat::SolicitaDescargaRecibidos;
    }
    Q_UNREACHABLE_RETURN(OperacionSat::SolicitaDescargaEmitidos);
}

TipoDescarga tipoDescargaDe(OperacionSat operacion)
{
    switch (operacion) {
    case OperacionSat::SolicitaDescargaEmitidos: return TipoDescarga::Emitidos;
    case OperacionSat::SolicitaDescargaRecibidos: return TipoDescarga::Recibidos;
    }
    Q_UNREACHABLE_RETURN(TipoDescarga::Emitidos);
}

} // namespace satcfdi
