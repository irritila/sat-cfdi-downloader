#pragma once

#include "ports/respaldo/ConfiguracionRespaldo.h"

#include <QString>

namespace satcfdi {

// T025.4 (D14): ConfiguracionRespaldo en `<datos>/preferencias.ini`, seccion
// `respaldo` (QSettings con sync() y comprobacion de estado; QSettings escribe
// con un temporal y rename). Claves:
//   respaldo/carpeta/ruta, respaldo/carpeta/bookmark (base64),
//   respaldo/carpeta/uuidVolumen, respaldo/diario,
//   respaldo/kek/contrasena, respaldo/kek/recuperacion (ReferenciaKek),
//   respaldo/ultimo/intento, respaldo/ultimo/exito (ISO-8601 UTC con ms),
//   respaldo/ultimo/resultado,
//   respaldo/ultimo/resumen/{fecha, estado, bytesDatos, faltantes, omitidos,
//   depurados, copiasSinRegistro, duplicadosProveedor, avisoDepuracion,
//   avisoRegistro, errorDestino, errorCaptura, errorContenedor} (T025.6 C6:
//   solo fecha, claves estables [a-z_], conteos y true/false; un resumen sin
//   fecha o estado validos se lee ausente, un campo corrupto como 0/false/vacio,
//   y nada de esto afecta `integra`).
// `respaldo/idInstalacion` (T025.3) convive en la seccion y no se toca. Valores
// invalidos se leen como ausentes (nunca un error que bloquee el arranque),
// pero una referencia de KEK presente e invalida, una sola referencia o un ini
// inexistente dejan la lectura NO integra (ConfiguracionLeida::integra).
class ConfiguracionRespaldoIni final : public respaldo::AlmacenConfiguracionRespaldo {
public:
    explicit ConfiguracionRespaldoIni(QString rutaPreferencias) : m_ruta(std::move(rutaPreferencias)) {}

    Resultado<respaldo::ConfiguracionLeida, respaldo::ErrorConfiguracionRespaldo> cargar() override;
    Resultado<Exito, respaldo::ErrorConfiguracionRespaldo> guardar(const respaldo::ConfiguracionRespaldo& c) override;

private:
    QString m_ruta;
};

} // namespace satcfdi
