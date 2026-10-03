#pragma once

// FakeConfiguracionAppService: ConfiguracionAppService con futures
// controlados por la prueba (T004). Header-only; misma forma de reutilizacion
// que FakeOSIntegration.h. Enlazar satcfdi_application.
//
// - Cada llamada se registra en `llamadas` ("obtener",
//   "actualizarInicioAutomatico(true)", "actualizarMonitoreoPausado(false)",
//   "registrarUltimoCierre") y queda PENDIENTE (future sin terminar).
// - confirmarSiguiente() aplica la escritura pendiente mas antigua a
//   `confirmada`, emite configuracionCambiada (solo escrituras) y DESPUES
//   completa el future con exito, como el servicio real tras commit.
// - fallarSiguiente(error) completa el mas antiguo con error, sin senal y sin
//   cambiar `confirmada`.
// - Con `autoConfirmar = true` cada llamada se confirma en el acto (el future
//   devuelto ya esta terminado).
// - `reloj` fija el instante usado en actualizadaEn/ultimoCierreEn.
// Todo ocurre en el hilo de la prueba (grafico); sin dispatcher ni SQLite.

#include "application/configuration/ConfiguracionAppService.h"

#include <QDateTime>
#include <QPromise>
#include <QStringList>
#include <QTimeZone>

#include <functional>
#include <memory>

namespace fakes {

class FakeConfiguracionAppService final : public satcfdi::ConfiguracionAppService {
    Q_OBJECT

public:
    using ConfiguracionAppService::ConfiguracionAppService;

    satcfdi::ConfiguracionApp confirmada{false, false, std::nullopt,
                                         QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC)};
    QStringList llamadas;
    bool autoConfirmar = false;
    std::function<QDateTime()> reloj = [] {
        return QDateTime(QDate(2026, 10, 3), QTime(12, 0, 0, 250), QTimeZone::UTC);
    };

    int pendientes() const { return int(m_pendientes.size()); }

    // Devuelve false si no habia nada pendiente.
    bool confirmarSiguiente()
    {
        if (m_pendientes.isEmpty()) {
            return false;
        }
        Pendiente p = m_pendientes.takeFirst();
        if (p.aplicar) {
            p.aplicar(confirmada);
            emit configuracionCambiada(confirmada);
        }
        terminar(*p.promesa, ResultadoConfiguracion::exito(confirmada));
        return true;
    }

    bool fallarSiguiente(satcfdi::ErrorPersistencia error)
    {
        if (m_pendientes.isEmpty()) {
            return false;
        }
        Pendiente p = m_pendientes.takeFirst();
        terminar(*p.promesa, ResultadoConfiguracion::fallo(std::move(error)));
        return true;
    }

    QFuture<ResultadoConfiguracion> obtener() override
    {
        return encolar(QStringLiteral("obtener"), {});
    }

    QFuture<ResultadoConfiguracion> actualizarInicioAutomatico(bool habilitado) override
    {
        const QDateTime en = reloj();
        return encolar(QStringLiteral("actualizarInicioAutomatico(%1)").arg(texto(habilitado)),
                       [habilitado, en](satcfdi::ConfiguracionApp& c) {
                           c.inicioAutomaticoHabilitado = habilitado;
                           c.actualizadaEn = en;
                       });
    }

    QFuture<ResultadoConfiguracion> actualizarMonitoreoPausado(bool pausado) override
    {
        const QDateTime en = reloj();
        return encolar(QStringLiteral("actualizarMonitoreoPausado(%1)").arg(texto(pausado)),
                       [pausado, en](satcfdi::ConfiguracionApp& c) {
                           c.monitoreoPausado = pausado;
                           c.actualizadaEn = en;
                       });
    }

    QFuture<ResultadoConfiguracion> registrarUltimoCierre() override
    {
        const QDateTime en = reloj();
        return encolar(QStringLiteral("registrarUltimoCierre"), [en](satcfdi::ConfiguracionApp& c) {
            c.ultimoCierreEn = en;
            c.actualizadaEn = en;
        });
    }

private:
    struct Pendiente {
        std::shared_ptr<QPromise<ResultadoConfiguracion>> promesa;
        std::function<void(satcfdi::ConfiguracionApp&)> aplicar; // vacio = lectura
    };

    static QString texto(bool b) { return b ? QStringLiteral("true") : QStringLiteral("false"); }

    static void terminar(QPromise<ResultadoConfiguracion>& promesa, ResultadoConfiguracion r)
    {
        promesa.addResult(std::move(r));
        promesa.finish();
    }

    QFuture<ResultadoConfiguracion> encolar(QString llamada,
                                            std::function<void(satcfdi::ConfiguracionApp&)> aplicar)
    {
        llamadas.append(std::move(llamada));
        auto promesa = std::make_shared<QPromise<ResultadoConfiguracion>>();
        promesa->start();
        QFuture<ResultadoConfiguracion> f = promesa->future();
        m_pendientes.append(Pendiente{promesa, std::move(aplicar)});
        if (autoConfirmar) {
            confirmarSiguiente();
        }
        return f;
    }

    QList<Pendiente> m_pendientes;
};

} // namespace fakes
