// Pruebas de satcfdi_crypto (T005, corte B): validacion de e.firma con
// fixtures sinteticos generados en la prueba y contenedor AEAD v1.

#include "FixturesEFirma.h"

#include "infrastructure/crypto/ContenedorCredencial.h"
#include "infrastructure/crypto/EFirmaOpenSsl.h"

#include <QTest>

#include <memory>

using namespace satcfdi;
using Categoria = ErrorSecretStore::Categoria;

namespace {

const QString kUuid = QStringLiteral("0b6f4c4e-6a8e-4d55-9a3f-2f1f6a1b9c01");
const QString kOtroUuid = QStringLiteral("0b6f4c4e-6a8e-4d55-9a3f-2f1f6a1b9c02");

BufferSecreto buffer(const QByteArray& b)
{
    return fixtures::FixturesEFirma::bufferDe(b);
}

} // namespace

class TestCrypto : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<fixtures::FixturesEFirma> fx;

    Resultado<crypto::EFirmaValidada, ErrorSecretStore> validar(const QString& cer, const QString& key,
                                                                const QByteArray& contrasena,
                                                                const QString& rfc = fixtures::kRfc,
                                                                const QDateTime& ahora = fixtures::ahoraVigente())
    {
        return crypto::validarEFirma(buffer(fx->leer(cer)), buffer(fx->leer(key)), buffer(contrasena), rfc, ahora);
    }

    void esperarCategoria(const Resultado<crypto::EFirmaValidada, ErrorSecretStore>& r, Categoria c)
    {
        QVERIFY(!r.esExito());
        QCOMPARE(claveEstable(r.error().categoria), claveEstable(c));
    }

private slots:
    void initTestCase()
    {
        fx = std::make_unique<fixtures::FixturesEFirma>();
        QVERIFY2(fx->valido(), qPrintable(fx->error()));
    }

    // --- Exito y metadata. ---
    void efirmaValidaConCadaEsquemaPkcs8_data()
    {
        QTest::addColumn<QString>("llave");
        QTest::newRow("PBES2 3DES") << QStringLiteral("efirma.key");
        QTest::newRow("PBES2 AES-256") << QStringLiteral("efirma_aes.key");
        QTest::newRow("PKCS#5 v1.5 PBE-SHA1-3DES") << QStringLiteral("efirma_pbe1.key");
    }
    void efirmaValidaConCadaEsquemaPkcs8()
    {
        QFETCH(QString, llave);
        const auto r = validar(QStringLiteral("efirma.cer"), llave, fx->contrasena());
        QVERIFY2(r.esExito(), r.esExito() ? "" : qPrintable(r.error().diagnostico));
        QCOMPARE(r.valor().metadata.numeroSerie, fixtures::kSerie);
        QCOMPARE(r.valor().metadata.vigenteDesde, fixtures::utc(2025, 1, 1));
        QCOMPARE(r.valor().metadata.vigenteHasta, fixtures::utc(2029, 1, 1));
    }

    void leerCertificadoExtraeRfcYMetadata()
    {
        const QByteArray der = fx->leer(QStringLiteral("efirma.cer"));
        const auto info = crypto::leerCertificado(reinterpret_cast<const std::uint8_t*>(der.constData()),
                                                  static_cast<std::size_t>(der.size()));
        QVERIFY(info.esExito());
        QCOMPARE(info.valor().rfc, fixtures::kRfc);
        QVERIFY(info.valor().rfcValido);
        QVERIFY(!info.valor().tieneUnidadOrganizacional);

        const QByteArray csd = fx->leer(QStringLiteral("csd.cer"));
        const auto infoCsd = crypto::leerCertificado(reinterpret_cast<const std::uint8_t*>(csd.constData()),
                                                     static_cast<std::size_t>(csd.size()));
        QVERIFY(infoCsd.esExito());
        QVERIFY(infoCsd.valor().tieneUnidadOrganizacional);
    }

    // --- Cada categoria. ---
    void contrasenaIncorrecta()
    {
        esperarCategoria(validar(QStringLiteral("efirma.cer"), QStringLiteral("efirma.key"), "otra-clave"),
                         Categoria::ContrasenaIncorrecta);
        esperarCategoria(validar(QStringLiteral("efirma.cer"), QStringLiteral("efirma_pbe1.key"), ""),
                         Categoria::ContrasenaIncorrecta);
    }
    void parejaIncompatible()
    {
        esperarCategoria(validar(QStringLiteral("efirma.cer"), QStringLiteral("segundo_par.key"), fx->contrasena()),
                         Categoria::ParejaIncompatible);
    }
    void csdConOuNoEsEFirma()
    {
        // Mismo RFC y par valido: solo la regla DU2 (OU no vacio) lo rechaza.
        esperarCategoria(validar(QStringLiteral("csd.cer"), QStringLiteral("csd.key"), fx->contrasena()),
                         Categoria::NoEsEFirma);
    }
    void rfcNoCoincide()
    {
        const auto r = validar(QStringLiteral("otro_rfc.cer"), QStringLiteral("otro_rfc.key"), fx->contrasena());
        esperarCategoria(r, Categoria::RfcNoCoincide);
        // El diagnostico no revela ninguno de los dos RFC.
        QVERIFY(!r.error().diagnostico.contains(fixtures::kOtroRfc));
        QVERIFY(!r.error().diagnostico.contains(fixtures::kRfc));
        esperarCategoria(validar(QStringLiteral("efirma.cer"), QStringLiteral("efirma.key"), fx->contrasena(),
                                 QString()),
                         Categoria::RfcNoCoincide);
    }
    void vigenciaConRelojInyectado()
    {
        esperarCategoria(validar(QStringLiteral("efirma.cer"), QStringLiteral("efirma.key"), fx->contrasena(),
                                 fixtures::kRfc, fixtures::antesDeVigencia()),
                         Categoria::NoVigenteAun);
        esperarCategoria(validar(QStringLiteral("efirma.cer"), QStringLiteral("efirma.key"), fx->contrasena(),
                                 fixtures::kRfc, fixtures::despuesDeVigencia()),
                         Categoria::Vencida);
        // Limite inferior inclusivo.
        QVERIFY(validar(QStringLiteral("efirma.cer"), QStringLiteral("efirma.key"), fx->contrasena(), fixtures::kRfc,
                        fixtures::utc(2025, 1, 1))
                    .esExito());
    }
    void formatosInvalidos()
    {
        // Certificado PEM, llave sin cifrar, llave como certificado y basura.
        esperarCategoria(validar(QStringLiteral("efirma.pem"), QStringLiteral("efirma.key"), fx->contrasena()),
                         Categoria::FormatoInvalido);
        esperarCategoria(validar(QStringLiteral("efirma.cer"), QStringLiteral("llave_sin_cifrar.key"), fx->contrasena()),
                         Categoria::FormatoInvalido);
        esperarCategoria(validar(QStringLiteral("efirma.key"), QStringLiteral("efirma.key"), fx->contrasena()),
                         Categoria::FormatoInvalido);

        const QByteArray cer = fx->leer(QStringLiteral("efirma.cer"));
        const QByteArray key = fx->leer(QStringLiteral("efirma.key"));
        const auto con = [&](const QByteArray& c, const QByteArray& k) {
            return crypto::validarEFirma(buffer(c), buffer(k), buffer(fx->contrasena()), fixtures::kRfc,
                                         fixtures::ahoraVigente());
        };
        esperarCategoria(con(cer.left(cer.size() / 2), key), Categoria::FormatoInvalido); // truncado
        esperarCategoria(con(cer + QByteArray(1, '\0'), key), Categoria::FormatoInvalido); // bytes sobrantes
        esperarCategoria(con(QByteArray(), key), Categoria::FormatoInvalido);
        esperarCategoria(con(QByteArray(70 * 1024, '\x30'), key), Categoria::FormatoInvalido); // > limite
        esperarCategoria(con(cer, key.left(key.size() - 3)), Categoria::FormatoInvalido);
        esperarCategoria(con(cer, QByteArray("no es der")), Categoria::FormatoInvalido);
    }

    void descifrarLlaveDevuelvePkcs8SinCifrar()
    {
        const auto r = crypto::descifrarLlavePkcs8(buffer(fx->leer(QStringLiteral("efirma.key"))),
                                                   buffer(fx->contrasena()));
        QVERIFY(r.esExito());
        const QByteArray esperado = fx->leer(QStringLiteral("llave_sin_cifrar.key"));
        QVERIFY(r.valor().igualA(esperado.constData(), static_cast<std::size_t>(esperado.size())));

        const auto mala = crypto::descifrarLlavePkcs8(buffer(fx->leer(QStringLiteral("efirma.key"))), buffer("x"));
        QVERIFY(!mala.esExito());
        QCOMPARE(mala.error().categoria, Categoria::ContrasenaIncorrecta);
    }

    // --- DS1: normalizacion del valor 2.5.4.45. ---
    void rfcDesdeValor_data()
    {
        QTest::addColumn<QByteArray>("valor");
        QTest::addColumn<QString>("esperado");
        QTest::newRow("RFC / CURP") << QByteArray("GOMA800101AB1 / GOMA800101HDFRRN09") << fixtures::kRfc;
        QTest::newRow("minusculas y espacios") << QByteArray("  goma800101ab1 /x") << fixtures::kRfc;
        QTest::newRow("solo RFC moral") << QByteArray("ABC010101AB1") << QStringLiteral("ABC010101AB1");
        QTest::newRow("sin separador con espacios") << QByteArray(" ABC010101AB1 ") << QStringLiteral("ABC010101AB1");
        QTest::newRow("control") << QByteArray("GOMA800101AB1\t/") << QString();
        QTest::newRow("NUL") << QByteArray("GOMA800101AB1\0", 14) << QString();
        QTest::newRow("no ASCII") << QByteArray("GOMÑ800101AB1") << QString();
        QTest::newRow("gramatica invalida") << QByteArray("GOMA8001AB1") << QString();
        QTest::newRow("vacio izquierdo") << QByteArray(" / GOMA800101HDFRRN09") << QString();
    }
    void rfcDesdeValor()
    {
        QFETCH(QByteArray, valor);
        QFETCH(QString, esperado);
        QCOMPARE(crypto::rfcDesdeValorX500UniqueIdentifier(reinterpret_cast<const std::uint8_t*>(valor.constData()),
                                                           static_cast<std::size_t>(valor.size())),
                 esperado);
    }

    // --- DS3: contenedor AEAD v1. ---
    void contenedorIdaYVuelta()
    {
        auto clave = crypto::generarClaveContenedor();
        QVERIFY(clave.esExito());
        QCOMPARE(clave.valor().tamano(), crypto::kTamanoClaveContenedor);
        const QByteArray cer = fx->leer(QStringLiteral("efirma.cer"));
        const QByteArray key = fx->leer(QStringLiteral("efirma.key"));
        auto carga = crypto::empaquetarCarga(buffer(cer), buffer(key));
        QVERIFY(carga.esExito());

        auto blob = crypto::cifrarContenedor(clave.valor(), kUuid, carga.valor());
        QVERIFY(blob.esExito());
        QCOMPARE(blob.valor().left(4), QByteArray("SCC1"));
        QCOMPARE(static_cast<std::size_t>(blob.valor().size()),
                 crypto::kTamanoCabecera + carga.valor().tamano() + crypto::kTamanoTag);
        // El texto plano no aparece en el ciphertext.
        QVERIFY(!blob.valor().contains(cer.mid(100, 32)));

        const auto* datos = reinterpret_cast<const std::uint8_t*>(blob.valor().constData());
        const auto tamano = static_cast<std::size_t>(blob.valor().size());
        auto plano = crypto::descifrarContenedor(clave.valor(), kUuid, datos, tamano);
        QVERIFY(plano.esExito());
        auto partes = crypto::desempaquetarCarga(plano.valor());
        QVERIFY(partes.esExito());
        QVERIFY(partes.valor().certificadoDer.igualA(cer.constData(), static_cast<std::size_t>(cer.size())));
        QVERIFY(partes.valor().llaveCifradaDer.igualA(key.constData(), static_cast<std::size_t>(key.size())));

        // Nonce aleatorio: dos cifrados de la misma carga difieren.
        auto otro = crypto::cifrarContenedor(clave.valor(), kUuid, carga.valor());
        QVERIFY(otro.esExito());
        QVERIFY(otro.valor().mid(7, 12) != blob.valor().mid(7, 12));
    }

    void contenedorRechazaManipulacion()
    {
        auto clave = crypto::generarClaveContenedor();
        auto carga = crypto::empaquetarCarga(buffer("certificado"), buffer("llave"));
        auto blob = crypto::cifrarContenedor(clave.valor(), kUuid, carga.valor());
        QVERIFY(blob.esExito());
        const QByteArray original = blob.valor();

        const auto descifrar = [&](const QByteArray& b, const BufferSecreto& k, const QString& uuid) {
            return crypto::descifrarContenedor(k, uuid, reinterpret_cast<const std::uint8_t*>(b.constData()),
                                               static_cast<std::size_t>(b.size()));
        };
        const auto danado = [](const Resultado<BufferSecreto, ErrorSecretStore>& r) {
            return !r.esExito() && r.error().categoria == Categoria::CredencialDanada;
        };

        QVERIFY(descifrar(original, clave.valor(), kUuid).esExito());
        QVERIFY(danado(descifrar(original, clave.valor(), kOtroUuid))); // AAD: otra generacion
        auto otraClave = crypto::generarClaveContenedor();
        QVERIFY(danado(descifrar(original, otraClave.valor(), kUuid)));
        QVERIFY(danado(descifrar(original, buffer("corta"), kUuid)));

        for (const int posicion : {0, 4, 5, 6, 10, static_cast<int>(crypto::kTamanoCabecera),
                                   static_cast<int>(original.size() - 1)}) {
            QByteArray alterado = original;
            alterado[posicion] = static_cast<char>(alterado[posicion] ^ 0x01);
            QVERIFY2(danado(descifrar(alterado, clave.valor(), kUuid)), qPrintable(QString::number(posicion)));
        }
        QVERIFY(danado(descifrar(original.left(original.size() - 1), clave.valor(), kUuid)));
        QVERIFY(danado(descifrar(original + "x", clave.valor(), kUuid)));
        QVERIFY(danado(descifrar(QByteArray(), clave.valor(), kUuid)));
        // ctLen enorme: se rechaza antes de reservar memoria.
        QByteArray enorme = original;
        enorme[19] = '\x7F';
        QVERIFY(danado(descifrar(enorme, clave.valor(), kUuid)));
    }
};

QTEST_GUILESS_MAIN(TestCrypto)
#include "TestCrypto.moc"
