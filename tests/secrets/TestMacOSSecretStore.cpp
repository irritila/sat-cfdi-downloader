// Pruebas de MacOSSecretStore con FakeKeychainApi (T005, corte B): exito,
// reemplazo, residuos, reconciliacion, material danado/faltante, mapeo de
// OSStatus y centinelas de fuga (DS5).

#include "FakeKeychainApi.h"
#include "FixturesEFirma.h"

#include "infrastructure/secrets/macos/MacOSSecretStore.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTest>

#include <memory>
#include <sys/stat.h>

using namespace satcfdi;
using Categoria = ErrorSecretStore::Categoria;
using Op = fakes::FakeKeychainApi::Operacion;
namespace os = satcfdi::secrets::osstatus;

namespace {

const QString kBundle = QStringLiteral("mx.adenium.satcfdi-downloader.pruebas");

QStringList gMensajes;
QtMessageHandler gManejadorPrevio = nullptr;

void capturar(QtMsgType tipo, const QMessageLogContext& contexto, const QString& mensaje)
{
    gMensajes.append(mensaje);
    if (gManejadorPrevio != nullptr) {
        gManejadorPrevio(tipo, contexto, mensaje);
    }
}

int modo(const QString& ruta)
{
    struct stat st {};
    if (::lstat(QFile::encodeName(ruta).constData(), &st) != 0) {
        return -1;
    }
    return static_cast<int>(st.st_mode & 0777);
}

} // namespace

class TestMacOSSecretStore : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<fixtures::FixturesEFirma> fx;
    std::unique_ptr<QTemporaryDir> raiz;
    std::shared_ptr<fakes::FakeKeychainApi> keychain;
    std::unique_ptr<MacOSSecretStore> store;
    QList<QByteArray> centinelas;

    QString dirCredenciales() const { return raiz->filePath(QStringLiteral("datos/credentials")); }

    QStringList archivos() const
    {
        return QDir(dirCredenciales()).entryList(QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    }

    Resultado<CredencialPreparada, ErrorSecretStore> preparar(const QString& cer = QStringLiteral("efirma.cer"),
                                                             const QString& key = QStringLiteral("efirma.key"),
                                                             const QDateTime& ahora = fixtures::ahoraVigente())
    {
        return store->prepararEFirma(fx->entrada(cer, key), fixtures::kRfc, ahora);
    }

    CredencialRef prepararYConfirmar()
    {
        auto r = preparar();
        if (!r.esExito()) {
            return {};
        }
        CredencialPreparada p = std::move(r).valor();
        p.confirmar();
        return p.referencia();
    }

    void verificarSinResiduos()
    {
        QVERIFY2(archivos().isEmpty(), qPrintable(archivos().join(u',')));
        QCOMPARE(keychain->cantidad(), std::size_t{0});
    }

    // DS5: hallazgos de centinelas en el directorio de datos de la prueba
    // (incluye el contenedor cifrado, que puede existir pero no debe contener
    // texto plano), en los mensajes de log capturados y en `extra`.
    QStringList fugas(const QStringList& extra = {}) const
    {
        QStringList hallazgos;
        QDirIterator it(raiz->path(), QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString ruta = it.next();
            QFile f(ruta);
            if (!f.open(QIODevice::ReadOnly)) {
                hallazgos.append(QStringLiteral("ilegible: ") + QFileInfo(ruta).fileName());
                continue;
            }
            const QByteArray contenido = f.readAll();
            for (const QByteArray& c : std::as_const(centinelas)) {
                if (contenido.contains(c)) {
                    hallazgos.append(QStringLiteral("archivo: ") + QFileInfo(ruta).fileName());
                }
            }
        }
        const QStringList textos = gMensajes + extra;
        for (const QString& t : textos) {
            const QByteArray utf8 = t.toUtf8();
            for (const QByteArray& c : std::as_const(centinelas)) {
                if (utf8.contains(c)) {
                    hallazgos.append(QStringLiteral("mensaje"));
                }
            }
            if (t.contains(fx->directorio()) || t.contains(raiz->path()) || t.contains(fixtures::kRfc)) {
                hallazgos.append(QStringLiteral("mensaje con ruta o RFC"));
            }
        }
        return hallazgos;
    }

    void verificarCentinelas(const QStringList& extra = {})
    {
        const QStringList hallazgos = fugas(extra);
        QVERIFY2(hallazgos.isEmpty(), qPrintable(hallazgos.join(u';')));
    }

private slots:
    void initTestCase()
    {
        fx = std::make_unique<fixtures::FixturesEFirma>();
        QVERIFY2(fx->valido(), qPrintable(fx->error()));
        gManejadorPrevio = qInstallMessageHandler(capturar);

        // Centinelas DS5: contrasena, ventanas del DER del .cer y del .key,
        // su base64 y su hex, y la llave descifrada.
        const QByteArray cer = fx->leer(QStringLiteral("efirma.cer"));
        const QByteArray key = fx->leer(QStringLiteral("efirma.key"));
        const QByteArray plana = fx->leer(QStringLiteral("llave_sin_cifrar.key"));
        centinelas = {fx->contrasena(), cer.mid(200, 48), key.mid(200, 48), plana.mid(300, 48),
                      cer.toBase64().mid(200, 48), key.toBase64().mid(200, 48), cer.toHex().mid(400, 64),
                      key.toHex().mid(400, 64), plana.toBase64().mid(400, 48)};
    }

    void cleanupTestCase() { qInstallMessageHandler(gManejadorPrevio); }

    void init()
    {
        raiz = std::make_unique<QTemporaryDir>();
        QVERIFY(raiz->isValid());
        keychain = std::make_shared<fakes::FakeKeychainApi>();
        store = std::make_unique<MacOSSecretStore>(dirCredenciales(), kBundle, keychain);
        gMensajes.clear();
    }

    void cleanup()
    {
        store.reset();
        keychain.reset();
        raiz.reset();
    }

    // --- Exito. ---
    void importacionExitosa()
    {
        auto r = preparar();
        QVERIFY2(r.esExito(), r.esExito() ? "" : qPrintable(claveEstable(r.error().categoria)));
        CredencialPreparada p = std::move(r).valor();
        const CredencialRef ref = p.referencia();
        QVERIFY(!ref.esNula());
        QCOMPARE(p.metadata().numeroSerie, fixtures::kSerie);
        QCOMPARE(p.metadata().vigenteDesde, fixtures::utc(2025, 1, 1));
        QCOMPARE(p.metadata().vigenteHasta, fixtures::utc(2029, 1, 1));

        // Archivo 0600 con nombre derivado (no contiene el UUID), dir 0700.
        QCOMPARE(modo(dirCredenciales()), 0700);
        const QStringList lista = archivos();
        QCOMPARE(lista.size(), 1);
        QCOMPARE(lista.first(), MacOSSecretStore::nombreContenedor(ref.uuid()));
        QVERIFY(!lista.first().contains(ref.uuid()));
        QCOMPARE(modo(dirCredenciales() + u'/' + lista.first()), 0600);

        // Keychain: servicios versionados, cuenta = UUID; contrasena exacta;
        // clave de 32 bytes.
        QCOMPARE(store->servicioContrasena(), kBundle + QStringLiteral(".efirma.v1.password"));
        QCOMPARE(store->servicioClaveEnvoltura(), kBundle + QStringLiteral(".efirma.v1.wrapping-key"));
        QCOMPARE(keychain->cantidad(), std::size_t{2});
        const BufferSecreto* pwd = keychain->dato(store->servicioContrasena(), ref.uuid());
        QVERIFY(pwd != nullptr);
        QVERIFY(pwd->igualA(fx->contrasena().constData(), static_cast<std::size_t>(fx->contrasena().size())));
        const BufferSecreto* clave = keychain->dato(store->servicioClaveEnvoltura(), ref.uuid());
        QVERIFY(clave != nullptr);
        QCOMPARE(clave->tamano(), std::size_t{32});

        p.confirmar();
        p = CredencialPreparada(); // destruir confirmada: no descarta
        QCOMPARE(archivos().size(), 1);

        auto estado = store->obtenerEstado(ref, fixtures::ahoraVigente());
        QVERIFY(estado.esExito());
        QCOMPARE(estado.valor(), EstadoCredencial::Lista);
        QCOMPARE(store->obtenerEstado(ref, fixtures::antesDeVigencia()).valor(), EstadoCredencial::NoVigenteAun);
        QCOMPARE(store->obtenerEstado(ref, fixtures::despuesDeVigencia()).valor(), EstadoCredencial::Vencida);

        auto material = store->obtenerMaterialFirma(ref);
        QVERIFY(material.esExito());
        const QByteArray cer = fx->leer(QStringLiteral("efirma.cer"));
        const QByteArray plana = fx->leer(QStringLiteral("llave_sin_cifrar.key"));
        QVERIFY(material.valor().certificadoDer().igualA(cer.constData(), static_cast<std::size_t>(cer.size())));
        QVERIFY(material.valor().llavePrivadaDer().igualA(plana.constData(), static_cast<std::size_t>(plana.size())));
        QVERIFY(material.valor().contrasena().igualA(fx->contrasena().constData(),
                                                     static_cast<std::size_t>(fx->contrasena().size())));
        verificarCentinelas();
    }

    void preparadaSinConfirmarSeDescarta()
    {
        {
            auto r = preparar();
            QVERIFY(r.esExito());
            QCOMPARE(archivos().size(), 1);
        } // RAII: descartador
        verificarSinResiduos();
    }

    // --- Validacion antes de escribir: sin residuos. ---
    void fallosDeValidacionNoDejanResiduos_data()
    {
        QTest::addColumn<QString>("cer");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QByteArray>("contrasena");
        QTest::addColumn<QDateTime>("ahora");
        QTest::addColumn<int>("categoria");
        QTest::addColumn<int>("origen");
        const QByteArray ok; // vacio = contrasena del fixture
        const auto c = [](Categoria x) { return static_cast<int>(x); };
        // T005.1 (DA4): columna `origen` esperado (OrigenErrorEFirma).
        const auto o = [](OrigenErrorEFirma x) { return static_cast<int>(x); };
        const int ninguno = o(OrigenErrorEFirma::Ninguno);
        QTest::newRow("contrasena") << "efirma.cer" << "efirma.key" << QByteArray("mala") << fixtures::ahoraVigente()
                                    << c(Categoria::ContrasenaIncorrecta) << o(OrigenErrorEFirma::Contrasena);
        QTest::newRow("pareja") << "efirma.cer" << "segundo_par.key" << ok << fixtures::ahoraVigente()
                                << c(Categoria::ParejaIncompatible) << ninguno;
        QTest::newRow("csd") << "csd.cer" << "csd.key" << ok << fixtures::ahoraVigente() << c(Categoria::NoEsEFirma)
                             << ninguno;
        QTest::newRow("rfc") << "otro_rfc.cer" << "otro_rfc.key" << ok << fixtures::ahoraVigente()
                             << c(Categoria::RfcNoCoincide) << ninguno;
        QTest::newRow("vencida") << "efirma.cer" << "efirma.key" << ok << fixtures::despuesDeVigencia()
                                 << c(Categoria::Vencida) << ninguno;
        QTest::newRow("no vigente") << "efirma.cer" << "efirma.key" << ok << fixtures::antesDeVigencia()
                                    << c(Categoria::NoVigenteAun) << ninguno;
        QTest::newRow("pem") << "efirma.pem" << "efirma.key" << ok << fixtures::ahoraVigente()
                             << c(Categoria::FormatoInvalido) << o(OrigenErrorEFirma::Certificado);
        QTest::newRow("sin cifrar") << "efirma.cer" << "llave_sin_cifrar.key" << ok << fixtures::ahoraVigente()
                                    << c(Categoria::FormatoInvalido) << o(OrigenErrorEFirma::Llave);
        QTest::newRow("cer inexistente") << "no-existe.cer" << "efirma.key" << ok << fixtures::ahoraVigente()
                                         << c(Categoria::ArchivoIlegible) << o(OrigenErrorEFirma::Certificado);
        QTest::newRow("key directorio") << "efirma.cer" << "." << ok << fixtures::ahoraVigente()
                                        << c(Categoria::ArchivoIlegible) << o(OrigenErrorEFirma::Llave);
        QTest::newRow("key inexistente") << "efirma.cer" << "no-existe.key" << ok << fixtures::ahoraVigente()
                                         << c(Categoria::ArchivoIlegible) << o(OrigenErrorEFirma::Llave);
        QTest::newRow("cer en lugar de key") << "efirma.cer" << "efirma.cer" << ok << fixtures::ahoraVigente()
                                             << c(Categoria::FormatoInvalido) << o(OrigenErrorEFirma::Llave);
    }
    void fallosDeValidacionNoDejanResiduos()
    {
        QFETCH(QString, cer);
        QFETCH(QString, key);
        QFETCH(QByteArray, contrasena);
        QFETCH(QDateTime, ahora);
        QFETCH(int, categoria);
        QFETCH(int, origen);
        EntradaEFirma e = fx->entrada(cer, key);
        if (!contrasena.isEmpty()) {
            e.contrasena = fixtures::FixturesEFirma::bufferDe(contrasena);
        }
        const auto r = store->prepararEFirma(std::move(e), fixtures::kRfc, ahora);
        QVERIFY(!r.esExito());
        QCOMPARE(claveEstable(r.error().categoria), claveEstable(static_cast<Categoria>(categoria)));
        QCOMPARE(claveEstable(r.error().origen), claveEstable(static_cast<OrigenErrorEFirma>(origen)));
        QVERIFY(e.contrasena.vacio()); // la entrada se consumio
        verificarSinResiduos();
        QVERIFY(!QFileInfo::exists(dirCredenciales())); // nada se escribio
        verificarCentinelas({r.error().diagnostico});
    }

    // --- Fallos de Keychain/escritura: sin residuos. ---
    void fallosDeAlmacenNoDejanResiduos_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<bool>("enContrasena"); // false: falla al guardar la clave
        QTest::addColumn<int>("categoria");
        const auto c = [](Categoria x) { return static_cast<int>(x); };
        QTest::newRow("bloqueado/clave") << int(os::kInteractionNotAllowed) << false << c(Categoria::AlmacenBloqueado);
        QTest::newRow("bloqueado/pwd") << int(os::kInteractionNotAllowed) << true << c(Categoria::AlmacenBloqueado);
        QTest::newRow("denegado") << int(os::kAuthFailed) << true << c(Categoria::AccesoDenegado);
        QTest::newRow("cancelado") << int(os::kUserCanceled) << true << c(Categoria::CanceladoPorUsuario);
        QTest::newRow("entitlement") << int(os::kMissingEntitlement) << false << c(Categoria::AlmacenMalConfigurado);
        QTest::newRow("no disponible") << int(os::kNotAvailable) << true << c(Categoria::AlmacenNoDisponible);
        QTest::newRow("solo lectura") << int(os::kReadOnly) << true << c(Categoria::FalloEscritura);
        QTest::newRow("duplicado") << int(os::kDuplicateItem) << true << c(Categoria::Interno);
    }
    void fallosDeAlmacenNoDejanResiduos()
    {
        QFETCH(int, status);
        QFETCH(bool, enContrasena);
        QFETCH(int, categoria);
        keychain->fallos[Op::Agregar] = {static_cast<std::int32_t>(status),
                                         enContrasena ? store->servicioContrasena() : store->servicioClaveEnvoltura()};
        const auto r = preparar();
        QVERIFY(!r.esExito());
        QCOMPARE(claveEstable(r.error().categoria), claveEstable(static_cast<Categoria>(categoria)));
        QCOMPARE(r.error().codigoNativo, std::optional<int>(status));
        QCOMPARE(r.error().origen, OrigenErrorEFirma::Ninguno); // fallos de almacen: sin campo
        verificarSinResiduos();
        verificarCentinelas({r.error().diagnostico});
    }

    void falloDeEscrituraSinResiduos()
    {
        // La ruta de credenciales es un archivo: no se puede crear el directorio.
        QVERIFY(QDir().mkpath(QFileInfo(dirCredenciales()).absolutePath()));
        QFile bloqueo(dirCredenciales());
        QVERIFY(bloqueo.open(QIODevice::WriteOnly));
        bloqueo.close();
        const auto r = preparar();
        QVERIFY(!r.esExito());
        QCOMPARE(r.error().categoria, Categoria::FalloEscritura);
        QCOMPARE(keychain->cantidad(), std::size_t{0});
    }

    void directorioEnlaceSimbolicoSeRechaza()
    {
        QTemporaryDir otro;
        QVERIFY(QDir().mkpath(QFileInfo(dirCredenciales()).absolutePath()));
        QVERIFY(QFile::link(otro.path(), dirCredenciales()));
        const auto r = preparar();
        QVERIFY(!r.esExito());
        QCOMPARE(r.error().categoria, Categoria::FalloEscritura);
        QVERIFY(QDir(otro.path()).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot));
        QCOMPARE(keychain->cantidad(), std::size_t{0});
    }

    // Revision de seguridad 03 (TOCTOU): `credentials/` se sustituye DESPUES
    // de crearse y usarse. Toda operacion posterior falla de forma segura: no
    // escribe, lee ni borra en el destino sustituto.
    void sustitucionDelDirectorioTrasCreacion()
    {
        const CredencialRef a = prepararYConfirmar();
        QVERIFY(!a.esNula());
        const QString nombreA = MacOSSecretStore::nombreContenedor(a.uuid());
        const QString real = dirCredenciales() + QStringLiteral(".real");
        QVERIFY(QDir().rename(dirCredenciales(), real));

        // `credentials` -> enlace a un directorio ajeno con un senuelo que
        // tiene el nombre del contenedor de `a`.
        QTemporaryDir ajeno;
        QVERIFY(QFile::link(ajeno.path(), dirCredenciales()));
        QFile senuelo(ajeno.filePath(nombreA));
        QVERIFY(senuelo.open(QIODevice::WriteOnly));
        senuelo.write("senuelo");
        senuelo.close();
        const auto contenidoAjeno = [&] {
            return QDir(ajeno.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        };

        // Lectura: no lee el senuelo; el material se reporta danado.
        QCOMPARE(store->obtenerEstado(a, fixtures::ahoraVigente()).valor(), EstadoCredencial::MaterialDanado);
        QCOMPARE(store->obtenerMaterialFirma(a).error().categoria, Categoria::CredencialDanada);

        // Escritura: FalloEscritura; nada en el directorio ajeno ni en Keychain.
        const auto b = preparar();
        QVERIFY(!b.esExito());
        QCOMPARE(b.error().categoria, Categoria::FalloEscritura);
        QCOMPARE(contenidoAjeno(), QStringList{nombreA});
        QCOMPARE(keychain->cantidad(), std::size_t{2});

        // Reconciliacion: no puede enumerar el directorio -> falla sin borrar.
        const auto r = store->reconciliar({});
        QVERIFY(!r.esExito());
        QCOMPARE(r.error().categoria, Categoria::FalloEscritura);
        QCOMPARE(keychain->cantidad(), std::size_t{2});

        // Eliminar: el senuelo ajeno NO se borra (FalloEscritura); los items
        // Keychain de la generacion si se eliminan (el contenedor real queda
        // ilegible y la reconciliacion lo limpia despues).
        const auto e = store->eliminar(a);
        QVERIFY(!e.esExito());
        QCOMPARE(e.error().categoria, Categoria::FalloEscritura);
        QCOMPARE(contenidoAjeno(), QStringList{nombreA});
        QCOMPARE(keychain->cantidad(), std::size_t{0});

        // Restaurar el directorio real: el residuo se reconcilia.
        QVERIFY(QFile::remove(dirCredenciales()));
        QVERIFY(QDir().rename(real, dirCredenciales()));
        QCOMPARE(store->obtenerEstado(a, fixtures::ahoraVigente()).valor(), EstadoCredencial::MaterialFaltante);
        const auto limpieza = store->reconciliar({});
        QVERIFY(limpieza.esExito());
        QCOMPARE(limpieza.valor().generacionesEliminadas, 1);
        verificarSinResiduos();
        QCOMPARE(contenidoAjeno(), QStringList{nombreA});
        verificarCentinelas({b.error().diagnostico, e.error().diagnostico, r.error().diagnostico});
    }

    void directorioConPermisosAbiertosSeCorrigeOSeRechaza()
    {
        // asegurar() (escritura) corrige a 0700 un directorio PROPIO; las
        // demas operaciones lo rechazan sin corregir.
        const CredencialRef a = prepararYConfirmar();
        QVERIFY(QFile::setPermissions(dirCredenciales(), QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                             | QFileDevice::ExeOwner | QFileDevice::ReadOther
                                                             | QFileDevice::ExeOther));
        QCOMPARE(store->obtenerEstado(a, fixtures::ahoraVigente()).valor(), EstadoCredencial::MaterialDanado);
        QVERIFY(!store->reconciliar({a}).esExito());
        QVERIFY(!prepararYConfirmar().esNula());
        QCOMPARE(modo(dirCredenciales()), 0700);
        QCOMPARE(store->obtenerEstado(a, fixtures::ahoraVigente()).valor(), EstadoCredencial::Lista);
    }

    // --- Reemplazo. ---
    void reemplazoValidoYFallido()
    {
        const CredencialRef anterior = prepararYConfirmar();
        QVERIFY(!anterior.esNula());

        // Reemplazo fallido (contrasena): la anterior sigue usable.
        EntradaEFirma mala = fx->entrada(QStringLiteral("efirma.cer"), QStringLiteral("efirma.key"));
        mala.contrasena = fixtures::FixturesEFirma::bufferDe("incorrecta");
        QVERIFY(!store->prepararEFirma(std::move(mala), fixtures::kRfc, fixtures::ahoraVigente()).esExito());
        // Reemplazo fallido (Keychain): la anterior sigue usable.
        keychain->fallos[Op::Agregar] = {os::kInteractionNotAllowed, std::nullopt};
        QVERIFY(!preparar().esExito());
        keychain->fallos.clear();
        QCOMPARE(store->obtenerEstado(anterior, fixtures::ahoraVigente()).valor(), EstadoCredencial::Lista);
        QVERIFY(store->obtenerMaterialFirma(anterior).esExito());
        QCOMPARE(archivos().size(), 1);

        // Reemplazo valido: candidata, commit (simulado), confirmar, borrar anterior.
        const CredencialRef nueva = prepararYConfirmar();
        QVERIFY(!nueva.esNula() && !(nueva == anterior));
        QCOMPARE(archivos().size(), 2);
        QVERIFY(store->eliminar(anterior).esExito());
        QCOMPARE(archivos().size(), 1);
        QCOMPARE(keychain->cantidad(), std::size_t{2});
        QCOMPARE(store->obtenerEstado(nueva, fixtures::ahoraVigente()).valor(), EstadoCredencial::Lista);
        QCOMPARE(store->obtenerEstado(anterior, fixtures::ahoraVigente()).valor(), EstadoCredencial::MaterialFaltante);
        verificarCentinelas();
    }

    // --- Material faltante / danado. ---
    void materialFaltante_data()
    {
        QTest::addColumn<int>("parte"); // 0 archivo, 1 clave, 2 contrasena
        QTest::newRow("contenedor") << 0;
        QTest::newRow("clave") << 1;
        QTest::newRow("contrasena") << 2;
    }
    void materialFaltante()
    {
        QFETCH(int, parte);
        const CredencialRef ref = prepararYConfirmar();
        QVERIFY(!ref.esNula());
        if (parte == 0) {
            QVERIFY(QFile::remove(dirCredenciales() + u'/' + MacOSSecretStore::nombreContenedor(ref.uuid())));
        } else {
            keychain->quitar(parte == 1 ? store->servicioClaveEnvoltura() : store->servicioContrasena(), ref.uuid());
        }
        const auto estado = store->obtenerEstado(ref, fixtures::ahoraVigente());
        QVERIFY(estado.esExito());
        QCOMPARE(estado.valor(), EstadoCredencial::MaterialFaltante);
        const auto material = store->obtenerMaterialFirma(ref);
        QVERIFY(!material.esExito());
        QCOMPARE(material.error().categoria, Categoria::CredencialNoEncontrada);
    }

    void materialDanado_data()
    {
        QTest::addColumn<int>("caso"); // 0 byte alterado, 1 truncado, 2 clave de otro tamano, 3 clave distinta
        QTest::newRow("byte alterado") << 0;
        QTest::newRow("truncado") << 1;
        QTest::newRow("clave corta") << 2;
        QTest::newRow("otra clave") << 3;
    }
    void materialDanado()
    {
        QFETCH(int, caso);
        const CredencialRef ref = prepararYConfirmar();
        QVERIFY(!ref.esNula());
        const QString ruta = dirCredenciales() + u'/' + MacOSSecretStore::nombreContenedor(ref.uuid());
        QFile f(ruta);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QByteArray datos = f.readAll();
        f.close();
        if (caso == 0 || caso == 1) {
            if (caso == 0) {
                datos[datos.size() / 2] = static_cast<char>(datos[datos.size() / 2] ^ 0x40);
            } else {
                datos.chop(5);
            }
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(datos);
            f.close();
        } else {
            keychain->poner(store->servicioClaveEnvoltura(), ref.uuid(),
                            caso == 2 ? QByteArray(16, 'k') : QByteArray(32, 'k'));
        }
        QCOMPARE(store->obtenerEstado(ref, fixtures::ahoraVigente()).valor(), EstadoCredencial::MaterialDanado);
        const auto material = store->obtenerMaterialFirma(ref);
        QVERIFY(!material.esExito());
        QCOMPARE(material.error().categoria, Categoria::CredencialDanada);
    }

    void contrasenaAlteradaEnKeychainEsDanada()
    {
        const CredencialRef ref = prepararYConfirmar();
        keychain->poner(store->servicioContrasena(), ref.uuid(), "otra");
        QCOMPARE(store->obtenerEstado(ref, fixtures::ahoraVigente()).valor(), EstadoCredencial::Lista);
        const auto material = store->obtenerMaterialFirma(ref);
        QVERIFY(!material.esExito());
        QCOMPARE(material.error().categoria, Categoria::CredencialDanada);
    }

    void erroresDeKeychainEnLectura_data()
    {
        QTest::addColumn<int>("operacion");
        QTest::addColumn<int>("status");
        QTest::addColumn<int>("categoria");
        const auto c = [](Categoria x) { return static_cast<int>(x); };
        QTest::newRow("copiar bloqueado") << int(Op::Copiar) << int(os::kInteractionNotAllowed)
                                          << c(Categoria::AlmacenBloqueado);
        QTest::newRow("copiar denegado") << int(Op::Copiar) << int(os::kNoAccessForItem) << c(Categoria::AccesoDenegado);
        QTest::newRow("existe no disponible") << int(Op::Existe) << int(os::kServiceNotAvailable)
                                              << c(Categoria::AlmacenNoDisponible);
        QTest::newRow("copiar entitlement") << int(Op::Copiar) << int(os::kMissingEntitlement)
                                            << c(Categoria::AlmacenMalConfigurado);
        QTest::newRow("desconocido") << int(Op::Copiar) << -1 << c(Categoria::Interno);
    }
    void erroresDeKeychainEnLectura()
    {
        QFETCH(int, operacion);
        QFETCH(int, status);
        QFETCH(int, categoria);
        const CredencialRef ref = prepararYConfirmar();
        keychain->fallos[static_cast<Op>(operacion)] = {static_cast<std::int32_t>(status), std::nullopt};
        const auto estado = store->obtenerEstado(ref, fixtures::ahoraVigente());
        QVERIFY(!estado.esExito());
        QCOMPARE(claveEstable(estado.error().categoria), claveEstable(static_cast<Categoria>(categoria)));
        if (static_cast<Op>(operacion) == Op::Copiar) {
            const auto material = store->obtenerMaterialFirma(ref);
            QVERIFY(!material.esExito());
            QCOMPARE(claveEstable(material.error().categoria), claveEstable(static_cast<Categoria>(categoria)));
        }
        keychain->fallos.clear();
        QCOMPARE(store->obtenerEstado(ref, fixtures::ahoraVigente()).valor(), EstadoCredencial::Lista);
    }

    // --- Eliminar. ---
    void eliminarEsIdempotente()
    {
        const CredencialRef ref = prepararYConfirmar();
        QVERIFY(store->eliminar(ref).esExito());
        verificarSinResiduos();
        QVERIFY(store->eliminar(ref).esExito());
        QVERIFY(store->eliminar(CredencialRef::generar()).esExito());
        QVERIFY(store->eliminar(CredencialRef()).esExito());
    }

    void eliminarConKeychainBloqueado()
    {
        const CredencialRef ref = prepararYConfirmar();
        keychain->fallos[Op::Eliminar] = {os::kInteractionNotAllowed, std::nullopt};
        const auto r = store->eliminar(ref);
        QVERIFY(!r.esExito());
        QCOMPARE(r.error().categoria, Categoria::AlmacenBloqueado);
        keychain->fallos.clear();
        QVERIFY(store->eliminar(ref).esExito());
        verificarSinResiduos();
    }

    // --- Reconciliacion. ---
    void reconciliacionEliminaHuerfanas()
    {
        const CredencialRef vigente = prepararYConfirmar();
        const CredencialRef huerfanaCompleta = prepararYConfirmar();
        const CredencialRef soloArchivo = prepararYConfirmar();
        const CredencialRef soloKeychain = prepararYConfirmar();
        keychain->quitar(store->servicioClaveEnvoltura(), soloArchivo.uuid());
        keychain->quitar(store->servicioContrasena(), soloArchivo.uuid());
        QVERIFY(QFile::remove(dirCredenciales() + u'/' + MacOSSecretStore::nombreContenedor(soloKeychain.uuid())));
        // Temporal abandonado y archivo ajeno (no se toca).
        QFile temporal(dirCredenciales() + QStringLiteral("/.tmp-0123456789abcdef0123456789abcdef"));
        QVERIFY(temporal.open(QIODevice::WriteOnly));
        temporal.close();
        QFile ajeno(dirCredenciales() + QStringLiteral("/LEEME.txt"));
        QVERIFY(ajeno.open(QIODevice::WriteOnly));
        ajeno.close();
        // Item Keychain ajeno (cuenta no UUID): no se toca.
        keychain->poner(store->servicioContrasena(), QStringLiteral("no-es-uuid"), "x");

        const auto r = store->reconciliar({vigente});
        QVERIFY(r.esExito());
        QCOMPARE(r.valor().generacionesConservadas, 1);
        QCOMPARE(r.valor().generacionesEliminadas, 3);
        QCOMPARE(r.valor().fallosLimpieza, 0);

        QStringList esperados{MacOSSecretStore::nombreContenedor(vigente.uuid()), QStringLiteral("LEEME.txt")};
        esperados.sort();
        QStringList obtenidos = archivos();
        obtenidos.sort();
        QCOMPARE(obtenidos, esperados);
        QCOMPARE(keychain->cantidad(), std::size_t{3}); // 2 de la vigente + ajeno
        QCOMPARE(store->obtenerEstado(vigente, fixtures::ahoraVigente()).valor(), EstadoCredencial::Lista);

        // Idempotente.
        const auto otra = store->reconciliar({vigente});
        QVERIFY(otra.esExito());
        QCOMPARE(otra.valor().generacionesEliminadas, 0);
    }

    void reconciliacionNoBorraSiNoPuedeEnumerar_data()
    {
        QTest::addColumn<int>("caso"); // 0 keychain listar, 1 directorio ilegible
        QTest::newRow("keychain bloqueado") << 0;
        QTest::newRow("directorio ilegible") << 1;
    }
    void reconciliacionNoBorraSiNoPuedeEnumerar()
    {
        QFETCH(int, caso);
        const CredencialRef a = prepararYConfirmar();
        const CredencialRef b = prepararYConfirmar();
        if (caso == 0) {
            keychain->fallos[Op::Listar] = {os::kInteractionNotAllowed, store->servicioContrasena()};
        } else {
            QVERIFY(QFile::setPermissions(dirCredenciales(), QFileDevice::WriteOwner));
        }
        const auto r = store->reconciliar({}); // ambas serian huerfanas
        if (caso == 1) {
            QVERIFY(QFile::setPermissions(dirCredenciales(),
                                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        }
        QVERIFY(!r.esExito());
        QCOMPARE(r.error().categoria, caso == 0 ? Categoria::AlmacenBloqueado : Categoria::FalloEscritura);
        keychain->fallos.clear();
        QCOMPARE(keychain->cantidad(), std::size_t{4});
        QCOMPARE(archivos().size(), 2);
        QCOMPARE(store->obtenerEstado(a, fixtures::ahoraVigente()).valor(), EstadoCredencial::Lista);
        QCOMPARE(store->obtenerEstado(b, fixtures::ahoraVigente()).valor(), EstadoCredencial::Lista);
    }

    // --- DS6: mapeo completo de OSStatus. ---
    void mapeoOSStatus_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<int>("categoria");
        const auto f = [](const char* n, std::int32_t s, Categoria c) {
            QTest::newRow(n) << static_cast<int>(s) << static_cast<int>(c);
        };
        f("ItemNotFound", os::kItemNotFound, Categoria::CredencialNoEncontrada);
        f("InteractionNotAllowed", os::kInteractionNotAllowed, Categoria::AlmacenBloqueado);
        f("InteractionRequired", os::kInteractionRequired, Categoria::AlmacenBloqueado);
        f("DataNotAvailable", os::kDataNotAvailable, Categoria::AlmacenBloqueado);
        f("InDarkWake", os::kInDarkWake, Categoria::AlmacenBloqueado);
        f("AuthFailed", os::kAuthFailed, Categoria::AccesoDenegado);
        f("RestrictedAPI", os::kRestrictedApi, Categoria::AccesoDenegado);
        f("NoAccessForItem", os::kNoAccessForItem, Categoria::AccesoDenegado);
        f("UserCanceled", os::kUserCanceled, Categoria::CanceladoPorUsuario);
        f("MissingEntitlement", os::kMissingEntitlement, Categoria::AlmacenMalConfigurado);
        f("Param", os::kParam, Categoria::AlmacenMalConfigurado);
        f("BadReq", os::kBadReq, Categoria::AlmacenMalConfigurado);
        f("NoSuchAttr", os::kNoSuchAttr, Categoria::AlmacenMalConfigurado);
        f("NoSuchClass", os::kNoSuchClass, Categoria::AlmacenMalConfigurado);
        f("NotAvailable", os::kNotAvailable, Categoria::AlmacenNoDisponible);
        f("ServiceNotAvailable", os::kServiceNotAvailable, Categoria::AlmacenNoDisponible);
        f("NoSuchKeychain", os::kNoSuchKeychain, Categoria::AlmacenNoDisponible);
        f("InvalidKeychain", os::kInvalidKeychain, Categoria::AlmacenNoDisponible);
        f("NoDefaultKeychain", os::kNoDefaultKeychain, Categoria::AlmacenNoDisponible);
        f("Unimplemented", os::kUnimplemented, Categoria::AlmacenNoDisponible);
        f("ReadOnly", os::kReadOnly, Categoria::FalloEscritura);
        f("WrPerm", os::kWrPerm, Categoria::FalloEscritura);
        f("IO", os::kIo, Categoria::FalloEscritura);
        f("DiskFull", os::kDiskFull, Categoria::FalloEscritura);
        f("DuplicateItem", os::kDuplicateItem, Categoria::Interno);
        f("desconocido", -99999, Categoria::Interno);
    }
    void mapeoOSStatus()
    {
        QFETCH(int, status);
        QFETCH(int, categoria);
        QCOMPARE(claveEstable(secrets::categoriaDeOSStatus(status)), claveEstable(static_cast<Categoria>(categoria)));
        const ErrorSecretStore e = secrets::errorDeOSStatus(status, "op.prueba");
        QCOMPARE(e.codigoNativo, std::optional<int>(status));
        QCOMPARE(e.diagnostico, QStringLiteral("op.prueba"));
    }

    // El escaneo detecta fugas plantadas (evita un falso verde).
    void centinelasDetectanFugas()
    {
        QVERIFY(fugas().isEmpty());
        QVERIFY(QDir().mkpath(dirCredenciales()));
        QFile f(dirCredenciales() + QStringLiteral("/fuga.bin"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("prefijo" + fx->contrasena() + "sufijo");
        f.close();
        QVERIFY(!fugas().isEmpty());
        QVERIFY(QFile::remove(f.fileName()));
        QVERIFY(!fugas({QStringLiteral("error con ") + fixtures::kRfc}).isEmpty());
        QVERIFY(!fugas({QString::fromLatin1(fx->leer(QStringLiteral("efirma.cer")).toBase64())}).isEmpty());
        QVERIFY(fugas().isEmpty());
    }

    void nombresDerivados()
    {
        const QString uuid = CredencialRef::generar().uuid();
        const QString nombre = MacOSSecretStore::nombreContenedor(uuid);
        QVERIFY(MacOSSecretStore::esNombreContenedor(nombre));
        QCOMPARE(nombre, MacOSSecretStore::nombreContenedor(uuid));
        QVERIFY(nombre != MacOSSecretStore::nombreContenedor(CredencialRef::generar().uuid()));
        QVERIFY(!MacOSSecretStore::esNombreContenedor(QStringLiteral("LEEME.txt")));
        QVERIFY(MacOSSecretStore::esNombreTemporal(QStringLiteral(".tmp-0123456789abcdef0123456789abcdef")));
        QVERIFY(!MacOSSecretStore::esNombreTemporal(QStringLiteral(".tmp-xyz")));
    }
};

QTEST_GUILESS_MAIN(TestMacOSSecretStore)
#include "TestMacOSSecretStore.moc"
