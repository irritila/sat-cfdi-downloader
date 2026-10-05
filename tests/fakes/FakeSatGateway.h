#pragma once

// SatGateway falso para pruebas de OperacionesSat (T009 D11). Thread-safe
// (QMutex): la prueba lo programa en el hilo grafico y el OperacionExecutor lo
// invoca en su hilo.
//
// Guion por operacion: cola FIFO de Paso; con la cola vacia responde el exito
// por defecto (fixtures de T006). Valores tomados de tests/sat/fixtures/*.xml
// (sanitizados y ficticios); TestFakeSatGateway (tests/sat) verifica que
// coinciden con lo que parsea el adaptador real.
//
//   fakes::FakeSatGateway sat;
//   sat.programar(Op::Crear, Paso::conCreacion(FakeSatGateway::rechazo301()));
//   sat.programar(Op::Verificar, Paso::conError(FakeSatGateway::faultSintetico()));
//   sat.programar(Op::Descargar, Paso::bloqueado(FaseSatGateway::DespuesDeEnvio));
//   ... sat.esperarBloqueo(); cancelar o sat.liberar();
//
// Token: autenticar() entrega "fake-token-N" con Expires = ahora() + ttl. Con
// rechazarTokenVencido (por defecto), una operacion con token vacio o vencido
// devuelve RespuestaExplicita HTTP 401 con tokenRechazado (forma INFERIDA).

#include "fakes/FakePackageStorage.h"

#include "ports/SatGateway.h"

#include <QDateTime>
#include <QDeadlineTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>

#include <algorithm>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <optional>

namespace fakes {

class FakeSatGateway final : public satcfdi::SatGateway {
public:
    enum class Op { Autenticar, Crear, Verificar, Descargar };

    struct Paso {
        std::optional<satcfdi::ErrorSatGateway> error;
        std::optional<satcfdi::RespuestaCreacion> creacion;
        std::optional<satcfdi::RespuestaVerificacion> verificacion;
        std::optional<satcfdi::RespuestaDescarga> descarga; // codEstatus/mensaje
        std::optional<QByteArray> paquete;                  // ZIP para el receptor (solo 5000)
        // Bloquea hasta liberar() o la cancelacion; cancelado -> error con
        // esta fase y cancelada=true. Liberado -> continua con el resto del paso.
        bool bloquear = false;
        satcfdi::FaseSatGateway faseAlCancelar = satcfdi::FaseSatGateway::DespuesDeEnvio;

        static Paso conError(satcfdi::ErrorSatGateway e) { Paso p; p.error = std::move(e); return p; }
        static Paso conCreacion(satcfdi::RespuestaCreacion r) { Paso p; p.creacion = std::move(r); return p; }
        static Paso conVerificacion(satcfdi::RespuestaVerificacion r) { Paso p; p.verificacion = std::move(r); return p; }
        static Paso conDescarga(satcfdi::RespuestaDescarga r, std::optional<QByteArray> paquete = std::nullopt)
        {
            Paso p;
            p.descarga = std::move(r);
            p.paquete = std::move(paquete);
            return p;
        }
        static Paso bloqueado(satcfdi::FaseSatGateway fase = satcfdi::FaseSatGateway::DespuesDeEnvio)
        {
            Paso p;
            p.bloquear = true;
            p.faseAlCancelar = fase;
            return p;
        }
    };

    // --- Fixtures de T006 (valores ficticios) -------------------------------------
    // solicitud_emitidos_ok.xml
    static satcfdi::RespuestaCreacion creacionAceptada()
    {
        return {QStringLiteral("5000"), QStringLiteral("Solicitud Aceptada"),
                QStringLiteral("4e80345d-917f-40bb-a98f-4a73939343c5")};
    }
    // solicitud_recibidos_rechazo.xml
    static satcfdi::RespuestaCreacion rechazo301()
    {
        return {QStringLiteral("301"), QStringLiteral("XML Mal Formado"), std::nullopt};
    }
    // verificacion_terminada.xml
    static satcfdi::RespuestaVerificacion verificacionTerminada()
    {
        satcfdi::RespuestaVerificacion r;
        r.codEstatus = QStringLiteral("5000");
        r.estadoSolicitud = 3;
        r.codigoEstadoSolicitud = QStringLiteral("5000");
        r.mensaje = QStringLiteral("Solicitud Aceptada");
        r.numeroCfdi = 12;
        r.idsPaquete = {QStringLiteral("4e80345d-917f-40bb-a98f-4a73939343c5_01"),
                        QStringLiteral("4e80345d-917f-40bb-a98f-4a73939343c5_02")};
        return r;
    }
    // descarga_ok.xml: Paquete ficticio de 64 bytes.
    static QByteArray paqueteOk()
    {
        return QByteArray::fromBase64(QByteArrayLiteral(
            "UEsDBAABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4fICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ow=="));
    }
    static satcfdi::RespuestaDescarga descargaAceptada()
    {
        return {QStringLiteral("5000"), QStringLiteral("Solicitud Aceptada"), false, 0};
    }
    // descarga_vencido.xml
    static satcfdi::RespuestaDescarga descargaVencida5007()
    {
        return {QStringLiteral("5007"), QStringLiteral("No existe el paquete solicitado"), false, 0};
    }
    // fault_sintetico.xml (sintetico: Fault NO observado en SAT), HTTP 500.
    static satcfdi::ErrorSatGateway faultSintetico()
    {
        satcfdi::ErrorSatGateway e;
        e.fase = satcfdi::FaseSatGateway::RespuestaExplicita;
        e.estadoHttp = 500;
        // Igual que el gateway real: faultcode permitido SIN prefijo QName.
        e.codigoSat = QStringLiteral("InvalidSecurity");
        e.tokenRechazado = true; // inferido del faultcode de seguridad
        e.diagnosticoSanitizado = QStringLiteral("VerificaSolicitudDescarga: HTTP 500; Fault InvalidSecurity");
        return e;
    }
    static satcfdi::ErrorSatGateway errorDeFase(satcfdi::FaseSatGateway fase, bool deadline = false)
    {
        satcfdi::ErrorSatGateway e;
        e.fase = fase;
        e.deadlineVencido = deadline;
        e.diagnosticoSanitizado = satcfdi::claveEstable(fase);
        return e;
    }

    // --- Programacion ----------------------------------------------------------------
    std::function<QDateTime()> ahora = [] { return QDateTime::currentDateTimeUtc(); };
    std::chrono::seconds ttlToken{300};
    bool rechazarTokenVencido = true;

    void programar(Op op, Paso paso)
    {
        QMutexLocker l(&m_mutex);
        m_guion[op].push_back(std::move(paso));
    }
    // Libera TODOS los pasos bloqueados (actuales y el siguiente que bloquee).
    void liberar()
    {
        QMutexLocker l(&m_mutex);
        m_liberado = true;
        m_cambio.wakeAll();
    }
    // Espera (tiempo real acotado) a que una llamada este bloqueada.
    bool esperarBloqueo(int maximoMs = 5000)
    {
        QMutexLocker l(&m_mutex);
        QDeadlineTimer limite(maximoMs);
        while (m_bloqueadas == 0) {
            if (!m_cambio.wait(&m_mutex, limite)) {
                return m_bloqueadas > 0;
            }
        }
        return true;
    }

    // --- Observacion --------------------------------------------------------------------
    int autenticaciones() const { return llamadas(Op::Autenticar); }
    int llamadas(Op op) const
    {
        QMutexLocker l(&m_mutex);
        return m_llamadas.count(op) ? m_llamadas.at(op) : 0;
    }
    int maximoActivas() const
    {
        QMutexLocker l(&m_mutex);
        return m_maxActivas;
    }
    QList<satcfdi::SolicitudSat> solicitudes() const
    {
        QMutexLocker l(&m_mutex);
        return m_solicitudes;
    }
    QList<satcfdi::ConsultaSolicitudSat> verificaciones() const
    {
        QMutexLocker l(&m_mutex);
        return m_verificaciones;
    }
    QList<satcfdi::ConsultaPaqueteSat> descargas() const
    {
        QMutexLocker l(&m_mutex);
        return m_descargas;
    }

    // --- SatGateway ----------------------------------------------------------------------
    satcfdi::Resultado<satcfdi::TokenSat, satcfdi::ErrorSatGateway>
    autenticar(const satcfdi::MaterialFirma&, const satcfdi::Cancelacion& cancelacion) override
    {
        using R = satcfdi::Resultado<satcfdi::TokenSat, satcfdi::ErrorSatGateway>;
        Activa a(*this, Op::Autenticar);
        Paso p = siguiente(Op::Autenticar);
        if (auto e = bloquearSiAplica(p, cancelacion)) {
            return R::fallo(*e);
        }
        if (p.error) {
            return R::fallo(*p.error);
        }
        const QDateTime creado = ahora();
        int n = 0;
        {
            QMutexLocker l(&m_mutex);
            n = ++m_tokens;
        }
        return R::exito(satcfdi::TokenSat(QStringLiteral("fake-token-%1").arg(n), creado,
                                          creado.addSecs(qint64(ttlToken.count()))));
    }

    satcfdi::Resultado<satcfdi::RespuestaCreacion, satcfdi::ErrorSatGateway>
    crearSolicitud(const satcfdi::TokenSat& token, const satcfdi::SolicitudSat& solicitud, const satcfdi::MaterialFirma&,
                   const satcfdi::Cancelacion& cancelacion) override
    {
        using R = satcfdi::Resultado<satcfdi::RespuestaCreacion, satcfdi::ErrorSatGateway>;
        Activa a(*this, Op::Crear);
        {
            QMutexLocker l(&m_mutex);
            m_solicitudes.append(solicitud);
        }
        if (auto e = tokenInvalido(token)) {
            return R::fallo(*e);
        }
        Paso p = siguiente(Op::Crear);
        if (auto e = bloquearSiAplica(p, cancelacion)) {
            return R::fallo(*e);
        }
        if (p.error) {
            return R::fallo(*p.error);
        }
        return R::exito(p.creacion.value_or(creacionAceptada()));
    }

    satcfdi::Resultado<satcfdi::RespuestaVerificacion, satcfdi::ErrorSatGateway>
    verificarSolicitud(const satcfdi::TokenSat& token, const satcfdi::ConsultaSolicitudSat& consulta,
                       const satcfdi::MaterialFirma&, const satcfdi::Cancelacion& cancelacion) override
    {
        using R = satcfdi::Resultado<satcfdi::RespuestaVerificacion, satcfdi::ErrorSatGateway>;
        Activa a(*this, Op::Verificar);
        {
            QMutexLocker l(&m_mutex);
            m_verificaciones.append(consulta);
        }
        if (auto e = tokenInvalido(token)) {
            return R::fallo(*e);
        }
        Paso p = siguiente(Op::Verificar);
        if (auto e = bloquearSiAplica(p, cancelacion)) {
            return R::fallo(*e);
        }
        if (p.error) {
            return R::fallo(*p.error);
        }
        return R::exito(p.verificacion.value_or(verificacionTerminada()));
    }

    satcfdi::Resultado<satcfdi::RespuestaDescarga, satcfdi::ErrorSatGateway>
    descargarPaquete(const satcfdi::TokenSat& token, const satcfdi::ConsultaPaqueteSat& consulta,
                     const satcfdi::MaterialFirma&, satcfdi::ReceptorPaqueteSat& receptor,
                     const satcfdi::Cancelacion& cancelacion) override
    {
        using R = satcfdi::Resultado<satcfdi::RespuestaDescarga, satcfdi::ErrorSatGateway>;
        Activa a(*this, Op::Descargar);
        {
            QMutexLocker l(&m_mutex);
            m_descargas.append(consulta);
        }
        if (auto e = tokenInvalido(token)) {
            return R::fallo(*e);
        }
        Paso p = siguiente(Op::Descargar);
        if (auto e = bloquearSiAplica(p, cancelacion)) {
            return R::fallo(*e);
        }
        if (p.error) {
            return R::fallo(*p.error);
        }
        satcfdi::RespuestaDescarga r = p.descarga.value_or(descargaAceptada());
        if (r.codEstatus == QStringLiteral("5000")) {
            const QByteArray zip = p.paquete.value_or(paqueteOk());
            QList<QByteArray> chunks;
            for (qsizetype i = 0; i < zip.size(); i += 16) {
                chunks.append(zip.mid(i, 16));
            }
            FuenteZipEnMemoria fuente(chunks);
            receptor.recibir(fuente);
            r.paqueteEntregado = true;
            r.bytesPaquete = zip.size();
        }
        return R::exito(r);
    }

private:
    // Cuenta llamadas y concurrencia (debe ser 1 bajo el ejecutor serial).
    struct Activa {
        Activa(FakeSatGateway& f, Op op) : fake(f)
        {
            QMutexLocker l(&fake.m_mutex);
            ++fake.m_llamadas[op];
            fake.m_maxActivas = std::max(fake.m_maxActivas, ++fake.m_activas);
        }
        ~Activa()
        {
            QMutexLocker l(&fake.m_mutex);
            --fake.m_activas;
        }
        FakeSatGateway& fake;
    };

    Paso siguiente(Op op)
    {
        QMutexLocker l(&m_mutex);
        auto& cola = m_guion[op];
        if (cola.empty()) {
            return Paso{};
        }
        Paso p = std::move(cola.front());
        cola.pop_front();
        return p;
    }

    std::optional<satcfdi::ErrorSatGateway> tokenInvalido(const satcfdi::TokenSat& token)
    {
        if (!rechazarTokenVencido || token.vigenteEn(ahora())) {
            return std::nullopt;
        }
        satcfdi::ErrorSatGateway e;
        e.fase = satcfdi::FaseSatGateway::RespuestaExplicita;
        e.estadoHttp = 401;
        e.tokenRechazado = true;
        e.diagnosticoSanitizado = QStringLiteral("HTTP 401 (token rechazado, inferido)");
        return e;
    }

    std::optional<satcfdi::ErrorSatGateway> bloquearSiAplica(const Paso& p, const satcfdi::Cancelacion& cancelacion)
    {
        if (!p.bloquear) {
            return std::nullopt;
        }
        QMutexLocker l(&m_mutex);
        ++m_bloqueadas;
        m_cambio.wakeAll();
        while (!m_liberado) {
            l.unlock();
            const bool cancelada = cancelacion.solicitada();
            l.relock();
            if (cancelada) {
                --m_bloqueadas;
                satcfdi::ErrorSatGateway e = errorDeFase(p.faseAlCancelar);
                e.cancelada = true;
                return e;
            }
            m_cambio.wait(&m_mutex, 10);
        }
        --m_bloqueadas;
        return std::nullopt;
    }

    mutable QMutex m_mutex;
    QWaitCondition m_cambio;
    std::map<Op, std::deque<Paso>> m_guion;
    std::map<Op, int> m_llamadas;
    QList<satcfdi::SolicitudSat> m_solicitudes;
    QList<satcfdi::ConsultaSolicitudSat> m_verificaciones;
    QList<satcfdi::ConsultaPaqueteSat> m_descargas;
    int m_tokens = 0;
    int m_activas = 0;
    int m_maxActivas = 0;
    int m_bloqueadas = 0;
    bool m_liberado = false;
};

} // namespace fakes
