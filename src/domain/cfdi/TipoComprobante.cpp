#include "domain/cfdi/TipoComprobante.h"

namespace satcfdi {

TipoComprobante tipoComprobanteDesdeTexto(QStringView texto)
{
    if (texto == u"I") {
        return TipoComprobante::Ingreso;
    }
    if (texto == u"E") {
        return TipoComprobante::Egreso;
    }
    if (texto == u"T") {
        return TipoComprobante::Traslado;
    }
    if (texto == u"N") {
        return TipoComprobante::Nomina;
    }
    if (texto == u"P") {
        return TipoComprobante::Pago;
    }
    // CFDI 3.2.
    if (texto.compare(u"ingreso", Qt::CaseInsensitive) == 0) {
        return TipoComprobante::Ingreso;
    }
    if (texto.compare(u"egreso", Qt::CaseInsensitive) == 0) {
        return TipoComprobante::Egreso;
    }
    if (texto.compare(u"traslado", Qt::CaseInsensitive) == 0) {
        return TipoComprobante::Traslado;
    }
    return TipoComprobante::Desconocido;
}

QLatin1StringView codigoTipoComprobante(TipoComprobante tipo)
{
    switch (tipo) {
    case TipoComprobante::Ingreso:
        return QLatin1StringView("I");
    case TipoComprobante::Egreso:
        return QLatin1StringView("E");
    case TipoComprobante::Traslado:
        return QLatin1StringView("T");
    case TipoComprobante::Nomina:
        return QLatin1StringView("N");
    case TipoComprobante::Pago:
        return QLatin1StringView("P");
    case TipoComprobante::Desconocido:
        break;
    }
    return QLatin1StringView("");
}

} // namespace satcfdi
