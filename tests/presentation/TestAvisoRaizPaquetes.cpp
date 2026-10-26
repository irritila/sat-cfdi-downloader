// satcfdi_aviso_raiz_tests (T019 D8): AvisoRaizPaquetesViewModel sin aviso y
// con cada motivo; fijo durante la sesion; rutas siempre ~/... (sin rutas
// absolutas del home).

#include "AvisoRaizPaquetesViewModel.h"

#include <QDir>
#include <QSignalSpy>
#include <QTest>

using namespace satcfdi;

namespace {

using Motivo = AvisoRaizPaquetesViewModel::Motivo;

const QString kLegada = QStringLiteral("~/SAT-CFDI-Downloader/paquetes");
const QString kNueva = QStringLiteral("~/Archivo-Fiscal/paquetes");

AvisoRaizPaquetesViewModel::Datos datos(Motivo motivo)
{
    const QString efectiva = motivo == Motivo::RaizNoDisponible ? kNueva : kLegada;
    return {motivo, kLegada, kNueva, efectiva};
}

} // namespace

Q_DECLARE_METATYPE(satcfdi::AvisoRaizPaquetesViewModel::Motivo)

class TestAvisoRaizPaquetes : public QObject {
    Q_OBJECT

private slots:
    void sinAvisoQuedaInactivo();
    void cadaMotivo_data();
    void cadaMotivo();
    void errorConRaizNuevaAnteriorNoEsCarpeta();
    void esFijoDuranteLaSesion();
    void ningunoNoActiva();
};

void TestAvisoRaizPaquetes::sinAvisoQuedaInactivo()
{
    AvisoRaizPaquetesViewModel vm;
    QVERIFY(!vm.activo());
    QCOMPARE(vm.motivo(), Motivo::Ninguno);
    QVERIFY(vm.variante().isEmpty());
    QVERIFY(vm.titulo().isEmpty());
    QVERIFY(vm.descripcion().isEmpty());
    QVERIFY(vm.rutaLegada().isEmpty());
    QVERIFY(vm.rutaNueva().isEmpty());
    QVERIFY(vm.rutaEfectiva().isEmpty());
}

void TestAvisoRaizPaquetes::cadaMotivo_data()
{
    QTest::addColumn<Motivo>("motivo");
    QTest::addColumn<QString>("variante");
    QTest::addColumn<QString>("titulo");
    QTest::addColumn<QString>("descripcion");

    QTest::newRow("conflicto")
        << Motivo::Conflicto << QStringLiteral("advertencia")
        << QStringLiteral("Los archivos descargados siguen en la carpeta anterior.")
        << QStringLiteral("No se movió ~/SAT-CFDI-Downloader/paquetes a ~/Archivo-Fiscal/paquetes porque la "
                          "carpeta nueva ya tiene contenido. Esta sesión usa ~/SAT-CFDI-Downloader/paquetes. "
                          "Revisa las dos carpetas y deja los archivos descargados solo en una; el cambio se "
                          "intentará de nuevo al reiniciar la aplicación.");
    QTest::newRow("error")
        << Motivo::Error << QStringLiteral("advertencia") << QStringLiteral("No se pudo mover la carpeta de archivos descargados.")
        << QStringLiteral("No se movió ~/SAT-CFDI-Downloader/paquetes a ~/Archivo-Fiscal/paquetes y ningún "
                          "archivo se copió ni se borró. Esta sesión usa ~/SAT-CFDI-Downloader/paquetes. El cambio "
                          "se intentará de nuevo al reiniciar la aplicación.");
    QTest::newRow("raiz-no-disponible")
        << Motivo::RaizNoDisponible << QStringLiteral("error")
        << QStringLiteral("No se pudo crear la carpeta de archivos descargados.")
        << QStringLiteral("~/Archivo-Fiscal/paquetes no existe o no es una carpeta, así que las descargas pueden "
                          "fallar. Esta sesión usa ~/Archivo-Fiscal/paquetes. Se intentará crearla de nuevo al "
                          "reiniciar la aplicación.");
}

void TestAvisoRaizPaquetes::cadaMotivo()
{
    QFETCH(Motivo, motivo);
    QFETCH(QString, variante);
    QFETCH(QString, titulo);
    QFETCH(QString, descripcion);

    AvisoRaizPaquetesViewModel vm;
    QSignalSpy cambio(&vm, &AvisoRaizPaquetesViewModel::avisoChanged);
    vm.fijar(datos(motivo));
    QCOMPARE(cambio.count(), 1);
    QVERIFY(vm.activo());
    QCOMPARE(vm.motivo(), motivo);
    QCOMPARE(vm.variante(), variante);
    QCOMPARE(vm.titulo(), titulo);
    QCOMPARE(vm.descripcion(), descripcion);
    QCOMPARE(vm.rutaLegada(), kLegada);
    QCOMPARE(vm.rutaNueva(), kNueva);
    QCOMPARE(vm.rutaEfectiva(), datos(motivo).rutaEfectiva);
    // Dice que carpeta usa esta sesion y nunca muestra rutas absolutas.
    QVERIFY(vm.descripcion().contains(QStringLiteral("Esta sesión usa ") + vm.rutaEfectiva()));
    QVERIFY(!vm.descripcion().contains(QDir::homePath()));
    QVERIFY(!vm.descripcion().contains(QStringLiteral("/Users/")));
}

// Corte 1: origen que no es directorio -> Error con la raiz nueva en uso.
void TestAvisoRaizPaquetes::errorConRaizNuevaAnteriorNoEsCarpeta()
{
    AvisoRaizPaquetesViewModel vm;
    vm.fijar({Motivo::Error, kLegada, kNueva, kNueva});
    QVERIFY(vm.activo());
    QCOMPARE(vm.variante(), QStringLiteral("advertencia"));
    QCOMPARE(vm.rutaEfectiva(), kNueva);
    QCOMPARE(vm.titulo(), QStringLiteral("La carpeta anterior de archivos descargados no es válida."));
    QCOMPARE(vm.descripcion(),
             QStringLiteral("La ruta anterior ~/SAT-CFDI-Downloader/paquetes no es una carpeta; no se modificó. "
                            "Esta sesión usa ~/Archivo-Fiscal/paquetes."));
}

void TestAvisoRaizPaquetes::esFijoDuranteLaSesion()
{
    AvisoRaizPaquetesViewModel vm;
    QSignalSpy cambio(&vm, &AvisoRaizPaquetesViewModel::avisoChanged);
    vm.fijar(datos(Motivo::Conflicto));
    const QString descripcion = vm.descripcion();
    vm.fijar(datos(Motivo::RaizNoDisponible));
    vm.fijar({});
    QCOMPARE(cambio.count(), 1);
    QVERIFY(vm.activo());
    QCOMPARE(vm.motivo(), Motivo::Conflicto);
    QCOMPARE(vm.descripcion(), descripcion);
}

void TestAvisoRaizPaquetes::ningunoNoActiva()
{
    AvisoRaizPaquetesViewModel vm;
    QSignalSpy cambio(&vm, &AvisoRaizPaquetesViewModel::avisoChanged);
    vm.fijar({Motivo::Ninguno, kLegada, kNueva, kNueva});
    QCOMPARE(cambio.count(), 0);
    QVERIFY(!vm.activo());
    QVERIFY(vm.rutaLegada().isEmpty());
}

QTEST_GUILESS_MAIN(TestAvisoRaizPaquetes)

#include "TestAvisoRaizPaquetes.moc"
