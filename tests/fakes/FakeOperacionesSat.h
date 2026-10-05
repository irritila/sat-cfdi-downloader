#pragma once

// OperacionesSat falso y thread-safe (T007, verificacion 2). Pensado para
// llamarse desde el hilo del OperacionExecutor mientras la prueba lo observa
// y controla desde el hilo grafico. NO usa PromesasPendientes (solo hilo
// grafico): sincroniza con QMutex/QWaitCondition.
//
// - Respuestas por GUION (FIFO por operacion): responder*(...). Sin guion se
//   usa la respuesta por defecto: enviar -> 5000 con IdSolicitud "SAT-<n>";
//   verificar -> 5000 Aceptada; descargar -> exito con ruta
//   "paquetes/<paqueteId>.zip"; existeArchivoFinal -> no existe;
//   obtenerEstadoCredencial -> credenciales[perfil] o Lista.
// - BARRERA: bloquearSiguiente(op) detiene la siguiente llamada a `op` dentro
//   del puerto hasta liberar() o hasta que se solicite su cancelacion; en ese
//   caso devuelve FallaOperacion{cancelada = true, fase = faseAlCancelar}.
//   esperarBloqueo(ms) espera (sin sleep) a que una llamada este detenida; BLOQUEA
//   el hilo que llama: si la llamada depende del event loop grafico (p. ej. un
//   ciclo de WorkerLocal), esperar con QTest::qWaitFor([&]{ return activas() == 1; }).
// - REGISTRO: llamadas() en orden ("enviar:<solicitudId>", "verificar:<id>",
//   "descargar:<paqueteId>", "existeArchivoFinal:<paqueteId>",
//   "credencial:<perfilId>"), resultados() ("enviar:ok",
//   "verificar:falla:RespuestaExplicita:300[:cancelada]"), concurrencia
//   maxima, llamadas activas e hilos usados.

#include "application/operaciones/OperacionesSat.h"

#include <QDeadlineTimer>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QWaitCondition>

#include <deque>
#include <optional>

namespace fakes {

class FakeOperacionesSat final : public satcfdi::OperacionesSat {
public:
    enum class Op { Enviar, Verificar, Descargar, ExisteArchivoFinal, EstadoCredencial };

    template <typename T>
    using R = satcfdi::Resultado<T, satcfdi::FallaOperacion>;

    // --- Guion ---------------------------------------------------------------
    void responderEnvio(R<satcfdi::ResultadoEnvio> r) { agregar(m_envios, std::move(r)); }
    void responderVerificacion(R<satcfdi::ResultadoVerificacion> r) { agregar(m_verificaciones, std::move(r)); }
    void responderDescarga(R<satcfdi::ResultadoDescarga> r) { agregar(m_descargas, std::move(r)); }
    void responderArchivoFinal(R<satcfdi::ResultadoArchivoFinal> r) { agregar(m_archivos, std::move(r)); }
    void fijarCredencial(const satcfdi::PerfilId& perfil, satcfdi::EstadoCredencial estado)
    {
        QMutexLocker l(&m_mutex);
        m_credenciales.insert(perfil, estado);
    }
    void fallarCredencial(const satcfdi::PerfilId& perfil, satcfdi::FallaOperacion falla)
    {
        QMutexLocker l(&m_mutex);
        m_fallasCredencial.insert(perfil, std::move(falla));
    }

    // --- Barrera ---------------------------------------------------------------
    void bloquearSiguiente(Op op, satcfdi::FaseOperacion faseAlCancelar = satcfdi::FaseOperacion::DespuesDeEnvio)
    {
        QMutexLocker l(&m_mutex);
        m_bloqueos.insert(static_cast<int>(op));
        m_faseAlCancelar.insert(static_cast<int>(op), faseAlCancelar);
    }
    bool esperarBloqueo(int ms = 5000)
    {
        QMutexLocker l(&m_mutex);
        QDeadlineTimer limite(ms);
        while (m_detenidas == 0) {
            if (!m_cambio.wait(&m_mutex, limite)) {
                return m_detenidas > 0;
            }
        }
        return true;
    }
    void liberar()
    {
        QMutexLocker l(&m_mutex);
        ++m_liberaciones;
        m_cambio.wakeAll();
    }

    // --- Registro --------------------------------------------------------------
    QStringList llamadas() const { QMutexLocker l(&m_mutex); return m_llamadas; }
    QStringList resultados() const { QMutexLocker l(&m_mutex); return m_resultados; }
    int concurrenciaMaxima() const { QMutexLocker l(&m_mutex); return m_maxActivas; }
    int activas() const { QMutexLocker l(&m_mutex); return m_activas; }
    QSet<QThread*> hilos() const { QMutexLocker l(&m_mutex); return m_hilos; }

    // --- OperacionesSat --------------------------------------------------------
    R<satcfdi::ResultadoEnvio> enviar(const satcfdi::ContextoEnvio& c) override
    {
        return ejecutar<satcfdi::ResultadoEnvio>(Op::Enviar, QStringLiteral("enviar"), c.solicitud.id.texto(),
                                                 c.cancelacion, m_envios, [this]() {
                                                     satcfdi::ResultadoEnvio r;
                                                     r.codEstatus = QStringLiteral("5000");
                                                     r.mensaje = QStringLiteral("Solicitud Aceptada");
                                                     r.idSolicitudSat = QStringLiteral("SAT-%1").arg(++m_contadorIds);
                                                     return r;
                                                 });
    }

    R<satcfdi::ResultadoVerificacion> verificar(const satcfdi::ContextoVerificacion& c) override
    {
        return ejecutar<satcfdi::ResultadoVerificacion>(Op::Verificar, QStringLiteral("verificar"),
                                                        c.solicitudId.texto(), c.cancelacion, m_verificaciones,
                                                        []() { return satcfdi::ResultadoVerificacion{}; });
    }

    R<satcfdi::ResultadoDescarga> descargar(const satcfdi::ContextoDescarga& c) override
    {
        const QString id = c.paqueteId;
        return ejecutar<satcfdi::ResultadoDescarga>(Op::Descargar, QStringLiteral("descargar"), id, c.cancelacion,
                                                    m_descargas, [id]() {
                                                        satcfdi::ResultadoDescarga r;
                                                        r.rutaFinal = QStringLiteral("paquetes/%1.zip").arg(id);
                                                        return r;
                                                    });
    }

    R<satcfdi::ResultadoArchivoFinal> existeArchivoFinal(const satcfdi::ContextoArchivoFinal& c) override
    {
        return ejecutar<satcfdi::ResultadoArchivoFinal>(Op::ExisteArchivoFinal, QStringLiteral("existeArchivoFinal"),
                                                        c.paqueteId, c.cancelacion, m_archivos,
                                                        []() { return satcfdi::ResultadoArchivoFinal{}; });
    }

    R<satcfdi::EstadoCredencial> obtenerEstadoCredencial(const satcfdi::PerfilId& perfil) override
    {
        QMutexLocker l(&m_mutex);
        m_llamadas.append(QStringLiteral("credencial:") + perfil.texto());
        m_hilos.insert(QThread::currentThread());
        if (m_fallasCredencial.contains(perfil)) {
            m_resultados.append(QStringLiteral("credencial:falla"));
            return R<satcfdi::EstadoCredencial>::fallo(m_fallasCredencial.value(perfil));
        }
        const auto estado = m_credenciales.value(perfil, satcfdi::EstadoCredencial::Lista);
        m_resultados.append(QStringLiteral("credencial:") + satcfdi::claveEstable(estado));
        return R<satcfdi::EstadoCredencial>::exito(estado);
    }

private:
    template <typename T>
    void agregar(std::deque<R<T>>& cola, R<T> r)
    {
        QMutexLocker l(&m_mutex);
        cola.push_back(std::move(r));
    }

    template <typename T, typename Defecto>
    R<T> ejecutar(Op op, const QString& nombre, const QString& id, const satcfdi::SenalCancelacion& cancelacion,
                  std::deque<R<T>>& cola, Defecto porDefecto)
    {
        // Fuera del mutex: si ya estaba cancelada, la notificacion corre aqui.
        cancelacion.alSolicitar([this]() {
            QMutexLocker l2(&m_mutex);
            m_cambio.wakeAll();
        });
        QMutexLocker l(&m_mutex);
        m_llamadas.append(nombre + QLatin1Char(':') + id);
        m_hilos.insert(QThread::currentThread());
        ++m_activas;
        m_maxActivas = std::max(m_maxActivas, m_activas);
        bool cancelada = false;
        if (m_bloqueos.remove(static_cast<int>(op))) {
            const int liberacionesAntes = m_liberaciones;
            ++m_detenidas;
            m_cambio.wakeAll();
            while (m_liberaciones == liberacionesAntes && !cancelacion.solicitada()) {
                m_cambio.wait(&m_mutex);
            }
            --m_detenidas;
            cancelada = m_liberaciones == liberacionesAntes;
        }
        R<T> r = cancelada ? R<T>::fallo(fallaCancelada(op))
                 : cola.empty() ? R<T>::exito(porDefecto())
                                : tomar(cola);
        --m_activas;
        m_resultados.append(nombre + QLatin1Char(':') + descripcion(r));
        m_cambio.wakeAll();
        return r;
    }

    template <typename T>
    static R<T> tomar(std::deque<R<T>>& cola)
    {
        R<T> r = std::move(cola.front());
        cola.pop_front();
        return r;
    }

    satcfdi::FallaOperacion fallaCancelada(Op op) const
    {
        satcfdi::FallaOperacion f = satcfdi::FallaOperacion::de(
            m_faseAlCancelar.value(static_cast<int>(op), satcfdi::FaseOperacion::DespuesDeEnvio), std::nullopt,
            QStringLiteral("cancelada"));
        f.cancelada = true;
        return f;
    }

    template <typename T>
    static QString descripcion(const R<T>& r)
    {
        if (r.esExito()) {
            return QStringLiteral("ok");
        }
        QString d = QStringLiteral("falla:") + satcfdi::claveFalla(r.error());
        if (r.error().cancelada) {
            d += QStringLiteral(":cancelada");
        }
        return d;
    }

    mutable QMutex m_mutex;
    QWaitCondition m_cambio;
    std::deque<R<satcfdi::ResultadoEnvio>> m_envios;
    std::deque<R<satcfdi::ResultadoVerificacion>> m_verificaciones;
    std::deque<R<satcfdi::ResultadoDescarga>> m_descargas;
    std::deque<R<satcfdi::ResultadoArchivoFinal>> m_archivos;
    QHash<satcfdi::PerfilId, satcfdi::EstadoCredencial> m_credenciales;
    QHash<satcfdi::PerfilId, satcfdi::FallaOperacion> m_fallasCredencial;
    QSet<int> m_bloqueos;
    QHash<int, satcfdi::FaseOperacion> m_faseAlCancelar;
    int m_detenidas = 0;
    int m_liberaciones = 0;
    int m_activas = 0;
    int m_maxActivas = 0;
    int m_contadorIds = 0;
    QStringList m_llamadas;
    QStringList m_resultados;
    QSet<QThread*> m_hilos;
};

} // namespace fakes
