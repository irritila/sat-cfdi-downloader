// Pruebas de LectorZipLibzip (T015.1, T015 D4/D11). Los ZIP se generan aqui
// con libzip en un QTemporaryDir; contenido sintetico, sin datos reales.

#include "fakes/FakeCfdiArchiveReader.h"
#include "infrastructure/cfdi/zip/LectorZipLibzip.h"

#include <QFile>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

#include <zip.h>

#include <sys/stat.h>

#include <vector>

using namespace satcfdi;

namespace {

constexpr qint64 kMiB = 1024 * 1024;

const QByteArray kXmlA = QByteArrayLiteral(
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?><cfdi:Comprobante Version=\"4.0\" Total=\"1.00\"/>");
const QByteArray kXmlB = QByteArrayLiteral("<?xml version=\"1.0\"?><cfdi:Comprobante Version=\"3.3\"/>");

enum class Especial { Ninguno, Enlace, Cifrada };

struct EntradaPrueba {
    QByteArray nombre;
    QByteArray contenido;
    Especial especial = Especial::Ninguno;
    bool almacenada = false; // ZIP_CM_STORE (sin compresion)
    bool directorio = false;
};

QByteArray aleatorio(qsizetype tamano, quint32 semilla = 7)
{
    QByteArray datos(tamano, Qt::Uninitialized);
    QRandomGenerator generador(semilla);
    for (qsizetype i = 0; i < tamano; ++i)
        datos[i] = static_cast<char>(generador.bounded(256));
    return datos;
}

zip_uint16_t metodoCifradoSoportado()
{
    if (zip_encryption_method_supported(ZIP_EM_AES_256, 1))
        return ZIP_EM_AES_256;
    return ZIP_EM_TRAD_PKWARE;
}

// Crea un ZIP real con libzip. Los buffers deben vivir hasta zip_close.
QString crearZip(const QTemporaryDir& dir, const QString& nombre, const std::vector<EntradaPrueba>& entradas)
{
    const QString ruta = dir.filePath(nombre);
    int error = 0;
    zip_t* zip = zip_open(QFile::encodeName(ruta).constData(), ZIP_CREATE | ZIP_TRUNCATE, &error);
    if (!zip)
        return {};
    for (const auto& e : entradas) {
        zip_int64_t indice = -1;
        if (e.directorio) {
            indice = zip_dir_add(zip, e.nombre.constData(), ZIP_FL_ENC_RAW);
        } else {
            zip_source_t* fuente = zip_source_buffer(zip, e.contenido.constData(),
                                                     static_cast<zip_uint64_t>(e.contenido.size()), 0);
            indice = zip_file_add(zip, e.nombre.constData(), fuente, ZIP_FL_ENC_RAW);
            if (indice < 0)
                zip_source_free(fuente);
        }
        if (indice < 0) {
            zip_discard(zip);
            return {};
        }
        const auto i = static_cast<zip_uint64_t>(indice);
        if (e.almacenada)
            zip_set_file_compression(zip, i, ZIP_CM_STORE, 0);
        if (e.especial == Especial::Enlace)
            zip_file_set_external_attributes(zip, i, 0, ZIP_OPSYS_UNIX,
                                             static_cast<zip_uint32_t>(S_IFLNK | 0777) << 16);
        if (e.especial == Especial::Cifrada)
            zip_file_set_encryption(zip, i, metodoCifradoSoportado(), "clave-prueba");
    }
    if (zip_close(zip) != 0) {
        zip_discard(zip);
        return {};
    }
    return ruta;
}

QByteArray leerArchivo(const QString& ruta)
{
    QFile f(ruta);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{};
}

bool escribirArchivo(const QString& ruta, const QByteArray& datos)
{
    QFile f(ruta);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(datos) == datos.size();
}

void escribirLe32(QByteArray& datos, qsizetype pos, quint32 valor)
{
    for (int b = 0; b < 4; ++b)
        datos[pos + b] = static_cast<char>((valor >> (8 * b)) & 0xFFu);
}

struct Recorrido {
    Resultado<ResumenLecturaZip, ErrorPaqueteZip> resultado;
    QList<EntradaXmlCfdi> entregadas;
};

Recorrido recorrer(CfdiArchiveReader& lector, const QString& ruta)
{
    QList<EntradaXmlCfdi> entregadas;
    auto r = lector.recorrer(ruta, [&](const EntradaXmlCfdi& e) {
        entregadas.append(e);
        return true;
    });
    return Recorrido{std::move(r), entregadas};
}

int omitidas(const ResumenLecturaZip& r, MotivoOmision m)
{
    return r.omitidas.value(m, 0);
}

// Captura de logs para D11.
QStringList g_logs;
void capturarLog(QtMsgType, const QMessageLogContext&, const QString& mensaje)
{
    g_logs.append(mensaje);
}

} // namespace

class TestLectorZipLibzip : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    void limitesProductivosSonLosDeD4()
    {
        LectorZipLibzip lector;
        QCOMPARE(lector.limites().maxBytesEntrada, 16 * kMiB);
        QCOMPARE(lector.limites().razonCompresionMax, qint64{200});
        QCOMPARE(lector.limites().umbralRazonBytes, 1 * kMiB);
        QCOMPARE(lector.limites().maxBytesTotal, 1024 * kMiB);
        QCOMPARE(lector.limites().maxEntradas, qint64{20000});
    }

    void xmlValidosEnOrden()
    {
        const QString ruta = crearZip(m_dir, "orden.zip",
                                      {{"a.xml", kXmlA}, {"nota.txt", "texto"}, {"sub/", {}, Especial::Ninguno, false, true},
                                       {"sub/B.XML", kXmlB}, {"c.Xml", kXmlA, Especial::Ninguno, true}});
        QVERIFY(!ruta.isEmpty());
        LectorZipLibzip lector;
        const auto rec = recorrer(lector, ruta);
        QVERIFY(rec.resultado.esExito());
        QCOMPARE(rec.resultado.valor().xmlEntregados, 3);
        QCOMPARE(omitidas(rec.resultado.valor(), MotivoOmision::NoXml), 2); // nota.txt y sub/
        QCOMPARE(rec.entregadas.size(), 3);
        QCOMPARE(rec.entregadas[0].indice, 0);
        QCOMPARE(rec.entregadas[0].contenido, kXmlA);
        QCOMPARE(rec.entregadas[1].indice, 3);
        QCOMPARE(rec.entregadas[1].contenido, kXmlB);
        QCOMPARE(rec.entregadas[2].indice, 4);
        QCOMPARE(rec.entregadas[2].contenido, kXmlA);
    }

    void zipVacioDeEntradasEsExito()
    {
        // libzip no escribe un ZIP sin entradas; se usa el EOCD minimo.
        const QString ruta = m_dir.filePath("sin-entradas.zip");
        QVERIFY(escribirArchivo(ruta, QByteArray("PK\x05\x06", 4) + QByteArray(18, '\0')));
        LectorZipLibzip lector;
        const auto rec = recorrer(lector, ruta);
        QVERIFY(rec.resultado.esExito());
        QCOMPARE(rec.resultado.valor().xmlEntregados, 0);
        QVERIFY(rec.resultado.valor().omitidas.isEmpty());
    }

    void omisionPorMotivo_data()
    {
        QTest::addColumn<QByteArray>("nombre");
        QTest::addColumn<int>("especial");
        QTest::addColumn<int>("motivo");
        QTest::newRow("punto-punto") << QByteArray("../fuera.xml") << 0 << int(MotivoOmision::RutaInvalida);
        QTest::newRow("punto-punto-interno") << QByteArray("a/../../b.xml") << 0 << int(MotivoOmision::RutaInvalida);
        QTest::newRow("absoluta") << QByteArray("/abs.xml") << 0 << int(MotivoOmision::RutaInvalida);
        QTest::newRow("unidad") << QByteArray("C:x.xml") << 0 << int(MotivoOmision::RutaInvalida);
        QTest::newRow("diagonal-invertida") << QByteArray("a\\b.xml") << 0 << int(MotivoOmision::RutaInvalida);
        QTest::newRow("enlace") << QByteArray("enlace.xml") << int(Especial::Enlace) << int(MotivoOmision::Enlace);
        QTest::newRow("cifrada") << QByteArray("cifrada.xml") << int(Especial::Cifrada) << int(MotivoOmision::Cifrada);
        QTest::newRow("no-xml") << QByteArray("factura.pdf") << 0 << int(MotivoOmision::NoXml);
        QTest::newRow("xml-sin-punto") << QByteArray("archivoxml") << 0 << int(MotivoOmision::NoXml);
    }

    void omisionPorMotivo()
    {
        QFETCH(QByteArray, nombre);
        QFETCH(int, especial);
        QFETCH(int, motivo);
        const QString ruta = crearZip(m_dir, "motivo.zip",
                                      {{"antes.xml", kXmlA},
                                       {nombre, kXmlB, static_cast<Especial>(especial)},
                                       {"despues.xml", kXmlA}});
        QVERIFY(!ruta.isEmpty());
        LectorZipLibzip lector;
        const auto rec = recorrer(lector, ruta);
        QVERIFY(rec.resultado.esExito());
        const auto& resumen = rec.resultado.valor();
        QCOMPARE(resumen.xmlEntregados, 2);
        QCOMPARE(omitidas(resumen, static_cast<MotivoOmision>(motivo)), 1);
        QCOMPARE(resumen.omitidas.size(), 1);
        QCOMPARE(rec.entregadas[0].indice, 0);
        QCOMPARE(rec.entregadas[1].indice, 2);
    }

    void excedeTamanoPorEntrada()
    {
        // Produccion: 16 MiB + 1 byte incompresible (sin razon de compresion).
        const QString ruta = crearZip(m_dir, "grande.zip",
                                      {{"grande.xml", aleatorio(16 * kMiB + 1), Especial::Ninguno, true},
                                       {"justo.xml", aleatorio(16 * kMiB, 11), Especial::Ninguno, true},
                                       {"ok.xml", kXmlA}});
        QVERIFY(!ruta.isEmpty());
        LectorZipLibzip lector;
        const auto rec = recorrer(lector, ruta);
        QVERIFY(rec.resultado.esExito());
        QCOMPARE(omitidas(rec.resultado.valor(), MotivoOmision::ExcedeTamano), 1);
        QCOMPARE(rec.resultado.valor().xmlEntregados, 2);
        QCOMPARE(rec.entregadas[0].contenido.size(), 16 * kMiB);
        QCOMPARE(rec.entregadas[1].indice, 2);
    }

    void razonDeCompresion()
    {
        // 2 MiB de un solo byte: razon >> 200 y > 1 MiB -> omitida. 1 MiB
        // exacto con la misma razon se acepta (el umbral es "mas de").
        const QString ruta = crearZip(m_dir, "razon.zip",
                                      {{"comprimible.xml", QByteArray(2 * kMiB, ' ')},
                                       {"umbral.xml", QByteArray(1 * kMiB, ' ')},
                                       {"ok.xml", kXmlA}});
        QVERIFY(!ruta.isEmpty());
        LectorZipLibzip lector;
        const auto rec = recorrer(lector, ruta);
        QVERIFY(rec.resultado.esExito());
        QCOMPARE(omitidas(rec.resultado.valor(), MotivoOmision::RazonCompresion), 1);
        QCOMPARE(rec.resultado.valor().xmlEntregados, 2);
        QCOMPARE(rec.entregadas[0].indice, 1);
    }

    void zipBombaConEncabezadoMentiroso()
    {
        // 64 MiB de ceros (deflate ~64 KiB) con tamanos declarados de 100
        // bytes en el encabezado local y el directorio central: el atajo por
        // tamano declarado no aplica y la lectura acotada debe cortar.
        const QByteArray ceros(64 * kMiB, '\0');
        QString ruta = crearZip(m_dir, "bomba.zip", {{"bomba.xml", ceros}, {"ok.xml", kXmlA}});
        QVERIFY(!ruta.isEmpty());
        QByteArray datos = leerArchivo(ruta);
        QVERIFY(datos.startsWith("PK\x03\x04"));
        escribirLe32(datos, 22, 100); // tamano descomprimido, encabezado local
        const qsizetype central = datos.indexOf(QByteArray("PK\x01\x02", 4));
        QVERIFY(central > 0);
        escribirLe32(datos, central + 24, 100); // tamano descomprimido, directorio central
        QVERIFY(escribirArchivo(ruta, datos));

        LectorZipLibzip lector;
        const auto rec = recorrer(lector, ruta);
        QVERIFY(rec.resultado.esExito());
        const auto& resumen = rec.resultado.valor();
        QCOMPARE(resumen.xmlEntregados, 1);
        QCOMPARE(rec.entregadas[0].indice, 1);
        // La lectura acotada corta al pasar 1 MiB real con razon > 200:1.
        QCOMPARE(omitidas(resumen, MotivoOmision::RazonCompresion), 1);
    }

    void lecturaAcotadaPorEntradaConLimitesReducidos()
    {
        // Encabezado mentiroso + datos incompresibles (deflate; con STORE libzip
        // rechaza la inconsistencia al abrir): solo el conteo de bytes reales
        // detecta el exceso por entrada.
        LimitesLectorZip limites;
        limites.maxBytesEntrada = 8 * 1024;
        limites.bytesPorTrozo = 1000;
        const QString ruta = crearZip(m_dir, "acotada.zip",
                                      {{"x.xml", aleatorio(20 * 1024)}, {"ok.xml", kXmlA}});
        QVERIFY(!ruta.isEmpty());
        QByteArray datos = leerArchivo(ruta);
        escribirLe32(datos, 22, 100);
        const qsizetype central = datos.indexOf(QByteArray("PK\x01\x02", 4));
        QVERIFY(central > 0);
        escribirLe32(datos, central + 24, 100);
        QVERIFY(escribirArchivo(ruta, datos));

        LectorZipLibzip lector(limites);
        const auto rec = recorrer(lector, ruta);
        QVERIFY(rec.resultado.esExito());
        QCOMPARE(rec.resultado.valor().xmlEntregados, 1);
        QCOMPARE(omitidas(rec.resultado.valor(), MotivoOmision::ExcedeTamano), 1);
    }

    void errorDeLecturaPorCrc()
    {
        const QByteArray marca = QByteArrayLiteral("<cfdi:Comprobante MARCA-CRC-0123456789/>");
        const QString ruta = crearZip(m_dir, "crc.zip",
                                      {{"malo.xml", marca, Especial::Ninguno, true}, {"ok.xml", kXmlA}});
        QVERIFY(!ruta.isEmpty());
        QByteArray datos = leerArchivo(ruta);
        const qsizetype pos = datos.indexOf("MARCA-CRC");
        QVERIFY(pos > 0);
        datos[pos] = 'X';
        QVERIFY(escribirArchivo(ruta, datos));

        LectorZipLibzip lector;
        const auto rec = recorrer(lector, ruta);
        QVERIFY(rec.resultado.esExito());
        QCOMPARE(omitidas(rec.resultado.valor(), MotivoOmision::ErrorLectura), 1);
        QCOMPARE(rec.resultado.valor().xmlEntregados, 1);
        QCOMPARE(rec.entregadas[0].indice, 1);
    }

    void cancelacionDesdeCallback()
    {
        const QString ruta = crearZip(m_dir, "cancelar.zip", {{"a.xml", kXmlA}, {"b.xml", kXmlB}, {"c.xml", kXmlA}});
        QVERIFY(!ruta.isEmpty());
        LectorZipLibzip lector;
        int llamadas = 0;
        const auto r = lector.recorrer(ruta, [&](const EntradaXmlCfdi&) {
            ++llamadas;
            return llamadas < 2;
        });
        QVERIFY(r.esExito());
        QCOMPARE(llamadas, 2);
        QCOMPARE(r.valor().xmlEntregados, 2);
    }

    void zipInexistente()
    {
        LectorZipLibzip lector;
        const auto rec = recorrer(lector, m_dir.filePath("no-existe.zip"));
        QVERIFY(!rec.resultado.esExito());
        QCOMPARE(rec.resultado.error().tipo, ErrorPaqueteZip::Tipo::NoExiste);
        QCOMPARE(rec.resultado.error().causa, ErrorPaqueteZip::Causa::NoExiste);
        QVERIFY(rec.entregadas.isEmpty());
    }

    void archivoQueNoEsZip()
    {
        const QString ruta = m_dir.filePath("texto.zip");
        QVERIFY(escribirArchivo(ruta, QByteArray("esto no es un zip\n").repeated(100)));
        LectorZipLibzip lector;
        const auto rec = recorrer(lector, ruta);
        QVERIFY(!rec.resultado.esExito());
        QCOMPARE(rec.resultado.error().tipo, ErrorPaqueteZip::Tipo::Ilegible);
        QCOMPARE(rec.resultado.error().causa, ErrorPaqueteZip::Causa::Corrupto);
    }

    void archivoVacio()
    {
        const QString ruta = m_dir.filePath("vacio.zip");
        QVERIFY(escribirArchivo(ruta, {}));
        LectorZipLibzip lector;
        const auto rec = recorrer(lector, ruta);
        QVERIFY(!rec.resultado.esExito());
        QCOMPARE(rec.resultado.error().tipo, ErrorPaqueteZip::Tipo::Ilegible);
        QCOMPARE(rec.resultado.error().causa, ErrorPaqueteZip::Causa::Truncado);
    }

    void zipTruncado()
    {
        const QString ruta = crearZip(m_dir, "truncado.zip",
                                      {{"a.xml", aleatorio(4096)}, {"b.xml", kXmlB}});
        QVERIFY(!ruta.isEmpty());
        const QByteArray datos = leerArchivo(ruta);
        QVERIFY(escribirArchivo(ruta, datos.left(datos.size() / 2)));
        LectorZipLibzip lector;
        const auto rec = recorrer(lector, ruta);
        QVERIFY(!rec.resultado.esExito());
        QCOMPARE(rec.resultado.error().tipo, ErrorPaqueteZip::Tipo::Ilegible);
        QCOMPARE(rec.resultado.error().causa, ErrorPaqueteZip::Causa::Truncado);
        QVERIFY(rec.entregadas.isEmpty());
    }

    void directorioCentralCorrupto()
    {
        const QString ruta = crearZip(m_dir, "central.zip", {{"a.xml", kXmlA}, {"b.xml", kXmlB}});
        QVERIFY(!ruta.isEmpty());
        QByteArray datos = leerArchivo(ruta);
        const qsizetype central = datos.indexOf(QByteArray("PK\x01\x02", 4));
        QVERIFY(central > 0);
        datos[central] = 'X';
        QVERIFY(escribirArchivo(ruta, datos));
        LectorZipLibzip lector;
        const auto rec = recorrer(lector, ruta);
        QVERIFY(!rec.resultado.esExito());
        QCOMPARE(rec.resultado.error().tipo, ErrorPaqueteZip::Tipo::Ilegible);
        QCOMPARE(rec.resultado.error().causa, ErrorPaqueteZip::Causa::Corrupto);
        QVERIFY(rec.entregadas.isEmpty());
    }

    void demasiadasEntradasProduccion()
    {
        std::vector<EntradaPrueba> entradas;
        entradas.reserve(20001);
        for (int i = 0; i < 20001; ++i)
            entradas.push_back({QByteArray("e") + QByteArray::number(i) + ".txt", {}});
        const QString ruta = crearZip(m_dir, "muchas.zip", entradas);
        QVERIFY(!ruta.isEmpty());
        LectorZipLibzip lector;
        const auto rec = recorrer(lector, ruta);
        QVERIFY(!rec.resultado.esExito());
        QCOMPARE(rec.resultado.error().tipo, ErrorPaqueteZip::Tipo::Ilegible);
        QCOMPARE(rec.resultado.error().causa, ErrorPaqueteZip::Causa::DemasiadasEntradas);
        QVERIFY(rec.entregadas.isEmpty());
    }

    void entradasEnElLimiteSeAceptan()
    {
        LimitesLectorZip limites;
        limites.maxEntradas = 3;
        const QString ruta = crearZip(m_dir, "tres.zip", {{"a.xml", kXmlA}, {"b.xml", kXmlA}, {"c.xml", kXmlA}});
        LectorZipLibzip lector(limites);
        QVERIFY(recorrer(lector, ruta).resultado.esExito());
        const QString ruta4 = crearZip(m_dir, "cuatro.zip",
                                       {{"a.xml", kXmlA}, {"b.xml", kXmlA}, {"c.xml", kXmlA}, {"d.xml", kXmlA}});
        const auto rec = recorrer(lector, ruta4);
        QVERIFY(!rec.resultado.esExito());
        QCOMPARE(rec.resultado.error().causa, ErrorPaqueteZip::Causa::DemasiadasEntradas);
    }

    void excedeTotalAcumulado()
    {
        // Limites reducidos: 3 entradas de 4 KiB con total maximo de 10 KiB.
        LimitesLectorZip limites;
        limites.maxBytesTotal = 10 * 1024;
        const QString ruta = crearZip(m_dir, "total.zip",
                                      {{"a.xml", aleatorio(4096, 1)}, {"b.xml", aleatorio(4096, 2)},
                                       {"c.xml", aleatorio(4096, 3)}});
        QVERIFY(!ruta.isEmpty());
        LectorZipLibzip lector(limites);
        const auto rec = recorrer(lector, ruta);
        QVERIFY(!rec.resultado.esExito());
        QCOMPARE(rec.resultado.error().tipo, ErrorPaqueteZip::Tipo::Ilegible);
        QCOMPARE(rec.resultado.error().causa, ErrorPaqueteZip::Causa::ExcedeTotal);
        // Las dos primeras ya se entregaron: el contrato obliga al llamador a
        // descartarlas al recibir el error.
        QCOMPARE(rec.entregadas.size(), 2);
    }

    void logsSinRutaNiNombres()
    {
        const QString ruta = crearZip(m_dir, "LOGSECRETO-XAXX010101000.zip",
                                      {{"NOMBRE-ENTRADA-SECRETA.xml", QByteArray(2 * kMiB, ' ')},
                                       {"../OTRA-SECRETA.xml", kXmlA},
                                       {"ok.xml", kXmlA}});
        QVERIFY(!ruta.isEmpty());
        QLoggingCategory::setFilterRules(QStringLiteral("satcfdi.cfdi.zip.debug=true"));
        g_logs.clear();
        const auto anterior = qInstallMessageHandler(capturarLog);
        LectorZipLibzip lector;
        const auto rec = recorrer(lector, ruta);
        // Errores por paquete tambien registran.
        QFile::resize(ruta, 100);
        const auto recError = recorrer(lector, ruta);
        qInstallMessageHandler(anterior);
        QLoggingCategory::setFilterRules(QString());

        QVERIFY(rec.resultado.esExito());
        QVERIFY(!recError.resultado.esExito());
        QVERIFY(!g_logs.isEmpty());
        for (const QString& linea : std::as_const(g_logs)) {
            QVERIFY2(!linea.contains("SECRET"), qPrintable(linea));
            QVERIFY2(!linea.contains("XAXX"), qPrintable(linea));
            QVERIFY2(!linea.contains(m_dir.path()), qPrintable(linea));
            QVERIFY2(!linea.contains("cfdi:Comprobante"), qPrintable(linea));
        }
    }

    void fakeRespetaElContrato()
    {
        fakes::FakeCfdiArchiveReader fake;
        fake.agregarXml(kXmlA);
        fake.agregarOmision(MotivoOmision::NoXml);
        fake.agregarXml(kXmlB);
        const auto rec = recorrer(fake, QStringLiteral("/x.zip"));
        QVERIFY(rec.resultado.esExito());
        QCOMPARE(rec.entregadas.size(), 2);
        QCOMPARE(rec.entregadas[1].indice, 2);
        QCOMPARE(omitidas(rec.resultado.valor(), MotivoOmision::NoXml), 1);

        fake.programarError({ErrorPaqueteZip::Tipo::Ilegible, ErrorPaqueteZip::Causa::Truncado});
        const auto recError = recorrer(fake, QStringLiteral("/x.zip"));
        QVERIFY(!recError.resultado.esExito());
        QVERIFY(recError.entregadas.isEmpty());
        QCOMPARE(fake.rutasRecorridas().size(), 2);
    }

    // T015.4: leerEntrada devuelve solo la entrada de esa posicion, con las
    // mismas reglas que recorrer (lector real y la implementacion por
    // defecto del puerto que usan los fakes).
    void leerEntradaPorPosicion()
    {
        const QString ruta = crearZip(m_dir, "posicion.zip",
                                      {{"a.xml", kXmlA}, {"nota.txt", "texto"}, {"../malo.xml", kXmlA},
                                       {"sub/B.XML", kXmlB}});
        QVERIFY(!ruta.isEmpty());
        LectorZipLibzip lector;
        auto r = lector.leerEntrada(ruta, 0);
        QVERIFY(r.esExito() && r.valor());
        QCOMPARE(*r.valor(), kXmlA);
        r = lector.leerEntrada(ruta, 3);
        QVERIFY(r.esExito() && r.valor());
        QCOMPARE(*r.valor(), kXmlB);
        // Omitidas (no XML, ruta invalida) y fuera de rango: exito sin contenido.
        for (int i : {1, 2, 4, -1}) {
            r = lector.leerEntrada(ruta, i);
            QVERIFY(r.esExito());
            QVERIFY(!r.valor());
        }
        r = lector.leerEntrada(m_dir.filePath("no-existe.zip"), 0);
        QVERIFY(!r.esExito());
        QCOMPARE(r.error().tipo, ErrorPaqueteZip::Tipo::NoExiste);

        fakes::FakeCfdiArchiveReader fake;
        fake.agregarXml(kXmlA);
        fake.agregarOmision(MotivoOmision::NoXml);
        fake.agregarXml(kXmlB);
        QCOMPARE(*fake.leerEntrada(QStringLiteral("/x.zip"), 2).valor(), kXmlB);
        QVERIFY(!fake.leerEntrada(QStringLiteral("/x.zip"), 1).valor());
        fake.programarError({ErrorPaqueteZip::Tipo::Ilegible, ErrorPaqueteZip::Causa::Corrupto});
        QVERIFY(!fake.leerEntrada(QStringLiteral("/x.zip"), 0).esExito());
    }
};

QTEST_GUILESS_MAIN(TestLectorZipLibzip)
#include "TestLectorZipLibzip.moc"
