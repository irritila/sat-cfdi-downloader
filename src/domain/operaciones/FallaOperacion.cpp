#include "domain/operaciones/FallaOperacion.h"

namespace satcfdi {

QString claveEstable(FaseOperacion fase)
{
    switch (fase) {
    case FaseOperacion::Preparacion: return QStringLiteral("Preparacion");
    case FaseOperacion::Autenticacion: return QStringLiteral("Autenticacion");
    case FaseOperacion::AntesDeEnvio: return QStringLiteral("AntesDeEnvio");
    case FaseOperacion::DespuesDeEnvio: return QStringLiteral("DespuesDeEnvio");
    case FaseOperacion::RespuestaExplicita: return QStringLiteral("RespuestaExplicita");
    case FaseOperacion::Almacenamiento: return QStringLiteral("Almacenamiento");
    }
    return {};
}

QString claveEstable(CausaAlmacenamiento causa)
{
    switch (causa) {
    case CausaAlmacenamiento::ColisionDestino: return QStringLiteral("ColisionDestino");
    case CausaAlmacenamiento::EscrituraFallida: return QStringLiteral("EscrituraFallida");
    case CausaAlmacenamiento::EspacioInsuficiente: return QStringLiteral("EspacioInsuficiente");
    }
    return {};
}

std::optional<FaseOperacion> faseOperacionDesdeClave(QStringView clave)
{
    for (FaseOperacion f : kFasesOperacion) {
        if (claveEstable(f) == clave) {
            return f;
        }
    }
    return std::nullopt;
}

QString claveFalla(const FallaOperacion& falla)
{
    QString clave = claveEstable(falla.fase);
    if (falla.codigo && !falla.codigo->trimmed().isEmpty()) {
        clave += QLatin1Char(':') + falla.codigo->trimmed();
    }
    return clave;
}

QString textoUltimoError(const FallaOperacion& f)
{
    QString d = claveFalla(f);
    if (f.causaAlmacenamiento) {
        d += QLatin1Char('/') + claveEstable(*f.causaAlmacenamiento);
    }
    if (f.cancelada) {
        d += QStringLiteral(" (cancelada)");
    }
    if (!f.diagnosticoSanitizado.isEmpty()) {
        d += QStringLiteral(": ") + f.diagnosticoSanitizado;
    }
    return d;
}

std::optional<UltimoErrorDesglosado> desglosarUltimoError(QStringView texto)
{
    const qsizetype separador = texto.indexOf(u": ");
    QStringView cabeza = separador < 0 ? texto : texto.left(separador);
    QStringView cuerpo = separador < 0 ? QStringView() : texto.mid(separador + 2);

    UltimoErrorDesglosado r;
    const QStringView marcaCancelada = u" (cancelada)";
    if (cabeza.endsWith(marcaCancelada)) {
        r.cancelada = true;
        cabeza.chop(marcaCancelada.size());
    }
    const qsizetype dosPuntos = cabeza.indexOf(u':');
    const QStringView claveFase = dosPuntos < 0 ? cabeza : cabeza.left(dosPuntos);
    QStringView resto = dosPuntos < 0 ? QStringView() : cabeza.mid(dosPuntos + 1);
    // La causa solo existe en Almacenamiento: "Almacenamiento[:codigo]/<causa>".
    QStringView fase = claveFase;
    if (dosPuntos < 0) {
        const qsizetype barra = cabeza.indexOf(u'/');
        if (barra >= 0) {
            fase = cabeza.left(barra);
            resto = cabeza.mid(barra); // "/<causa>"
        }
    }
    const std::optional<FaseOperacion> f = faseOperacionDesdeClave(fase);
    if (!f) {
        return std::nullopt;
    }
    r.fase = *f;
    if (r.fase == FaseOperacion::Almacenamiento) {
        const qsizetype barra = resto.lastIndexOf(u'/');
        if (barra >= 0) {
            const QStringView clave = resto.mid(barra + 1);
            for (CausaAlmacenamiento c : {CausaAlmacenamiento::ColisionDestino, CausaAlmacenamiento::EscrituraFallida,
                                          CausaAlmacenamiento::EspacioInsuficiente}) {
                if (claveEstable(c) == clave) {
                    r.causaAlmacenamiento = c;
                }
            }
            resto = resto.left(barra);
        }
    }
    if (!resto.isEmpty()) {
        r.codigo = resto.toString();
    }
    // El primer " [" separa el mensaje D10 (sin corchetes) del detalle, que
    // puede llevar marcadores del saneador ("[RFC]", "[REDACTED:...]").
    const qsizetype corchete = cuerpo.indexOf(u" [");
    if (cuerpo.endsWith(u']') && corchete >= 0) {
        r.detalleTecnico = cuerpo.mid(corchete + 2, cuerpo.size() - corchete - 3).toString();
        cuerpo = cuerpo.left(corchete);
    } else if (cuerpo.startsWith(u'[') && cuerpo.endsWith(u']')) {
        r.detalleTecnico = cuerpo.mid(1, cuerpo.size() - 2).toString();
        cuerpo = QStringView();
    }
    r.mensaje = cuerpo.toString();
    return r;
}

} // namespace satcfdi
