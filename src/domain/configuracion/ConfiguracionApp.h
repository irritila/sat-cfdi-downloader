#pragma once

#include <QDateTime>

#include <optional>

namespace satcfdi {

// Fila unica de configuracion_app (id = 1, implicito; la siembra la migracion
// 001). Escritura desde T004 via ConfiguracionAppRepository.
struct ConfiguracionApp {
    bool inicioAutomaticoHabilitado = false;   // inicio_automatico_habilitado
    bool monitoreoPausado = false;             // monitoreo_pausado
    std::optional<QDateTime> ultimoCierreEn;   // ultimo_cierre_en
    QDateTime actualizadaEn;                   // actualizada_en
};

} // namespace satcfdi
