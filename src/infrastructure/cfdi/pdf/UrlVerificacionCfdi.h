#pragma once

// URL del codigo QR de verificacion del SAT (T015.4, T015 D8; Anexo 20, DOF
// 2022, seccion D). Funciones puras: sin E/S, sin logs y seguras desde
// cualquier hilo.
//
// Regla vigente (a validar escaneando el QR de un CFDI real del usuario; si
// el portal no la acepta se ajusta aqui y se registra la regla validada):
//
//   https://verificacfdi.facturaelectronica.sat.gob.mx/default.aspx
//     ?id=<UUID>&re=<RFC emisor>&rr=<RFC receptor>&tt=<total>&fe=<sello>
//
// - id: UUID del TimbreFiscalDigital tal como lo normaliza el parser.
// - tt: desde el TEXTO exacto del atributo Total (nunca double): 10 digitos
//   enteros rellenados con ceros a la izquierda, punto y 6 decimales
//   rellenados con ceros a la derecha ("14300" -> "0000014300.000000").
// - fe: ultimos 8 caracteres del atributo Sello del comprobante, SIN
//   codificar ("rH8/bw==" queda igual), como el ejemplo del Anexo 20.
// - re/rr: el RFC tal cual; solo '&' (valido en RFC de personas morales)
//   se escribe como "%26" porque si no cortaria el parametro de la URL.
//   Supuesto pendiente de validar con el portal.

#include <QString>
#include <QStringView>

#include <optional>

namespace satcfdi {

inline constexpr QLatin1StringView kUrlBaseVerificacionCfdi{
    "https://verificacfdi.facturaelectronica.sat.gob.mx/default.aspx"};

struct DatosQrCfdi {
    QString uuid;
    QString rfcEmisor;
    QString rfcReceptor;
    QString totalTexto; // texto exacto del atributo Total
    QString sello;      // atributo Sello completo (se usan sus ultimos 8)
};

// `tt` a partir del texto del total. nullopt si el texto no es un importe
// valido (gramatica de ImporteCfdi) o no cabe en 10 enteros y 6 decimales
// (los ceros no significativos no cuentan).
std::optional<QString> totalParaQr(QStringView totalTexto);

// URL completa. nullopt si falta el UUID, algun RFC o el sello, o si el total
// no se puede representar (totalParaQr).
std::optional<QString> urlVerificacionCfdi(const DatosQrCfdi& datos);

} // namespace satcfdi
