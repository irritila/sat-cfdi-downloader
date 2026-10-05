#pragma once

// PackageStorage en memoria para pruebas de T007 y T009 (T008). Usa la MISMA
// derivacion y gramatica que el adaptador real (ports/storage/RutaPaquete.h),
// consume la fuente por chunks y respeta las garantias del puerto: nunca
// sobrescribe ni borra finales, valida entradas sin efectos y solo elimina
// temporales propios.
//
// Thread-safe (QMutex): la prueba lo prepara en el hilo grafico y el
// OperacionExecutor lo usa en su hilo.
//
//   fakes::FakePackageStorage storage;
//   storage.programarFalla(ErrorAlmacenamiento::SinEspacio); // siguiente guardado
//   storage.agregarFinal(ruta, "bytes");                     // final preexistente
//   storage.agregarTemporal(rutaTemporal);                   // para el escaneo

#include "ports/PackageStorage.h"

#include <QHash>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QStringList>

#include <functional>
#include <optional>
#include <utility>

namespace fakes {

// Fuente por chunks en memoria. `fallarEnChunk` (1-based) devuelve un
// ErrorFuenteZip al pedir ese chunk; `alEntregar(k)` se invoca tras entregar
// el chunk k (1-based), p. ej. para solicitar una cancelacion en el chunk K.
class FuenteZipEnMemoria final : public satcfdi::FuenteZipPorChunks {
public:
    explicit FuenteZipEnMemoria(QList<QByteArray> chunks) : m_chunks(std::move(chunks)) {}

    int fallarEnChunk = 0;
    std::function<void(int)> alEntregar;

    int entregados() const { return m_siguiente; }

    satcfdi::Resultado<std::optional<QByteArray>, satcfdi::ErrorFuenteZip> siguiente() override
    {
        using R = satcfdi::Resultado<std::optional<QByteArray>, satcfdi::ErrorFuenteZip>;
        if (fallarEnChunk == m_siguiente + 1) {
            return R::fallo(satcfdi::ErrorFuenteZip{QStringLiteral("fuente interrumpida")});
        }
        if (m_siguiente >= m_chunks.size()) {
            return R::exito(std::nullopt);
        }
        const QByteArray chunk = m_chunks.at(m_siguiente++);
        if (alEntregar) {
            alEntregar(m_siguiente);
        }
        return R::exito(chunk);
    }

private:
    QList<QByteArray> m_chunks;
    int m_siguiente = 0;
};

class FakePackageStorage final : public satcfdi::PackageStorage {
public:
    using ErrorAlmacenamiento = satcfdi::ErrorAlmacenamiento;

    // --- Programacion -----------------------------------------------------------
    // Siguiente guardarAtomico falla con `error` (despues de consumir la fuente
    // hasta el final si `consumirFuente`), sin dejar final ni temporal.
    void programarFalla(ErrorAlmacenamiento error, bool consumirFuente = true)
    {
        QMutexLocker l(&m_mutex);
        m_fallaGuardado = error;
        m_consumirAntesDeFallar = consumirFuente;
    }
    // Siguiente guardado exitoso devuelve advertenciaDurabilidad = true (D7).
    void programarAdvertenciaDurabilidad()
    {
        QMutexLocker l(&m_mutex);
        m_advertencia = true;
    }
    // existeArchivoFinal / escanearRecuperacion fallan con `error` hasta limpiar.
    void programarFallaLectura(std::optional<ErrorAlmacenamiento> error)
    {
        QMutexLocker l(&m_mutex);
        m_fallaLectura = error;
    }
    void agregarFinal(const QString& rutaRelativa, const QByteArray& bytes = QByteArrayLiteral("ZIP"))
    {
        QMutexLocker l(&m_mutex);
        m_finales.insert(rutaRelativa, bytes);
    }
    void agregarTemporal(const QString& rutaRelativa)
    {
        QMutexLocker l(&m_mutex);
        m_temporales.insert(rutaRelativa);
    }

    // --- Observacion --------------------------------------------------------------
    std::optional<QByteArray> bytesFinal(const QString& rutaRelativa) const
    {
        QMutexLocker l(&m_mutex);
        return m_finales.contains(rutaRelativa) ? std::optional<QByteArray>(m_finales.value(rutaRelativa))
                                                : std::nullopt;
    }
    QStringList finales() const
    {
        QMutexLocker l(&m_mutex);
        return m_finales.keys();
    }
    QStringList temporales() const
    {
        QMutexLocker l(&m_mutex);
        QStringList t = m_temporales.values();
        t.sort();
        return t;
    }
    // Registro de llamadas: "guardar:<ruta>", "existe:<ruta>", "escanear",
    // "eliminarTemporal:<ruta>".
    QStringList llamadas() const
    {
        QMutexLocker l(&m_mutex);
        return m_llamadas;
    }

    // --- PackageStorage ------------------------------------------------------------
    satcfdi::Resultado<satcfdi::ArchivoFinal, satcfdi::ErrorGuardarZip>
    guardarAtomico(const satcfdi::UbicacionPaquete& ubicacion, satcfdi::FuenteZipPorChunks& fuente,
                   const satcfdi::Cancelacion& cancelacion) override
    {
        using R = satcfdi::Resultado<satcfdi::ArchivoFinal, satcfdi::ErrorGuardarZip>;
        using satcfdi::ErrorGuardarZip;
        const auto rel = derivarRutaRelativa(ubicacion);
        QMutexLocker l(&m_mutex);
        m_llamadas.append(QStringLiteral("guardar:") + (rel ? rel.valor() : QStringLiteral("<invalida>")));
        if (!rel) {
            return R::fallo(ErrorGuardarZip::deAlmacenamiento(ErrorAlmacenamiento::EntradaInvalida));
        }
        if (m_finales.contains(rel.valor())) {
            return R::fallo(ErrorGuardarZip::deAlmacenamiento(ErrorAlmacenamiento::ColisionDestino));
        }
        const std::optional<ErrorAlmacenamiento> falla = std::exchange(m_fallaGuardado, std::nullopt);
        if (falla && !m_consumirAntesDeFallar) {
            return R::fallo(ErrorGuardarZip::deAlmacenamiento(*falla));
        }
        QByteArray bytes;
        for (;;) {
            if (cancelacion.solicitada()) {
                return R::fallo(ErrorGuardarZip::deAlmacenamiento(ErrorAlmacenamiento::Cancelada));
            }
            l.unlock(); // la fuente puede bloquear o cancelar desde otro hilo
            auto chunk = fuente.siguiente();
            l.relock();
            if (!chunk) {
                return R::fallo(ErrorGuardarZip::deFuente(chunk.error().diagnostico));
            }
            if (!chunk.valor()) {
                break;
            }
            bytes.append(*chunk.valor());
        }
        if (falla) {
            return R::fallo(ErrorGuardarZip::deAlmacenamiento(*falla));
        }
        if (m_finales.contains(rel.valor())) { // colision durante la escritura
            return R::fallo(ErrorGuardarZip::deAlmacenamiento(ErrorAlmacenamiento::ColisionDestino));
        }
        m_finales.insert(rel.valor(), bytes);
        return R::exito(satcfdi::ArchivoFinal{rel.valor(), std::exchange(m_advertencia, false)});
    }

    satcfdi::Resultado<bool, ErrorAlmacenamiento> existeArchivoFinal(const QString& rutaRelativa) override
    {
        using R = satcfdi::Resultado<bool, ErrorAlmacenamiento>;
        QMutexLocker l(&m_mutex);
        m_llamadas.append(QStringLiteral("existe:") + rutaRelativa);
        if (!satcfdi::rutapaquete::esRutaFinalValida(rutaRelativa)) {
            return R::fallo(ErrorAlmacenamiento::EntradaInvalida);
        }
        if (m_fallaLectura) {
            return R::fallo(*m_fallaLectura);
        }
        return R::exito(m_finales.contains(rutaRelativa));
    }

    satcfdi::Resultado<QList<satcfdi::HallazgoFilesystem>, ErrorAlmacenamiento> escanearRecuperacion() override
    {
        using R = satcfdi::Resultado<QList<satcfdi::HallazgoFilesystem>, ErrorAlmacenamiento>;
        namespace rp = satcfdi::rutapaquete;
        QMutexLocker l(&m_mutex);
        m_llamadas.append(QStringLiteral("escanear"));
        if (m_fallaLectura) {
            return R::fallo(*m_fallaLectura);
        }
        QMap<QString, satcfdi::HallazgoFilesystem> ordenados;
        auto agregar = [&](const QString& ruta, bool esFinal) {
            const auto c = rp::separarRutaPropia(ruta);
            if (!c) {
                return;
            }
            satcfdi::HallazgoFilesystem h;
            h.rutaRelativa = ruta;
            h.solicitudId = *satcfdi::SolicitudId::desdeTexto(c->solicitudId);
            if (esFinal && rp::esNombreFinal(c->nombre)) {
                h.tipo = satcfdi::TipoHallazgo::Final;
                h.archivoFinal = c->nombre;
            } else if (const auto asociado = rp::finalDeTemporal(c->nombre); !esFinal && asociado) {
                h.tipo = satcfdi::TipoHallazgo::Temporal;
                h.archivoFinal = *asociado;
            } else {
                return; // ajeno
            }
            ordenados.insert(ruta, h);
        };
        for (auto it = m_finales.cbegin(); it != m_finales.cend(); ++it) {
            agregar(it.key(), true);
        }
        for (const QString& t : m_temporales) {
            agregar(t, false);
        }
        return R::exito(ordenados.values());
    }

    satcfdi::Resultado<satcfdi::Exito, ErrorAlmacenamiento>
    eliminarTemporal(const satcfdi::HallazgoFilesystem& temporal) override
    {
        using R = satcfdi::Resultado<satcfdi::Exito, ErrorAlmacenamiento>;
        namespace rp = satcfdi::rutapaquete;
        QMutexLocker l(&m_mutex);
        m_llamadas.append(QStringLiteral("eliminarTemporal:") + temporal.rutaRelativa);
        const auto c = rp::separarRutaPropia(temporal.rutaRelativa);
        const auto asociado = c ? rp::finalDeTemporal(c->nombre) : std::nullopt;
        if (temporal.tipo != satcfdi::TipoHallazgo::Temporal || !asociado || *asociado != temporal.archivoFinal
            || c->solicitudId != temporal.solicitudId.texto()) {
            return R::fallo(ErrorAlmacenamiento::EntradaInvalida);
        }
        m_temporales.remove(temporal.rutaRelativa);
        return R::exito(satcfdi::Exito{});
    }

private:
    mutable QMutex m_mutex;
    QMap<QString, QByteArray> m_finales;
    QSet<QString> m_temporales;
    QStringList m_llamadas;
    std::optional<ErrorAlmacenamiento> m_fallaGuardado;
    bool m_consumirAntesDeFallar = true;
    bool m_advertencia = false;
    std::optional<ErrorAlmacenamiento> m_fallaLectura;
};

} // namespace fakes
