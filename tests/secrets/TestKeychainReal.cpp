// Prueba OPCIONAL contra Keychain real (T005, DA8). Solo se compila con
// -DSATCFDI_KEYCHAIN_REAL_TESTS=ON. Usa un servicio aislado y aleatorio y
// borra todo lo que crea. Hace QSKIP cuando el entorno no aplica
// (errSecMissingEntitlement sin firma/entitlements, interaccion no permitida,
// keychain no disponible); falla ante cualquier otro resultado inesperado.

#include "FixturesEFirma.h"

#include "infrastructure/secrets/macos/KeychainApiMacOS.h"
#include "infrastructure/secrets/macos/MacOSSecretStore.h"

#include <QRandomGenerator>
#include <QTest>

using namespace satcfdi;
namespace os = satcfdi::secrets::osstatus;

class TestKeychainReal : public QObject {
    Q_OBJECT

private:
    QString m_bundle;
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
                                        m_bundle + QStringLiteral(".api")}) {
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
};

QTEST_GUILESS_MAIN(TestKeychainReal)
#include "TestKeychainReal.moc"
