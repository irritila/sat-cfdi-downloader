#pragma once

// Puerto del contenedor de respaldo cifrado AFB1 (T025.1; T025 D2, D6, D9,
// D12, D16, D17; ADR 0021). Sin tipos de OpenSSL: la implementacion vive en
// infrastructure/crypto/ContenedorRespaldoOpenSsl.
//
// El contenedor no sabe de SQLite, paquetes ni carpetas de destino: recibe
// rutas relativas ya decididas por la captura (T025.2) y escribe/lee un
// QIODevice que el llamador abre y cierra (T025.3 maneja `.partial`,
// F_FULLFSYNC y el rename; T025.5 el staging).
//
// Hilo: ninguna operacion es para el hilo grafico (Argon2id tarda y reserva
// 64 MiB; el streaming procesa GB). Una instancia de ContenedorRespaldo no
// tiene estado mutable y puede usarse desde cualquier hilo; un
// EscritorRespaldo se usa desde un solo hilo.
//
// Errores: ErrorRespaldo lleva una categoria y un diagnostico ASCII estable
// (p. ej. "bloque.tag"). Nunca contiene rutas, nombres de archivo, RFC,
// contrasenas, claves ni contenido. Una contrasena o clave de recuperacion
// incorrecta (o una cabecera alterada, que no se puede distinguir sin la
// clave) produce siempre CredencialIncorrecta.

#include "domain/common/Resultado.h"
#include "ports/persistence/Exito.h"
#include "ports/secrets/SecretStoreTypes.h"
#include "ports/storage/TiposAlmacenamiento.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QUuid>
#include <QtGlobal>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

QT_BEGIN_NAMESPACE
class QIODevice;
QT_END_NAMESPACE

namespace satcfdi::respaldo {

inline constexpr std::size_t kTamanoSal = 16;
inline constexpr std::size_t kTamanoKek = 32;
inline constexpr std::size_t kTamanoClaveRecuperacion = 32;
inline constexpr std::size_t kTamanoIdRespaldo = 16;
inline constexpr std::size_t kTamanoSha256 = 32;
inline constexpr std::size_t kTamanoBloque = 1024 * 1024;
inline constexpr qsizetype kMaxMetadatos = 1024 * 1024;
inline constexpr qsizetype kMaxBytesRuta = 1024;
inline constexpr qsizetype kMaxBytesComponente = 255;
// C2: limites por respaldo. Superarlos da LimiteExcedido en escritor y lector.
// El manifiesto vive en memoria mientras se escribe o verifica, asi que su
// tope acota la memoria del pipeline junto con los bloques de 1 MiB.
inline constexpr quint32 kMaxEntradas = 100'000;
inline constexpr qsizetype kMaxBytesManifiesto = 16 * 1024 * 1024;
// Carpetas distintas entre todas las rutas (acota el indice de choques con
// rutas hostiles de muchos componentes).
inline constexpr quint32 kMaxCarpetas = 2 * kMaxEntradas;

// Parametros Argon2id (D9). Los valores por omision son los minimos
// aceptados; la sal es fija por contrasena configurada (D12) y no es secreta.
struct ParametrosKdf {
    static constexpr quint32 kMemoriaKibMinima = 64 * 1024; // 64 MiB
    static constexpr quint32 kMemoriaKibMaxima = 256 * 1024; // 256 MiB (techo anti-DoS al leer)
    static constexpr quint32 kIteracionesMinimas = 3;
    static constexpr quint32 kIteracionesMaximas = 8;
    static constexpr quint32 kParalelismoMinimo = 1;
    static constexpr quint32 kParalelismoMaximo = 4;

    quint32 memoriaKib = kMemoriaKibMinima;
    quint32 iteraciones = kIteracionesMinimas;
    quint32 paralelismo = kParalelismoMinimo;
    QByteArray sal; // exactamente kTamanoSal bytes

    static ParametrosKdf predeterminados(QByteArray sal)
    {
        ParametrosKdf p;
        p.sal = std::move(sal);
        return p;
    }

    bool validos() const noexcept
    {
        return memoriaKib >= kMemoriaKibMinima && memoriaKib <= kMemoriaKibMaxima
            && iteraciones >= kIteracionesMinimas && iteraciones <= kIteracionesMaximas
            && paralelismo >= kParalelismoMinimo && paralelismo <= kParalelismoMaximo
            && sal.size() == static_cast<qsizetype>(kTamanoSal);
    }

    bool operator==(const ParametrosKdf&) const = default;
};

struct ErrorRespaldo {
    enum class Categoria {
        KdfNoDisponible,             // OpenSSL sin Argon2id (< 3.2 o proveedor ausente)
        ParametrosInvalidos,         // argumento del llamador (contrasena vacia, kek de otro tipo...)
        ParametrosKdfInvalidos,      // Argon2id fuera de los rangos de ParametrosKdf
        CredencialIncorrecta,        // contrasena o clave de recuperacion incorrecta (generico)
        ClaveRecuperacionMalFormada, // texto que no es una clave AFB1 (longitud, alfabeto, control)
        FormatoNoReconocido,         // no es AFB1 o cabecera invalida
        VersionNoSoportada,          // AFB1 de una version/suite que esta app no conoce
        Danado,                      // bloque alterado, truncado, reordenado, hash o manifiesto incoherente
        RutaInvalida,                // ruta relativa hostil o duplicada; entrada que no es archivo regular
        FuenteInvalida,              // fuente que es enlace, no regular o no abrible
        FuenteFaltante,              // la fuente desaparecio al abrirla
        FuenteCambio,                // la fuente cambio o desaparecio durante la lectura (D11)
        LimiteExcedido,              // > kMaxEntradas, manifiesto > kMaxBytesManifiesto o > kMaxCarpetas
        Lectura,                     // E/S al leer el contenedor
        Escritura,                   // E/S al escribir el contenedor o extraer
        DestinoInvalido,             // staging inexistente, enlace, ajeno, no 0700 o no vacio
        Cancelado,
        Interno,                     // OpenSSL/aleatorio
    };

    Categoria categoria = Categoria::Interno;
    QString diagnostico; // ASCII estable, sin datos sensibles

    static ErrorRespaldo de(Categoria categoria, QString diagnostico = {})
    {
        ErrorRespaldo e;
        e.categoria = categoria;
        e.diagnostico = std::move(diagnostico);
        return e;
    }

    bool operator==(const ErrorRespaldo&) const = default;
};

// Clave estable de la categoria ("credencial_incorrecta", "danado", ...).
inline QString claveEstable(ErrorRespaldo::Categoria c)
{
    using C = ErrorRespaldo::Categoria;
    switch (c) {
    case C::KdfNoDisponible: return QStringLiteral("kdf_no_disponible");
    case C::ParametrosInvalidos: return QStringLiteral("parametros_invalidos");
    case C::ParametrosKdfInvalidos: return QStringLiteral("parametros_kdf_invalidos");
    case C::CredencialIncorrecta: return QStringLiteral("credencial_incorrecta");
    case C::ClaveRecuperacionMalFormada: return QStringLiteral("clave_recuperacion_mal_formada");
    case C::FormatoNoReconocido: return QStringLiteral("formato_no_reconocido");
    case C::VersionNoSoportada: return QStringLiteral("version_no_soportada");
    case C::Danado: return QStringLiteral("danado");
    case C::RutaInvalida: return QStringLiteral("ruta_invalida");
    case C::FuenteInvalida: return QStringLiteral("fuente_invalida");
    case C::FuenteFaltante: return QStringLiteral("fuente_faltante");
    case C::FuenteCambio: return QStringLiteral("fuente_cambio");
    case C::LimiteExcedido: return QStringLiteral("limite_excedido");
    case C::Lectura: return QStringLiteral("lectura");
    case C::Escritura: return QStringLiteral("escritura");
    case C::DestinoInvalido: return QStringLiteral("destino_invalido");
    case C::Cancelado: return QStringLiteral("cancelado");
    case C::Interno: return QStringLiteral("interno");
    }
    return QStringLiteral("interno");
}

// Clave de cifrado de claves (KEK) de 32 bytes, move-only. Una KEK de
// contrasena lleva los ParametrosKdf (no secretos) con los que se derivo, que
// el escritor copia a la cabecera; una de recuperacion no lleva parametros.
// T025.4 la guarda en Keychain: clave() expone los bytes y desdeBytes() la
// reconstruye junto con sus parametros.
class KekRespaldo {
public:
    enum class Tipo { Contrasena, Recuperacion };

    KekRespaldo() = default;
    KekRespaldo(KekRespaldo&&) noexcept = default;
    KekRespaldo& operator=(KekRespaldo&&) noexcept = default;
    KekRespaldo(const KekRespaldo&) = delete;
    KekRespaldo& operator=(const KekRespaldo&) = delete;

    // nullopt-equivalente: devuelve una KEK vacia (valida() == false) si el
    // tamano no es kTamanoKek o si una KEK de contrasena trae parametros
    // invalidos.
    static KekRespaldo desdeBytes(Tipo tipo, ParametrosKdf parametros, BufferSecreto clave)
    {
        KekRespaldo k;
        if (clave.tamano() != kTamanoKek || (tipo == Tipo::Contrasena && !parametros.validos())) {
            return k;
        }
        k.m_tipo = tipo;
        k.m_parametros = tipo == Tipo::Contrasena ? std::move(parametros) : ParametrosKdf{};
        k.m_clave = std::move(clave);
        return k;
    }

    bool valida() const noexcept { return m_clave.tamano() == kTamanoKek; }
    Tipo tipo() const noexcept { return m_tipo; }
    const ParametrosKdf& parametros() const noexcept { return m_parametros; }
    const BufferSecreto& clave() const noexcept { return m_clave; }

private:
    Tipo m_tipo = Tipo::Contrasena;
    ParametrosKdf m_parametros;
    BufferSecreto m_clave;
};

// Clave de recuperacion (D6, D16): 32 bytes aleatorios, move-only. Se muestra
// al usuario en forma imprimible (ContenedorRespaldo::formatearClaveRecuperacion)
// y no se guarda en el equipo; la app solo conserva su KEK derivada.
class ClaveRecuperacion {
public:
    ClaveRecuperacion() = default;
    explicit ClaveRecuperacion(BufferSecreto bytes) : m_bytes(std::move(bytes)) {}
    ClaveRecuperacion(ClaveRecuperacion&&) noexcept = default;
    ClaveRecuperacion& operator=(ClaveRecuperacion&&) noexcept = default;

    bool valida() const noexcept { return m_bytes.tamano() == kTamanoClaveRecuperacion; }
    const BufferSecreto& bytes() const noexcept { return m_bytes; }

private:
    BufferSecreto m_bytes;
};

// Cabecera en claro (D9, D17). Se puede leer sin clave (retencion y KDF), pero
// solo queda AUTENTICADA (los 204 bytes, con ambas envolturas) tras
// desenvolver la DEK y autenticar el bloque 0, cuya AAD la incluye
// (autenticarCabecera, verificar, extraer): un archivo ajeno puede imitarla.
struct CabeceraRespaldo {
    quint8 version = 0;
    QUuid idInstalacion;
    QByteArray idRespaldo; // kTamanoIdRespaldo bytes aleatorios
    QDateTime creadoEnUtc; // milisegundos, UTC
    ParametrosKdf kdf;
    quint32 tamanoBloque = 0;

    bool operator==(const CabeceraRespaldo&) const = default;
};

struct EntradaManifiesto {
    enum class Estado { Presente, Faltante };

    QString rutaRelativa;
    Estado estado = Estado::Presente;
    quint64 tamano = 0;
    QByteArray sha256; // kTamanoSha256 bytes; vacio si Faltante

    bool operator==(const EntradaManifiesto&) const = default;
};

// Resumen de un respaldo. No lleva la lista de entradas para que la memoria
// no crezca con su numero (C2): quien la necesite la recibe entrada por
// entrada con OpcionesLectura::alLeerEntrada.
struct InventarioRespaldo {
    CabeceraRespaldo cabecera;
    QByteArray metadatos; // opacos para el contenedor (los define T025.2)
    quint32 presentes = 0;
    quint32 faltantes = 0;
    quint64 bytesDatos = 0; // suma de tamanos de entradas Presente

    quint32 entradas() const noexcept { return presentes + faltantes; }
};

struct DatosNuevoRespaldo {
    QUuid idInstalacion;   // D17; no nulo
    QDateTime creadoEnUtc; // valido; se guarda en ms UTC
    QByteArray metadatos;  // <= kMaxMetadatos; van cifrados
};

// Bytes de datos de entradas procesados hasta ahora (escritura o lectura).
using ProgresoRespaldo = std::function<void(quint64 bytesProcesados)>;

// Lectura (verificar/extraer). `alLeerEntrada` recibe cada entrada en orden
// del flujo, ya comprobada contra su SHA-256 pero ANTES de autenticar el
// manifiesto y el fin del contenedor: lo recibido solo es valido si la
// llamada termina con exito.
struct OpcionesLectura {
    Cancelacion cancelacion;
    ProgresoRespaldo progreso;
    std::function<void(const EntradaManifiesto&)> alLeerEntrada;
};

// Reglas de ruta relativa dentro del respaldo (ADR 0004 y ADR 0021). Pura.
// Acepta "a/b/c.zip": componentes no vacios separados por '/', sin '/' inicial
// ni final, sin "//", sin componentes que empiecen por '.' (cubre "." y ".."),
// ni por '~', sin '\\', ':', NUL ni controles, UTF-8 <= kMaxBytesRuta y cada
// componente <= kMaxBytesComponente, en forma NFC. Defensa en profundidad de
// D3: el primer componente no puede ser "credentials" (sin distinguir
// mayusculas).
inline bool esRutaRelativaValida(QStringView ruta)
{
    if (ruta.isEmpty() || ruta.toUtf8().size() > kMaxBytesRuta) {
        return false;
    }
    const QString texto = ruta.toString();
    if (texto.normalized(QString::NormalizationForm_C) != texto) {
        return false;
    }
    for (const QChar c : ruta) {
        const char16_t u = c.unicode();
        if (u < 0x20 || u == 0x7F || u == u'\\' || u == u':' || c.category() == QChar::Other_Control) {
            return false;
        }
    }
    const QStringList componentes = texto.split(QLatin1Char('/'));
    for (const QString& comp : componentes) {
        if (comp.isEmpty() || comp.startsWith(QLatin1Char('.')) || comp.startsWith(QLatin1Char('~'))
            || comp.toUtf8().size() > kMaxBytesComponente) {
            return false;
        }
    }
    if (componentes.constFirst().compare(QLatin1String("credentials"), Qt::CaseInsensitive) == 0) {
        return false;
    }
    // Suplentes sueltos: toUtf8 los convierte en U+FFFD; se rechazan.
    return QString::fromUtf8(texto.toUtf8()) == texto;
}

// T025.2 (D11): huella de una fuente al congelar la captura. Si se pasa a
// agregarArchivo y el archivo abierto no coincide (tamano, mtime en ns, inodo,
// dispositivo), la fuente cambio despues de congelarse: FuenteCambio.
struct HuellaArchivo {
    quint64 tamano = 0;
    qint64 mtimeNs = 0;
    quint64 inodo = 0;
    quint64 dispositivo = 0;

    bool operator==(const HuellaArchivo&) const = default;
};

// Escritor en streaming de un respaldo. Lo crea ContenedorRespaldo::crearEscritor,
// que ya escribio la cabecera. Orden: agregar*/anotarFaltante en cualquier
// combinacion y finalizar() una vez. Tras el primer error el escritor queda
// fallido: toda llamada posterior devuelve ese error y el destino debe
// descartarse (T025.3 borra el `.partial`). Nada de lo escrito es valido hasta
// que finalizar() tiene exito.
class EscritorRespaldo {
public:
    virtual ~EscritorRespaldo() = default;

    // Copia el archivo regular `rutaRelativaFuente` bajo `raizFuente`
    // (absoluta). Se abre componente a componente desde el descriptor de la
    // raiz: la raiz con O_DIRECTORY|O_NOFOLLOW, cada intermedio con
    // openat(O_DIRECTORY|O_NOFOLLOW) y el archivo con openat(O_NOFOLLOW|
    // O_NONBLOCK). Un enlace en la raiz, en un ancestro o en el archivo nunca se
    // sigue (FuenteInvalida); un FIFO o dispositivo tampoco. Si cambian tamano,
    // mtime o inodo durante la lectura, o desaparece, FuenteCambio (D11). Si no
    // existe al abrirla, FuenteFaltante (el llamador decide si anota un
    // faltante). Con `esperada` (T025.2), una fuente cuya huella al abrirla no
    // coincide da FuenteCambio ("fuente.huella"). Errores previos a escribir
    // (ruta, fuente, huella, limite) no dejan el escritor fallido.
    virtual Resultado<Exito, ErrorRespaldo> agregarArchivo(const QString& rutaRelativa, const QString& raizFuente,
                                                           const QString& rutaRelativaFuente,
                                                           const Cancelacion& cancelacion = {},
                                                           const std::optional<HuellaArchivo>& esperada = std::nullopt) = 0;

    // Entrada pequena desde memoria (p. ej. preferencias filtradas, T025.2).
    virtual Resultado<Exito, ErrorRespaldo> agregarDatos(const QString& rutaRelativa, const QByteArray& datos) = 0;

    // ZIP referenciado que ya faltaba al congelar la lista (D11).
    virtual Resultado<Exito, ErrorRespaldo> anotarFaltante(const QString& rutaRelativa) = 0;

    // Escribe el manifiesto y el bloque final. No hace fsync ni cierra el
    // destino (responsabilidad del llamador).
    virtual Resultado<InventarioRespaldo, ErrorRespaldo> finalizar() = 0;
};

class ContenedorRespaldo {
public:
    virtual ~ContenedorRespaldo() = default;

    // D12: Argon2id disponible en el OpenSSL enlazado (EVP_KDF_fetch). Se
    // comprueba tambien dentro de cada derivacion.
    virtual Resultado<Exito, ErrorRespaldo> comprobarDisponible() const = 0;

    // Sal aleatoria de kTamanoSal bytes para una contrasena nueva (D12).
    virtual Resultado<QByteArray, ErrorRespaldo> generarSal() const = 0;

    // Argon2id(contrasena, parametros) -> KEK de contrasena. Se deriva una vez
    // por contrasena configurada y se reutiliza (T025.4 la guarda en Keychain).
    // Errores: ParametrosInvalidos (contrasena vacia), ParametrosKdfInvalidos,
    // KdfNoDisponible, Interno.
    virtual Resultado<KekRespaldo, ErrorRespaldo> derivarKekContrasena(const BufferSecreto& contrasena,
                                                                       const ParametrosKdf& parametros) const = 0;

    // D6: clave de recuperacion nueva (CSPRNG).
    virtual Resultado<ClaveRecuperacion, ErrorRespaldo> generarClaveRecuperacion() const = 0;

    // Forma imprimible: 56 caracteres Crockford Base32 (32 bytes de clave + 3
    // de control) en 14 grupos de 4 separados por '-'. ASCII en BufferSecreto.
    virtual Resultado<BufferSecreto, ErrorRespaldo> formatearClaveRecuperacion(const ClaveRecuperacion& clave) const = 0;

    // Inverso tolerante: ignora espacios y '-', acepta minusculas y O/I/L como
    // 0/1/1. Error ClaveRecuperacionMalFormada si la longitud, el alfabeto o el
    // control no cuadran (detecta errores de captura, no si la clave es la
    // correcta para un respaldo).
    virtual Resultado<ClaveRecuperacion, ErrorRespaldo> leerClaveRecuperacion(const BufferSecreto& texto) const = 0;

    // HKDF-SHA256 de la clave de recuperacion -> KEK de recuperacion.
    virtual Resultado<KekRespaldo, ErrorRespaldo> derivarKekRecuperacion(const ClaveRecuperacion& clave) const = 0;

    // Genera DEK, id de respaldo y prefijo de nonce aleatorios, envuelve la DEK
    // con ambas KEK (D16) y escribe la cabecera en `destino` (abierto para
    // escritura; el escritor no lo posee y debe vivir menos que el).
    virtual Resultado<std::unique_ptr<EscritorRespaldo>, ErrorRespaldo>
    crearEscritor(QIODevice& destino, const KekRespaldo& kekContrasena, const KekRespaldo& kekRecuperacion,
                  DatosNuevoRespaldo datos, ProgresoRespaldo progreso = {}) const = 0;

    // Lee y valida la cabecera en claro desde el inicio de `origen`, sin clave.
    virtual Resultado<CabeceraRespaldo, ErrorRespaldo> leerCabecera(QIODevice& origen) const = 0;

    // Desenvuelve la DEK con `kek` y autentica el bloque 0 (como mucho 1 MiB),
    // cuya AAD incluye los 204 bytes de cabecera: prueba que la cabecera
    // completa (ambas envolturas incluidas) es autentica y que la KEK abre el
    // respaldo. NO prueba que el resto del archivo este completo: una copia
    // truncada conserva una cabecera autentica. La retencion (D17, T025.3)
    // deduplica por idRespaldo y nunca borra por un id repetido.
    virtual Resultado<CabeceraRespaldo, ErrorRespaldo> autenticarCabecera(QIODevice& origen,
                                                                          const KekRespaldo& kek) const = 0;

    // Recorre y autentica TODO el contenedor (cada bloque con su marco, orden,
    // bloque final, fin de archivo real, hash de cada entrada y manifiesto) sin
    // escribir nada. `origen` puede ser secuencial: lecturas parciales o en
    // cero se completan con waitForReadyRead; el fin solo se acepta si el
    // dispositivo lo confirma (read() en 0 y waitForReadyRead() falso).
    virtual Resultado<InventarioRespaldo, ErrorRespaldo> verificar(QIODevice& origen, const KekRespaldo& kek,
                                                                   const OpcionesLectura& opciones = {}) const = 0;

    // Igual que verificar() y ademas escribe cada entrada Presente bajo
    // `staging`: un directorio PRIVADO validado por descriptor (abierto con
    // O_DIRECTORY|O_NOFOLLOW; fstat: dueno = euid y modo 0700; vacio al
    // enumerarlo sobre el descriptor). Escribe sin seguir enlaces (openat +
    // O_NOFOLLOW + O_EXCL, carpetas 0700, archivos 0600) y comprueba cada
    // archivo contra su SHA-256 al terminarlo. Las entradas se escriben ANTES
    // de autenticar el manifiesto y el fin: el contenido del staging NO es
    // confiable mientras la llamada no devuelva exito, y T025.5 nunca lo
    // promueve sin ese exito. Ante cualquier error vacia el staging recorriendo
    // cada carpeta por su descriptor (unlinkat sobre el descriptor del padre) y
    // devuelve el error. No hace fsync.
    virtual Resultado<InventarioRespaldo, ErrorRespaldo> extraer(QIODevice& origen, const KekRespaldo& kek,
                                                                 const QString& staging,
                                                                 const OpcionesLectura& opciones = {}) const = 0;
};

} // namespace satcfdi::respaldo
