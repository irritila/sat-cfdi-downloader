#pragma once

// T025.3: DestinoRespaldo en memoria con inyeccion de errores. Archivos
// regulares en un mapa; los "enlaces" existen pero listar() no los devuelve y
// abrir/borrar los rechaza, como el adaptador POSIX. Un archivo puede marcarse
// "sin descargar" (D20): abrirParaLeer() lo rechaza y cuenta el intento.
// Thread-safe (el servicio lo usa desde su hilo; la prueba lo inspecciona desde
// el grafico).

#include "ports/respaldo/DestinoRespaldo.h"

#include <QBuffer>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>

namespace fakes {

class FakeDestinoRespaldo final : public satcfdi::respaldo::DestinoRespaldo {
public:
    using ErrorDestino = satcfdi::respaldo::ErrorDestino;
    using EntradaDestino = satcfdi::respaldo::EntradaDestino;
    using C = ErrorDestino::Categoria;

    enum class Falla {
        Ninguna,
        CarpetaInexistente,
        SinEscritura,
        VolumenDesmontado,
        SinEspacioAlValidar,
        SinEspacioAlEscribir, // la escritura falla tras `limiteEscritura` bytes (ENOSPC)
        Fsync,
        Rename,
        ExclusivoNoSoportado,
        CarpetaCambiada, // comprobarIdentidad() falla
    };

    // --- Configuracion -------------------------------------------------------
    Falla falla = Falla::Ninguna;
    qint64 limiteEscritura = 4096;
    quint64 bytesLibres = 1ULL << 40;
    std::function<void()> alSincronizar;
    // Llamado tras cada publicacion con el nombre publicado (p. ej. para
    // "subir" a la nube las copias anteriores).
    std::function<void(const QString&)> alPublicar;
    // comprobarIdentidad() falla desde la llamada N+1 (N >= 0); -1 = nunca.
    int fallarIdentidadTras = -1;
    // abrirParaLeer() devuelve un dispositivo SECUENCIAL (sin seek): un
    // segundo intento de autenticacion sobre el mismo dispositivo fallaria.
    bool lecturaSecuencial = false;

    void ponerArchivo(const QString& nombre, const QByteArray& datos)
    {
        QMutexLocker l(&m_mutex);
        m_archivos.insert(nombre, datos);
        m_inodos.insert(nombre, ++m_ultimoInodo);
    }
    void ponerEnlace(const QString& nombre)
    {
        QMutexLocker l(&m_mutex);
        m_enlaces.insert(nombre);
    }
    void marcarSinDescargar(const QString& nombre, bool sinDescargar = true)
    {
        QMutexLocker l(&m_mutex);
        if (sinDescargar) {
            m_sinDescargar.insert(nombre);
        } else {
            m_sinDescargar.remove(nombre);
        }
    }
    // Simula que el proveedor reemplazo el archivo (otro inodo).
    void cambiarInodo(const QString& nombre)
    {
        QMutexLocker l(&m_mutex);
        m_inodos.insert(nombre, ++m_ultimoInodo);
    }

    // --- Inspeccion ----------------------------------------------------------
    QMap<QString, QByteArray> archivos() const
    {
        QMutexLocker l(&m_mutex);
        return m_archivos;
    }
    QStringList parciales() const
    {
        QMutexLocker l(&m_mutex);
        QStringList r;
        for (auto it = m_archivos.cbegin(); it != m_archivos.cend(); ++it) {
            if (it.key().endsWith(satcfdi::respaldo::nombres::kSufijoParcial)) {
                r.append(it.key());
            }
        }
        return r;
    }
    QStringList publicados() const
    {
        QMutexLocker l(&m_mutex);
        QStringList r;
        for (auto it = m_archivos.cbegin(); it != m_archivos.cend(); ++it) {
            if (it.key().endsWith(satcfdi::respaldo::nombres::kSufijoPublicado)) {
                r.append(it.key());
            }
        }
        r.sort();
        return r;
    }
    int validaciones() const
    {
        QMutexLocker l(&m_mutex);
        return m_validaciones;
    }
    int lecturasSinDescargar() const
    {
        QMutexLocker l(&m_mutex);
        return m_lecturasSinDescargar;
    }
    int publicaciones() const
    {
        QMutexLocker l(&m_mutex);
        return m_publicaciones;
    }

    // --- Puerto --------------------------------------------------------------
    satcfdi::Resultado<satcfdi::Exito, ErrorDestino> abrir() override
    {
        using R = satcfdi::Resultado<satcfdi::Exito, ErrorDestino>;
        QMutexLocker l(&m_mutex);
        if (falla == Falla::CarpetaInexistente) {
            return R::fallo(ErrorDestino::de(C::CarpetaInexistente, QStringLiteral("fake")));
        }
        if (falla == Falla::VolumenDesmontado) {
            return R::fallo(ErrorDestino::de(C::VolumenNoDisponible, QStringLiteral("fake")));
        }
        m_abierta = true;
        return R::exito(satcfdi::Exito{});
    }
    QString identidad() const override { return QStringLiteral("1:42"); }
    satcfdi::Resultado<satcfdi::Exito, ErrorDestino> comprobarIdentidad() override
    {
        using R = satcfdi::Resultado<satcfdi::Exito, ErrorDestino>;
        QMutexLocker l(&m_mutex);
        ++m_comprobaciones;
        if (falla == Falla::CarpetaCambiada || (fallarIdentidadTras >= 0 && m_comprobaciones > fallarIdentidadTras)) {
            return R::fallo(ErrorDestino::de(C::CarpetaCambiada, QStringLiteral("fake")));
        }
        return R::exito(satcfdi::Exito{});
    }

    satcfdi::Resultado<satcfdi::respaldo::EstadoDestino, ErrorDestino> validar(quint64 necesarios) override
    {
        using R = satcfdi::Resultado<satcfdi::respaldo::EstadoDestino, ErrorDestino>;
        QMutexLocker l(&m_mutex);
        ++m_validaciones;
        if (falla == Falla::SinEscritura) {
            return R::fallo(ErrorDestino::de(C::SinEscritura, QStringLiteral("fake")));
        }
        if (falla == Falla::SinEspacioAlValidar || bytesLibres < necesarios) {
            return R::fallo(ErrorDestino::de(C::EspacioInsuficiente, QStringLiteral("fake")));
        }
        satcfdi::respaldo::EstadoDestino e;
        e.bytesLibres = bytesLibres;
        return R::exito(e);
    }

    satcfdi::Resultado<std::unique_ptr<satcfdi::respaldo::ArchivoParcial>, ErrorDestino>
    crearParcial(const QString& propietario) override;

    satcfdi::Resultado<QList<EntradaDestino>, ErrorDestino> listar() override
    {
        QMutexLocker l(&m_mutex);
        QList<EntradaDestino> r;
        for (auto it = m_archivos.cbegin(); it != m_archivos.cend(); ++it) {
            r.append(entrada(it.key()));
        }
        return satcfdi::Resultado<QList<EntradaDestino>, ErrorDestino>::exito(r);
    }

    satcfdi::Resultado<std::unique_ptr<QIODevice>, ErrorDestino> abrirParaLeer(const QString& nombre) override
    {
        using R = satcfdi::Resultado<std::unique_ptr<QIODevice>, ErrorDestino>;
        QMutexLocker l(&m_mutex);
        if (m_enlaces.contains(nombre)) {
            return R::fallo(ErrorDestino::de(C::NoRegular, QStringLiteral("fake.enlace")));
        }
        if (!m_archivos.contains(nombre)) {
            return R::fallo(ErrorDestino::de(C::Lectura, QStringLiteral("fake.no_existe")));
        }
        if (m_sinDescargar.contains(nombre)) {
            ++m_lecturasSinDescargar; // la prueba exige que nunca ocurra
            return R::fallo(ErrorDestino::de(C::SinDescargar, QStringLiteral("carpeta.sin_descargar")));
        }
        if (lecturaSecuencial) {
            return R::exito(std::make_unique<LecturaSecuencial>(m_archivos.value(nombre)));
        }
        auto b = std::make_unique<QBuffer>();
        b->setData(m_archivos.value(nombre));
        b->open(QIODevice::ReadOnly);
        return R::exito(std::unique_ptr<QIODevice>(std::move(b)));
    }

    satcfdi::Resultado<satcfdi::Exito, ErrorDestino> borrar(const QString& nombre) override
    {
        using R = satcfdi::Resultado<satcfdi::Exito, ErrorDestino>;
        QMutexLocker l(&m_mutex);
        if (m_enlaces.contains(nombre)) {
            return R::fallo(ErrorDestino::de(C::NoRegular, QStringLiteral("fake.enlace")));
        }
        if (m_archivos.remove(nombre) == 0) {
            return R::fallo(ErrorDestino::de(C::Lectura, QStringLiteral("fake.no_existe")));
        }
        m_sinDescargar.remove(nombre);
        return R::exito(satcfdi::Exito{});
    }

    satcfdi::Resultado<int, ErrorDestino> limpiarParciales(const QString& propietario) override
    {
        QMutexLocker l(&m_mutex);
        int n = 0;
        for (const QString& k : m_archivos.keys()) {
            if (satcfdi::respaldo::nombres::esParcialDe(k, propietario)) {
                m_archivos.remove(k);
                ++n;
            }
        }
        return satcfdi::Resultado<int, ErrorDestino>::exito(n);
    }

    // --- Uso interno ---------------------------------------------------------
    EntradaDestino entrada(const QString& nombre) const
    {
        EntradaDestino e;
        e.nombre = nombre;
        e.tamano = static_cast<quint64>(m_archivos.value(nombre).size());
        e.dispositivo = 1;
        e.inodo = m_inodos.value(nombre);
        e.sinDescargar = m_sinDescargar.contains(nombre);
        return e;
    }

    mutable QMutex m_mutex;
    QMap<QString, QByteArray> m_archivos;
    QMap<QString, quint64> m_inodos;
    QSet<QString> m_enlaces;
    QSet<QString> m_sinDescargar;
    quint64 m_ultimoInodo = 1000;
    int m_validaciones = 0;
    int m_secuencia = 0;
    int m_lecturasSinDescargar = 0;
    int m_publicaciones = 0;
    int m_comprobaciones = 0;
    bool m_abierta = false;

    class LecturaSecuencial final : public QIODevice {
    public:
        explicit LecturaSecuencial(QByteArray datos) : m_datos(std::move(datos)) { open(QIODevice::ReadOnly); }
        bool isSequential() const override { return true; }
        qint64 bytesAvailable() const override { return m_datos.size() - m_pos + QIODevice::bytesAvailable(); }

    protected:
        qint64 readData(char* d, qint64 n) override
        {
            const qint64 t = std::min<qint64>(n, m_datos.size() - m_pos);
            std::memcpy(d, m_datos.constData() + m_pos, static_cast<std::size_t>(t));
            m_pos += t;
            return t;
        }
        qint64 writeData(const char*, qint64) override { return -1; }

    private:
        QByteArray m_datos;
        qint64 m_pos = 0;
    };
};

namespace detalle {

class DispositivoFake final : public QIODevice {
public:
    DispositivoFake(FakeDestinoRespaldo& d, QString nombre, std::optional<qint64> limite)
        : m_d(d), m_nombre(std::move(nombre)), m_limite(limite)
    {
    }
    bool isSequential() const override { return true; }
    bool fallo() const { return m_fallo; }

protected:
    qint64 readData(char*, qint64) override { return -1; }
    qint64 writeData(const char* datos, qint64 n) override
    {
        QMutexLocker l(&m_d.m_mutex);
        if (m_limite && m_escrito + n > *m_limite) {
            m_fallo = true;
            return -1;
        }
        m_d.m_archivos[m_nombre].append(datos, n);
        m_escrito += n;
        return n;
    }

private:
    FakeDestinoRespaldo& m_d;
    QString m_nombre;
    std::optional<qint64> m_limite;
    qint64 m_escrito = 0;
    bool m_fallo = false;
};

class ParcialFake final : public satcfdi::respaldo::ArchivoParcial {
public:
    using ErrorDestino = satcfdi::respaldo::ErrorDestino;
    using C = ErrorDestino::Categoria;

    ParcialFake(FakeDestinoRespaldo& d, QString nombre)
        : m_d(d), m_nombre(std::move(nombre)),
          m_dispositivo(d, m_nombre,
                        d.falla == FakeDestinoRespaldo::Falla::SinEspacioAlEscribir
                            ? std::optional<qint64>(d.limiteEscritura)
                            : std::nullopt)
    {
        m_dispositivo.open(QIODevice::WriteOnly | QIODevice::Unbuffered);
    }

    QString nombre() const override { return m_nombre; }
    bool enCarpetaDestino() const override { return true; }
    QIODevice& dispositivo() override { return m_dispositivo; }
    std::optional<ErrorDestino> errorEscritura() const override
    {
        if (m_dispositivo.fallo()) {
            return ErrorDestino::de(C::EspacioInsuficiente, QStringLiteral("fake.enospc"));
        }
        return std::nullopt;
    }
    satcfdi::Resultado<satcfdi::Exito, ErrorDestino> sincronizar() override
    {
        using R = satcfdi::Resultado<satcfdi::Exito, ErrorDestino>;
        if (m_d.alSincronizar) {
            m_d.alSincronizar();
        }
        if (m_d.falla == FakeDestinoRespaldo::Falla::Fsync) {
            return R::fallo(ErrorDestino::de(C::Sincronizacion, QStringLiteral("fake.fsync")));
        }
        return R::exito(satcfdi::Exito{});
    }
    satcfdi::Resultado<satcfdi::respaldo::EntradaDestino, ErrorDestino> publicar(const QString& final) override
    {
        using R = satcfdi::Resultado<satcfdi::respaldo::EntradaDestino, ErrorDestino>;
        satcfdi::respaldo::EntradaDestino e;
        {
            QMutexLocker l(&m_d.m_mutex);
            ++m_d.m_publicaciones;
            if (m_d.falla == FakeDestinoRespaldo::Falla::Rename) {
                return R::fallo(ErrorDestino::de(C::Publicacion, QStringLiteral("fake.rename")));
            }
            if (m_d.falla == FakeDestinoRespaldo::Falla::ExclusivoNoSoportado) {
                return R::fallo(ErrorDestino::de(C::ExclusivoNoSoportado, QStringLiteral("publicar.excl_no_soportado")));
            }
            if (m_d.m_archivos.contains(final) || m_d.m_enlaces.contains(final)) {
                return R::fallo(ErrorDestino::de(C::Conflicto, QStringLiteral("fake.existe")));
            }
            m_d.m_archivos.insert(final, m_d.m_archivos.take(m_nombre));
            m_d.m_inodos.insert(final, ++m_d.m_ultimoInodo);
            m_publicado = true;
            e = m_d.entrada(final);
        }
        if (m_d.alPublicar) {
            m_d.alPublicar(final);
        }
        return R::exito(e);
    }
    void descartar() override
    {
        QMutexLocker l(&m_d.m_mutex);
        if (!m_publicado) {
            m_d.m_archivos.remove(m_nombre);
        }
        m_publicado = true;
    }

private:
    FakeDestinoRespaldo& m_d;
    QString m_nombre;
    DispositivoFake m_dispositivo;
    bool m_publicado = false;
};

} // namespace detalle

inline satcfdi::Resultado<std::unique_ptr<satcfdi::respaldo::ArchivoParcial>, FakeDestinoRespaldo::ErrorDestino>
FakeDestinoRespaldo::crearParcial(const QString& propietario)
{
    using R = satcfdi::Resultado<std::unique_ptr<satcfdi::respaldo::ArchivoParcial>, ErrorDestino>;
    QString nombre;
    {
        QMutexLocker l(&m_mutex);
        nombre = satcfdi::respaldo::nombres::prefijoParcial(propietario)
            + QStringLiteral("%1").arg(++m_secuencia, 16, 16, QLatin1Char('0'))
            + satcfdi::respaldo::nombres::kSufijoParcial;
        m_archivos.insert(nombre, QByteArray());
    }
    return R::exito(std::make_unique<detalle::ParcialFake>(*this, nombre));
}

} // namespace fakes
