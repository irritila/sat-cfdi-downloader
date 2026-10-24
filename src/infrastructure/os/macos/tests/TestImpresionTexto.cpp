// T025.6 C4: la vista de impresion de la clave de recuperacion, renderizada a
// PDF en memoria (sin panel, impresora ni archivos). Clave sintetica.

#include "infrastructure/os/macos/ImpresionTextoMacOS.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace satcfdi::macos;
using RI = satcfdi::OSIntegration::ResultadoImpresion;

namespace {

const QString kTitulo = QStringLiteral("Clave de recuperación de Archivo Fiscal");
const QString kClave = QStringLiteral("ABCD EFGH JKMN PQRS TVWX YZ01 2345 6789 ABCD EFGH JKMN PQRS TVWX YZ01");

} // namespace

class TestImpresionTexto : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tmp;

private slots:
    void initTestCase()
    {
        // Un TMPDIR propio y vacio: si algo escribiera temporales, apareceria aqui.
        QVERIFY(m_tmp.isValid());
        qputenv("TMPDIR", QFile::encodeName(m_tmp.path()));
    }

    void claveEnUnaPaginaYVistaVaciada()
    {
        const InspeccionImpresion r = inspeccionarImpresionEnMemoria(kTitulo, kClave);
        QVERIFY(r.valida);
        QCOMPARE(r.textoVista, kTitulo + QStringLiteral("\n\n") + kClave);
        QCOMPARE(r.paginas, 1);
        QVERIFY(r.bytesPdf > 0);
        QVERIFY(r.vistaVaciada);
    }

    void textoLargoSePagina()
    {
        QStringList lineas;
        for (int i = 0; i < 400; ++i) {
            lineas.append(kClave);
        }
        const InspeccionImpresion r = inspeccionarImpresionEnMemoria(kTitulo, lineas.join(QLatin1Char('\n')));
        QVERIFY(r.valida);
        QVERIFY2(r.paginas > 1, qPrintable(QString::number(r.paginas)));
        QVERIFY(r.vistaVaciada);
    }

    void sinNSAppOTextoVacioNoMuestraNada()
    {
        // Prueba sin NSApplication: nunca intenta abrir el panel.
        QCOMPARE(imprimirTextoMacOS(kTitulo, QString()), RI::Fallido);
        QCOMPARE(imprimirTextoMacOS(kTitulo, kClave), RI::Fallido);
    }

    void noSeEscribenArchivos()
    {
        QVERIFY(inspeccionarImpresionEnMemoria(kTitulo, kClave).valida);
        QCOMPARE(QDir(m_tmp.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestImpresionTexto)
#include "TestImpresionTexto.moc"
