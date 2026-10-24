#pragma once

// T025.6: fakes de los puertos de PuertosRespaldoPresentacion.h con promesas
// manuales (la prueba decide cuando y como termina cada operacion). Datos
// sinteticos; los secretos recibidos se conservan solo para compararlos con
// BufferSecreto::igualA. Solo hilo grafico.

#include "PuertosRespaldoPresentacion.h"
#include "fakes/PromesasPendientes.h"

#include <QList>
#include <QString>
#include <QFuture>
#include <QTimeZone>

#include <memory>
#include <optional>

namespace fakes {

inline bool mismoSecreto(const std::shared_ptr<satcfdi::BufferSecreto>& buffer, const char* texto)
{
    const QByteArray esperado(texto);
    return buffer && buffer->tamano() == static_cast<std::size_t>(esperado.size())
           && buffer->igualA(esperado.constData(), static_cast<std::size_t>(esperado.size()));
}

class FakeControlRespaldo : public satcfdi::ControlRespaldoPresentacion {
public:
    using ControlRespaldoPresentacion::ControlRespaldoPresentacion;

    satcfdi::EstadoRespaldoPresentacion estadoActual;
    std::optional<satcfdi::ResultadoRespaldoPresentacion> ultimo;

    PromesasPendientes<ResultadoOperacion> carpeta;
    PromesasPendientes<ResultadoClave> contrasena;
    PromesasPendientes<ResultadoOperacion> cambio;
    PromesasPendientes<ResultadoOperacion> diario;
    PromesasPendientes<satcfdi::ResultadoRespaldoPresentacion> respaldos;

    QStringList rutas;
    QList<bool> diarios;
    std::shared_ptr<satcfdi::BufferSecreto> ultimaContrasena;
    std::shared_ptr<satcfdi::BufferSecreto> ultimaVerificacion;
    satcfdi::respaldo::VerificacionCambio::Tipo ultimoTipo = satcfdi::respaldo::VerificacionCambio::Tipo::ContrasenaAnterior;

    QUrl url; // B3
    satcfdi::EstadoRespaldoPresentacion estado() const override { return estadoActual; }
    QUrl urlCarpeta() const override { return url; }
    std::optional<satcfdi::ResultadoRespaldoPresentacion> ultimoResultado() const override { return ultimo; }

    QFuture<ResultadoOperacion> configurarCarpeta(const QString& rutaLocal) override
    {
        rutas << rutaLocal;
        return carpeta.nueva();
    }
    QFuture<ResultadoClave> configurarContrasena(std::shared_ptr<satcfdi::BufferSecreto> c) override
    {
        ultimaContrasena = std::move(c);
        return contrasena.nueva();
    }
    QFuture<ResultadoOperacion> cambiarContrasena(std::shared_ptr<satcfdi::BufferSecreto> nueva,
                                                   satcfdi::respaldo::VerificacionCambio v) override
    {
        ultimaContrasena = std::move(nueva);
        ultimaVerificacion = std::move(v.texto);
        ultimoTipo = v.tipo;
        return cambio.nueva();
    }
    QFuture<ResultadoOperacion> activarDiario(bool activado) override
    {
        diarios << activado;
        return diario.nueva();
    }
    QFuture<satcfdi::ResultadoRespaldoPresentacion> respaldarAhora() override { return respaldos.nueva(); }

    void cambiarEstado(const satcfdi::EstadoRespaldoPresentacion& e)
    {
        estadoActual = e;
        emit estadoCambiado();
    }
    void emitirProgreso(satcfdi::respaldo::RespaldoService::Fase fase, quint64 bytes, quint64 total)
    {
        emit progreso(fase, bytes, total);
    }
    void emitirTerminado(const satcfdi::ResultadoRespaldoPresentacion& r) { emit respaldoTerminado(r); }

    // Configurado y listo para respaldar.
    static satcfdi::EstadoRespaldoPresentacion listo()
    {
        satcfdi::EstadoRespaldoPresentacion e;
        e.cargado = true;
        e.carpetaConfigurada = true;
        e.nombreCarpeta = QStringLiteral("Respaldos");
        e.contrasenaConfigurada = true;
        return e;
    }

    static ResultadoOperacion ok() { return ResultadoOperacion::exito({}); }
    static ResultadoOperacion fallo(satcfdi::respaldo::ErrorGestorRespaldo::Categoria c)
    {
        // Diagnostico con datos que nunca deben verse.
        return ResultadoOperacion::fallo(
            satcfdi::respaldo::ErrorGestorRespaldo::de(c, QStringLiteral("carpeta./Users/usuario/Respaldos")));
    }
};

class FakeControlRestauracion : public satcfdi::ControlRestauracionPresentacion {
public:
    using ControlRestauracionPresentacion::ControlRestauracionPresentacion;

    PromesasPendientes<ResultadoPreparacion> preparaciones;
    PromesasPendientes<satcfdi::respaldo::ResultadoSolicitudRestauracion> solicitudes;
    QStringList archivos;
    QList<satcfdi::respaldo::PeticionRestauracion::Credencial> credenciales;
    std::shared_ptr<satcfdi::BufferSecreto> ultimoSecreto;
    int cancelaciones = 0;
    int descartes = 0;
    bool estaPreparada = false;

    QFuture<ResultadoPreparacion> preparar(const QString& rutaArchivo,
                                           satcfdi::respaldo::PeticionRestauracion::Credencial credencial,
                                           std::shared_ptr<satcfdi::BufferSecreto> secreto) override
    {
        archivos << rutaArchivo;
        credenciales << credencial;
        ultimoSecreto = std::move(secreto);
        return preparaciones.nueva();
    }
    void cancelar() override
    {
        ++cancelaciones;
        // Como el servicio: resuelve de inmediato con Cancelado.
        if (preparaciones.size() > 0) {
            preparaciones.resolverUltima(ResultadoPreparacion::fallo(
                satcfdi::respaldo::ErrorRestauracion::de(satcfdi::respaldo::ErrorRestauracion::Categoria::Cancelado)));
        }
    }
    QFuture<void> descartar() override
    {
        ++descartes;
        estaPreparada = false;
        return QtFuture::makeReadyVoidFuture();
    }
    QFuture<satcfdi::respaldo::ResultadoSolicitudRestauracion> solicitar() override { return solicitudes.nueva(); }
    bool enCurso() const override { return false; }
    bool preparada() const override { return estaPreparada; }

    void emitirProgreso(satcfdi::respaldo::RestauracionService::Fase fase, quint64 bytes, quint64 total)
    {
        emit progreso(fase, bytes, total);
    }

    static satcfdi::respaldo::ResumenRestauracion resumen()
    {
        satcfdi::respaldo::ResumenRestauracion r;
        r.idRespaldo = QByteArray::fromHex("00112233445566778899aabbccddeeff");
        r.creadoEnUtc = QDateTime(QDate(2026, 10, 1), QTime(16, 30), QTimeZone::UTC);
        r.versionAppRespaldo = QStringLiteral("1.0.0");
        r.perfiles = 2;
        r.solicitudes = 5;
        r.paquetes = 1;
        r.faltantes = 1;
        return r;
    }
};

class FakeDatosPrevios : public satcfdi::DatosPreviosRestauracionPresentacion {
public:
    PromesasPendientes<bool> consultas;
    PromesasPendientes<satcfdi::ResultadoDescartePresentacion> descartes;
    satcfdi::ResultadoArranquePresentacion arranque;

    QFuture<bool> hayDatosPrevios() override { return consultas.nueva(); }
    QFuture<satcfdi::ResultadoDescartePresentacion> descartarDatosPrevios() override { return descartes.nueva(); }
    satcfdi::ResultadoArranquePresentacion restauracionAlArrancar() const override { return arranque; }
};

} // namespace fakes
