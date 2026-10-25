#pragma once

// T026.1 (D12): DestinoExportacion en memoria con inyeccion de fallas para
// las pruebas del servicio de exportacion (T026.2). Cada carpeta destino es un
// mapa nombre -> bytes; los `.partial` vivos se ven en parciales(). Sigue el
// contrato del puerto: cualquier salida sin publicar borra el `.partial`
// (salvo DestinoExistente al publicar), nunca sobrescribe y una falla del
// fsync de carpeta tras el "rename" da exito con advertencia.
// Thread-safe (el servicio lo usa desde su hilo; la prueba inspecciona desde
// el grafico).

#include "ports/exportacion/DestinoExportacion.h"

#include <QBuffer>
#include <QByteArray>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>

#include <cerrno>
#include <functional>
#include <memory>
#include <optional>

namespace fakes {

class FakeDestinoExportacion final : public satcfdi::exportacion::DestinoExportacion {
public:
    using Error = satcfdi::exportacion::ErrorDestinoExportacion;
    using C = Error::Codigo;
    using Publicacion = satcfdi::exportacion::PublicacionExportacion;
    using Resumen = satcfdi::exportacion::ResumenLimpiezaExportacion;

    enum class Falla {
        Ninguna,
        Crear,             // reservar() falla al crear el .partial
        Registro,          // reservar() falla al registrar (otro volumen)
        Escribir,          // la escritura falla (EIO) tras `limiteEscritura` bytes
        DiscoLleno,        // la escritura falla (ENOSPC) tras `limiteEscritura` bytes
        Sincronizar,       // F_FULLFSYNC
        Cerrar,
        Renombrar,         // EIO en el rename
        ExclusivoNoSoportado,
        SincronizarCarpeta, // fsync de carpeta tras el rename: exito con advertencia
    };

    // --- Configuracion -------------------------------------------------------
    Falla falla = Falla::Ninguna;
    qint64 limiteEscritura = 0;
    // Llamado justo antes del "rename" (p. ej. para crear el nombre final y
    // simular una colision tardia con ponerArchivo()).
    std::function<void()> antesDeRenombrar;
    // Llamado en cada escritura del dispositivo (p. ej. compuerta de cancelacion).
    std::function<void(qint64 bytes)> alEscribir;
    bool enOtroVolumen = false;

    void ponerArchivo(const QString& carpeta, const QString& nombre, const QByteArray& datos = {})
    {
        QMutexLocker l(&m_estado->mutex);
        m_estado->carpetas[carpeta].insert(nombre, datos);
    }
    void quitarCarpeta(const QString& carpeta)
    {
        QMutexLocker l(&m_estado->mutex);
        m_estado->noDisponibles.append(carpeta);
    }
    // Simula un `.partial` huerfano de un proceso anterior.
    void ponerHuerfano(const QString& carpeta, const QString& nombre)
    {
        QMutexLocker l(&m_estado->mutex);
        m_estado->huerfanos[carpeta].append(nombre);
    }

    // --- Inspeccion ----------------------------------------------------------
    QMap<QString, QByteArray> archivos(const QString& carpeta) const
    {
        QMutexLocker l(&m_estado->mutex);
        return m_estado->carpetas.value(carpeta);
    }
    std::optional<QByteArray> archivo(const QString& carpeta, const QString& nombre) const
    {
        QMutexLocker l(&m_estado->mutex);
        const auto c = m_estado->carpetas.value(carpeta);
        if (!c.contains(nombre)) {
            return std::nullopt;
        }
        return c.value(nombre);
    }
    // `.partial` vivos (reservados y sin publicar ni descartar).
    int parcialesVivos() const
    {
        QMutexLocker l(&m_estado->mutex);
        return m_estado->vivos;
    }
    int reservas() const
    {
        QMutexLocker l(&m_estado->mutex);
        return m_estado->reservas;
    }
    int descartes() const
    {
        QMutexLocker l(&m_estado->mutex);
        return m_estado->descartes;
    }
    int limpiezas() const
    {
        QMutexLocker l(&m_estado->mutex);
        return m_estado->limpiezas;
    }

    // --- Puerto --------------------------------------------------------------
    satcfdi::Resultado<std::unique_ptr<satcfdi::exportacion::ArchivoParcialExportacion>, Error>
    reservar(const QString& carpetaDestino, const QString& nombreFinal) override
    {
        using R = satcfdi::Resultado<std::unique_ptr<satcfdi::exportacion::ArchivoParcialExportacion>, Error>;
        QMutexLocker l(&m_estado->mutex);
        if (nombreFinal.isEmpty() || nombreFinal.contains(QLatin1Char('/')) || nombreFinal.startsWith(QLatin1Char('.'))
            || nombreFinal.toUtf8().size() > 255) {
            return R::fallo(Error::de(C::NombreInvalido));
        }
        if (m_estado->noDisponibles.contains(carpetaDestino)) {
            return R::fallo(Error::de(C::CarpetaNoDisponible, ENOENT));
        }
        if (m_estado->carpetas.value(carpetaDestino).contains(nombreFinal)) {
            return R::fallo(Error::de(C::DestinoExistente, EEXIST));
        }
        if (falla == Falla::Registro && enOtroVolumen) {
            return R::fallo(Error::de(C::Registro, EIO));
        }
        if (falla == Falla::Crear) {
            return R::fallo(Error::de(C::Creacion, EIO));
        }
        ++m_estado->reservas;
        ++m_estado->vivos;
        return R::exito(std::make_unique<Parcial>(*this, m_estado, carpetaDestino, nombreFinal));
    }

    satcfdi::Resultado<Resumen, Error> limpiarParcialesPropios() override
    {
        QMutexLocker l(&m_estado->mutex);
        ++m_estado->limpiezas;
        Resumen r;
        for (auto it = m_estado->huerfanos.begin(); it != m_estado->huerfanos.end(); ++it) {
            r.borrados += static_cast<int>(it.value().size());
        }
        m_estado->huerfanos.clear();
        return satcfdi::Resultado<Resumen, Error>::exito(r);
    }

private:
    struct Estado {
        mutable QMutex mutex;
        QMap<QString, QMap<QString, QByteArray>> carpetas;
        QMap<QString, QStringList> huerfanos;
        QStringList noDisponibles;
        int vivos = 0;
        int reservas = 0;
        int descartes = 0;
        int limpiezas = 0;
    };

    class Dispositivo final : public QBuffer {
    public:
        Dispositivo(FakeDestinoExportacion& f, QByteArray* datos) : QBuffer(datos), m_f(f) {}
        std::optional<Error> error;

    protected:
        qint64 writeData(const char* d, qint64 n) override
        {
            if (m_f.alEscribir) {
                m_f.alEscribir(n);
            }
            if (error) {
                return -1;
            }
            if ((m_f.falla == Falla::Escribir || m_f.falla == Falla::DiscoLleno) && m_escritos + n > m_f.limiteEscritura) {
                error = m_f.falla == Falla::DiscoLleno ? Error::de(C::EspacioInsuficiente, ENOSPC)
                                                       : Error::de(C::Escritura, EIO);
                return -1;
            }
            m_escritos += n;
            return QBuffer::writeData(d, n);
        }

    private:
        FakeDestinoExportacion& m_f;
        qint64 m_escritos = 0;
    };

    class Parcial final : public satcfdi::exportacion::ArchivoParcialExportacion {
    public:
        Parcial(FakeDestinoExportacion& f, std::shared_ptr<Estado> estado, QString carpeta, QString nombre)
            : m_f(f), m_estado(std::move(estado)), m_carpeta(std::move(carpeta)), m_nombre(std::move(nombre)),
              m_dispositivo(f, &m_datos)
        {
            m_dispositivo.open(QIODevice::ReadWrite | QIODevice::Unbuffered);
        }
        ~Parcial() override { descartar(); }

        QIODevice& dispositivo() override { return m_dispositivo; }
        bool enCarpetaDestino() const override { return m_f.enOtroVolumen; }
        std::optional<Error> errorEscritura() const override { return m_dispositivo.error; }

        satcfdi::Resultado<satcfdi::Exito, Error> truncar(qint64 tamano) override
        {
            using R = satcfdi::Resultado<satcfdi::Exito, Error>;
            if (m_terminado) {
                return R::fallo(Error::de(C::Estado));
            }
            m_datos.resize(tamano);
            return R::exito(satcfdi::Exito{});
        }

        satcfdi::Resultado<satcfdi::Exito, Error> sincronizar() override
        {
            using R = satcfdi::Resultado<satcfdi::Exito, Error>;
            if (m_terminado) {
                return R::fallo(Error::de(C::Estado));
            }
            if (m_f.falla == Falla::Sincronizar) {
                return R::fallo(Error::de(C::Sincronizacion, EIO));
            }
            return R::exito(satcfdi::Exito{});
        }

        satcfdi::Resultado<Publicacion, Error> publicar() override { return publicarComo(m_nombre); }

        satcfdi::Resultado<Publicacion, Error> publicarComo(const QString& nombreFinal) override
        {
            using R = satcfdi::Resultado<Publicacion, Error>;
            if (m_terminado) {
                return R::fallo(Error::de(C::Estado));
            }
            if (nombreFinal.isEmpty() || nombreFinal.contains(QLatin1Char('/')) || nombreFinal.startsWith(QLatin1Char('.'))) {
                return R::fallo(Error::de(C::NombreInvalido));
            }
            const auto fallar = [&](Error e) {
                descartar();
                return R::fallo(e);
            };
            if (m_dispositivo.error) {
                return fallar(*m_dispositivo.error);
            }
            if (m_f.falla == Falla::Sincronizar) {
                return fallar(Error::de(C::Sincronizacion, EIO));
            }
            if (m_f.falla == Falla::Cerrar) {
                return fallar(Error::de(C::Cierre, EIO));
            }
            if (m_f.antesDeRenombrar) {
                m_f.antesDeRenombrar();
            }
            QMutexLocker l(&m_estado->mutex);
            if (m_estado->carpetas.value(m_carpeta).contains(nombreFinal)) {
                return R::fallo(Error::de(C::DestinoExistente, EEXIST));
            }
            if (m_f.falla == Falla::Renombrar || m_f.falla == Falla::ExclusivoNoSoportado) {
                l.unlock();
                return fallar(m_f.falla == Falla::Renombrar ? Error::de(C::Publicacion, EIO)
                                                            : Error::de(C::ExclusivoNoSoportado, ENOTSUP));
            }
            m_estado->carpetas[m_carpeta].insert(nombreFinal, m_datos);
            m_terminado = true;
            --m_estado->vivos;
            Publicacion p;
            p.tamanoBytes = static_cast<quint64>(m_datos.size());
            if (m_f.falla == Falla::SincronizarCarpeta) {
                p.advertenciaDurabilidad = true;
                p.errnoDurabilidad = EIO;
            }
            return R::exito(p);
        }

        void descartar() noexcept override
        {
            if (m_terminado) {
                return;
            }
            m_terminado = true;
            m_dispositivo.close();
            QMutexLocker l(&m_estado->mutex);
            --m_estado->vivos;
            ++m_estado->descartes;
        }

    private:
        FakeDestinoExportacion& m_f;
        std::shared_ptr<Estado> m_estado;
        QString m_carpeta;
        QString m_nombre;
        QByteArray m_datos;
        Dispositivo m_dispositivo;
        bool m_terminado = false;
    };

    std::shared_ptr<Estado> m_estado = std::make_shared<Estado>();
};

} // namespace fakes
