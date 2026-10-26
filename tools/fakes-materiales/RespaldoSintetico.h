#pragma once

// T024.2 (D9): puertos de presentacion del respaldo (T025.6) gobernados por
// datos. Sin hilos ni temporizadores: el estado y el ultimo resultado son
// datos; respaldarAhora() y preparar() se resuelven al instante con el
// resultado declarado o quedan pendientes hasta terminarRespaldo() /
// terminarPreparacion(); el progreso solo avanza con emitirProgreso*().
// La clave de recuperacion es sintetica. Solo el nombre de la carpeta
// ("Respaldos"); la URL apunta a /Users/usuario/Respaldos.

#include "presentation/viewmodels/PuertosRespaldoPresentacion.h"

#include <QPromise>

#include <memory>
#include <optional>
#include <vector>

namespace materiales {

using namespace satcfdi;

class ControlRespaldoSintetico final : public ControlRespaldoPresentacion {
public:
    EstadoRespaldoPresentacion estadoActual = configurado();
    std::optional<ResultadoRespaldoPresentacion> ultimo = exitoDeEjemplo();
    // nullopt: respaldarAhora() queda pendiente (en curso) hasta terminarRespaldo().
    std::optional<ResultadoRespaldoPresentacion> resultadoRespaldo;

    ~ControlRespaldoSintetico() override;

    EstadoRespaldoPresentacion estado() const override { return estadoActual; }
    std::optional<ResultadoRespaldoPresentacion> ultimoResultado() const override { return ultimo; }
    QUrl urlCarpeta() const override;

    QFuture<ResultadoOperacion> configurarCarpeta(const QString& rutaLocal) override;
    QFuture<ResultadoClave> configurarContrasena(std::shared_ptr<BufferSecreto> contrasena) override;
    QFuture<ResultadoOperacion> cambiarContrasena(std::shared_ptr<BufferSecreto> nueva,
                                                   respaldo::VerificacionCambio verificacion) override;
    QFuture<ResultadoOperacion> activarDiario(bool activado) override;
    QFuture<ResultadoRespaldoPresentacion> respaldarAhora() override;

    void fijarEstado(const EstadoRespaldoPresentacion& e);
    void emitirProgreso(respaldo::RespaldoService::Fase fase, quint64 bytes, quint64 total);
    // Resuelve el respaldo pendiente (si lo hay) y emite respaldoTerminado.
    void terminarRespaldo(const ResultadoRespaldoPresentacion& r);

    // Carpeta y contrasena configuradas, diario activo.
    static EstadoRespaldoPresentacion configurado();
    // Respaldo correcto del 2026-10-14 03:00 UTC (diario).
    static ResultadoRespaldoPresentacion exitoDeEjemplo();
    static QString claveRecuperacionSintetica();

private:
    std::vector<std::unique_ptr<QPromise<ResultadoRespaldoPresentacion>>> m_respaldos;
};

class ControlRestauracionSintetico final : public ControlRestauracionPresentacion {
public:
    // nullopt: preparar() queda pendiente hasta terminarPreparacion().
    std::optional<ResultadoPreparacion> resultadoPreparacion = ResultadoPreparacion::exito(resumenDeEjemplo());

    ~ControlRestauracionSintetico() override;

    QFuture<ResultadoPreparacion> preparar(const QString& rutaArchivo, respaldo::PeticionRestauracion::Credencial credencial,
                                           std::shared_ptr<BufferSecreto> secreto) override;
    void cancelar() override;
    QFuture<void> descartar() override;
    // Siempre pendiente: la app saldria; las herramientas nunca la piden.
    QFuture<respaldo::ResultadoSolicitudRestauracion> solicitar() override;
    bool enCurso() const override { return !m_preparaciones.empty() && !m_preparaciones.back()->future().isFinished(); }
    bool preparada() const override { return m_preparada; }

    void emitirProgreso(respaldo::RestauracionService::Fase fase, quint64 bytes, quint64 total);
    void terminarPreparacion(const ResultadoPreparacion& r);

    static respaldo::ResumenRestauracion resumenDeEjemplo();

private:
    bool m_preparada = false;
    std::vector<std::unique_ptr<QPromise<ResultadoPreparacion>>> m_preparaciones;
    std::vector<std::unique_ptr<QPromise<respaldo::ResultadoSolicitudRestauracion>>> m_solicitudes;
};

class DatosPreviosSinteticos final : public DatosPreviosRestauracionPresentacion {
public:
    bool hay = false;
    ResultadoArranquePresentacion arranque;

    QFuture<bool> hayDatosPrevios() override { return QtFuture::makeReadyValueFuture(hay); }
    QFuture<ResultadoDescartePresentacion> descartarDatosPrevios() override;
    ResultadoArranquePresentacion restauracionAlArrancar() const override { return arranque; }
};

} // namespace materiales
