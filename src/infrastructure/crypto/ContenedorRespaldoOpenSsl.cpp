#include "infrastructure/crypto/ContenedorRespaldoOpenSsl.h"

#include "infrastructure/crypto/ContenedorRespaldoFormato.h"

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QSet>
#include <QTimeZone>

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/rand.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

namespace satcfdi::crypto {

using respaldo::CabeceraRespaldo;
using respaldo::ClaveRecuperacion;
using respaldo::DatosNuevoRespaldo;
using respaldo::EntradaManifiesto;
using respaldo::ErrorRespaldo;
using respaldo::EscritorRespaldo;
using respaldo::InventarioRespaldo;
using respaldo::KekRespaldo;
using respaldo::ParametrosKdf;
using respaldo::ProgresoRespaldo;

namespace {

using C = ErrorRespaldo::Categoria;
using Cabecera = std::array<std::uint8_t, afb1::kTamanoCabecera>;
using ResultadoVacio = Resultado<Exito, ErrorRespaldo>;

constexpr std::size_t kTamanoChunkFuente = 256 * 1024;
constexpr std::uint8_t kRolContrasena = 1;
constexpr std::uint8_t kRolRecuperacion = 2;
constexpr char kInfoHkdfRecuperacion[] = "AFB1 kek recuperacion v1";
constexpr char kEtiquetaControlRecuperacion[] = "AFB1 control recuperacion v1";
constexpr char kAlfabetoCrockford[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr std::size_t kBytesControlRecuperacion = 3;
constexpr std::size_t kCaracteresRecuperacion = 56; // (32 + 3) * 8 / 5

ErrorRespaldo error(C categoria, const char* diagnostico)
{
    ERR_clear_error();
    return ErrorRespaldo::de(categoria, QString::fromLatin1(diagnostico));
}

ResultadoVacio ok()
{
    return ResultadoVacio::exito(Exito{});
}

ResultadoVacio falla(C categoria, const char* diagnostico)
{
    return ResultadoVacio::fallo(error(categoria, diagnostico));
}

struct LiberarCtx {
    void operator()(EVP_CIPHER_CTX* p) const noexcept { EVP_CIPHER_CTX_free(p); }
};
using CtxPtr = std::unique_ptr<EVP_CIPHER_CTX, LiberarCtx>;

struct LiberarMd {
    void operator()(EVP_MD_CTX* p) const noexcept { EVP_MD_CTX_free(p); }
};
using MdPtr = std::unique_ptr<EVP_MD_CTX, LiberarMd>;

struct LiberarKdf {
    void operator()(EVP_KDF* p) const noexcept { EVP_KDF_free(p); }
};
using KdfPtr = std::unique_ptr<EVP_KDF, LiberarKdf>;

struct LiberarKdfCtx {
    void operator()(EVP_KDF_CTX* p) const noexcept { EVP_KDF_CTX_free(p); }
};
using KdfCtxPtr = std::unique_ptr<EVP_KDF_CTX, LiberarKdfCtx>;

class Descriptor {
public:
    explicit Descriptor(int fd = -1) noexcept : m_fd(fd) {}
    ~Descriptor() { cerrar(); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    Descriptor(Descriptor&& o) noexcept : m_fd(std::exchange(o.m_fd, -1)) {}
    Descriptor& operator=(Descriptor&& o) noexcept
    {
        if (this != &o) {
            cerrar();
            m_fd = std::exchange(o.m_fd, -1);
        }
        return *this;
    }
    int fd() const noexcept { return m_fd; }
    bool valido() const noexcept { return m_fd >= 0; }
    bool cerrar() noexcept
    {
        if (m_fd < 0) {
            return true;
        }
        const int r = ::close(m_fd);
        m_fd = -1;
        return r == 0;
    }

private:
    int m_fd;
};

// --- Enteros big-endian. ---------------------------------------------------

void poner16(std::uint8_t* p, std::uint16_t v)
{
    p[0] = static_cast<std::uint8_t>(v >> 8);
    p[1] = static_cast<std::uint8_t>(v);
}

void poner32(std::uint8_t* p, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        p[i] = static_cast<std::uint8_t>(v >> (24 - 8 * i));
    }
}

void poner64(std::uint8_t* p, std::uint64_t v)
{
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<std::uint8_t>(v >> (56 - 8 * i));
    }
}

std::uint16_t tomar16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>((std::uint16_t{p[0]} << 8) | std::uint16_t{p[1]});
}

std::uint32_t tomar32(const std::uint8_t* p)
{
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v = (v << 8) | std::uint32_t{p[i]};
    }
    return v;
}

std::uint64_t tomar64(const std::uint8_t* p)
{
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | std::uint64_t{p[i]};
    }
    return v;
}

void agregar8(QByteArray& b, std::uint8_t v)
{
    b.append(static_cast<char>(v));
}

void agregar16(QByteArray& b, std::uint16_t v)
{
    std::uint8_t t[2];
    poner16(t, v);
    b.append(reinterpret_cast<const char*>(t), 2);
}

void agregar32(QByteArray& b, std::uint32_t v)
{
    std::uint8_t t[4];
    poner32(t, v);
    b.append(reinterpret_cast<const char*>(t), 4);
}

void agregar64(QByteArray& b, std::uint64_t v)
{
    std::uint8_t t[8];
    poner64(t, v);
    b.append(reinterpret_cast<const char*>(t), 8);
}

// --- Aleatorio, SHA-256. ---------------------------------------------------

bool aleatorio(std::uint8_t* destino, std::size_t n)
{
    return n == 0 || RAND_bytes(destino, static_cast<int>(n)) == 1;
}

class Sha256 {
public:
    Sha256() : m_ctx(EVP_MD_CTX_new())
    {
        m_ok = m_ctx && EVP_DigestInit_ex(m_ctx.get(), EVP_sha256(), nullptr) == 1;
    }
    void actualizar(const void* datos, std::size_t n)
    {
        m_ok = m_ok && (n == 0 || EVP_DigestUpdate(m_ctx.get(), datos, n) == 1);
    }
    // nullopt si OpenSSL fallo en cualquier punto.
    std::optional<std::array<std::uint8_t, respaldo::kTamanoSha256>> finalizar()
    {
        std::array<std::uint8_t, respaldo::kTamanoSha256> salida{};
        unsigned int n = 0;
        if (!m_ok || EVP_DigestFinal_ex(m_ctx.get(), salida.data(), &n) != 1 || n != salida.size()) {
            return std::nullopt;
        }
        return salida;
    }

private:
    MdPtr m_ctx;
    bool m_ok = false;
};

// --- E/S QIODevice. --------------------------------------------------------

constexpr int kEsperaLecturaMs = 30'000;
constexpr int kMaxEsperasSinDatos = 100'000;

bool escribirTodo(QIODevice& destino, const void* datos, std::size_t n)
{
    const char* p = static_cast<const char*>(datos);
    std::size_t escrito = 0;
    while (escrito < n) {
        const qint64 r = destino.write(p + escrito, static_cast<qint64>(n - escrito));
        if (r <= 0) {
            return false;
        }
        escrito += static_cast<std::size_t>(r);
    }
    return true;
}

enum class EstadoLectura { Completa, FinDeArchivo, Error };

// C1: lee exactamente `n` bytes. En un dispositivo de acceso aleatorio,
// read() en 0 es fin de archivo. En uno secuencial, una lectura parcial o en
// 0 se reintenta tras waitForReadyRead(); el fin solo se acepta cuando read()
// devuelve 0, waitForReadyRead() falla y no quedan bytes disponibles. Nunca se
// usa atEnd() para decidir el fin. `leidos` recibe lo leido en cualquier caso.
EstadoLectura leerExacto(QIODevice& origen, void* destino, std::size_t n, std::size_t* leidos = nullptr)
{
    char* p = static_cast<char*>(destino);
    std::size_t total = 0;
    int esperas = 0;
    EstadoLectura estado = EstadoLectura::Completa;
    while (total < n) {
        const qint64 r = origen.read(p + total, static_cast<qint64>(n - total));
        if (r < 0) {
            estado = EstadoLectura::Error;
            break;
        }
        if (r > 0) {
            total += static_cast<std::size_t>(r);
            esperas = 0;
            continue;
        }
        if (!origen.isSequential()) {
            estado = EstadoLectura::FinDeArchivo;
            break;
        }
        if (++esperas > kMaxEsperasSinDatos) {
            estado = EstadoLectura::Error;
            break;
        }
        if (!origen.waitForReadyRead(kEsperaLecturaMs) && origen.bytesAvailable() <= 0) {
            estado = EstadoLectura::FinDeArchivo;
            break;
        }
    }
    if (leidos) {
        *leidos = total;
    }
    return estado;
}

// --- KDF. ------------------------------------------------------------------

KdfPtr argon2id()
{
    return KdfPtr(EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr));
}

// --- Cabecera y envolturas. ------------------------------------------------

struct CabeceraPreparada {
    Cabecera bytes{};
    BufferSecreto dek;
};

bool aesGcm(bool cifrar, EVP_CIPHER_CTX* ctx, const std::uint8_t* nonce, const std::uint8_t* aad,
            std::size_t largoAad, const std::uint8_t* entrada, std::size_t largo, std::uint8_t* salida,
            std::uint8_t* tag)
{
    int n = 0;
    if (cifrar) {
        if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, nullptr, nonce) != 1
            || EVP_EncryptUpdate(ctx, nullptr, &n, aad, static_cast<int>(largoAad)) != 1
            || (largo > 0 && EVP_EncryptUpdate(ctx, salida, &n, entrada, static_cast<int>(largo)) != 1)
            || (largo > 0 && static_cast<std::size_t>(n) != largo)) {
            return false;
        }
        int final = 0;
        return EVP_EncryptFinal_ex(ctx, salida + largo, &final) == 1 && final == 0
            && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, static_cast<int>(afb1::kTamanoTag), tag) == 1;
    }
    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, nullptr, nonce) != 1
        || EVP_DecryptUpdate(ctx, nullptr, &n, aad, static_cast<int>(largoAad)) != 1
        || (largo > 0 && EVP_DecryptUpdate(ctx, salida, &n, entrada, static_cast<int>(largo)) != 1)
        || (largo > 0 && static_cast<std::size_t>(n) != largo)
        || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(afb1::kTamanoTag), tag) != 1) {
        return false;
    }
    int final = 0;
    return EVP_DecryptFinal_ex(ctx, salida + largo, &final) == 1 && final == 0;
}

CtxPtr contextoGcm(bool cifrar, const BufferSecreto& clave)
{
    CtxPtr ctx(EVP_CIPHER_CTX_new());
    if (!ctx) {
        return {};
    }
    const bool ok = cifrar
        ? EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
            && EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(afb1::kTamanoNonce),
                                   nullptr) == 1
            && EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, clave.datos(), nullptr) == 1
        : EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
            && EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(afb1::kTamanoNonce),
                                   nullptr) == 1
            && EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, clave.datos(), nullptr) == 1;
    return ok ? std::move(ctx) : CtxPtr{};
}

std::array<std::uint8_t, afb1::kOffsetEnvolturas + 1> aadEnvoltura(const Cabecera& cabecera, std::uint8_t rol)
{
    std::array<std::uint8_t, afb1::kOffsetEnvolturas + 1> aad{};
    std::memcpy(aad.data(), cabecera.data(), afb1::kOffsetEnvolturas);
    aad[afb1::kOffsetEnvolturas] = rol;
    return aad;
}

bool envolver(const KekRespaldo& kek, const BufferSecreto& dek, Cabecera& cabecera, std::uint8_t rol)
{
    std::uint8_t* envoltura = cabecera.data() + afb1::kOffsetEnvolturas
        + (rol == kRolContrasena ? 0 : afb1::kTamanoEnvoltura);
    if (!aleatorio(envoltura, afb1::kTamanoNonce)) {
        return false;
    }
    CtxPtr ctx = contextoGcm(true, kek.clave());
    const auto aad = aadEnvoltura(cabecera, rol);
    return ctx
        && aesGcm(true, ctx.get(), envoltura, aad.data(), aad.size(), dek.datos(), dek.tamano(),
                  envoltura + afb1::kTamanoNonce, envoltura + afb1::kTamanoNonce + afb1::kTamanoDek);
}

Resultado<BufferSecreto, ErrorRespaldo> desenvolver(const KekRespaldo& kek, const Cabecera& cabecera)
{
    using R = Resultado<BufferSecreto, ErrorRespaldo>;
    const std::uint8_t rol = kek.tipo() == KekRespaldo::Tipo::Contrasena ? kRolContrasena : kRolRecuperacion;
    const std::uint8_t* envoltura = cabecera.data() + afb1::kOffsetEnvolturas
        + (rol == kRolContrasena ? 0 : afb1::kTamanoEnvoltura);
    CtxPtr ctx = contextoGcm(false, kek.clave());
    if (!ctx) {
        return R::fallo(error(C::Interno, "openssl.ctx"));
    }
    BufferSecreto dek(afb1::kTamanoDek);
    std::array<std::uint8_t, afb1::kTamanoTag> tag{};
    std::memcpy(tag.data(), envoltura + afb1::kTamanoNonce + afb1::kTamanoDek, tag.size());
    const auto aad = aadEnvoltura(cabecera, rol);
    if (!aesGcm(false, ctx.get(), envoltura, aad.data(), aad.size(), envoltura + afb1::kTamanoNonce,
                afb1::kTamanoDek, dek.datos(), tag.data())) {
        return R::fallo(error(C::CredencialIncorrecta, "credencial"));
    }
    return R::exito(std::move(dek));
}

Resultado<CabeceraPreparada, ErrorRespaldo> prepararCabecera(const KekRespaldo& kekContrasena,
                                                             const KekRespaldo& kekRecuperacion,
                                                             const DatosNuevoRespaldo& datos)
{
    using R = Resultado<CabeceraPreparada, ErrorRespaldo>;
    if (!kekContrasena.valida() || kekContrasena.tipo() != KekRespaldo::Tipo::Contrasena
        || !kekContrasena.parametros().validos()) {
        return R::fallo(error(C::ParametrosInvalidos, "kek.contrasena"));
    }
    if (!kekRecuperacion.valida() || kekRecuperacion.tipo() != KekRespaldo::Tipo::Recuperacion) {
        return R::fallo(error(C::ParametrosInvalidos, "kek.recuperacion"));
    }
    if (datos.idInstalacion.isNull()) {
        return R::fallo(error(C::ParametrosInvalidos, "datos.instalacion"));
    }
    if (!datos.creadoEnUtc.isValid()) {
        return R::fallo(error(C::ParametrosInvalidos, "datos.fecha"));
    }
    if (datos.metadatos.size() > respaldo::kMaxMetadatos) {
        return R::fallo(error(C::ParametrosInvalidos, "datos.metadatos"));
    }

    CabeceraPreparada prep;
    prep.dek = BufferSecreto(afb1::kTamanoDek);
    Cabecera& c = prep.bytes;
    std::memcpy(c.data(), afb1::kMagic, 4);
    c[4] = afb1::kVersion;
    c[5] = afb1::kSuite;
    poner16(c.data() + 6, 0);
    const QByteArray uuid = datos.idInstalacion.toRfc4122();
    std::memcpy(c.data() + 8, uuid.constData(), 16);
    const ParametrosKdf& kdf = kekContrasena.parametros();
    poner64(c.data() + 40, static_cast<std::uint64_t>(datos.creadoEnUtc.toMSecsSinceEpoch()));
    poner32(c.data() + 48, kdf.memoriaKib);
    poner32(c.data() + 52, kdf.iteraciones);
    poner32(c.data() + 56, kdf.paralelismo);
    std::memcpy(c.data() + 60, kdf.sal.constData(), respaldo::kTamanoSal);
    poner32(c.data() + 76, static_cast<std::uint32_t>(respaldo::kTamanoBloque));
    if (!aleatorio(c.data() + 24, respaldo::kTamanoIdRespaldo)
        || !aleatorio(c.data() + 80, afb1::kTamanoPrefijoNonce) || !aleatorio(prep.dek.datos(), afb1::kTamanoDek)) {
        return R::fallo(error(C::Interno, "rand"));
    }
    if (!envolver(kekContrasena, prep.dek, c, kRolContrasena)
        || !envolver(kekRecuperacion, prep.dek, c, kRolRecuperacion)) {
        return R::fallo(error(C::Interno, "openssl.envoltura"));
    }
    return R::exito(std::move(prep));
}

Resultado<CabeceraRespaldo, ErrorRespaldo> interpretarCabecera(const Cabecera& c)
{
    using R = Resultado<CabeceraRespaldo, ErrorRespaldo>;
    if (std::memcmp(c.data(), afb1::kMagic, 4) != 0) {
        return R::fallo(error(C::FormatoNoReconocido, "cabecera.magic"));
    }
    if (c[4] != afb1::kVersion) {
        return R::fallo(error(C::VersionNoSoportada, "cabecera.version"));
    }
    if (c[5] != afb1::kSuite) {
        return R::fallo(error(C::VersionNoSoportada, "cabecera.suite"));
    }
    if (tomar16(c.data() + 6) != 0) {
        return R::fallo(error(C::FormatoNoReconocido, "cabecera.reservado"));
    }
    CabeceraRespaldo cab;
    cab.version = c[4];
    cab.idInstalacion = QUuid::fromRfc4122(QByteArrayView(c.data() + 8, 16));
    if (cab.idInstalacion.isNull()) {
        return R::fallo(error(C::FormatoNoReconocido, "cabecera.instalacion"));
    }
    cab.idRespaldo = QByteArray(reinterpret_cast<const char*>(c.data() + 24), respaldo::kTamanoIdRespaldo);
    cab.creadoEnUtc = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(tomar64(c.data() + 40)), QTimeZone::UTC);
    if (!cab.creadoEnUtc.isValid()) {
        return R::fallo(error(C::FormatoNoReconocido, "cabecera.fecha"));
    }
    cab.kdf.memoriaKib = tomar32(c.data() + 48);
    cab.kdf.iteraciones = tomar32(c.data() + 52);
    cab.kdf.paralelismo = tomar32(c.data() + 56);
    cab.kdf.sal = QByteArray(reinterpret_cast<const char*>(c.data() + 60), respaldo::kTamanoSal);
    cab.tamanoBloque = tomar32(c.data() + 76);
    if (cab.tamanoBloque != respaldo::kTamanoBloque) {
        return R::fallo(error(C::FormatoNoReconocido, "cabecera.bloque"));
    }
    if (!cab.kdf.validos()) {
        return R::fallo(error(C::ParametrosKdfInvalidos, "cabecera.kdf"));
    }
    return R::exito(std::move(cab));
}

struct CabeceraLeida {
    Cabecera bytes{};
    CabeceraRespaldo publica;
};

Resultado<CabeceraLeida, ErrorRespaldo> leerCabeceraDe(QIODevice& origen)
{
    using R = Resultado<CabeceraLeida, ErrorRespaldo>;
    if (!origen.isOpen() || !origen.isReadable()) {
        return R::fallo(error(C::Lectura, "origen.cerrado"));
    }
    if (!origen.isSequential() && !origen.seek(0)) {
        return R::fallo(error(C::Lectura, "origen.seek"));
    }
    CabeceraLeida leida;
    switch (leerExacto(origen, leida.bytes.data(), leida.bytes.size())) {
    case EstadoLectura::Error: return R::fallo(error(C::Lectura, "origen.lectura"));
    case EstadoLectura::FinDeArchivo: return R::fallo(error(C::FormatoNoReconocido, "cabecera.corta"));
    case EstadoLectura::Completa: break;
    }
    auto cab = interpretarCabecera(leida.bytes);
    if (!cab) {
        return R::fallo(std::move(cab).error());
    }
    leida.publica = std::move(cab).valor();
    return R::exito(std::move(leida));
}

// Cabecera + DEK. La KEK de contrasena debe traer los mismos parametros que
// la cabecera; si no, no puede abrirla (error generico). Desenvolver solo
// autentica cabecera[0..84) y la envoltura del rol; los 204 bytes completos
// quedan autenticados con el bloque 0.
struct Apertura {
    CabeceraLeida cabecera;
    BufferSecreto dek;
};

Resultado<Apertura, ErrorRespaldo> abrir(QIODevice& origen, const KekRespaldo& kek)
{
    using R = Resultado<Apertura, ErrorRespaldo>;
    if (!kek.valida()) {
        return R::fallo(error(C::ParametrosInvalidos, "kek"));
    }
    auto leida = leerCabeceraDe(origen);
    if (!leida) {
        return R::fallo(std::move(leida).error());
    }
    if (kek.tipo() == KekRespaldo::Tipo::Contrasena && !(kek.parametros() == leida.valor().publica.kdf)) {
        return R::fallo(error(C::CredencialIncorrecta, "credencial"));
    }
    auto dek = desenvolver(kek, leida.valor().bytes);
    if (!dek) {
        return R::fallo(std::move(dek).error());
    }
    Apertura a;
    a.cabecera = std::move(leida).valor();
    a.dek = std::move(dek).valor();
    return R::exito(std::move(a));
}

constexpr std::size_t kTamanoAadBloque = afb1::kTamanoCabecera + 8 + 4 + 1;

std::array<std::uint8_t, kTamanoAadBloque> aadBloque(const Cabecera& cabecera, std::uint64_t indice,
                                                      std::uint32_t largo, bool final)
{
    std::array<std::uint8_t, kTamanoAadBloque> aad{};
    std::memcpy(aad.data(), cabecera.data(), cabecera.size());
    poner64(aad.data() + cabecera.size(), indice);
    poner32(aad.data() + cabecera.size() + 8, largo);
    aad[cabecera.size() + 12] = final ? 1 : 0;
    return aad;
}

std::array<std::uint8_t, afb1::kTamanoNonce> nonceBloque(const Cabecera& cabecera, std::uint64_t indice)
{
    std::array<std::uint8_t, afb1::kTamanoNonce> nonce{};
    std::memcpy(nonce.data(), cabecera.data() + 80, afb1::kTamanoPrefijoNonce);
    poner64(nonce.data() + afb1::kTamanoPrefijoNonce, indice);
    return nonce;
}

// --- Flujo cifrado por bloques: escritura. ---------------------------------
// Acumula texto plano; un bloque lleno solo se emite cuando llega mas texto,
// asi el bloque final nunca queda vacio. Cada bloque lleva su marco (largo,
// final) en claro y cubierto por la AAD (C1).
class CifradorBloques {
public:
    CifradorBloques(QIODevice& destino, BufferSecreto dek, const Cabecera& cabecera)
        : m_destino(destino)
        , m_dek(std::move(dek))
        , m_cabecera(cabecera)
        , m_plano(respaldo::kTamanoBloque)
        , m_cifrado(afb1::kTamanoBloqueCifrado)
        , m_ctx(contextoGcm(true, m_dek))
    {
    }

    ~CifradorBloques() { OPENSSL_cleanse(m_plano.datos(), m_plano.tamano()); }

    bool listo() const noexcept { return static_cast<bool>(m_ctx); }

    ResultadoVacio escribir(const void* datos, std::size_t n)
    {
        const auto* p = static_cast<const std::uint8_t*>(datos);
        while (n > 0) {
            if (m_usado == m_plano.tamano()) {
                auto r = emitir(false);
                if (!r) {
                    return r;
                }
            }
            const std::size_t toma = std::min(n, m_plano.tamano() - m_usado);
            std::memcpy(m_plano.datos() + m_usado, p, toma);
            m_usado += toma;
            p += toma;
            n -= toma;
        }
        return ok();
    }

    ResultadoVacio escribir(const QByteArray& b)
    {
        return escribir(b.constData(), static_cast<std::size_t>(b.size()));
    }

    ResultadoVacio cerrar()
    {
        if (m_usado == 0) {
            return falla(C::Interno, "flujo.vacio");
        }
        return emitir(true);
    }

private:
    ResultadoVacio emitir(bool final)
    {
        const auto largo = static_cast<std::uint32_t>(m_usado);
        const auto nonce = nonceBloque(m_cabecera, m_indice);
        const auto aad = aadBloque(m_cabecera, m_indice, largo, final);
        poner32(m_cifrado.data(), largo);
        m_cifrado[4] = final ? 1 : 0;
        std::uint8_t* ct = m_cifrado.data() + afb1::kTamanoMarco;
        if (!aesGcm(true, m_ctx.get(), nonce.data(), aad.data(), aad.size(), m_plano.datos(), m_usado, ct,
                    ct + m_usado)) {
            return falla(C::Interno, "openssl.bloque");
        }
        if (!escribirTodo(m_destino, m_cifrado.data(), afb1::kTamanoMarco + m_usado + afb1::kTamanoTag)) {
            return falla(C::Escritura, "destino.escritura");
        }
        OPENSSL_cleanse(m_plano.datos(), m_usado);
        m_usado = 0;
        ++m_indice;
        return ok();
    }

    QIODevice& m_destino;
    BufferSecreto m_dek;
    Cabecera m_cabecera;
    BufferSecreto m_plano;
    std::vector<std::uint8_t> m_cifrado;
    CtxPtr m_ctx;
    std::size_t m_usado = 0;
    std::uint64_t m_indice = 0;
};

// --- Flujo cifrado por bloques: lectura. -----------------------------------
class DescifradorBloques {
public:
    DescifradorBloques(QIODevice& origen, BufferSecreto dek, const Cabecera& cabecera, const Cancelacion& cancelacion)
        : m_origen(origen)
        , m_dek(std::move(dek))
        , m_cabecera(cabecera)
        , m_cancelacion(cancelacion)
        , m_plano(respaldo::kTamanoBloque)
        , m_cifrado(respaldo::kTamanoBloque + afb1::kTamanoTag)
        , m_ctx(contextoGcm(false, m_dek))
    {
    }

    ~DescifradorBloques() { OPENSSL_cleanse(m_plano.datos(), m_plano.tamano()); }

    bool listo() const noexcept { return static_cast<bool>(m_ctx); }

    // Exactamente `n` bytes del flujo logico; Danado si el flujo termina antes.
    ResultadoVacio leer(void* destino, std::size_t n)
    {
        auto* p = static_cast<std::uint8_t*>(destino);
        while (n > 0) {
            if (m_pos == m_largo) {
                auto r = cargar();
                if (!r) {
                    return r;
                }
            }
            const std::size_t toma = std::min(n, m_largo - m_pos);
            std::memcpy(p, m_plano.datos() + m_pos, toma);
            m_pos += toma;
            p += toma;
            n -= toma;
        }
        return ok();
    }

    // true si se consumio todo el texto plano del bloque final y despues el
    // dispositivo confirma fin de archivo; false si queda flujo o hay bytes
    // tras el bloque final.
    Resultado<bool, ErrorRespaldo> alFinal()
    {
        using R = Resultado<bool, ErrorRespaldo>;
        if (m_pos < m_largo) {
            return R::exito(false);
        }
        if (!m_finalVisto) {
            auto r = cargar();
            if (!r) {
                return R::fallo(std::move(r).error());
            }
            return R::exito(false);
        }
        std::uint8_t extra = 0;
        switch (leerExacto(m_origen, &extra, 1)) {
        case EstadoLectura::Error: return R::fallo(error(C::Lectura, "origen.lectura"));
        case EstadoLectura::Completa: return R::exito(false);
        case EstadoLectura::FinDeArchivo: break;
        }
        return R::exito(true);
    }

private:
    ResultadoVacio cargar()
    {
        if (m_finalVisto) {
            return falla(C::Danado, "flujo.truncado");
        }
        if (m_cancelacion.solicitada()) {
            return falla(C::Cancelado, "cancelado");
        }
        std::uint8_t marco[afb1::kTamanoMarco];
        std::size_t leidos = 0;
        switch (leerExacto(m_origen, marco, sizeof(marco), &leidos)) {
        case EstadoLectura::Error: return falla(C::Lectura, "origen.lectura");
        case EstadoLectura::FinDeArchivo: return falla(C::Danado, leidos == 0 ? "flujo.sin_final" : "bloque.corto");
        case EstadoLectura::Completa: break;
        }
        const std::uint32_t largo = tomar32(marco);
        const std::uint8_t marcaFinal = marco[4];
        if (marcaFinal > 1 || largo == 0 || largo > respaldo::kTamanoBloque
            || (marcaFinal == 0 && largo != respaldo::kTamanoBloque)) {
            return falla(C::Danado, "bloque.marco");
        }
        switch (leerExacto(m_origen, m_cifrado.data(), largo + afb1::kTamanoTag)) {
        case EstadoLectura::Error: return falla(C::Lectura, "origen.lectura");
        case EstadoLectura::FinDeArchivo: return falla(C::Danado, "bloque.corto");
        case EstadoLectura::Completa: break;
        }
        const bool final = marcaFinal == 1;
        const auto nonce = nonceBloque(m_cabecera, m_indice);
        const auto aad = aadBloque(m_cabecera, m_indice, largo, final);
        if (!aesGcm(false, m_ctx.get(), nonce.data(), aad.data(), aad.size(), m_cifrado.data(), largo,
                    m_plano.datos(), m_cifrado.data() + largo)) {
            OPENSSL_cleanse(m_plano.datos(), largo);
            ERR_clear_error();
            return falla(C::Danado, "bloque.tag");
        }
        m_largo = largo;
        m_pos = 0;
        m_finalVisto = final;
        ++m_indice;
        return ok();
    }

    QIODevice& m_origen;
    BufferSecreto m_dek;
    Cabecera m_cabecera;
    const Cancelacion& m_cancelacion;
    BufferSecreto m_plano;
    std::vector<std::uint8_t> m_cifrado;
    CtxPtr m_ctx;
    std::size_t m_largo = 0;
    std::size_t m_pos = 0;
    std::uint64_t m_indice = 0;
    bool m_finalVisto = false;
};

bool mismoMtime(const struct stat& a, const struct stat& b)
{
#if defined(__APPLE__)
    return a.st_mtimespec.tv_sec == b.st_mtimespec.tv_sec && a.st_mtimespec.tv_nsec == b.st_mtimespec.tv_nsec;
#else
    return a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec;
#endif
}

// --- Rutas. ----------------------------------------------------------------

// Rechaza duplicados y choques archivo/carpeta sin distinguir mayusculas
// (APFS por omision no las distingue). Guarda 64 bits de SHA-256 de cada ruta
// y de cada carpeta (casefold), asi la memoria no depende del largo de las
// rutas (C2): ~24-32 B por clave. Las carpetas distintas se limitan a
// kMaxCarpetas para acotar rutas hostiles con muchos componentes. Una colision
// de 64 bits solo provoca un rechazo espurio (falla cerrado).
class RegistroRutas {
public:
    enum class Resultado { Aceptada, Duplicada, LimiteCarpetas };

    RegistroRutas()
    {
        m_archivos.reserve(1024);
        m_carpetas.reserve(1024);
    }

    Resultado agregar(const QString& ruta)
    {
        const QString clave = ruta.toCaseFolded();
        const auto propia = huella(clave);
        if (!propia || m_archivos.count(*propia) != 0 || m_carpetas.count(*propia) != 0) {
            return Resultado::Duplicada;
        }
        std::vector<std::uint64_t> nuevas;
        for (qsizetype pos = clave.indexOf(QLatin1Char('/')); pos >= 0; pos = clave.indexOf(QLatin1Char('/'), pos + 1)) {
            const auto prefijo = huella(QStringView(clave).left(pos));
            if (!prefijo || m_archivos.count(*prefijo) != 0) {
                return Resultado::Duplicada;
            }
            if (m_carpetas.count(*prefijo) == 0) {
                nuevas.push_back(*prefijo);
            }
        }
        if (m_carpetas.size() + nuevas.size() > respaldo::kMaxCarpetas) {
            return Resultado::LimiteCarpetas;
        }
        m_carpetas.insert(nuevas.begin(), nuevas.end());
        m_archivos.insert(*propia);
        return Resultado::Aceptada;
    }

private:
    static std::optional<std::uint64_t> huella(QStringView texto)
    {
        const QByteArray utf8 = texto.toUtf8();
        Sha256 sha;
        sha.actualizar(utf8.constData(), static_cast<std::size_t>(utf8.size()));
        const auto d = sha.finalizar();
        if (!d) {
            return std::nullopt;
        }
        return tomar64(d->data());
    }

    std::unordered_set<std::uint64_t> m_archivos;
    std::unordered_set<std::uint64_t> m_carpetas;
};

// Traduce el resultado del registro a un error (nullopt si se acepto).
std::optional<ErrorRespaldo> errorRuta(RegistroRutas::Resultado r)
{
    switch (r) {
    case RegistroRutas::Resultado::Aceptada: return std::nullopt;
    case RegistroRutas::Resultado::Duplicada: return error(C::RutaInvalida, "entrada.duplicada");
    case RegistroRutas::Resultado::LimiteCarpetas: return error(C::LimiteExcedido, "carpetas.limite");
    }
    return error(C::Interno, "rutas");
}

// Ruta del flujo -> QString valida (UTF-8 estricto + reglas del puerto).
std::optional<QString> rutaDesdeUtf8(const QByteArray& bytes)
{
    const QString ruta = QString::fromUtf8(bytes);
    if (ruta.toUtf8() != bytes || !respaldo::esRutaRelativaValida(ruta)) {
        return std::nullopt;
    }
    return ruta;
}

// Fila del manifiesto (sin tipo de registro ni cuenta).
QByteArray filaManifiesto(std::uint8_t estado, const QByteArray& rutaUtf8, std::uint64_t tamano,
                          const QByteArray& sha256)
{
    QByteArray b;
    b.reserve(1 + 2 + rutaUtf8.size() + 8 + static_cast<qsizetype>(respaldo::kTamanoSha256));
    agregar8(b, estado);
    agregar16(b, static_cast<std::uint16_t>(rutaUtf8.size()));
    b.append(rutaUtf8);
    agregar64(b, tamano);
    b.append(sha256);
    return b;
}

qsizetype tamanoFila(const QByteArray& rutaUtf8)
{
    return 1 + 2 + rutaUtf8.size() + 8 + static_cast<qsizetype>(respaldo::kTamanoSha256);
}

const QByteArray& shaCeros()
{
    static const QByteArray ceros(static_cast<qsizetype>(respaldo::kTamanoSha256), '\0');
    return ceros;
}

// Bytes en segmentos de 1 MiB: crece sin duplicar su capacidad (C2).
class BufferSegmentado {
public:
    static constexpr qsizetype kSegmento = 1024 * 1024;

    void agregar(const QByteArray& datos)
    {
        const char* p = datos.constData();
        qsizetype n = datos.size();
        while (n > 0) {
            if (m_segmentos.isEmpty() || m_segmentos.constLast().size() == kSegmento) {
                m_segmentos.append(QByteArray());
                m_segmentos.last().reserve(kSegmento);
            }
            QByteArray& actual = m_segmentos.last();
            const qsizetype toma = std::min(n, kSegmento - actual.size());
            actual.append(p, toma);
            p += toma;
            n -= toma;
        }
        m_tamano += datos.size();
    }

    qsizetype tamano() const noexcept { return m_tamano; }
    const QList<QByteArray>& segmentos() const noexcept { return m_segmentos; }

private:
    QList<QByteArray> m_segmentos;
    qsizetype m_tamano = 0;
};

// Encabezado de un registro ENTRADA (sin datos ni hash).
QByteArray encabezadoEntrada(const QByteArray& rutaUtf8, std::uint64_t tamano)
{
    QByteArray b;
    agregar8(b, afb1::kRegistroEntrada);
    agregar8(b, afb1::kClaseArchivoRegular);
    agregar16(b, static_cast<std::uint16_t>(rutaUtf8.size()));
    b.append(rutaUtf8);
    agregar64(b, tamano);
    return b;
}

// --- Fuentes (C3). -----------------------------------------------------------

template <typename F>
int reintentarEintr(F&& abrirFn)
{
    int fd = -1;
    do {
        fd = abrirFn();
    } while (fd < 0 && errno == EINTR);
    return fd;
}

ErrorRespaldo errorApertura(int codigo)
{
    if (codigo == ENOENT) {
        return error(C::FuenteFaltante, "fuente.no_existe");
    }
    if (codigo == ELOOP || codigo == ENOTDIR) {
        return error(C::FuenteInvalida, "fuente.enlace");
    }
    return error(C::FuenteInvalida, "fuente.abrir");
}

struct FuenteAbierta {
    Descriptor padre;
    Descriptor archivo;
    QByteArray nombre;
    struct stat estado {};
};

// Abre `relativa` bajo `raiz` componente a componente sin seguir enlaces.
Resultado<FuenteAbierta, ErrorRespaldo> abrirFuente(const QString& raiz, const QString& relativa)
{
    using R = Resultado<FuenteAbierta, ErrorRespaldo>;
    if (!QDir::isAbsolutePath(raiz)) {
        return R::fallo(error(C::FuenteInvalida, "fuente.raiz"));
    }
    const QStringList partes = relativa.split(QLatin1Char('/'));
    if (relativa.isEmpty()) {
        return R::fallo(error(C::FuenteInvalida, "fuente.ruta"));
    }
    for (const QString& parte : partes) {
        if (parte.isEmpty() || parte == QLatin1String(".") || parte == QLatin1String("..")
            || parte.contains(QChar(u'\0'))) {
            return R::fallo(error(C::FuenteInvalida, "fuente.ruta"));
        }
    }
    const QByteArray raizNativa = QFile::encodeName(raiz);
    Descriptor actual(reintentarEintr(
        [&] { return ::open(raizNativa.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC); }));
    if (!actual.valido()) {
        return R::fallo(errorApertura(errno));
    }
    for (qsizetype i = 0; i + 1 < partes.size(); ++i) {
        const QByteArray nombre = QFile::encodeName(partes.at(i));
        Descriptor siguiente(reintentarEintr([&] {
            return ::openat(actual.fd(), nombre.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        }));
        if (!siguiente.valido()) {
            return R::fallo(errorApertura(errno));
        }
        actual = std::move(siguiente);
    }
    FuenteAbierta f;
    f.nombre = QFile::encodeName(partes.constLast());
    f.archivo = Descriptor(reintentarEintr([&] {
        return ::openat(actual.fd(), f.nombre.constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    }));
    if (!f.archivo.valido()) {
        return R::fallo(errorApertura(errno));
    }
    if (::fstat(f.archivo.fd(), &f.estado) != 0 || !S_ISREG(f.estado.st_mode)) {
        return R::fallo(error(C::FuenteInvalida, "fuente.no_regular"));
    }
    f.padre = std::move(actual);
    return R::exito(std::move(f));
}

// --- Escritor. -------------------------------------------------------------

class EscritorRespaldoOpenSsl final : public EscritorRespaldo {
public:
    EscritorRespaldoOpenSsl(QIODevice& destino, CabeceraPreparada prep, CabeceraRespaldo publica,
                            QByteArray metadatos, ProgresoRespaldo progreso)
        : m_cifrador(destino, std::move(prep.dek), prep.bytes)
        , m_progreso(std::move(progreso))
        , m_chunk(kTamanoChunkFuente)
    {
        m_inventario.cabecera = std::move(publica);
        m_inventario.metadatos = std::move(metadatos);
    }

    ~EscritorRespaldoOpenSsl() override { OPENSSL_cleanse(m_chunk.datos(), m_chunk.tamano()); }

    ResultadoVacio iniciar()
    {
        if (!m_cifrador.listo()) {
            return fallar(error(C::Interno, "openssl.ctx"));
        }
        return fatal(m_cifrador.escribir(afb1::registroMetadatos(m_inventario.metadatos)));
    }

    ResultadoVacio agregarArchivo(const QString& rutaRelativa, const QString& raizFuente,
                                  const QString& rutaRelativaFuente, const Cancelacion& cancelacion) override
    {
        const QByteArray rutaUtf8 = rutaRelativa.toUtf8();
        if (auto r = admitir(rutaRelativa, rutaUtf8); !r) {
            return r;
        }
        auto abierta = abrirFuente(raizFuente, rutaRelativaFuente);
        if (!abierta) {
            return ResultadoVacio::fallo(std::move(abierta).error());
        }
        FuenteAbierta fuente = std::move(abierta).valor();
        if (auto e = errorRuta(m_rutas.agregar(rutaRelativa))) {
            return ResultadoVacio::fallo(*e);
        }

        // Desde aqui cualquier error deja el escritor fallido.
        const struct stat& antes = fuente.estado;
        const auto tamano = static_cast<std::uint64_t>(antes.st_size);
        if (auto r = fatal(m_cifrador.escribir(encabezadoEntrada(rutaUtf8, tamano))); !r) {
            return r;
        }
        Sha256 sha;
        std::uint64_t total = 0;
        for (;;) {
            if (cancelacion.solicitada()) {
                return fallar(error(C::Cancelado, "cancelado"));
            }
            const ssize_t n = ::read(fuente.archivo.fd(), m_chunk.datos(), m_chunk.tamano());
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return fallar(error(C::FuenteCambio, "fuente.lectura"));
            }
            if (n == 0) {
                break;
            }
            const auto leidos = static_cast<std::size_t>(n);
            total += leidos;
            if (total > tamano) {
                return fallar(error(C::FuenteCambio, "fuente.tamano"));
            }
            sha.actualizar(m_chunk.datos(), leidos);
            if (auto r = fatal(m_cifrador.escribir(m_chunk.datos(), leidos)); !r) {
                return r;
            }
            avanzar(leidos);
        }
        struct stat despues {};
        struct stat porRuta {};
        if (total != tamano || ::fstat(fuente.archivo.fd(), &despues) != 0 || despues.st_size != antes.st_size
            || !mismoMtime(despues, antes) || despues.st_nlink == 0
            || ::fstatat(fuente.padre.fd(), fuente.nombre.constData(), &porRuta, AT_SYMLINK_NOFOLLOW) != 0
            || porRuta.st_ino != antes.st_ino || porRuta.st_dev != antes.st_dev) {
            return fallar(error(C::FuenteCambio, "fuente.cambio"));
        }
        return cerrarEntrada(rutaUtf8, tamano, sha, rutaRelativa);
    }

    ResultadoVacio agregarDatos(const QString& rutaRelativa, const QByteArray& datos) override
    {
        const QByteArray rutaUtf8 = rutaRelativa.toUtf8();
        if (auto r = admitir(rutaRelativa, rutaUtf8); !r) {
            return r;
        }
        if (auto e = errorRuta(m_rutas.agregar(rutaRelativa))) {
            return ResultadoVacio::fallo(*e);
        }
        const auto tamano = static_cast<std::uint64_t>(datos.size());
        if (auto r = fatal(m_cifrador.escribir(encabezadoEntrada(rutaUtf8, tamano))); !r) {
            return r;
        }
        if (auto r = fatal(m_cifrador.escribir(datos)); !r) {
            return r;
        }
        Sha256 sha;
        sha.actualizar(datos.constData(), static_cast<std::size_t>(datos.size()));
        avanzar(static_cast<std::size_t>(datos.size()));
        return cerrarEntrada(rutaUtf8, tamano, sha, rutaRelativa);
    }

    ResultadoVacio anotarFaltante(const QString& rutaRelativa) override
    {
        const QByteArray rutaUtf8 = rutaRelativa.toUtf8();
        if (auto r = admitir(rutaRelativa, rutaUtf8); !r) {
            return r;
        }
        if (auto e = errorRuta(m_rutas.agregar(rutaRelativa))) {
            return ResultadoVacio::fallo(*e);
        }
        if (auto r = fatal(m_cifrador.escribir(afb1::registroFaltante(rutaUtf8))); !r) {
            return r;
        }
        m_manifiesto.agregar(filaManifiesto(1, rutaUtf8, 0, shaCeros()));
        ++m_inventario.faltantes;
        return ok();
    }

    Resultado<InventarioRespaldo, ErrorRespaldo> finalizar() override
    {
        using R = Resultado<InventarioRespaldo, ErrorRespaldo>;
        if (auto e = estado()) {
            return R::fallo(*e);
        }
        QByteArray encabezado;
        agregar8(encabezado, afb1::kRegistroManifiesto);
        agregar32(encabezado, m_inventario.entradas());
        if (auto r = fatal(m_cifrador.escribir(encabezado)); !r) {
            return R::fallo(std::move(r).error());
        }
        for (const QByteArray& segmento : m_manifiesto.segmentos()) {
            if (auto r = fatal(m_cifrador.escribir(segmento)); !r) {
                return R::fallo(std::move(r).error());
            }
        }
        if (auto r = fatal(m_cifrador.cerrar()); !r) {
            return R::fallo(std::move(r).error());
        }
        m_finalizado = true;
        return R::exito(m_inventario);
    }

private:
    std::optional<ErrorRespaldo> estado() const
    {
        if (m_error) {
            return m_error;
        }
        if (m_finalizado) {
            return error(C::ParametrosInvalidos, "escritor.finalizado");
        }
        return std::nullopt;
    }

    // Validaciones previas a escribir: no dejan el escritor fallido.
    ResultadoVacio admitir(const QString& rutaRelativa, const QByteArray& rutaUtf8) const
    {
        if (auto e = estado()) {
            return ResultadoVacio::fallo(*e);
        }
        if (!respaldo::esRutaRelativaValida(rutaRelativa)) {
            return falla(C::RutaInvalida, "entrada.ruta");
        }
        if (m_inventario.entradas() >= respaldo::kMaxEntradas) {
            return falla(C::LimiteExcedido, "entradas.limite");
        }
        if (m_manifiesto.tamano() + tamanoFila(rutaUtf8) > respaldo::kMaxBytesManifiesto) {
            return falla(C::LimiteExcedido, "manifiesto.limite");
        }
        return ok();
    }

    ResultadoVacio fallar(ErrorRespaldo e)
    {
        m_error = e;
        return ResultadoVacio::fallo(std::move(e));
    }

    ResultadoVacio fatal(ResultadoVacio r)
    {
        if (!r) {
            return fallar(std::move(r).error());
        }
        return r;
    }

    void avanzar(std::size_t n)
    {
        m_inventario.bytesDatos += n;
        if (m_progreso) {
            m_progreso(m_inventario.bytesDatos);
        }
    }

    ResultadoVacio cerrarEntrada(const QByteArray& rutaUtf8, std::uint64_t tamano, Sha256& sha, const QString&)
    {
        const auto digest = sha.finalizar();
        if (!digest) {
            return fallar(error(C::Interno, "openssl.sha256"));
        }
        if (auto r = fatal(m_cifrador.escribir(digest->data(), digest->size())); !r) {
            return r;
        }
        m_manifiesto.agregar(filaManifiesto(
            0, rutaUtf8, tamano,
            QByteArray(reinterpret_cast<const char*>(digest->data()), static_cast<qsizetype>(digest->size()))));
        ++m_inventario.presentes;
        return ok();
    }

    CifradorBloques m_cifrador;
    ProgresoRespaldo m_progreso;
    BufferSecreto m_chunk;
    RegistroRutas m_rutas;
    BufferSegmentado m_manifiesto;
    InventarioRespaldo m_inventario;
    std::optional<ErrorRespaldo> m_error;
    bool m_finalizado = false;
};

// --- Lectura: staging de extraccion (C4). ------------------------------------

// Nombres de las entradas de un directorio, leidos sobre un duplicado del
// descriptor (rewinddir: el desplazamiento se comparte con el original).
std::optional<std::vector<QByteArray>> nombresEn(int dirfd, std::size_t maximo)
{
    const int copia = ::dup(dirfd);
    if (copia < 0) {
        return std::nullopt;
    }
    DIR* dir = ::fdopendir(copia);
    if (dir == nullptr) {
        ::close(copia);
        return std::nullopt;
    }
    ::rewinddir(dir);
    std::vector<QByteArray> nombres;
    while (const dirent* e = ::readdir(dir)) {
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) {
            continue;
        }
        nombres.emplace_back(e->d_name);
        if (nombres.size() >= maximo) {
            break;
        }
    }
    ::closedir(dir);
    return nombres;
}

// Vacia `dirfd` sin seguir enlaces: cada entrada se borra con unlinkat sobre
// el descriptor de su padre; las carpetas se abren con O_NOFOLLOW.
void vaciarDirectorio(int dirfd, int profundidad) noexcept
{
    if (profundidad > 600) {
        return;
    }
    const auto nombres = nombresEn(dirfd, static_cast<std::size_t>(-1));
    if (!nombres) {
        return;
    }
    for (const QByteArray& nombre : *nombres) {
        if (::unlinkat(dirfd, nombre.constData(), 0) == 0) {
            continue;
        }
        Descriptor sub(::openat(dirfd, nombre.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (sub.valido()) {
            vaciarDirectorio(sub.fd(), profundidad + 1);
            sub.cerrar();
            (void)::unlinkat(dirfd, nombre.constData(), AT_REMOVEDIR);
        }
    }
}

class SumideroDirectorio {
public:
    ~SumideroDirectorio() { m_archivo.cerrar(); }

    ResultadoVacio abrirRaiz(const QString& staging)
    {
        if (!QDir::isAbsolutePath(staging)) {
            return falla(C::DestinoInvalido, "destino.relativo");
        }
        const QByteArray nativa = QFile::encodeName(staging);
        m_raiz = Descriptor(reintentarEintr(
            [&] { return ::open(nativa.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC); }));
        if (!m_raiz.valido()) {
            return falla(C::DestinoInvalido, "destino.abrir");
        }
        struct stat st {};
        if (::fstat(m_raiz.fd(), &st) != 0 || !S_ISDIR(st.st_mode)) {
            m_raiz.cerrar();
            return falla(C::DestinoInvalido, "destino.no_directorio");
        }
        if (st.st_uid != ::geteuid()) {
            m_raiz.cerrar();
            return falla(C::DestinoInvalido, "destino.dueno");
        }
        if ((st.st_mode & 07777) != 0700) {
            m_raiz.cerrar();
            return falla(C::DestinoInvalido, "destino.permisos");
        }
        const auto nombres = nombresEn(m_raiz.fd(), 1);
        if (!nombres || !nombres->empty()) {
            m_raiz.cerrar();
            return falla(C::DestinoInvalido, "destino.no_vacio");
        }
        return ok();
    }

    ResultadoVacio abrirArchivo(const QString& ruta)
    {
        const QStringList partes = ruta.split(QLatin1Char('/'));
        Descriptor actual(::dup(m_raiz.fd()));
        if (!actual.valido()) {
            return falla(C::Escritura, "destino.dup");
        }
        for (qsizetype i = 0; i + 1 < partes.size(); ++i) {
            const QByteArray nombre = QFile::encodeName(partes.at(i));
            if (::mkdirat(actual.fd(), nombre.constData(), 0700) != 0 && errno != EEXIST) {
                return falla(C::Escritura, "destino.mkdir");
            }
            Descriptor siguiente(
                ::openat(actual.fd(), nombre.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
            if (!siguiente.valido()) {
                return falla(C::DestinoInvalido, "destino.carpeta");
            }
            actual = std::move(siguiente);
        }
        const QByteArray nombre = QFile::encodeName(partes.constLast());
        m_archivo = Descriptor(::openat(actual.fd(), nombre.constData(),
                                        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        if (!m_archivo.valido()) {
            return falla(errno == EEXIST ? C::RutaInvalida : C::Escritura, "destino.archivo");
        }
        return ok();
    }

    ResultadoVacio escribir(const std::uint8_t* datos, std::size_t n)
    {
        while (n > 0) {
            const ssize_t r = ::write(m_archivo.fd(), datos, n);
            if (r < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return falla(C::Escritura, "destino.escritura");
            }
            datos += r;
            n -= static_cast<std::size_t>(r);
        }
        return ok();
    }

    ResultadoVacio cerrarArchivo()
    {
        return m_archivo.cerrar() ? ok() : falla(C::Escritura, "destino.cerrar");
    }

    // Deja el staging vacio (best effort).
    void deshacer() noexcept
    {
        m_archivo.cerrar();
        if (m_raiz.valido()) {
            vaciarDirectorio(m_raiz.fd(), 0);
        }
    }

private:
    Descriptor m_raiz;
    Descriptor m_archivo;
};

// --- Lectura: recorrido completo. ------------------------------------------

Resultado<InventarioRespaldo, ErrorRespaldo> recorrer(QIODevice& origen, const KekRespaldo& kek,
                                                      SumideroDirectorio* sumidero,
                                                      const respaldo::OpcionesLectura& opciones)
{
    using R = Resultado<InventarioRespaldo, ErrorRespaldo>;
    const auto fallo = [](C c, const char* d) { return R::fallo(error(c, d)); };
    const auto propagar = [](ResultadoVacio&& r) { return R::fallo(std::move(r).error()); };

    auto apertura = abrir(origen, kek);
    if (!apertura) {
        return R::fallo(std::move(apertura).error());
    }
    Apertura a = std::move(apertura).valor();
    DescifradorBloques flujo(origen, std::move(a.dek), a.cabecera.bytes, opciones.cancelacion);
    if (!flujo.listo()) {
        return fallo(C::Interno, "openssl.ctx");
    }

    InventarioRespaldo inventario;
    inventario.cabecera = a.cabecera.publica;

    std::uint8_t tipo = 0;
    std::uint8_t b8[8];
    if (auto r = flujo.leer(&tipo, 1); !r) {
        return propagar(std::move(r));
    }
    if (tipo != afb1::kRegistroMetadatos) {
        return fallo(C::Danado, "registro.metadatos");
    }
    if (auto r = flujo.leer(b8, 4); !r) {
        return propagar(std::move(r));
    }
    const std::uint32_t largoMeta = tomar32(b8);
    if (largoMeta > static_cast<std::uint32_t>(respaldo::kMaxMetadatos)) {
        return fallo(C::Danado, "registro.metadatos");
    }
    inventario.metadatos.resize(static_cast<qsizetype>(largoMeta));
    if (auto r = flujo.leer(inventario.metadatos.data(), largoMeta); !r) {
        return propagar(std::move(r));
    }

    const auto leerRuta = [&flujo](QByteArray& utf8) -> ResultadoVacio {
        std::uint8_t b2[2];
        if (auto r = flujo.leer(b2, 2); !r) {
            return r;
        }
        const std::uint16_t n = tomar16(b2);
        if (n == 0 || n > respaldo::kMaxBytesRuta) {
            return falla(C::RutaInvalida, "entrada.ruta");
        }
        utf8.resize(n);
        return flujo.leer(utf8.data(), n);
    };

    RegistroRutas rutas;
    BufferSegmentado esperado;
    BufferSecreto chunk(kTamanoChunkFuente);
    for (;;) {
        if (auto r = flujo.leer(&tipo, 1); !r) {
            return propagar(std::move(r));
        }
        if (tipo == afb1::kRegistroManifiesto) {
            break;
        }
        if (tipo != afb1::kRegistroEntrada && tipo != afb1::kRegistroFaltante) {
            return fallo(C::Danado, "registro.tipo");
        }
        if (inventario.entradas() >= respaldo::kMaxEntradas) {
            return fallo(C::LimiteExcedido, "entradas.limite");
        }
        if (tipo == afb1::kRegistroEntrada) {
            std::uint8_t clase = 0;
            if (auto r = flujo.leer(&clase, 1); !r) {
                return propagar(std::move(r));
            }
            if (clase != afb1::kClaseArchivoRegular) {
                return fallo(C::RutaInvalida, "entrada.clase");
            }
        }
        QByteArray rutaUtf8;
        if (auto r = leerRuta(rutaUtf8); !r) {
            return propagar(std::move(r));
        }
        const auto ruta = rutaDesdeUtf8(rutaUtf8);
        if (!ruta) {
            return fallo(C::RutaInvalida, "entrada.ruta");
        }
        if (esperado.tamano() + tamanoFila(rutaUtf8) > respaldo::kMaxBytesManifiesto) {
            return fallo(C::LimiteExcedido, "manifiesto.limite");
        }
        if (auto e = errorRuta(rutas.agregar(*ruta))) {
            return R::fallo(*e);
        }
        EntradaManifiesto entrada;
        entrada.rutaRelativa = *ruta;
        if (tipo == afb1::kRegistroFaltante) {
            entrada.estado = EntradaManifiesto::Estado::Faltante;
            esperado.agregar(filaManifiesto(1, rutaUtf8, 0, shaCeros()));
            ++inventario.faltantes;
            if (opciones.alLeerEntrada) {
                opciones.alLeerEntrada(entrada);
            }
            continue;
        }
        if (auto r = flujo.leer(b8, 8); !r) {
            return propagar(std::move(r));
        }
        const std::uint64_t tamano = tomar64(b8);
        if (sumidero) {
            if (auto r = sumidero->abrirArchivo(*ruta); !r) {
                return propagar(std::move(r));
            }
        }
        Sha256 sha;
        std::uint64_t restante = tamano;
        while (restante > 0) {
            const auto toma = static_cast<std::size_t>(std::min<std::uint64_t>(restante, chunk.tamano()));
            if (auto r = flujo.leer(chunk.datos(), toma); !r) {
                return propagar(std::move(r));
            }
            sha.actualizar(chunk.datos(), toma);
            if (sumidero) {
                if (auto r = sumidero->escribir(chunk.datos(), toma); !r) {
                    return propagar(std::move(r));
                }
            }
            restante -= toma;
            inventario.bytesDatos += toma;
            if (opciones.progreso) {
                opciones.progreso(inventario.bytesDatos);
            }
        }
        std::array<std::uint8_t, respaldo::kTamanoSha256> guardado{};
        if (auto r = flujo.leer(guardado.data(), guardado.size()); !r) {
            return propagar(std::move(r));
        }
        const auto calculado = sha.finalizar();
        if (!calculado) {
            return fallo(C::Interno, "openssl.sha256");
        }
        if (CRYPTO_memcmp(calculado->data(), guardado.data(), guardado.size()) != 0) {
            return fallo(C::Danado, "entrada.sha256");
        }
        if (sumidero) {
            if (auto r = sumidero->cerrarArchivo(); !r) {
                return propagar(std::move(r));
            }
        }
        entrada.tamano = tamano;
        entrada.sha256 = QByteArray(reinterpret_cast<const char*>(guardado.data()),
                                    static_cast<qsizetype>(guardado.size()));
        esperado.agregar(filaManifiesto(0, rutaUtf8, tamano, entrada.sha256));
        ++inventario.presentes;
        if (opciones.alLeerEntrada) {
            opciones.alLeerEntrada(entrada);
        }
    }

    // Manifiesto: cuenta y filas byte a byte contra lo visto.
    if (auto r = flujo.leer(b8, 4); !r) {
        return propagar(std::move(r));
    }
    const std::uint32_t cuenta = tomar32(b8);
    if (cuenta > respaldo::kMaxEntradas) {
        return fallo(C::LimiteExcedido, "entradas.limite");
    }
    if (cuenta != inventario.entradas()) {
        return fallo(C::Danado, "manifiesto.incoherente");
    }
    for (const QByteArray& segmento : esperado.segmentos()) {
        qsizetype hecho = 0;
        while (hecho < segmento.size()) {
            const auto toma = static_cast<std::size_t>(
                std::min<qsizetype>(segmento.size() - hecho, static_cast<qsizetype>(chunk.tamano())));
            if (auto r = flujo.leer(chunk.datos(), toma); !r) {
                return propagar(std::move(r));
            }
            if (std::memcmp(chunk.datos(), segmento.constData() + hecho, toma) != 0) {
                return fallo(C::Danado, "manifiesto.incoherente");
            }
            hecho += static_cast<qsizetype>(toma);
        }
    }
    auto fin = flujo.alFinal();
    if (!fin) {
        return R::fallo(std::move(fin).error());
    }
    if (!fin.valor()) {
        return fallo(C::Danado, "flujo.extra");
    }
    return R::exito(std::move(inventario));
}

// --- Clave de recuperacion. ------------------------------------------------

std::optional<std::array<std::uint8_t, kBytesControlRecuperacion>> controlRecuperacion(const std::uint8_t* clave)
{
    Sha256 sha;
    sha.actualizar(kEtiquetaControlRecuperacion, sizeof(kEtiquetaControlRecuperacion) - 1);
    sha.actualizar(clave, respaldo::kTamanoClaveRecuperacion);
    auto digest = sha.finalizar();
    if (!digest) {
        return std::nullopt;
    }
    std::array<std::uint8_t, kBytesControlRecuperacion> control{};
    std::memcpy(control.data(), digest->data(), control.size());
    OPENSSL_cleanse(digest->data(), digest->size());
    return control;
}

int valorCrockford(std::uint8_t c)
{
    if (c >= 'a' && c <= 'z') {
        c = static_cast<std::uint8_t>(c - 'a' + 'A');
    }
    if (c == 'O') {
        return 0;
    }
    if (c == 'I' || c == 'L') {
        return 1;
    }
    for (int i = 0; i < 32; ++i) {
        if (static_cast<std::uint8_t>(kAlfabetoCrockford[i]) == c) {
            return i;
        }
    }
    return -1;
}

} // namespace

// --- API publica. ------------------------------------------------------------

Resultado<Exito, ErrorRespaldo> ContenedorRespaldoOpenSsl::comprobarDisponible() const
{
    return argon2id() ? ok() : falla(C::KdfNoDisponible, "kdf.argon2id");
}

Resultado<QByteArray, ErrorRespaldo> ContenedorRespaldoOpenSsl::generarSal() const
{
    using R = Resultado<QByteArray, ErrorRespaldo>;
    QByteArray sal(static_cast<qsizetype>(respaldo::kTamanoSal), '\0');
    if (!aleatorio(reinterpret_cast<std::uint8_t*>(sal.data()), respaldo::kTamanoSal)) {
        return R::fallo(error(C::Interno, "rand"));
    }
    return R::exito(std::move(sal));
}

Resultado<KekRespaldo, ErrorRespaldo>
ContenedorRespaldoOpenSsl::derivarKekContrasena(const BufferSecreto& contrasena, const ParametrosKdf& parametros) const
{
    using R = Resultado<KekRespaldo, ErrorRespaldo>;
    if (contrasena.vacio()) {
        return R::fallo(error(C::ParametrosInvalidos, "contrasena.vacia"));
    }
    if (!parametros.validos()) {
        return R::fallo(error(C::ParametrosKdfInvalidos, "kdf.parametros"));
    }
    KdfPtr kdf = argon2id();
    if (!kdf) {
        return R::fallo(error(C::KdfNoDisponible, "kdf.argon2id"));
    }
    KdfCtxPtr ctx(EVP_KDF_CTX_new(kdf.get()));
    if (!ctx) {
        return R::fallo(error(C::Interno, "kdf.ctx"));
    }
    std::uint32_t iteraciones = parametros.iteraciones;
    std::uint32_t memoria = parametros.memoriaKib;
    std::uint32_t carriles = parametros.paralelismo;
    std::uint32_t hilos = 1;
    QByteArray sal = parametros.sal;
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD, const_cast<std::uint8_t*>(contrasena.datos()),
                                          contrasena.tamano()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, sal.data(), static_cast<std::size_t>(sal.size())),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ITER, &iteraciones),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_MEMCOST, &memoria),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_LANES, &carriles),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_THREADS, &hilos),
        OSSL_PARAM_construct_end(),
    };
    BufferSecreto clave(respaldo::kTamanoKek);
    if (EVP_KDF_derive(ctx.get(), clave.datos(), clave.tamano(), params) != 1) {
        return R::fallo(error(C::Interno, "kdf.derivar"));
    }
    KekRespaldo kek = KekRespaldo::desdeBytes(KekRespaldo::Tipo::Contrasena, parametros, std::move(clave));
    if (!kek.valida()) {
        return R::fallo(error(C::Interno, "kdf.kek"));
    }
    return R::exito(std::move(kek));
}

Resultado<ClaveRecuperacion, ErrorRespaldo> ContenedorRespaldoOpenSsl::generarClaveRecuperacion() const
{
    using R = Resultado<ClaveRecuperacion, ErrorRespaldo>;
    BufferSecreto bytes(respaldo::kTamanoClaveRecuperacion);
    if (!aleatorio(bytes.datos(), bytes.tamano())) {
        return R::fallo(error(C::Interno, "rand"));
    }
    return R::exito(ClaveRecuperacion(std::move(bytes)));
}

Resultado<BufferSecreto, ErrorRespaldo>
ContenedorRespaldoOpenSsl::formatearClaveRecuperacion(const ClaveRecuperacion& clave) const
{
    using R = Resultado<BufferSecreto, ErrorRespaldo>;
    if (!clave.valida()) {
        return R::fallo(error(C::ParametrosInvalidos, "recuperacion.clave"));
    }
    auto control = controlRecuperacion(clave.bytes().datos());
    if (!control) {
        return R::fallo(error(C::Interno, "openssl.sha256"));
    }
    std::array<std::uint8_t, respaldo::kTamanoClaveRecuperacion + kBytesControlRecuperacion> bruto{};
    std::memcpy(bruto.data(), clave.bytes().datos(), respaldo::kTamanoClaveRecuperacion);
    std::memcpy(bruto.data() + respaldo::kTamanoClaveRecuperacion, control->data(), control->size());

    constexpr std::size_t kGrupo = 4;
    BufferSecreto texto(kCaracteresRecuperacion + kCaracteresRecuperacion / kGrupo - 1);
    std::size_t salida = 0;
    std::uint32_t acumulado = 0;
    int bits = 0;
    std::size_t caracteres = 0;
    for (const std::uint8_t byte : bruto) {
        acumulado = (acumulado << 8) | byte;
        bits += 8;
        while (bits >= 5) {
            if (caracteres > 0 && caracteres % kGrupo == 0) {
                texto.datos()[salida++] = '-';
            }
            texto.datos()[salida++] = static_cast<std::uint8_t>(kAlfabetoCrockford[(acumulado >> (bits - 5)) & 0x1F]);
            bits -= 5;
            ++caracteres;
        }
    }
    acumulado = 0;
    OPENSSL_cleanse(bruto.data(), bruto.size());
    OPENSSL_cleanse(control->data(), control->size());
    if (caracteres != kCaracteresRecuperacion || salida != texto.tamano()) {
        return R::fallo(error(C::Interno, "recuperacion.formato"));
    }
    return R::exito(std::move(texto));
}

Resultado<ClaveRecuperacion, ErrorRespaldo>
ContenedorRespaldoOpenSsl::leerClaveRecuperacion(const BufferSecreto& texto) const
{
    using R = Resultado<ClaveRecuperacion, ErrorRespaldo>;
    const auto malFormada = [] { return R::fallo(error(C::ClaveRecuperacionMalFormada, "recuperacion.texto")); };
    std::array<std::uint8_t, respaldo::kTamanoClaveRecuperacion + kBytesControlRecuperacion> bruto{};
    std::size_t escritos = 0;
    std::size_t caracteres = 0;
    std::uint32_t acumulado = 0;
    int bits = 0;
    for (std::size_t i = 0; i < texto.tamano(); ++i) {
        const std::uint8_t c = texto.datos()[i];
        if (c == '-' || c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            continue;
        }
        const int v = valorCrockford(c);
        if (v < 0 || caracteres >= kCaracteresRecuperacion) {
            OPENSSL_cleanse(bruto.data(), bruto.size());
            return malFormada();
        }
        ++caracteres;
        acumulado = (acumulado << 5) | static_cast<std::uint32_t>(v);
        bits += 5;
        if (bits >= 8) {
            bruto[escritos++] = static_cast<std::uint8_t>(acumulado >> (bits - 8));
            bits -= 8;
            acumulado &= (1u << bits) - 1u;
        }
    }
    acumulado = 0;
    if (caracteres != kCaracteresRecuperacion || escritos != bruto.size()) {
        OPENSSL_cleanse(bruto.data(), bruto.size());
        return malFormada();
    }
    auto control = controlRecuperacion(bruto.data());
    if (!control) {
        OPENSSL_cleanse(bruto.data(), bruto.size());
        return R::fallo(error(C::Interno, "openssl.sha256"));
    }
    const bool controlOk = CRYPTO_memcmp(control->data(), bruto.data() + respaldo::kTamanoClaveRecuperacion,
                                         control->size()) == 0;
    ClaveRecuperacion clave(BufferSecreto::desdeBytes(bruto.data(), respaldo::kTamanoClaveRecuperacion));
    OPENSSL_cleanse(bruto.data(), bruto.size());
    OPENSSL_cleanse(control->data(), control->size());
    if (!controlOk) {
        return malFormada();
    }
    return R::exito(std::move(clave));
}

Resultado<KekRespaldo, ErrorRespaldo>
ContenedorRespaldoOpenSsl::derivarKekRecuperacion(const ClaveRecuperacion& clave) const
{
    using R = Resultado<KekRespaldo, ErrorRespaldo>;
    if (!clave.valida()) {
        return R::fallo(error(C::ParametrosInvalidos, "recuperacion.clave"));
    }
    KdfPtr kdf(EVP_KDF_fetch(nullptr, "HKDF", nullptr));
    KdfCtxPtr ctx(kdf ? EVP_KDF_CTX_new(kdf.get()) : nullptr);
    if (!ctx) {
        return R::fallo(error(C::Interno, "kdf.hkdf"));
    }
    char digest[] = "SHA256";
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digest, 0),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, const_cast<std::uint8_t*>(clave.bytes().datos()),
                                          clave.bytes().tamano()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, const_cast<char*>(kInfoHkdfRecuperacion),
                                          sizeof(kInfoHkdfRecuperacion) - 1),
        OSSL_PARAM_construct_end(),
    };
    BufferSecreto salida(respaldo::kTamanoKek);
    if (EVP_KDF_derive(ctx.get(), salida.datos(), salida.tamano(), params) != 1) {
        return R::fallo(error(C::Interno, "kdf.hkdf"));
    }
    return R::exito(KekRespaldo::desdeBytes(KekRespaldo::Tipo::Recuperacion, {}, std::move(salida)));
}

Resultado<std::unique_ptr<EscritorRespaldo>, ErrorRespaldo>
ContenedorRespaldoOpenSsl::crearEscritor(QIODevice& destino, const KekRespaldo& kekContrasena,
                                         const KekRespaldo& kekRecuperacion, DatosNuevoRespaldo datos,
                                         ProgresoRespaldo progreso) const
{
    using R = Resultado<std::unique_ptr<EscritorRespaldo>, ErrorRespaldo>;
    if (!destino.isOpen() || !destino.isWritable()) {
        return R::fallo(error(C::Escritura, "destino.cerrado"));
    }
    auto prep = prepararCabecera(kekContrasena, kekRecuperacion, datos);
    if (!prep) {
        return R::fallo(std::move(prep).error());
    }
    auto publica = interpretarCabecera(prep.valor().bytes);
    if (!publica) {
        return R::fallo(std::move(publica).error());
    }
    if (!escribirTodo(destino, prep.valor().bytes.data(), prep.valor().bytes.size())) {
        return R::fallo(error(C::Escritura, "destino.escritura"));
    }
    auto escritor = std::make_unique<EscritorRespaldoOpenSsl>(destino, std::move(prep).valor(),
                                                              std::move(publica).valor(), std::move(datos.metadatos),
                                                              std::move(progreso));
    if (auto r = escritor->iniciar(); !r) {
        return R::fallo(std::move(r).error());
    }
    return R::exito(std::move(escritor));
}

Resultado<CabeceraRespaldo, ErrorRespaldo> ContenedorRespaldoOpenSsl::leerCabecera(QIODevice& origen) const
{
    using R = Resultado<CabeceraRespaldo, ErrorRespaldo>;
    auto leida = leerCabeceraDe(origen);
    if (!leida) {
        return R::fallo(std::move(leida).error());
    }
    return R::exito(std::move(leida).valor().publica);
}

Resultado<CabeceraRespaldo, ErrorRespaldo> ContenedorRespaldoOpenSsl::autenticarCabecera(QIODevice& origen,
                                                                                         const KekRespaldo& kek) const
{
    using R = Resultado<CabeceraRespaldo, ErrorRespaldo>;
    auto apertura = abrir(origen, kek);
    if (!apertura) {
        return R::fallo(std::move(apertura).error());
    }
    Apertura a = std::move(apertura).valor();
    // Seguridad 1: el bloque 0 autentica los 204 bytes (AAD) con la DEK.
    const Cancelacion sinCancelacion;
    DescifradorBloques flujo(origen, std::move(a.dek), a.cabecera.bytes, sinCancelacion);
    if (!flujo.listo()) {
        return R::fallo(error(C::Interno, "openssl.ctx"));
    }
    std::uint8_t tipo = 0;
    if (auto r = flujo.leer(&tipo, 1); !r) {
        return R::fallo(std::move(r).error());
    }
    if (tipo != afb1::kRegistroMetadatos) {
        return R::fallo(error(C::Danado, "registro.metadatos"));
    }
    return R::exito(std::move(a.cabecera.publica));
}

Resultado<InventarioRespaldo, ErrorRespaldo>
ContenedorRespaldoOpenSsl::verificar(QIODevice& origen, const KekRespaldo& kek,
                                     const respaldo::OpcionesLectura& opciones) const
{
    return recorrer(origen, kek, nullptr, opciones);
}

Resultado<InventarioRespaldo, ErrorRespaldo>
ContenedorRespaldoOpenSsl::extraer(QIODevice& origen, const KekRespaldo& kek, const QString& staging,
                                   const respaldo::OpcionesLectura& opciones) const
{
    using R = Resultado<InventarioRespaldo, ErrorRespaldo>;
    SumideroDirectorio sumidero;
    if (auto r = sumidero.abrirRaiz(staging); !r) {
        return R::fallo(std::move(r).error());
    }
    auto resultado = recorrer(origen, kek, &sumidero, opciones);
    if (!resultado) {
        sumidero.deshacer();
    }
    return resultado;
}

// --- Helpers de formato (pruebas). -------------------------------------------

namespace afb1 {

QByteArray registroMetadatos(const QByteArray& metadatos)
{
    QByteArray b;
    agregar8(b, kRegistroMetadatos);
    agregar32(b, static_cast<std::uint32_t>(metadatos.size()));
    b.append(metadatos);
    return b;
}

QByteArray registroEntrada(std::uint8_t clase, const QByteArray& rutaUtf8, const QByteArray& datos,
                           const QByteArray& sha256)
{
    QByteArray b;
    agregar8(b, kRegistroEntrada);
    agregar8(b, clase);
    agregar16(b, static_cast<std::uint16_t>(rutaUtf8.size()));
    b.append(rutaUtf8);
    agregar64(b, static_cast<std::uint64_t>(datos.size()));
    b.append(datos);
    b.append(sha256);
    return b;
}

QByteArray registroFaltante(const QByteArray& rutaUtf8)
{
    QByteArray b;
    agregar8(b, kRegistroFaltante);
    agregar16(b, static_cast<std::uint16_t>(rutaUtf8.size()));
    b.append(rutaUtf8);
    return b;
}

QByteArray registroManifiesto(const QList<FilaManifiestoCruda>& filas)
{
    QByteArray b;
    agregar8(b, kRegistroManifiesto);
    agregar32(b, static_cast<std::uint32_t>(filas.size()));
    for (const auto& f : filas) {
        b.append(filaManifiesto(f.estado, f.rutaUtf8, f.tamano, f.sha256));
    }
    return b;
}

QByteArray sha256(const QByteArray& datos)
{
    Sha256 sha;
    sha.actualizar(datos.constData(), static_cast<std::size_t>(datos.size()));
    const auto d = sha.finalizar();
    return d ? QByteArray(reinterpret_cast<const char*>(d->data()), static_cast<qsizetype>(d->size())) : QByteArray();
}

Resultado<Exito, ErrorRespaldo> escribirFlujoCrudo(QIODevice& destino, const KekRespaldo& kekContrasena,
                                                   const KekRespaldo& kekRecuperacion,
                                                   const DatosNuevoRespaldo& datos, const QByteArray& flujoLogico)
{
    auto prep = prepararCabecera(kekContrasena, kekRecuperacion, datos);
    if (!prep) {
        return ResultadoVacio::fallo(std::move(prep).error());
    }
    if (!escribirTodo(destino, prep.valor().bytes.data(), prep.valor().bytes.size())) {
        return falla(C::Escritura, "destino.escritura");
    }
    const Cabecera cabecera = prep.valor().bytes;
    CifradorBloques cifrador(destino, std::move(prep).valor().dek, cabecera);
    if (!cifrador.listo()) {
        return falla(C::Interno, "openssl.ctx");
    }
    if (auto r = cifrador.escribir(flujoLogico); !r) {
        return r;
    }
    return cifrador.cerrar();
}

} // namespace afb1

} // namespace satcfdi::crypto
