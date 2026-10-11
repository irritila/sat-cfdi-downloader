#pragma once

// ArchivosZipCfdi en memoria (T015.2) para pruebas del calculo del indice:
// por ruta relativa, un estado, metadatos y SHA-256 programables. Cuenta las
// llamadas a sha256() (la revalidacion rapida no debe calcularlo). Thread-safe
// (QMutex): se programa en el hilo de la prueba y se usa en el del indexador.

#include "ports/cfdi/ArchivosZipCfdi.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QTest>
#include <QWaitCondition>

#include <optional>

namespace fakes {

class FakeArchivosZipCfdi final : public satcfdi::ArchivosZipCfdi {
public:
    struct Archivo {
        satcfdi::ArchivoZipLocal::Estado estado = satcfdi::ArchivoZipLocal::Estado::Existe;
        qint64 tamano = 100;
        qint64 modificadoMs = 1000;
        QString sha256 = QString(64, QLatin1Char('a'));
        bool shaFalla = false;
    };

    void fijar(const QString& rutaRelativa, Archivo archivo)
    {
        QMutexLocker bloqueo(&m_mutex);
        m_archivos.insert(rutaRelativa, archivo);
    }

    void quitar(const QString& rutaRelativa)
    {
        QMutexLocker bloqueo(&m_mutex);
        m_archivos.remove(rutaRelativa);
    }

    // Cambia el tamano despues de N consultas (simula un ZIP que cambia
    // durante la lectura).
    void cambiarTrasConsultas(const QString& rutaRelativa, int consultas)
    {
        QMutexLocker bloqueo(&m_mutex);
        m_cambiarRuta = rutaRelativa;
        m_cambiarTras = consultas;
    }

    // Compuerta de consultar(): cerrada, cada consulta se bloquea hasta
    // abrirConsultas() (calculo "en curso" determinista).
    void cerrarConsultas()
    {
        QMutexLocker bloqueo(&m_mutex);
        m_consultasAbiertas = false;
    }
    void abrirConsultas()
    {
        QMutexLocker bloqueo(&m_mutex);
        m_consultasAbiertas = true;
        m_condicion.wakeAll();
    }
    bool esperarConsultaBloqueada(int ms = 5000)
    {
        return QTest::qWaitFor(
            [this] {
                QMutexLocker bloqueo(&m_mutex);
                return m_consultaBloqueada;
            },
            ms);
    }

    int llamadasSha() const
    {
        QMutexLocker bloqueo(&m_mutex);
        return m_llamadasSha;
    }

    satcfdi::ArchivoZipLocal consultar(const QString& rutaRelativa) override
    {
        QMutexLocker bloqueo(&m_mutex);
        while (!m_consultasAbiertas) {
            m_consultaBloqueada = true;
            m_condicion.wait(&m_mutex);
        }
        m_consultaBloqueada = false;
        satcfdi::ArchivoZipLocal r;
        const auto it = m_archivos.find(rutaRelativa);
        if (it == m_archivos.end()) {
            r.estado = satcfdi::ArchivoZipLocal::Estado::NoExiste;
            return r;
        }
        if (rutaRelativa == m_cambiarRuta && m_cambiarTras >= 0 && m_cambiarTras-- == 0) {
            it->tamano += 1;
        }
        r.estado = it->estado;
        if (r.estado == satcfdi::ArchivoZipLocal::Estado::Existe) {
            r.rutaAbsoluta = QStringLiteral("/fake/") + rutaRelativa;
            r.tamano = it->tamano;
            r.modificadoMs = it->modificadoMs;
        }
        return r;
    }

    std::optional<QString> sha256(const QString& rutaAbsoluta, const std::function<bool()>& continuar) override
    {
        QMutexLocker bloqueo(&m_mutex);
        ++m_llamadasSha;
        if (continuar && !continuar()) {
            return std::nullopt;
        }
        const QString relativa = rutaAbsoluta.mid(QStringLiteral("/fake/").size());
        const auto it = m_archivos.constFind(relativa);
        if (it == m_archivos.constEnd() || it->shaFalla) {
            return std::nullopt;
        }
        return it->sha256;
    }

private:
    mutable QMutex m_mutex;
    QHash<QString, Archivo> m_archivos;
    QString m_cambiarRuta;
    int m_cambiarTras = -1;
    int m_llamadasSha = 0;
    QWaitCondition m_condicion;
    bool m_consultasAbiertas = true;
    bool m_consultaBloqueada = false;
};

} // namespace fakes
