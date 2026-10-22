#pragma once

// Puerto del almacen local de KEK del respaldo (T025.4; T025 D4, D12, D16;
// ADR 0010, ADR 0021). Guarda en el almacen seguro del sistema (Keychain en
// macOS: data protection keychain, no sincronizable,
// AfterFirstUnlockThisDeviceOnly) la KEK YA DERIVADA, con sus ParametrosKdf
// (incluida la sal), para que el respaldo diario no pida la contrasena. Nunca
// guarda la contrasena ni la clave de recuperacion formateada.
//
// Admite los dos tipos de KekRespaldo: el escritor AFB1 envuelve la DEK con
// la KEK de contrasena Y con la de recuperacion, asi que un respaldo sin
// intervencion del usuario necesita ambas (D16: cambiar la contrasena
// conserva la de recuperacion).
//
// Ambito: cada almacen opera SOLO sobre las KEK de un ambito, el id de
// instalacion (UUID canonico de `respaldo/idInstalacion`). Dos instalaciones
// en el mismo usuario (otro --data-dir) no ven ni borran las KEK de la otra,
// tampoco al reconciliar con listar()/borrar(). Un ambito vacio, no canonico o
// nulo no opera: toda llamada devuelve AlmacenMalConfigurado con diagnostico
// "ambito.invalido".
//
// Referencias: cada guardar() crea un item NUEVO con una referencia opaca
// (UUID v4 canonico) y nunca sobrescribe. Asi el cambio de contrasena es
// seguro ante una caida: guardar la nueva, persistir su referencia en
// preferencias y solo despues borrar la anterior. listar() permite al
// llamador reconciliar items huerfanos.
//
// Errores: ErrorAlmacenKek con categoria y diagnostico ASCII estable (la
// operacion) mas el codigo nativo (OSStatus) cuando lo hay; nunca la clave,
// la sal, la referencia ni nombres de servicio.
//
// Hilo: todas las llamadas bloquean (Keychain). SOLO fuera del hilo grafico
// (hilo del respaldo o del dispatcher). Reentrante.

#include "domain/common/Resultado.h"
#include "ports/persistence/Exito.h"
#include "ports/respaldo/ContenedorRespaldo.h"

#include <QList>
#include <QString>
#include <QStringView>
#include <QUuid>

#include <optional>
#include <utility>

namespace satcfdi::respaldo {

// Identidad opaca de una KEK guardada (UUID v4 canonico: minusculas, sin
// llaves). No contiene datos del usuario; se puede persistir en
// preferencias.ini. Copiable.
class ReferenciaKek {
public:
    ReferenciaKek() = default; // nula

    static ReferenciaKek generar()
    {
        ReferenciaKek r;
        r.m_uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        return r;
    }

    // Solo UUID canonico (36 caracteres, minusculas, sin llaves).
    static std::optional<ReferenciaKek> desdeTexto(QStringView texto)
    {
        const QUuid uuid = QUuid::fromString(texto);
        if (uuid.isNull() || texto.size() != 36) {
            return std::nullopt;
        }
        const QString canonico = uuid.toString(QUuid::WithoutBraces);
        if (canonico != texto) {
            return std::nullopt;
        }
        ReferenciaKek r;
        r.m_uuid = canonico;
        return r;
    }

    bool nula() const noexcept { return m_uuid.isEmpty(); }
    const QString& texto() const noexcept { return m_uuid; }

    friend bool operator==(const ReferenciaKek&, const ReferenciaKek&) = default;

private:
    QString m_uuid;
};

struct ErrorAlmacenKek {
    enum class Categoria {
        NoEncontrada,          // no hay item con esa referencia
        Danada,                // el item existe pero no es una KEK AFB1 valida
        KekInvalida,           // argumento: KEK vacia o de contrasena con parametros invalidos
        ReferenciaInvalida,    // argumento: referencia nula
        AlmacenBloqueado,      // Keychain bloqueado (antes del primer desbloqueo, dark wake)
        AccesoDenegado,
        AlmacenMalConfigurado, // binario sin firma/entitlements (errSecMissingEntitlement) o ambito invalido
        AlmacenNoDisponible,
        FalloEscritura,
        Interno,
    };

    Categoria categoria = Categoria::Interno;
    QString diagnostico;               // ASCII estable (p. ej. "keychain.copiar")
    std::optional<int> codigoNativo;   // OSStatus, si aplica

    static ErrorAlmacenKek de(Categoria categoria, QString diagnostico = {},
                              std::optional<int> codigoNativo = std::nullopt)
    {
        ErrorAlmacenKek e;
        e.categoria = categoria;
        e.diagnostico = std::move(diagnostico);
        e.codigoNativo = codigoNativo;
        return e;
    }

    bool operator==(const ErrorAlmacenKek&) const = default;
};

// Ambito valido: UUID canonico (minusculas, sin llaves) distinto del nulo.
inline bool esAmbitoKekValido(QStringView ambito)
{
    const QUuid uuid = QUuid::fromString(ambito);
    return ambito.size() == 36 && !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == ambito;
}

inline ErrorAlmacenKek errorAmbitoInvalido()
{
    return ErrorAlmacenKek::de(ErrorAlmacenKek::Categoria::AlmacenMalConfigurado, QStringLiteral("ambito.invalido"));
}

class AlmacenKekRespaldo {
public:
    virtual ~AlmacenKekRespaldo() = default;

    // Guarda `kek` (contrasena con parametros validos, o recuperacion) en un
    // item nuevo y devuelve su referencia. Errores: KekInvalida, categorias
    // del almacen.
    virtual Resultado<ReferenciaKek, ErrorAlmacenKek> guardar(const KekRespaldo& kek) = 0;

    // Reconstruye la KEK (tipo, parametros con sal y clave). Errores:
    // ReferenciaInvalida, NoEncontrada, Danada, categorias del almacen.
    virtual Resultado<KekRespaldo, ErrorAlmacenKek> cargar(const ReferenciaKek& referencia) = 0;

    // Borra el item. Idempotente: si no existia, exito. Errores:
    // ReferenciaInvalida, categorias del almacen.
    virtual Resultado<Exito, ErrorAlmacenKek> borrar(const ReferenciaKek& referencia) = 0;

    // Referencias guardadas en el ambito de este almacen (para reconciliar
    // huerfanas). Ignora cuentas que no son referencias canonicas.
    virtual Resultado<QList<ReferenciaKek>, ErrorAlmacenKek> listar() = 0;
};

} // namespace satcfdi::respaldo
