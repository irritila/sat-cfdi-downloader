#pragma once

#include "domain/common/Resultado.h"
#include "domain/configuracion/ConfiguracionApp.h"
#include "ports/persistence/ErrorPersistencia.h"

#include <QFuture>
#include <QObject>

namespace satcfdi {

// Contrato de configuracion local de la aplicacion (T004), consumido por
// AppLifecycleController. Expone solo estado CONFIRMADO en SQLite.
//
// Asincronia: cada operacion devuelve un QFuture que se completa en el hilo
// grafico (hilo del servicio). Nunca bloquea al llamador.
// Ownership/hilo: QObject con afinidad al hilo grafico; lo crea el composition
// root. Las senales se emiten en el hilo grafico.
// Senales: configuracionCambiada(c) se emite SOLO tras commit exitoso de una
// escritura, ANTES de completar el future, con la fila confirmada. Un error no
// emite senal y deja la configuracion sin cambios.
//
// La preferencia de inicio automatico NO es el estado efectivo del Login Item
// (runtime de OSIntegration); este servicio no llama al SO.
class ConfiguracionAppService : public QObject {
    Q_OBJECT

public:
    using ResultadoConfiguracion = Resultado<ConfiguracionApp, ErrorPersistencia>;

    using QObject::QObject;
    ~ConfiguracionAppService() override = default;

    // Lee la fila unica. Sin senal.
    virtual QFuture<ResultadoConfiguracion> obtener() = 0;

    // Persiste la preferencia local de inicio automatico.
    virtual QFuture<ResultadoConfiguracion> actualizarInicioAutomatico(bool habilitado) = 0;

    // Persiste monitoreo_pausado (pausar/reanudar; T007 lo consumira).
    virtual QFuture<ResultadoConfiguracion> actualizarMonitoreoPausado(bool pausado) = 0;

    // Registra ultimo_cierre_en con el reloj del servicio (salida explicita).
    virtual QFuture<ResultadoConfiguracion> registrarUltimoCierre() = 0;

signals:
    void configuracionCambiada(const satcfdi::ConfiguracionApp& configuracion);
};

} // namespace satcfdi
