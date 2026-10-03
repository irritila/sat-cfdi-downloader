#pragma once

#include <QDateTime>
#include <QHashFunctions>
#include <QString>
#include <QStringView>
#include <QtGlobal>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

QT_BEGIN_NAMESPACE
class QDebug;
QT_END_NAMESPACE

namespace satcfdi {

// Tipos del puerto SecretStore (T005, DA1; ADR 0006, ADR 0010).
//
// Reglas generales:
// - Los tipos SENSIBLES (BufferSecreto, EntradaEFirma, MaterialFirma,
//   CredencialPreparada) son move-only, no tienen conversion a QString,
//   QByteArray, QVariant ni QDebug (operator<< borrado) y no deben cruzar QML,
//   senales encoladas, QFuture ni lambdas capturadas por copia.
// - Ningun tipo de este archivo depende de OpenSSL ni de Security.framework.
// - Los tipos NO sensibles (CredencialRef, MetadataCredencial, EstadoCredencial,
//   ErrorSecretStore, ResumenReconciliacion) son copiables y no contienen
//   rutas, RFC ni secretos.

namespace secretos {

// Sobrescribe `n` bytes con cero de forma que el compilador no lo elimine
// (memset_s en Apple, escritura volatil en otras plataformas). BEST EFFORT:
// no cubre copias hechas por el allocator, swap, crash dumps, bibliotecas
// externas (OpenSSL, CFData) ni registros. Reducir copias y vida util sigue
// siendo responsabilidad del llamador.
void limpiarMemoria(void* datos, std::size_t n) noexcept;

} // namespace secretos

// Buffer propio de bytes sensibles: move-only y limpiado (best effort) al
// destruirse, al limpiar() y al ser asignado por movimiento. Tras un movimiento
// el origen queda vacio. Sin conversiones de salida: el acceso es solo por
// puntero + tamano para pasarlo a la API que lo consume.
class BufferSecreto {
public:
    BufferSecreto() noexcept = default;
    // Reserva `tamano` bytes inicializados a cero.
    explicit BufferSecreto(std::size_t tamano);
    ~BufferSecreto();

    BufferSecreto(const BufferSecreto&) = delete;
    BufferSecreto& operator=(const BufferSecreto&) = delete;
    BufferSecreto(BufferSecreto&& otro) noexcept;
    BufferSecreto& operator=(BufferSecreto&& otro) noexcept;

    // Copia `tamano` bytes de `datos` (el llamador limpia su origen si es suyo).
    static BufferSecreto desdeBytes(const void* datos, std::size_t tamano);
    // Codifica texto UTF-16 a UTF-8 directamente en el buffer, sin QByteArray
    // temporal. Suplentes UTF-16 sueltos se codifican como U+FFFD. Pensado para
    // la contrasena capturada en presentacion; el llamador debe soltar su
    // QString lo antes posible (QString no garantiza borrado).
    static BufferSecreto desdeTexto(QStringView texto);

    std::size_t tamano() const noexcept { return m_tamano; }
    bool vacio() const noexcept { return m_tamano == 0; }
    const std::uint8_t* datos() const noexcept { return m_datos; }
    std::uint8_t* datos() noexcept { return m_datos; }

    // Comparacion en tiempo constante respecto al contenido (no al tamano).
    bool igualA(const void* datos, std::size_t tamano) const noexcept;

    // Limpia y libera; deja el buffer vacio.
    void limpiar() noexcept;

private:
    std::uint8_t* m_datos = nullptr;
    std::size_t m_tamano = 0;
};

QDebug operator<<(QDebug, const BufferSecreto&) = delete;

// Rol de cada una de las tres referencias persistidas en credencial_sat.
enum class RolReferencia {
    Certificado, // certificado_ref   -> "scs1:<uuid>:cert"
    Contenedor,  // llave_privada_ref -> "scs1:<uuid>:container"
    Contrasena,  // contrasena_ref    -> "scs1:<uuid>:password"
};

// Identidad opaca de una GENERACION de credencial (UUID canonico v4). No
// contiene ruta, RFC ni secreto; copiable. Las tres referencias de SQLite se
// derivan de ella con el esquema versionado `scs1:<uuid>:<rol>`. Como
// ninguna referencia empieza por '/' ni contiene separadores de ruta, nunca
// es una ruta absoluta. El adaptador deriva internamente nombres de archivo e
// items Keychain a partir del UUID.
class CredencialRef {
public:
    CredencialRef() = default; // nula

    static CredencialRef generar();
    // Solo UUID canonico (minusculas, sin llaves).
    static std::optional<CredencialRef> desdeUuid(QStringView uuid);
    // Una referencia `scs1:<uuid>:<rol>` con el rol esperado.
    static std::optional<CredencialRef> desdeReferencia(QStringView referencia,
                                                        RolReferencia rolEsperado);
    // Las tres referencias de una fila: esquema scs1, roles correctos y UUID
    // comun. Cualquier discrepancia -> nullopt.
    static std::optional<CredencialRef> desdeReferencias(QStringView certificadoRef,
                                                         QStringView llavePrivadaRef,
                                                         QStringView contrasenaRef);

    bool esNula() const noexcept { return m_uuid.isEmpty(); }
    const QString& uuid() const noexcept { return m_uuid; }

    QString referencia(RolReferencia rol) const;
    QString referenciaCertificado() const { return referencia(RolReferencia::Certificado); }
    QString referenciaContenedor() const { return referencia(RolReferencia::Contenedor); }
    QString referenciaContrasena() const { return referencia(RolReferencia::Contrasena); }

    friend bool operator==(const CredencialRef&, const CredencialRef&) = default;
    friend size_t qHash(const CredencialRef& ref, size_t seed = 0) noexcept
    {
        return ::qHash(ref.m_uuid, seed);
    }

private:
    explicit CredencialRef(QString uuid) : m_uuid(std::move(uuid)) {}

    QString m_uuid;
};

// Metadata NO secreta del certificado, persistida en credencial_sat (002).
// `numeroSerie`: representacion canonica no vacia que fija el adaptador
// (ASCII imprimible, max 64). Vigencia UTC: notBefore <= ahora < notAfter.
struct MetadataCredencial {
    QString numeroSerie;
    QDateTime vigenteDesde; // notBefore, UTC
    QDateTime vigenteHasta; // notAfter, UTC

    friend bool operator==(const MetadataCredencial&, const MetadataCredencial&) = default;
};

// Entrada de importacion. Las rutas son de LECTURA EFIMERA (el adaptador lee
// .cer/.key durante prepararEFirma y no las conserva, persiste ni registra).
// La contrasena va en BufferSecreto. Move-only.
struct EntradaEFirma {
    QString rutaCertificado;
    QString rutaLlavePrivada;
    BufferSecreto contrasena;
};

QDebug operator<<(QDebug, const EntradaEFirma&) = delete;

// Material de firma para UNA operacion (T009): certificado DER, llave privada
// PKCS#8 DER YA DESCIFRADA y contrasena. Move-only, sin conversiones; cada
// buffer se limpia al destruirse. No se persiste ni se entrega fuera de la
// operacion de trabajo que lo pidio.
class MaterialFirma {
public:
    MaterialFirma(BufferSecreto certificadoDer, BufferSecreto llavePrivadaDer,
                  BufferSecreto contrasena) noexcept
        : m_certificadoDer(std::move(certificadoDer))
        , m_llavePrivadaDer(std::move(llavePrivadaDer))
        , m_contrasena(std::move(contrasena))
    {
    }

    MaterialFirma(const MaterialFirma&) = delete;
    MaterialFirma& operator=(const MaterialFirma&) = delete;
    MaterialFirma(MaterialFirma&&) noexcept = default;
    MaterialFirma& operator=(MaterialFirma&&) noexcept = default;
    ~MaterialFirma() = default;

    const BufferSecreto& certificadoDer() const noexcept { return m_certificadoDer; }
    const BufferSecreto& llavePrivadaDer() const noexcept { return m_llavePrivadaDer; }
    const BufferSecreto& contrasena() const noexcept { return m_contrasena; }

private:
    BufferSecreto m_certificadoDer;
    BufferSecreto m_llavePrivadaDer;
    BufferSecreto m_contrasena;
};

QDebug operator<<(QDebug, const MaterialFirma&) = delete;

// Generacion candidata ya validada y escrita por el adaptador, aun NO activa.
// RAII: si se destruye (o se reasigna) sin confirmar(), invoca el descartador
// inyectado por el adaptador, que elimina la generacion (best effort; si falla
// la reconciliacion la limpia despues). confirmar() se llama SOLO despues del
// COMMIT que activa la referencia en SQLite. Move-only.
//
// Hilo: el descartador corre en el hilo que destruye el objeto (dispatcher o
// ejecutor serial). Debe ser noexcept en la practica; las excepciones se
// suprimen.
class CredencialPreparada {
public:
    using Descartador = std::function<void(const CredencialRef&)>;

    CredencialPreparada() = default; // vacia: sin generacion
    CredencialPreparada(CredencialRef referencia, MetadataCredencial metadata,
                        Descartador descartador);
    ~CredencialPreparada();

    CredencialPreparada(const CredencialPreparada&) = delete;
    CredencialPreparada& operator=(const CredencialPreparada&) = delete;
    CredencialPreparada(CredencialPreparada&& otra) noexcept;
    CredencialPreparada& operator=(CredencialPreparada&& otra) noexcept;

    const CredencialRef& referencia() const noexcept { return m_referencia; }
    const MetadataCredencial& metadata() const noexcept { return m_metadata; }
    bool vacia() const noexcept { return m_referencia.esNula(); }
    bool confirmada() const noexcept { return m_confirmada; }

    // Desarma el descarte: la generacion queda activa.
    void confirmar() noexcept { m_confirmada = true; }
    // Descarta ahora si no se confirmo (idempotente).
    void descartar() noexcept;

private:
    CredencialRef m_referencia;
    MetadataCredencial m_metadata;
    Descartador m_descartador;
    bool m_confirmada = false;
};

QDebug operator<<(QDebug, const CredencialPreparada&) = delete;

// Estados cerrados de credencial (T005). `SinCredencial` y `Validando` los
// produce la aplicacion (sin fila / importacion en curso); el adaptador
// devuelve solo Lista, Vencida, NoVigenteAun, MaterialFaltante o
// MaterialDanado.
enum class EstadoCredencial {
    SinCredencial,
    Validando,
    Lista,
    Vencida,
    NoVigenteAun,
    MaterialFaltante,
    MaterialDanado,
};

inline constexpr std::array<EstadoCredencial, 7> kEstadosCredencial = {
    EstadoCredencial::SinCredencial, EstadoCredencial::Validando,
    EstadoCredencial::Lista,         EstadoCredencial::Vencida,
    EstadoCredencial::NoVigenteAun,  EstadoCredencial::MaterialFaltante,
    EstadoCredencial::MaterialDanado,
};

// Clave estable PascalCase (logs y pruebas; la UI traduce).
QString claveEstable(EstadoCredencial estado);

// Origen NO sensible de un error de importacion (T005.1, DA4): que entrada
// del formulario lo provoco, para enfocar el control correcto. Nunca incluye
// la ruta ni el contenido. Regla del adaptador:
// - ArchivoIlegible / FormatoInvalido del .cer -> Certificado;
// - ArchivoIlegible / FormatoInvalido de la .key -> Llave;
// - ContrasenaIncorrecta -> Contrasena;
// - cualquier otra categoria -> Ninguno.
enum class OrigenErrorEFirma {
    Ninguno,
    Certificado,
    Llave,
    Contrasena,
};

QString claveEstable(OrigenErrorEFirma origen);

// Error del puerto. Catalogo CERRADO de 17 categorias visibles (T005).
// `diagnostico`: texto tecnico saneado SOLO para logs sanitizados (p. ej.
// "keychain.copy", "pkcs8.decrypt"); nunca contiene rutas, RFC, contrasena,
// DER, base64 ni hex de material. `codigoNativo`: OSStatus o codigo OpenSSL.
// La UI y los mensajes visibles usan solo `categoria`.
struct ErrorSecretStore {
    enum class Categoria {
        ArchivoIlegible,
        FormatoInvalido,
        ContrasenaIncorrecta,
        ParejaIncompatible,
        RfcNoCoincide,
        NoEsEFirma,
        Vencida,
        NoVigenteAun,
        CredencialNoEncontrada,
        CredencialDanada,
        AlmacenBloqueado,
        AccesoDenegado,
        CanceladoPorUsuario,
        AlmacenMalConfigurado,
        AlmacenNoDisponible,
        FalloEscritura,
        Interno,
    };

    Categoria categoria = Categoria::Interno;
    QString diagnostico;
    std::optional<int> codigoNativo;
    // T005.1: solo significativo en prepararEFirma (ver OrigenErrorEFirma).
    OrigenErrorEFirma origen = OrigenErrorEFirma::Ninguno;

    ErrorSecretStore conOrigen(OrigenErrorEFirma nuevo) const&
    {
        ErrorSecretStore e = *this;
        e.origen = nuevo;
        return e;
    }
    ErrorSecretStore conOrigen(OrigenErrorEFirma nuevo) &&
    {
        origen = nuevo;
        return std::move(*this);
    }

    static ErrorSecretStore de(Categoria categoria, QString diagnostico = {},
                               std::optional<int> codigoNativo = std::nullopt)
    {
        ErrorSecretStore e;
        e.categoria = categoria;
        e.diagnostico = std::move(diagnostico);
        e.codigoNativo = codigoNativo;
        return e;
    }
};

inline constexpr std::array<ErrorSecretStore::Categoria, 17> kCategoriasErrorSecretStore = {
    ErrorSecretStore::Categoria::ArchivoIlegible,
    ErrorSecretStore::Categoria::FormatoInvalido,
    ErrorSecretStore::Categoria::ContrasenaIncorrecta,
    ErrorSecretStore::Categoria::ParejaIncompatible,
    ErrorSecretStore::Categoria::RfcNoCoincide,
    ErrorSecretStore::Categoria::NoEsEFirma,
    ErrorSecretStore::Categoria::Vencida,
    ErrorSecretStore::Categoria::NoVigenteAun,
    ErrorSecretStore::Categoria::CredencialNoEncontrada,
    ErrorSecretStore::Categoria::CredencialDanada,
    ErrorSecretStore::Categoria::AlmacenBloqueado,
    ErrorSecretStore::Categoria::AccesoDenegado,
    ErrorSecretStore::Categoria::CanceladoPorUsuario,
    ErrorSecretStore::Categoria::AlmacenMalConfigurado,
    ErrorSecretStore::Categoria::AlmacenNoDisponible,
    ErrorSecretStore::Categoria::FalloEscritura,
    ErrorSecretStore::Categoria::Interno,
};

QString claveEstable(ErrorSecretStore::Categoria categoria);

// Resultado de SecretStore::reconciliar().
struct ResumenReconciliacion {
    int generacionesConservadas = 0; // presentes en `vigentes`
    int generacionesEliminadas = 0;  // huerfanas eliminadas
    int fallosLimpieza = 0;          // huerfanas que no se pudieron eliminar

    friend bool operator==(const ResumenReconciliacion&, const ResumenReconciliacion&) = default;
};

} // namespace satcfdi
