// Prueba OPCIONAL contra Keychain real (T005, DA8). Solo se compila con
// -DSATCFDI_KEYCHAIN_REAL_TESTS=ON. Usa un servicio aislado y aleatorio y
// borra todo lo que crea. Hace QSKIP cuando el entorno no aplica
// (errSecMissingEntitlement sin firma/entitlements, interaccion no permitida,
// keychain no disponible); falla ante cualquier otro resultado inesperado.

#include "FixturesEFirma.h"

#include "infrastructure/secrets/macos/AlmacenKekRespaldoMacOS.h"
#include "infrastructure/secrets/macos/KeychainApiMacOS.h"
#include "infrastructure/secrets/macos/MacOSSecretStore.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <QRandomGenerator>
#include <QUuid>
#include <QTest>

using namespace satcfdi;
namespace os = satcfdi::secrets::osstatus;

class TestKeychainReal : public QObject {
    Q_OBJECT

private:
    QString m_bundle;
    const QString m_ambitoKek = QUuid::createUuid().toString(QUuid::WithoutBraces);
    std::shared_ptr<secrets::KeychainApiMacOS> m_api = std::make_shared<secrets::KeychainApiMacOS>();

    bool noAplica(std::int32_t status) const
    {
        return status == os::kMissingEntitlement || status == os::kInteractionNotAllowed
            || status == os::kInteractionRequired || status == os::kNotAvailable
            || status == os::kNoSuchKeychain || status == os::kServiceNotAvailable;
    }

private slots:
    void initTestCase()
    {
        m_bundle = QStringLiteral("mx.adenium.satcfdi-downloader.test-%1")
                       .arg(QRandomGenerator::system()->generate64(), 16, 16, QLatin1Char('0'));
    }

    void cleanupTestCase()
    {
        // Barrido del namespace aislado (best effort).
        for (const QString& servicio : {m_bundle + QStringLiteral(".efirma.v1.password"),
                                        m_bundle + QStringLiteral(".efirma.v1.wrapping-key"),
                                        m_bundle + QStringLiteral(".api"),
                                        m_bundle + QStringLiteral(".respaldo.v1.kek.") + m_ambitoKek}) {
            QStringList cuentas;
            if (m_api->listarCuentas(servicio, cuentas) == os::kSuccess) {
                for (const QString& c : std::as_const(cuentas)) {
                    m_api->eliminar(servicio, c);
                }
            }
        }
    }

    void apiBasica()
    {
        const QString servicio = m_bundle + QStringLiteral(".api");
        const QString cuenta = CredencialRef::generar().uuid();
        const BufferSecreto dato = BufferSecreto::desdeBytes("valor-prueba", 12);
        const std::int32_t alta = m_api->agregar(servicio, cuenta, dato);
        if (noAplica(alta)) {
            QSKIP(qPrintable(QStringLiteral("Keychain real no aplica en este entorno (OSStatus %1)").arg(alta)));
        }
        QCOMPARE(alta, os::kSuccess);
        QCOMPARE(m_api->agregar(servicio, cuenta, dato), os::kDuplicateItem);
        QCOMPARE(m_api->existe(servicio, cuenta), os::kSuccess);
        BufferSecreto leido;
        QCOMPARE(m_api->copiar(servicio, cuenta, leido), os::kSuccess);
        QVERIFY(leido.igualA("valor-prueba", 12));
        QStringList cuentas;
        QCOMPARE(m_api->listarCuentas(servicio, cuentas), os::kSuccess);
        QCOMPARE(cuentas, QStringList{cuenta});
        QCOMPARE(m_api->eliminar(servicio, cuenta), os::kSuccess);
        QCOMPARE(m_api->eliminar(servicio, cuenta), os::kItemNotFound);
        QCOMPARE(m_api->existe(servicio, cuenta), os::kItemNotFound);
        QCOMPARE(m_api->listarCuentas(servicio, cuentas), os::kSuccess);
        QVERIFY(cuentas.isEmpty());
    }

    void storeCompleto()
    {
        fixtures::FixturesEFirma fx;
        QVERIFY2(fx.valido(), qPrintable(fx.error()));
        QTemporaryDir dir;
        MacOSSecretStore store(dir.filePath(QStringLiteral("credentials")), m_bundle, m_api);
        auto r = store.prepararEFirma(fx.entrada(QStringLiteral("efirma.cer"), QStringLiteral("efirma.key")),
                                      fixtures::kRfc, fixtures::ahoraVigente());
        if (!r.esExito() && r.error().codigoNativo && noAplica(*r.error().codigoNativo)) {
            QSKIP(qPrintable(QStringLiteral("Keychain real no aplica (OSStatus %1)").arg(*r.error().codigoNativo)));
        }
        QVERIFY2(r.esExito(), r.esExito() ? "" : qPrintable(claveEstable(r.error().categoria)));
        CredencialPreparada p = std::move(r).valor();
        p.confirmar();
        const CredencialRef ref = p.referencia();
        QCOMPARE(store.obtenerEstado(ref, fixtures::ahoraVigente()).valor(), EstadoCredencial::Lista);
        QVERIFY(store.obtenerMaterialFirma(ref).esExito());
        const auto rec = store.reconciliar({});
        QVERIFY(rec.esExito());
        QCOMPARE(rec.valor().generacionesEliminadas, 1);
        QCOMPARE(store.obtenerEstado(ref, fixtures::ahoraVigente()).valor(), EstadoCredencial::MaterialFaltante);
    }

    // T025.4 (D4): ida y vuelta de la KEK del respaldo y atributos del item
    // leidos del propio Keychain: no sincronizable y ThisDeviceOnly.
    void kekRespaldoNoSincronizableThisDeviceOnly()
    {
        using namespace satcfdi::respaldo;
        secrets::AlmacenKekRespaldoMacOS almacen(m_bundle, m_ambitoKek, m_api);
        QByteArray sal(int(kTamanoSal), '\x5a');
        BufferSecreto clave(kTamanoKek);
        for (std::size_t i = 0; i < kTamanoKek; ++i) {
            clave.datos()[i] = static_cast<std::uint8_t>(0x30 + i);
        }
        const KekRespaldo kek = KekRespaldo::desdeBytes(KekRespaldo::Tipo::Contrasena,
                                                        ParametrosKdf::predeterminados(sal), std::move(clave));
        QVERIFY(kek.valida());
        const auto ref = almacen.guardar(kek);
        if (!ref.esExito() && ref.error().codigoNativo && noAplica(*ref.error().codigoNativo)) {
            QSKIP(qPrintable(QStringLiteral("Keychain real no aplica (OSStatus %1)").arg(*ref.error().codigoNativo)));
        }
        QVERIFY(ref.esExito());

        const auto cargada = almacen.cargar(ref.valor());
        QVERIFY(cargada.esExito());
        QCOMPARE(cargada.valor().parametros(), kek.parametros());
        QVERIFY(cargada.valor().clave().igualA(kek.clave().datos(), kek.clave().tamano()));

        // Atributos tal como los guardo Keychain (consulta independiente del
        // adaptador; kSecAttrSynchronizableAny para no filtrar por el valor).
        CFMutableDictionaryRef q = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                             &kCFTypeDictionaryValueCallBacks);
        CFStringRef servicio = almacen.servicio().toCFString();
        CFStringRef cuenta = ref.valor().texto().toCFString();
        CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
        CFDictionarySetValue(q, kSecAttrService, servicio);
        CFDictionarySetValue(q, kSecAttrAccount, cuenta);
        CFDictionarySetValue(q, kSecUseDataProtectionKeychain, kCFBooleanTrue);
        CFDictionarySetValue(q, kSecAttrSynchronizable, kSecAttrSynchronizableAny);
        CFDictionarySetValue(q, kSecReturnAttributes, kCFBooleanTrue);
        CFDictionarySetValue(q, kSecMatchLimit, kSecMatchLimitOne);
        CFTypeRef resultado = nullptr;
        const OSStatus status = SecItemCopyMatching(q, &resultado);
        CFRelease(q);
        CFRelease(servicio);
        CFRelease(cuenta);
        QCOMPARE(status, errSecSuccess);
        QVERIFY(resultado != nullptr && CFGetTypeID(resultado) == CFDictionaryGetTypeID());
        const auto atributos = static_cast<CFDictionaryRef>(resultado);
        const CFTypeRef sincronizable = CFDictionaryGetValue(atributos, kSecAttrSynchronizable);
        const CFTypeRef accesible = CFDictionaryGetValue(atributos, kSecAttrAccessible);
        // Ausente equivale a no sincronizable; si viene, debe ser false.
        const bool noSincronizable = sincronizable == nullptr
            || (CFGetTypeID(sincronizable) == CFBooleanGetTypeID()
                && !CFBooleanGetValue(static_cast<CFBooleanRef>(sincronizable)))
            || (CFGetTypeID(sincronizable) == CFNumberGetTypeID() && [&] {
                   int v = -1;
                   CFNumberGetValue(static_cast<CFNumberRef>(sincronizable), kCFNumberIntType, &v);
                   return v == 0;
               }());
        const bool thisDeviceOnly =
            accesible != nullptr && CFEqual(accesible, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
        CFRelease(resultado);
        QVERIFY(noSincronizable);
        QVERIFY(thisDeviceOnly);

        QVERIFY(almacen.borrar(ref.valor()).esExito());
        QCOMPARE(almacen.cargar(ref.valor()).error().categoria, ErrorAlmacenKek::Categoria::NoEncontrada);
    }
};

QTEST_GUILESS_MAIN(TestKeychainReal)
#include "TestKeychainReal.moc"
