#include "domain/cfdi/IndiceCfdi.h"

#include <QChar>
#include <QStringList>

namespace satcfdi {

QLatin1StringView claveEstadoIndice(EstadoIndicePaquete estado)
{
    switch (estado) {
    case EstadoIndicePaquete::Pendiente:
        return QLatin1StringView("Pendiente");
    case EstadoIndicePaquete::Indexado:
        return QLatin1StringView("Indexado");
    case EstadoIndicePaquete::OrigenNoDisponible:
        return QLatin1StringView("OrigenNoDisponible");
    case EstadoIndicePaquete::Ilegible:
        return QLatin1StringView("Ilegible");
    }
    return QLatin1StringView("Pendiente");
}

std::optional<EstadoIndicePaquete> estadoIndiceDesdeClave(QStringView clave)
{
    for (EstadoIndicePaquete e : {EstadoIndicePaquete::Pendiente, EstadoIndicePaquete::Indexado,
                                  EstadoIndicePaquete::OrigenNoDisponible, EstadoIndicePaquete::Ilegible}) {
        if (clave == claveEstadoIndice(e)) {
            return e;
        }
    }
    return std::nullopt;
}

QLatin1StringView claveEstadoParseo(EstadoParseo estado)
{
    switch (estado) {
    case EstadoParseo::Completo:
        return QLatin1StringView("Completo");
    case EstadoParseo::Parcial:
        return QLatin1StringView("Parcial");
    case EstadoParseo::VersionNoSoportada:
        return QLatin1StringView("VersionNoSoportada");
    case EstadoParseo::Ilegible:
        return QLatin1StringView("Ilegible");
    case EstadoParseo::NoEsCfdi:
        return QLatin1StringView("NoEsCfdi");
    }
    return QLatin1StringView("Ilegible");
}

std::optional<EstadoParseo> estadoParseoDesdeClave(QStringView clave)
{
    for (EstadoParseo e : {EstadoParseo::Completo, EstadoParseo::Parcial, EstadoParseo::VersionNoSoportada,
                           EstadoParseo::Ilegible, EstadoParseo::NoEsCfdi}) {
        if (clave == claveEstadoParseo(e)) {
            return e;
        }
    }
    return std::nullopt;
}

QString normalizarTextoBusqueda(QStringView texto)
{
    const QString descompuesto = texto.toString().normalized(QString::NormalizationForm_KD);
    QString resultado;
    resultado.reserve(descompuesto.size());
    bool espacioPendiente = false;
    for (const QChar c : descompuesto) {
        const QChar::Category categoria = c.category();
        if (categoria == QChar::Mark_NonSpacing || categoria == QChar::Mark_SpacingCombining
            || categoria == QChar::Mark_Enclosing) {
            continue;
        }
        if (c.isSpace()) {
            espacioPendiente = !resultado.isEmpty();
            continue;
        }
        if (espacioPendiente) {
            resultado.append(QLatin1Char(' '));
            espacioPendiente = false;
        }
        resultado.append(c);
    }
    return resultado.toCaseFolded();
}

QString textoBusquedaCfdi(const ResumenCfdi& resumen)
{
    QStringList partes;
    for (const QString* campo : {&resumen.emisor.rfc, &resumen.emisor.nombre, &resumen.receptor.rfc,
                                 &resumen.receptor.nombre, &resumen.uuid, &resumen.serie, &resumen.folio}) {
        const QString normalizado = normalizarTextoBusqueda(*campo);
        if (!normalizado.isEmpty()) {
            partes.append(normalizado);
        }
    }
    return partes.join(QLatin1Char(' '));
}

} // namespace satcfdi
