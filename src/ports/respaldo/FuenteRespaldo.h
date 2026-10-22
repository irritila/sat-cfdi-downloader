#pragma once

// Puerto de captura consistente para el respaldo (T025.2; T025 D3, D7, D10,
// D11, D14). Una captura congela, en un staging privado, todo lo que entra a
// un respaldo, y se puede verificar sin el contenedor de T025.1:
//   - copia de SQLite hecha con VACUUM INTO dentro del OperacionExecutor (D10);
//   - lista de ZIP de paquetes Descargado con ruta, leida de ESA copia;
//   - preferencias por lista blanca, sin la seccion `respaldo` ni rutas (D14);
//   - metadatos (version de esquema de la copia y version de la app).
// Nunca incluye `credentials/`, Keychain, logs de aplicacion ni temporales
// (D3, D7).

#include "domain/common/Resultado.h"
#include "ports/respaldo/ContenedorRespaldo.h"
#include "ports/persistence/Exito.h"

#include <QByteArray>
#include <QDateTime>
#include <QFuture>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <optional>
#include <utility>

namespace satcfdi::respaldo {

// Rutas fijas dentro del respaldo.
inline const QString kRutaRespaldoBase = QStringLiteral("base/satcfdi.sqlite3");
inline const QString kRutaRespaldoPreferencias = QStringLiteral("preferencias/preferencias.ini");
inline const QString kPrefijoRespaldoPaquetes = QStringLiteral("paquetes/");

// Nombres dentro del staging de una captura.
inline const QString kNombreCopiaBase = QStringLiteral("satcfdi.sqlite3");
inline const QString kNombrePreferencias = QStringLiteral("preferencias.ini");

struct ErrorCaptura {
    enum class Categoria {
        EjecutorNoDisponible, // el ejecutor rechazo la operacion (detenido)
        Staging,              // no se pudo crear o validar el staging privado
        Copia,                // VACUUM INTO fallo
        Integridad,           // integrity_check de la copia distinto de "ok"
        LecturaCopia,         // no se pudo abrir o leer la copia
        Preferencias,         // no se pudo leer o escribir el ini filtrado
        Interno,
    };

    Categoria categoria = Categoria::Interno;
    QString diagnostico; // ASCII estable, sin rutas, RFC ni contenido

    static ErrorCaptura de(Categoria c, QString diagnostico = {})
    {
        ErrorCaptura e;
        e.categoria = c;
        e.diagnostico = std::move(diagnostico);
        return e;
    }

    bool operator==(const ErrorCaptura&) const = default;
};

inline QString claveEstable(ErrorCaptura::Categoria c)
{
    using C = ErrorCaptura::Categoria;
    switch (c) {
    case C::EjecutorNoDisponible: return QStringLiteral("ejecutor_no_disponible");
    case C::Staging: return QStringLiteral("staging");
    case C::Copia: return QStringLiteral("copia");
    case C::Integridad: return QStringLiteral("integridad");
    case C::LecturaCopia: return QStringLiteral("lectura_copia");
    case C::Preferencias: return QStringLiteral("preferencias");
    case C::Interno: return QStringLiteral("interno");
    }
    return QStringLiteral("interno");
}

// Entrada inmutable: que copiar (`raiz` absoluta + `rutaRelativa`) y donde va
// dentro del respaldo. Base y preferencias tienen raiz = staging; los paquetes,
// la raiz efectiva de paquetes (ADR 0004) y su ruta_local.
//
// `huella`: lstat de la fuente al congelar la captura (D11). Sin huella, el
// paquete ya faltaba al congelar: se anota como faltante (advertencia). Con
// huella, una fuente que despues cambia o desaparece hace fallar el respaldo.
struct EntradaCaptura {
    enum class Tipo { Base, Preferencias, Paquete };

    Tipo tipo = Tipo::Paquete;
    QString rutaEnRespaldo;
    QString raiz;
    QString rutaRelativa;
    std::optional<HuellaArchivo> huella;

    bool faltabaAlCongelar() const noexcept { return !huella.has_value(); }

    bool operator==(const EntradaCaptura&) const = default;
};

struct MetadatosCaptura {
    static constexpr int kFormato = 1;

    int versionEsquema = 0; // MAX(schema_migrations.version) de la copia
    QString versionApp;
    QDateTime capturadaEnUtc;

    // JSON compacto y estable: {"formato":1,"esquema":N,"app":"x.y.z","capturada":"...Z"}.
    QByteArray serializar() const;
    static std::optional<MetadatosCaptura> desdeBytes(const QByteArray& json);

    bool operator==(const MetadatosCaptura&) const = default;
};

struct CapturaRespaldo {
    QString staging; // absoluto, privado (0700); lo borra FuenteRespaldo::limpiar
    QList<EntradaCaptura> entradas; // base, preferencias (si hay) y paquetes
    MetadatosCaptura metadatos;
    int paquetesOmitidos = 0; // ruta_local que no cumple ADR 0004 (no se copian)
    qint64 pausaEjecutorMs = 0; // duracion de la operacion del ejecutor (VACUUM INTO)

    int faltantesAlCongelar() const
    {
        int n = 0;
        for (const auto& e : entradas) {
            n += e.faltabaAlCongelar() ? 1 : 0;
        }
        return n;
    }

    int paquetes() const
    {
        int n = 0;
        for (const auto& e : entradas) {
            n += e.tipo == EntradaCaptura::Tipo::Paquete ? 1 : 0;
        }
        return n;
    }
};

class FuenteRespaldo {
public:
    virtual ~FuenteRespaldo() = default;

    // Crea un staging privado nuevo, encola la copia en el ejecutor (detras de
    // la operacion en curso) y, fuera del ejecutor, comprueba la copia y arma
    // la captura. Ante cualquier error borra el staging antes de completar. El
    // future se completa en un hilo del pool; continuar con then(contexto).
    virtual QFuture<Resultado<CapturaRespaldo, ErrorCaptura>> capturar() = 0;

    // Borra el staging de `captura` (idempotente). Se llama al terminar el
    // respaldo, con exito o sin el.
    virtual Resultado<Exito, ErrorCaptura> limpiar(const CapturaRespaldo& captura) = 0;

    // Al arrancar: borra stagings de capturas anteriores (proceso terminado a
    // mitad de un respaldo). Devuelve cuantos borro. No llamar con una
    // captura en curso (D18: un respaldo a la vez; al arrancar no hay otro).
    virtual int limpiarHuerfanos() = 0;
};

// --- Puertos de infraestructura (implementados sobre SQLite) -----------------

// Copia consistente de la base de trabajo. Se ejecuta en el HILO DEL EJECUTOR
// con la conexion de ese hilo, sin transaccion abierta (D10, ADR 0016).
class CopiaBaseDatos {
public:
    virtual ~CopiaBaseDatos() = default;
    // VACUUM INTO `rutaDestino` (absoluta, inexistente). Error Copia.
    virtual Resultado<Exito, ErrorCaptura> copiarA(const QString& rutaDestino) = 0;
};

struct ContenidoCopia {
    int versionEsquema = 0;
    // ruta_local de paquetes Descargado con ruta, no eliminados, de
    // solicitudes no eliminadas, en orden estable.
    QStringList rutasPaquetes;
};

// Lectura de una copia ya hecha, con una conexion propia abierta y cerrada en
// el hilo que llama (cualquiera menos el grafico). Deja la copia en modo
// journal DELETE (sin -wal ni -shm) y exige integrity_check = "ok".
class LectorCopiaBaseDatos {
public:
    virtual ~LectorCopiaBaseDatos() = default;
    virtual Resultado<ContenidoCopia, ErrorCaptura> leer(const QString& rutaCopia) = 0;
};

} // namespace satcfdi::respaldo
