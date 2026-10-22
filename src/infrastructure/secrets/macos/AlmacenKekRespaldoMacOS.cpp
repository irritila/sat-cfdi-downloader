#include "infrastructure/secrets/macos/AlmacenKekRespaldoMacOS.h"

#include "infrastructure/secrets/macos/KeychainApiMacOS.h"

#include <cstring>
#include <utility>

namespace satcfdi::secrets {

namespace {

using respaldo::ErrorAlmacenKek;
using respaldo::KekRespaldo;
using respaldo::ParametrosKdf;
using respaldo::ReferenciaKek;

constexpr char kMagic[4] = {'A', 'F', 'K', '1'};
constexpr std::uint8_t kTipoContrasena = 1;
constexpr std::uint8_t kTipoRecuperacion = 2;
constexpr std::size_t kOffsetTipo = 4;
constexpr std::size_t kOffsetMemoria = 8;
constexpr std::size_t kOffsetIteraciones = 12;
constexpr std::size_t kOffsetParalelismo = 16;
constexpr std::size_t kOffsetSal = 20;
constexpr std::size_t kOffsetKek = kOffsetSal + respaldo::kTamanoSal;
static_assert(kOffsetKek + respaldo::kTamanoKek == AlmacenKekRespaldoMacOS::kTamanoDato);

void escribirU32(std::uint8_t* p, quint32 v)
{
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

quint32 leerU32(const std::uint8_t* p)
{
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

ErrorAlmacenKek::Categoria categoriaAlmacen(std::int32_t status)
{
    using C = ErrorAlmacenKek::Categoria;
    using S = ErrorSecretStore::Categoria;
    switch (categoriaDeOSStatus(status)) {
    case S::CredencialNoEncontrada:
        return C::NoEncontrada;
    case S::AlmacenBloqueado:
        return C::AlmacenBloqueado;
    case S::AccesoDenegado:
    case S::CanceladoPorUsuario:
        return C::AccesoDenegado;
    case S::AlmacenMalConfigurado:
        return C::AlmacenMalConfigurado;
    case S::AlmacenNoDisponible:
        return C::AlmacenNoDisponible;
    case S::FalloEscritura:
        return C::FalloEscritura;
    default:
        return C::Interno;
    }
}

ErrorAlmacenKek errorDe(std::int32_t status, const char* operacion)
{
    return ErrorAlmacenKek::de(categoriaAlmacen(status), QString::fromLatin1(operacion), static_cast<int>(status));
}

} // namespace

AlmacenKekRespaldoMacOS::AlmacenKekRespaldoMacOS(QString bundleId, QString ambito,
                                                 std::shared_ptr<KeychainApi> keychain)
    : m_bundleId(std::move(bundleId))
    , m_ambito(std::move(ambito))
    , m_ambitoValido(respaldo::esAmbitoKekValido(m_ambito))
    , m_keychain(std::move(keychain))
{
    Q_ASSERT(!m_bundleId.isEmpty());
    Q_ASSERT(m_keychain);
}

std::unique_ptr<AlmacenKekRespaldoMacOS> AlmacenKekRespaldoMacOS::conKeychainDelSistema(QString bundleId, QString ambito)
{
    return std::make_unique<AlmacenKekRespaldoMacOS>(std::move(bundleId), std::move(ambito),
                                                     std::make_shared<KeychainApiMacOS>());
}

QString AlmacenKekRespaldoMacOS::servicio() const
{
    return m_ambitoValido ? m_bundleId + QStringLiteral(".respaldo.v1.kek.") + m_ambito : QString();
}

Resultado<ReferenciaKek, ErrorAlmacenKek> AlmacenKekRespaldoMacOS::guardar(const KekRespaldo& kek)
{
    using R = Resultado<ReferenciaKek, ErrorAlmacenKek>;
    if (!m_ambitoValido) {
        return R::fallo(respaldo::errorAmbitoInvalido());
    }
    const bool contrasena = kek.tipo() == KekRespaldo::Tipo::Contrasena;
    if (!kek.valida() || kek.clave().tamano() != respaldo::kTamanoKek || (contrasena && !kek.parametros().validos())) {
        return R::fallo(ErrorAlmacenKek::de(ErrorAlmacenKek::Categoria::KekInvalida, QStringLiteral("kek.invalida")));
    }

    BufferSecreto dato(kTamanoDato); // en cero
    std::uint8_t* p = dato.datos();
    std::memcpy(p, kMagic, sizeof kMagic);
    p[kOffsetTipo] = contrasena ? kTipoContrasena : kTipoRecuperacion;
    if (contrasena) {
        const ParametrosKdf& kdf = kek.parametros();
        escribirU32(p + kOffsetMemoria, kdf.memoriaKib);
        escribirU32(p + kOffsetIteraciones, kdf.iteraciones);
        escribirU32(p + kOffsetParalelismo, kdf.paralelismo);
        std::memcpy(p + kOffsetSal, kdf.sal.constData(), respaldo::kTamanoSal);
    }
    std::memcpy(p + kOffsetKek, kek.clave().datos(), respaldo::kTamanoKek);

    const ReferenciaKek referencia = ReferenciaKek::generar();
    const std::int32_t status = m_keychain->agregar(servicio(), referencia.texto(), dato);
    // `dato` se limpia al destruirse (BufferSecreto).
    if (status != osstatus::kSuccess) {
        // Un duplicado con un UUID nuevo no deberia ocurrir: Interno.
        if (status == osstatus::kDuplicateItem) {
            return R::fallo(ErrorAlmacenKek::de(ErrorAlmacenKek::Categoria::Interno, QStringLiteral("keychain.agregar"),
                                                static_cast<int>(status)));
        }
        return R::fallo(errorDe(status, "keychain.agregar"));
    }
    return R::exito(referencia);
}

Resultado<KekRespaldo, ErrorAlmacenKek> AlmacenKekRespaldoMacOS::cargar(const ReferenciaKek& referencia)
{
    using R = Resultado<KekRespaldo, ErrorAlmacenKek>;
    if (!m_ambitoValido) {
        return R::fallo(respaldo::errorAmbitoInvalido());
    }
    if (referencia.nula()) {
        return R::fallo(
            ErrorAlmacenKek::de(ErrorAlmacenKek::Categoria::ReferenciaInvalida, QStringLiteral("referencia.nula")));
    }
    BufferSecreto dato;
    const std::int32_t status = m_keychain->copiar(servicio(), referencia.texto(), dato);
    if (status != osstatus::kSuccess) {
        return R::fallo(errorDe(status, "keychain.copiar"));
    }
    const auto danada = [](const char* diagnostico) {
        return R::fallo(ErrorAlmacenKek::de(ErrorAlmacenKek::Categoria::Danada, QString::fromLatin1(diagnostico)));
    };
    if (dato.tamano() != kTamanoDato) {
        return danada("kek.tamano");
    }
    const std::uint8_t* p = dato.datos();
    if (std::memcmp(p, kMagic, sizeof kMagic) != 0 || p[5] != 0 || p[6] != 0 || p[7] != 0) {
        return danada("kek.formato");
    }
    BufferSecreto clave = BufferSecreto::desdeBytes(p + kOffsetKek, respaldo::kTamanoKek);
    KekRespaldo kek;
    if (p[kOffsetTipo] == kTipoContrasena) {
        ParametrosKdf kdf;
        kdf.memoriaKib = leerU32(p + kOffsetMemoria);
        kdf.iteraciones = leerU32(p + kOffsetIteraciones);
        kdf.paralelismo = leerU32(p + kOffsetParalelismo);
        kdf.sal = QByteArray(reinterpret_cast<const char*>(p + kOffsetSal), respaldo::kTamanoSal);
        kek = KekRespaldo::desdeBytes(KekRespaldo::Tipo::Contrasena, std::move(kdf), std::move(clave));
    } else if (p[kOffsetTipo] == kTipoRecuperacion) {
        for (std::size_t i = kOffsetMemoria; i < kOffsetKek; ++i) {
            if (p[i] != 0) {
                return danada("kek.formato");
            }
        }
        kek = KekRespaldo::desdeBytes(KekRespaldo::Tipo::Recuperacion, ParametrosKdf{}, std::move(clave));
    } else {
        return danada("kek.tipo");
    }
    if (!kek.valida()) {
        return danada("kek.parametros");
    }
    return R::exito(std::move(kek));
}

Resultado<Exito, ErrorAlmacenKek> AlmacenKekRespaldoMacOS::borrar(const ReferenciaKek& referencia)
{
    using R = Resultado<Exito, ErrorAlmacenKek>;
    if (!m_ambitoValido) {
        return R::fallo(respaldo::errorAmbitoInvalido());
    }
    if (referencia.nula()) {
        return R::fallo(
            ErrorAlmacenKek::de(ErrorAlmacenKek::Categoria::ReferenciaInvalida, QStringLiteral("referencia.nula")));
    }
    const std::int32_t status = m_keychain->eliminar(servicio(), referencia.texto());
    if (status != osstatus::kSuccess && status != osstatus::kItemNotFound) {
        return R::fallo(errorDe(status, "keychain.eliminar"));
    }
    return R::exito(Exito{});
}

Resultado<QList<ReferenciaKek>, ErrorAlmacenKek> AlmacenKekRespaldoMacOS::listar()
{
    using R = Resultado<QList<ReferenciaKek>, ErrorAlmacenKek>;
    if (!m_ambitoValido) {
        return R::fallo(respaldo::errorAmbitoInvalido());
    }
    QStringList cuentas;
    const std::int32_t status = m_keychain->listarCuentas(servicio(), cuentas);
    if (status != osstatus::kSuccess) {
        return R::fallo(errorDe(status, "keychain.listar"));
    }
    QList<ReferenciaKek> referencias;
    for (const QString& cuenta : std::as_const(cuentas)) {
        if (auto r = ReferenciaKek::desdeTexto(cuenta)) {
            referencias.append(*r);
        }
    }
    return R::exito(std::move(referencias));
}

} // namespace satcfdi::secrets
