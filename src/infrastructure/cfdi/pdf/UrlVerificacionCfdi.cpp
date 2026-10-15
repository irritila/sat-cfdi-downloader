#include "infrastructure/cfdi/pdf/UrlVerificacionCfdi.h"

#include "domain/cfdi/ImporteCfdi.h"

namespace satcfdi {

namespace {

constexpr qsizetype kDigitosEnteros = 10;
constexpr qsizetype kDigitosDecimales = 6;
constexpr qsizetype kCaracteresSello = 8;

QString rfcParaUrl(const QString& rfc)
{
    QString r = rfc.trimmed();
    r.replace(QLatin1Char('&'), QStringLiteral("%26"));
    return r;
}

} // namespace

std::optional<QString> totalParaQr(QStringView totalTexto)
{
    const auto importe = ImporteCfdi::desdeTexto(totalTexto);
    if (!importe) {
        return std::nullopt;
    }
    // Forma canonica exacta: sin ceros superfluos ("0014300.500" -> "14300.5").
    const QString canonico = importe->normalizado();
    const qsizetype punto = canonico.indexOf(QLatin1Char('.'));
    const QString entero = punto < 0 ? canonico : canonico.left(punto);
    const QString fraccion = punto < 0 ? QString() : canonico.mid(punto + 1);
    if (entero.size() > kDigitosEnteros || fraccion.size() > kDigitosDecimales) {
        return std::nullopt;
    }
    return entero.rightJustified(kDigitosEnteros, QLatin1Char('0')) + QLatin1Char('.')
           + fraccion.leftJustified(kDigitosDecimales, QLatin1Char('0'));
}

std::optional<QString> urlVerificacionCfdi(const DatosQrCfdi& datos)
{
    const QString uuid = datos.uuid.trimmed();
    const QString re = rfcParaUrl(datos.rfcEmisor);
    const QString rr = rfcParaUrl(datos.rfcReceptor);
    if (uuid.isEmpty() || re.isEmpty() || rr.isEmpty() || datos.sello.isEmpty()) {
        return std::nullopt;
    }
    const auto tt = totalParaQr(datos.totalTexto);
    if (!tt) {
        return std::nullopt;
    }
    const QString fe = datos.sello.size() <= kCaracteresSello ? datos.sello : datos.sello.right(kCaracteresSello);
    return QString(kUrlBaseVerificacionCfdi) + QStringLiteral("?id=") + uuid + QStringLiteral("&re=") + re
           + QStringLiteral("&rr=") + rr + QStringLiteral("&tt=") + *tt + QStringLiteral("&fe=") + fe;
}

} // namespace satcfdi
