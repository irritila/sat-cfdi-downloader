#pragma once

#include <QLatin1StringView>
#include <QStringView>

namespace satcfdi {

// c_TipoDeComprobante del Anexo 20 (3.3/4.0): I, E, T, N, P. CFDI 3.2 usaba
// "ingreso", "egreso" y "traslado" (sin nomina ni pago como tipo).
enum class TipoComprobante {
    Desconocido,
    Ingreso,
    Egreso,
    Traslado,
    Nomina,
    Pago,
};

// Acepta las claves de 3.3/4.0 y las palabras de 3.2 (sin distinguir
// mayusculas en estas ultimas). Cualquier otro texto -> Desconocido.
TipoComprobante tipoComprobanteDesdeTexto(QStringView texto);

// Clave de una letra ("I", "E", ...); "" para Desconocido.
QLatin1StringView codigoTipoComprobante(TipoComprobante tipo);

} // namespace satcfdi
