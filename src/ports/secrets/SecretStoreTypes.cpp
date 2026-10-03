// memset_s (C11 Anexo K) debe declararse antes de cualquier <string.h>.
#define __STDC_WANT_LIB_EXT1__ 1
#include <string.h>

#include "ports/secrets/SecretStoreTypes.h"

#include "domain/common/UuidCanonico.h"

#include <new>

namespace satcfdi {

namespace secretos {

void limpiarMemoria(void* datos, std::size_t n) noexcept
{
    if (datos == nullptr || n == 0) {
        return;
    }
#if defined(__APPLE__) || defined(__STDC_LIB_EXT1__)
    (void)memset_s(datos, n, 0, n);
#else
    volatile std::uint8_t* p = static_cast<volatile std::uint8_t*>(datos);
    while (n--) {
        *p++ = 0;
    }
#endif
}

} // namespace secretos

// --- BufferSecreto ---------------------------------------------------------

BufferSecreto::BufferSecreto(std::size_t tamano)
{
    if (tamano > 0) {
        m_datos = new std::uint8_t[tamano]();
        m_tamano = tamano;
    }
}

BufferSecreto::~BufferSecreto()
{
    limpiar();
}

BufferSecreto::BufferSecreto(BufferSecreto&& otro) noexcept
    : m_datos(std::exchange(otro.m_datos, nullptr))
    , m_tamano(std::exchange(otro.m_tamano, 0))
{
}

BufferSecreto& BufferSecreto::operator=(BufferSecreto&& otro) noexcept
{
    if (this != &otro) {
        limpiar();
        m_datos = std::exchange(otro.m_datos, nullptr);
        m_tamano = std::exchange(otro.m_tamano, 0);
    }
    return *this;
}

BufferSecreto BufferSecreto::desdeBytes(const void* datos, std::size_t tamano)
{
    BufferSecreto b(tamano);
    if (tamano > 0 && datos != nullptr) {
        memcpy(b.m_datos, datos, tamano);
    }
    return b;
}

BufferSecreto BufferSecreto::desdeTexto(QStringView texto)
{
    // Primera pasada: longitud UTF-8 exacta, para no reasignar (cada
    // reasignacion dejaria una copia sin limpiar).
    auto puntoCodigo = [&](qsizetype& i) -> char32_t {
        const char16_t c = texto.at(i).unicode();
        if (QChar::isHighSurrogate(c) && i + 1 < texto.size()
            && QChar::isLowSurrogate(texto.at(i + 1).unicode())) {
            const char32_t cp = QChar::surrogateToUcs4(c, texto.at(i + 1).unicode());
            i += 2;
            return cp;
        }
        ++i;
        if (QChar::isSurrogate(c)) {
            return 0xFFFD;
        }
        return c;
    };
    auto largo = [](char32_t cp) -> std::size_t {
        return cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    };

    std::size_t total = 0;
    for (qsizetype i = 0; i < texto.size();) {
        total += largo(puntoCodigo(i));
    }
    BufferSecreto b(total);
    std::uint8_t* out = b.m_datos;
    for (qsizetype i = 0; i < texto.size();) {
        const char32_t cp = puntoCodigo(i);
        switch (largo(cp)) {
        case 1:
            *out++ = static_cast<std::uint8_t>(cp);
            break;
        case 2:
            *out++ = static_cast<std::uint8_t>(0xC0 | (cp >> 6));
            *out++ = static_cast<std::uint8_t>(0x80 | (cp & 0x3F));
            break;
        case 3:
            *out++ = static_cast<std::uint8_t>(0xE0 | (cp >> 12));
            *out++ = static_cast<std::uint8_t>(0x80 | ((cp >> 6) & 0x3F));
            *out++ = static_cast<std::uint8_t>(0x80 | (cp & 0x3F));
            break;
        default:
            *out++ = static_cast<std::uint8_t>(0xF0 | (cp >> 18));
            *out++ = static_cast<std::uint8_t>(0x80 | ((cp >> 12) & 0x3F));
            *out++ = static_cast<std::uint8_t>(0x80 | ((cp >> 6) & 0x3F));
            *out++ = static_cast<std::uint8_t>(0x80 | (cp & 0x3F));
            break;
        }
    }
    return b;
}

bool BufferSecreto::igualA(const void* datos, std::size_t tamano) const noexcept
{
    if (tamano != m_tamano) {
        return false;
    }
    const auto* otro = static_cast<const std::uint8_t*>(datos);
    std::uint8_t diferencia = 0;
    for (std::size_t i = 0; i < tamano; ++i) {
        diferencia |= static_cast<std::uint8_t>(m_datos[i] ^ otro[i]);
    }
    return diferencia == 0;
}

void BufferSecreto::limpiar() noexcept
{
    if (m_datos != nullptr) {
        secretos::limpiarMemoria(m_datos, m_tamano);
        delete[] m_datos;
    }
    m_datos = nullptr;
    m_tamano = 0;
}

// --- CredencialRef -------------------------------------------------------

namespace {

constexpr QStringView kEsquema = u"scs1";

QStringView sufijoRol(RolReferencia rol)
{
    switch (rol) {
    case RolReferencia::Certificado:
        return u"cert";
    case RolReferencia::Contenedor:
        return u"container";
    case RolReferencia::Contrasena:
        return u"password";
    }
    return u"";
}

} // namespace

CredencialRef CredencialRef::generar()
{
    return CredencialRef(uuid::generarCanonico());
}

std::optional<CredencialRef> CredencialRef::desdeUuid(QStringView texto)
{
    if (!uuid::esCanonico(texto)) {
        return std::nullopt;
    }
    return CredencialRef(texto.toString());
}

std::optional<CredencialRef> CredencialRef::desdeReferencia(QStringView referencia,
                                                            RolReferencia rolEsperado)
{
    // Forma exacta: "scs1" ":" uuid(36) ":" rol.
    const QStringView rol = sufijoRol(rolEsperado);
    const qsizetype esperado = kEsquema.size() + 1 + 36 + 1 + rol.size();
    if (referencia.size() != esperado || !referencia.startsWith(kEsquema)
        || referencia.at(kEsquema.size()) != u':' || referencia.at(kEsquema.size() + 37) != u':'
        || !referencia.endsWith(rol)) {
        return std::nullopt;
    }
    return desdeUuid(referencia.mid(kEsquema.size() + 1, 36));
}

std::optional<CredencialRef> CredencialRef::desdeReferencias(QStringView certificadoRef,
                                                             QStringView llavePrivadaRef,
                                                             QStringView contrasenaRef)
{
    const auto cert = desdeReferencia(certificadoRef, RolReferencia::Certificado);
    const auto contenedor = desdeReferencia(llavePrivadaRef, RolReferencia::Contenedor);
    const auto contrasena = desdeReferencia(contrasenaRef, RolReferencia::Contrasena);
    if (!cert || !contenedor || !contrasena || !(*cert == *contenedor)
        || !(*cert == *contrasena)) {
        return std::nullopt;
    }
    return cert;
}

QString CredencialRef::referencia(RolReferencia rol) const
{
    if (esNula()) {
        return {};
    }
    return kEsquema.toString() + u':' + m_uuid + u':' + sufijoRol(rol).toString();
}

// --- CredencialPreparada ---------------------------------------------------

CredencialPreparada::CredencialPreparada(CredencialRef referencia, MetadataCredencial metadata,
                                         Descartador descartador)
    : m_referencia(std::move(referencia))
    , m_metadata(std::move(metadata))
    , m_descartador(std::move(descartador))
{
}

CredencialPreparada::~CredencialPreparada()
{
    descartar();
}

CredencialPreparada::CredencialPreparada(CredencialPreparada&& otra) noexcept
    : m_referencia(std::exchange(otra.m_referencia, CredencialRef()))
    , m_metadata(std::move(otra.m_metadata))
    , m_descartador(std::exchange(otra.m_descartador, Descartador()))
    , m_confirmada(std::exchange(otra.m_confirmada, false))
{
}

CredencialPreparada& CredencialPreparada::operator=(CredencialPreparada&& otra) noexcept
{
    if (this != &otra) {
        descartar();
        m_referencia = std::exchange(otra.m_referencia, CredencialRef());
        m_metadata = std::move(otra.m_metadata);
        m_descartador = std::exchange(otra.m_descartador, Descartador());
        m_confirmada = std::exchange(otra.m_confirmada, false);
    }
    return *this;
}

void CredencialPreparada::descartar() noexcept
{
    if (!m_confirmada && !m_referencia.esNula() && m_descartador) {
        try {
            m_descartador(m_referencia);
        } catch (...) {
            // Best effort: la reconciliacion limpia el residuo.
        }
    }
    m_descartador = Descartador();
    m_referencia = CredencialRef();
    m_confirmada = false;
}

// --- Claves estables -------------------------------------------------------

QString claveEstable(EstadoCredencial estado)
{
    switch (estado) {
    case EstadoCredencial::SinCredencial: return QStringLiteral("SinCredencial");
    case EstadoCredencial::Validando: return QStringLiteral("Validando");
    case EstadoCredencial::Lista: return QStringLiteral("Lista");
    case EstadoCredencial::Vencida: return QStringLiteral("Vencida");
    case EstadoCredencial::NoVigenteAun: return QStringLiteral("NoVigenteAun");
    case EstadoCredencial::MaterialFaltante: return QStringLiteral("MaterialFaltante");
    case EstadoCredencial::MaterialDanado: return QStringLiteral("MaterialDanado");
    }
    return {};
}

QString claveEstable(OrigenErrorEFirma origen)
{
    switch (origen) {
    case OrigenErrorEFirma::Ninguno: return QStringLiteral("Ninguno");
    case OrigenErrorEFirma::Certificado: return QStringLiteral("Certificado");
    case OrigenErrorEFirma::Llave: return QStringLiteral("Llave");
    case OrigenErrorEFirma::Contrasena: return QStringLiteral("Contrasena");
    }
    return {};
}

QString claveEstable(ErrorSecretStore::Categoria categoria)
{
    using C = ErrorSecretStore::Categoria;
    switch (categoria) {
    case C::ArchivoIlegible: return QStringLiteral("ArchivoIlegible");
    case C::FormatoInvalido: return QStringLiteral("FormatoInvalido");
    case C::ContrasenaIncorrecta: return QStringLiteral("ContrasenaIncorrecta");
    case C::ParejaIncompatible: return QStringLiteral("ParejaIncompatible");
    case C::RfcNoCoincide: return QStringLiteral("RfcNoCoincide");
    case C::NoEsEFirma: return QStringLiteral("NoEsEFirma");
    case C::Vencida: return QStringLiteral("Vencida");
    case C::NoVigenteAun: return QStringLiteral("NoVigenteAun");
    case C::CredencialNoEncontrada: return QStringLiteral("CredencialNoEncontrada");
    case C::CredencialDanada: return QStringLiteral("CredencialDanada");
    case C::AlmacenBloqueado: return QStringLiteral("AlmacenBloqueado");
    case C::AccesoDenegado: return QStringLiteral("AccesoDenegado");
    case C::CanceladoPorUsuario: return QStringLiteral("CanceladoPorUsuario");
    case C::AlmacenMalConfigurado: return QStringLiteral("AlmacenMalConfigurado");
    case C::AlmacenNoDisponible: return QStringLiteral("AlmacenNoDisponible");
    case C::FalloEscritura: return QStringLiteral("FalloEscritura");
    case C::Interno: return QStringLiteral("Interno");
    }
    return {};
}

} // namespace satcfdi
