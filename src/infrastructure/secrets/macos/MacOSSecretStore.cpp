#include "infrastructure/secrets/macos/MacOSSecretStore.h"

#include "domain/common/UuidCanonico.h"
#include "infrastructure/crypto/ContenedorCredencial.h"
#include "infrastructure/crypto/EFirmaOpenSsl.h"
#include "infrastructure/secrets/macos/KeychainApiMacOS.h"

#include <QCryptographicHash>
#include <QSet>

namespace satcfdi {

namespace {

using Categoria = ErrorSecretStore::Categoria;
namespace os = secrets::osstatus;

const QLatin1StringView kExtension(".scc");
const QLatin1StringView kPrefijoTemporal(".tmp-");
constexpr qsizetype kLargoNombre = 32; // hex

bool esHexMinusculas(QStringView texto)
{
    for (const QChar c : texto) {
        const char16_t u = c.unicode();
        if (!((u >= u'0' && u <= u'9') || (u >= u'a' && u <= u'f'))) {
            return false;
        }
    }
    return true;
}

} // namespace

// Resultado de abrir una generacion (archivo + Keychain + AEAD).
struct MacOSSecretStore::Apertura {
    enum class Tipo { Ok, Faltante, Danada, Error };
    Tipo tipo = Tipo::Error;
    ErrorSecretStore error;
    crypto::CargaContenedor carga;
};

MacOSSecretStore::MacOSSecretStore(QString directorioCredenciales, QString bundleId,
                                   std::shared_ptr<secrets::KeychainApi> keychain)
    : m_directorio(std::move(directorioCredenciales))
    , m_bundleId(std::move(bundleId))
    , m_keychain(std::move(keychain))
{
    Q_ASSERT(m_keychain);
    Q_ASSERT(!m_bundleId.isEmpty());
}

MacOSSecretStore::~MacOSSecretStore() = default;

std::unique_ptr<MacOSSecretStore> MacOSSecretStore::conKeychainDelSistema(QString directorioCredenciales,
                                                                          QString bundleId)
{
    return std::make_unique<MacOSSecretStore>(std::move(directorioCredenciales), std::move(bundleId),
                                              std::make_shared<secrets::KeychainApiMacOS>());
}

QString MacOSSecretStore::servicioContrasena() const
{
    return m_bundleId + QStringLiteral(".efirma.v1.password");
}

QString MacOSSecretStore::servicioClaveEnvoltura() const
{
    return m_bundleId + QStringLiteral(".efirma.v1.wrapping-key");
}

QString MacOSSecretStore::nombreContenedor(QStringView uuid)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayView("satcfdi.scs1.contenedor:"));
    hash.addData(uuid.toUtf8());
    return QString::fromLatin1(hash.result().toHex().left(kLargoNombre)) + kExtension;
}

bool MacOSSecretStore::esNombreContenedor(QStringView nombre)
{
    return nombre.size() == kLargoNombre + kExtension.size() && nombre.endsWith(kExtension)
        && esHexMinusculas(nombre.left(kLargoNombre));
}

bool MacOSSecretStore::esNombreTemporal(QStringView nombre)
{
    return nombre.size() == kPrefijoTemporal.size() + kLargoNombre && nombre.startsWith(kPrefijoTemporal)
        && esHexMinusculas(nombre.mid(kPrefijoTemporal.size()));
}

// --- prepararEFirma ---------------------------------------------------------

Resultado<CredencialPreparada, ErrorSecretStore>
MacOSSecretStore::prepararEFirma(EntradaEFirma&& entrada, QStringView rfcEsperado, const QDateTime& ahoraUtc)
{
    using R = Resultado<CredencialPreparada, ErrorSecretStore>;
    // Consume la entrada: la contrasena se limpia al salir de esta funcion.
    EntradaEFirma local = std::move(entrada);

    auto certificado = secrets::leerArchivoEntrada(local.rutaCertificado, crypto::kMaxCertificadoDer);
    // T005.1 (DA4): ArchivoIlegible/FormatoInvalido de lectura -> origen por archivo.
    if (!certificado) {
        return R::fallo(std::move(certificado).error().conOrigen(OrigenErrorEFirma::Certificado));
    }
    auto llave = secrets::leerArchivoEntrada(local.rutaLlavePrivada, crypto::kMaxLlaveDer);
    if (!llave) {
        return R::fallo(std::move(llave).error().conOrigen(OrigenErrorEFirma::Llave));
    }

    // Todo se valida antes de escribir nada.
    auto validada = crypto::validarEFirma(certificado.valor(), llave.valor(), local.contrasena, rfcEsperado, ahoraUtc);
    if (!validada) {
        return R::fallo(std::move(validada).error());
    }

    auto clave = crypto::generarClaveContenedor();
    if (!clave) {
        return R::fallo(std::move(clave).error());
    }
    auto carga = crypto::empaquetarCarga(certificado.valor(), llave.valor());
    if (!carga) {
        return R::fallo(std::move(carga).error());
    }
    const CredencialRef referencia = CredencialRef::generar();
    auto contenedor = crypto::cifrarContenedor(clave.valor(), referencia.uuid(), carga.valor());
    if (!contenedor) {
        return R::fallo(std::move(contenedor).error());
    }
    auto sufijo = crypto::bytesAleatorios(kLargoNombre / 2);
    if (!sufijo) {
        return R::fallo(std::move(sufijo).error());
    }

    // A partir de aqui hay efectos: cualquier fallo descarta la generacion.
    struct Descarte {
        MacOSSecretStore* store;
        const CredencialRef* referencia;
        bool armado = true;
        ~Descarte()
        {
            if (armado) {
                (void)store->eliminar(*referencia);
            }
        }
    } descarte{this, &referencia};

    if (auto r = m_directorio.asegurar(); !r) {
        return R::fallo(std::move(r).error());
    }
    const QString temporal = kPrefijoTemporal + QString::fromLatin1(sufijo.valor().toHex());
    if (auto r = m_directorio.escribirAtomico(nombreContenedor(referencia.uuid()), temporal, contenedor.valor());
        !r) {
        return R::fallo(std::move(r).error());
    }
    if (const auto s = m_keychain->agregar(servicioClaveEnvoltura(), referencia.uuid(), clave.valor());
        s != os::kSuccess) {
        return R::fallo(secrets::errorDeOSStatus(s, "keychain.agregar.clave"));
    }
    if (const auto s = m_keychain->agregar(servicioContrasena(), referencia.uuid(), local.contrasena);
        s != os::kSuccess) {
        return R::fallo(secrets::errorDeOSStatus(s, "keychain.agregar.contrasena"));
    }

    descarte.armado = false;
    return R::exito(CredencialPreparada(referencia, validada.valor().metadata, [this](const CredencialRef& ref) {
        (void)eliminar(ref);
    }));
}

// --- Apertura comun -----------------------------------------------------------

MacOSSecretStore::Apertura MacOSSecretStore::abrir(const CredencialRef& referencia, bool verificarContrasena)
{
    Apertura a;
    if (referencia.esNula()) {
        a.tipo = Apertura::Tipo::Faltante;
        return a;
    }
    QByteArray cifrado;
    switch (m_directorio.leer(nombreContenedor(referencia.uuid()), crypto::kMaxContenedor, cifrado)) {
    case secrets::DirectorioCredenciales::Lectura::NoExiste:
        a.tipo = Apertura::Tipo::Faltante;
        return a;
    case secrets::DirectorioCredenciales::Lectura::Error:
        a.tipo = Apertura::Tipo::Danada;
        return a;
    case secrets::DirectorioCredenciales::Lectura::Ok:
        break;
    }

    BufferSecreto clave;
    if (const auto s = m_keychain->copiar(servicioClaveEnvoltura(), referencia.uuid(), clave); s != os::kSuccess) {
        if (s == os::kItemNotFound) {
            a.tipo = Apertura::Tipo::Faltante;
        } else {
            a.tipo = Apertura::Tipo::Error;
            a.error = secrets::errorDeOSStatus(s, "keychain.copiar.clave");
        }
        return a;
    }
    if (verificarContrasena) {
        if (const auto s = m_keychain->existe(servicioContrasena(), referencia.uuid()); s != os::kSuccess) {
            if (s == os::kItemNotFound) {
                a.tipo = Apertura::Tipo::Faltante;
            } else {
                a.tipo = Apertura::Tipo::Error;
                a.error = secrets::errorDeOSStatus(s, "keychain.existe.contrasena");
            }
            return a;
        }
    }

    auto carga = crypto::descifrarContenedor(clave, referencia.uuid(),
                                             reinterpret_cast<const std::uint8_t*>(cifrado.constData()),
                                             static_cast<std::size_t>(cifrado.size()));
    if (!carga) {
        a.tipo = Apertura::Tipo::Danada;
        return a;
    }
    auto partes = crypto::desempaquetarCarga(carga.valor());
    if (!partes) {
        a.tipo = Apertura::Tipo::Danada;
        return a;
    }
    a.carga = std::move(partes).valor();
    a.tipo = Apertura::Tipo::Ok;
    return a;
}

// --- obtenerEstado / obtenerMaterialFirma -------------------------------------

Resultado<EstadoCredencial, ErrorSecretStore> MacOSSecretStore::obtenerEstado(const CredencialRef& referencia,
                                                                              const QDateTime& ahoraUtc)
{
    using R = Resultado<EstadoCredencial, ErrorSecretStore>;
    Apertura a = abrir(referencia, true);
    switch (a.tipo) {
    case Apertura::Tipo::Faltante:
        return R::exito(EstadoCredencial::MaterialFaltante);
    case Apertura::Tipo::Danada:
        return R::exito(EstadoCredencial::MaterialDanado);
    case Apertura::Tipo::Error:
        return R::fallo(std::move(a.error));
    case Apertura::Tipo::Ok:
        break;
    }
    auto info = crypto::leerCertificado(a.carga.certificadoDer.datos(), a.carga.certificadoDer.tamano());
    if (!info) {
        return R::exito(EstadoCredencial::MaterialDanado);
    }
    return R::exito(crypto::estadoPorVigencia(info.valor().metadata, ahoraUtc));
}

Resultado<MaterialFirma, ErrorSecretStore> MacOSSecretStore::obtenerMaterialFirma(const CredencialRef& referencia)
{
    using R = Resultado<MaterialFirma, ErrorSecretStore>;
    Apertura a = abrir(referencia, false);
    switch (a.tipo) {
    case Apertura::Tipo::Faltante:
        return R::fallo(ErrorSecretStore::de(Categoria::CredencialNoEncontrada, QStringLiteral("material.faltante")));
    case Apertura::Tipo::Danada:
        return R::fallo(ErrorSecretStore::de(Categoria::CredencialDanada, QStringLiteral("material.contenedor")));
    case Apertura::Tipo::Error:
        return R::fallo(std::move(a.error));
    case Apertura::Tipo::Ok:
        break;
    }
    BufferSecreto contrasena;
    if (const auto s = m_keychain->copiar(servicioContrasena(), referencia.uuid(), contrasena); s != os::kSuccess) {
        return R::fallo(secrets::errorDeOSStatus(s, "keychain.copiar.contrasena"));
    }
    auto llave = crypto::descifrarLlavePkcs8(a.carga.llaveCifradaDer, contrasena);
    if (!llave) {
        return R::fallo(ErrorSecretStore::de(Categoria::CredencialDanada, QStringLiteral("material.llave")));
    }
    return R::exito(
        MaterialFirma(std::move(a.carga.certificadoDer), std::move(llave).valor(), std::move(contrasena)));
}

// --- eliminar / reconciliar ---------------------------------------------------

Resultado<Exito, ErrorSecretStore> MacOSSecretStore::eliminar(const CredencialRef& referencia)
{
    using R = Resultado<Exito, ErrorSecretStore>;
    if (referencia.esNula()) {
        return R::exito(Exito{});
    }
    // Se intentan las tres partes aunque alguna falle; se reporta la primera.
    std::optional<ErrorSecretStore> primero;
    if (auto r = m_directorio.eliminar(nombreContenedor(referencia.uuid())); !r) {
        primero = std::move(r).error();
    }
    const auto s1 = m_keychain->eliminar(servicioClaveEnvoltura(), referencia.uuid());
    if (s1 != os::kSuccess && s1 != os::kItemNotFound && !primero) {
        primero = secrets::errorDeOSStatus(s1, "keychain.eliminar.clave");
    }
    const auto s2 = m_keychain->eliminar(servicioContrasena(), referencia.uuid());
    if (s2 != os::kSuccess && s2 != os::kItemNotFound && !primero) {
        primero = secrets::errorDeOSStatus(s2, "keychain.eliminar.contrasena");
    }
    if (primero) {
        return R::fallo(std::move(*primero));
    }
    return R::exito(Exito{});
}

Resultado<ResumenReconciliacion, ErrorSecretStore>
MacOSSecretStore::reconciliar(const QList<CredencialRef>& vigentes)
{
    using R = Resultado<ResumenReconciliacion, ErrorSecretStore>;

    QSet<QString> uuidsVigentes;
    QSet<QString> archivosVigentes;
    for (const CredencialRef& ref : vigentes) {
        if (!ref.esNula()) {
            uuidsVigentes.insert(ref.uuid());
            archivosVigentes.insert(nombreContenedor(ref.uuid()));
        }
    }

    // Enumerar TODO antes de borrar nada: si algo no se puede enumerar con
    // certeza, se falla sin efectos.
    QStringList cuentasClave;
    if (const auto s = m_keychain->listarCuentas(servicioClaveEnvoltura(), cuentasClave); s != os::kSuccess) {
        return R::fallo(secrets::errorDeOSStatus(s, "keychain.listar.clave"));
    }
    QStringList cuentasContrasena;
    if (const auto s = m_keychain->listarCuentas(servicioContrasena(), cuentasContrasena); s != os::kSuccess) {
        return R::fallo(secrets::errorDeOSStatus(s, "keychain.listar.contrasena"));
    }
    auto archivos = m_directorio.listar();
    if (!archivos) {
        return R::fallo(std::move(archivos).error());
    }

    ResumenReconciliacion resumen;
    resumen.generacionesConservadas = static_cast<int>(uuidsVigentes.size());

    // Generaciones huerfanas conocidas por Keychain (solo cuentas con forma de
    // UUID canonico: lo demas no lo creo este adaptador).
    QSet<QString> huerfanas;
    for (const QString& cuenta : cuentasClave + cuentasContrasena) {
        if (uuid::esCanonico(cuenta) && !uuidsVigentes.contains(cuenta)) {
            huerfanas.insert(cuenta);
        }
    }
    QSet<QString> archivosCubiertos = archivosVigentes;
    for (const QString& cuenta : std::as_const(huerfanas)) {
        archivosCubiertos.insert(nombreContenedor(cuenta));
        const auto ref = CredencialRef::desdeUuid(cuenta);
        if (ref && eliminar(*ref)) {
            ++resumen.generacionesEliminadas;
        } else {
            ++resumen.fallosLimpieza;
        }
    }

    // Contenedores sin items Keychain y temporales abandonados.
    for (const QString& nombre : std::as_const(archivos.valor())) {
        const bool contenedorHuerfano = esNombreContenedor(nombre) && !archivosCubiertos.contains(nombre);
        if (!contenedorHuerfano && !esNombreTemporal(nombre)) {
            continue;
        }
        if (m_directorio.eliminar(nombre)) {
            if (contenedorHuerfano) {
                ++resumen.generacionesEliminadas;
            }
        } else {
            ++resumen.fallosLimpieza;
        }
    }
    return R::exito(resumen);
}

} // namespace satcfdi
