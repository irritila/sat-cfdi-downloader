#pragma once

// CfdiArchiveReader con compuerta (T015.2): delega en otro lector (real o
// falso) y, con la compuerta cerrada, se bloquea antes de entregar cada XML
// hasta liberar(). Sirve para mantener una indexacion "en curso" de forma
// determinista (salida a mitad, operaciones SAT concurrentes). Cuenta los
// recorridos y registra el hilo donde corre. Thread-safe.
//
// T026.1: leerEntrada (relectura por posicion de la exportacion, T026 D11)
// delega en leerEntrada del lector interno y, con la compuerta cerrada, se
// bloquea antes de entregar el resultado; cuenta las lecturas por posicion
// aparte de los recorridos. La compuerta abierta (por omision) no cambia el
// comportamiento de los usos anteriores.

#include "ports/cfdi/CfdiArchiveReader.h"

#include <QDeadlineTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QTest>
#include <QThread>
#include <QWaitCondition>

#include <memory>

namespace fakes {

class LectorCfdiCompuerta final : public satcfdi::CfdiArchiveReader {
public:
    explicit LectorCfdiCompuerta(satcfdi::CfdiArchiveReader& interno)
        : m_interno(interno)
    {
    }

    // T026.2 (C7): duena del lector interno, para que la compuerta pueda
    // vivir solo por quien la retiene (p. ej. un hilo de exportacion
    // abandonado).
    explicit LectorCfdiCompuerta(std::shared_ptr<satcfdi::CfdiArchiveReader> interno)
        : m_propio(std::move(interno))
        , m_interno(*m_propio)
    {
    }

    void cerrar()
    {
        QMutexLocker b(&m_mutex);
        m_abierta = false;
    }

    void liberar()
    {
        QMutexLocker b(&m_mutex);
        m_abierta = true;
        m_condicion.wakeAll();
    }

    bool bloqueado() const
    {
        QMutexLocker b(&m_mutex);
        return m_bloqueado;
    }

    // Espera (procesando eventos) a que una lectura quede bloqueada.
    bool esperarBloqueo(int ms = 10000)
    {
        return QTest::qWaitFor([this] { return bloqueado(); }, ms);
    }

    QThread* hilo() const
    {
        QMutexLocker b(&m_mutex);
        return m_hilo;
    }

    int recorridos() const
    {
        QMutexLocker b(&m_mutex);
        return m_recorridos;
    }

    int lecturasPorPosicion() const
    {
        QMutexLocker b(&m_mutex);
        return m_lecturasPorPosicion;
    }

    satcfdi::Resultado<satcfdi::ResumenLecturaZip, satcfdi::ErrorPaqueteZip> recorrer(
        const QString& rutaZip, const std::function<bool(const satcfdi::EntradaXmlCfdi&)>& alXml) override
    {
        {
            QMutexLocker b(&m_mutex);
            m_hilo = QThread::currentThread();
            ++m_recorridos;
        }
        return m_interno.recorrer(rutaZip, [this, &alXml](const satcfdi::EntradaXmlCfdi& e) {
            esperarCompuerta();
            return alXml(e);
        });
    }

    satcfdi::Resultado<std::optional<QByteArray>, satcfdi::ErrorPaqueteZip> leerEntrada(const QString& rutaZip,
                                                                                       int indice) override
    {
        {
            QMutexLocker b(&m_mutex);
            m_hilo = QThread::currentThread();
            ++m_lecturasPorPosicion;
        }
        auto r = m_interno.leerEntrada(rutaZip, indice);
        esperarCompuerta();
        return r;
    }

private:
    void esperarCompuerta()
    {
        QMutexLocker b(&m_mutex);
        while (!m_abierta) {
            m_bloqueado = true;
            m_condicion.wait(&m_mutex, QDeadlineTimer(30000));
        }
        m_bloqueado = false;
    }

    std::shared_ptr<satcfdi::CfdiArchiveReader> m_propio; // nulo si es prestado
    satcfdi::CfdiArchiveReader& m_interno;
    mutable QMutex m_mutex;
    QWaitCondition m_condicion;
    bool m_abierta = true;
    bool m_bloqueado = false;
    QThread* m_hilo = nullptr;
    int m_recorridos = 0;
    int m_lecturasPorPosicion = 0;
};

} // namespace fakes
