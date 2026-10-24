// T025.6 H1/M1: PortapapelesSecretoMacOS sobre un NSPasteboard con nombre
// propio (no toca el portapapeles del usuario), plazo de borrado corto y una
// clave sintetica. Objective-C++ con ARC.

#import <AppKit/AppKit.h>

#include "infrastructure/os/macos/PortapapelesSecretoMacOS.h"

#include <QTest>
#include <QUuid>

using satcfdi::macos::PortapapelesSecretoMacOS;
using namespace std::chrono_literals;

namespace {

const QString kClave = QStringLiteral("ABCD EFGH JKMN PQRS TVWX YZ01 2345 6789 ABCD EFGH JKMN PQRS TVWX YZ01");

QString nombreUnico()
{
    return QStringLiteral("mx.adenium.satcfdi.prueba.") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

NSPasteboard* pb(const QString& nombre)
{
    return [NSPasteboard pasteboardWithName:nombre.toNSString()];
}

QString textoDe(const QString& nombre)
{
    NSString* s = [pb(nombre) stringForType:NSPasteboardTypeString];
    return s != nil ? QString::fromNSString(s) : QString();
}

QStringList tiposDe(const QString& nombre)
{
    QStringList r;
    for (NSPasteboardType t in pb(nombre).types) {
        r.append(QString::fromNSString(t));
    }
    return r;
}

} // namespace

class TestPortapapelesSecreto : public QObject {
    Q_OBJECT

private:
    QStringList m_nombres;

    QString nuevo()
    {
        m_nombres.append(nombreUnico());
        return m_nombres.constLast();
    }

private slots:
    void cleanupTestCase()
    {
        for (const QString& n : std::as_const(m_nombres)) {
            [pb(n) releaseGlobally];
        }
    }

    void copiaConTiposOcultoYTransitorioYSeBorra()
    {
        const QString nombre = nuevo();
        PortapapelesSecretoMacOS portapapeles(nombre, 300ms);
        QVERIFY(portapapeles.copiar(kClave));
        QCOMPARE(textoDe(nombre), kClave);
        const QStringList tipos = tiposDe(nombre);
        QVERIFY2(tipos.contains(QStringLiteral("public.utf8-plain-text")), qPrintable(tipos.join(u',')));
        QVERIFY(tipos.contains(QStringLiteral("org.nspasteboard.ConcealedType")));
        QVERIFY(tipos.contains(QStringLiteral("org.nspasteboard.TransientType")));
        QVERIFY(portapapeles.borradoPendiente());

        QTRY_VERIFY_WITH_TIMEOUT(textoDe(nombre).isEmpty(), 5000);
        QVERIFY(tiposDe(nombre).isEmpty());
        QVERIFY(!portapapeles.borradoPendiente());
    }

    void siElUsuarioCopioOtraCosaNoSeBorra()
    {
        const QString nombre = nuevo();
        PortapapelesSecretoMacOS portapapeles(nombre, 300ms);
        QVERIFY(portapapeles.copiar(kClave));
        [pb(nombre) clearContents];
        [pb(nombre) setString:@"otro texto sintetico" forType:NSPasteboardTypeString];
        QTRY_VERIFY_WITH_TIMEOUT(!portapapeles.borradoPendiente(), 5000);
        QCOMPARE(textoDe(nombre), QStringLiteral("otro texto sintetico"));
    }

    void borradoInmediatoAlSalir()
    {
        const QString nombre = nuevo();
        PortapapelesSecretoMacOS portapapeles(nombre, 60s);
        QVERIFY(portapapeles.copiar(kClave));
        QVERIFY(portapapeles.borrarSiSigueSiendoNuestro());
        QVERIFY(textoDe(nombre).isEmpty());
        QVERIFY(!portapapeles.borrarSiSigueSiendoNuestro()); // idempotente
    }

    void copiaNuevaReiniciaElPlazo()
    {
        const QString nombre = nuevo();
        PortapapelesSecretoMacOS portapapeles(nombre, 600ms);
        QVERIFY(portapapeles.copiar(kClave));
        QTest::qWait(400);
        QVERIFY(portapapeles.copiar(kClave));
        QTest::qWait(400); // 800 ms desde la primera copia, 400 desde la segunda
        QCOMPARE(textoDe(nombre), kClave);
        QTRY_VERIFY_WITH_TIMEOUT(textoDe(nombre).isEmpty(), 5000);
    }

    // B6: la actividad anti App Nap vive solo mientras hay borrado pendiente.
    void actividadMientrasHayBorradoPendiente()
    {
        const QString nombre = nuevo();
        PortapapelesSecretoMacOS portapapeles(nombre, 300ms);
        QVERIFY(!portapapeles.actividadActiva());
        QVERIFY(portapapeles.copiar(kClave));
        QVERIFY(portapapeles.actividadActiva());
        QVERIFY(portapapeles.copiar(kClave)); // reemplaza, no acumula
        QVERIFY(portapapeles.actividadActiva());
        QTRY_VERIFY_WITH_TIMEOUT(!portapapeles.borradoPendiente(), 5000);
        QVERIFY(!portapapeles.actividadActiva());

        // Tambien termina si el contenido ya no es nuestro al vencer el plazo.
        QVERIFY(portapapeles.copiar(kClave));
        [pb(nombre) clearContents];
        [pb(nombre) setString:@"otro" forType:NSPasteboardTypeString];
        QTRY_VERIFY_WITH_TIMEOUT(!portapapeles.actividadActiva(), 5000);

        // Y al borrar al salir.
        QVERIFY(portapapeles.copiar(kClave));
        QVERIFY(portapapeles.borrarSiSigueSiendoNuestro());
        QVERIFY(!portapapeles.actividadActiva());
    }

    void textoVacioNoTocaNada()
    {
        const QString nombre = nuevo();
        [pb(nombre) clearContents];
        [pb(nombre) setString:@"previo" forType:NSPasteboardTypeString];
        PortapapelesSecretoMacOS portapapeles(nombre, 300ms);
        QVERIFY(!portapapeles.copiar(QString()));
        QVERIFY(!portapapeles.borradoPendiente());
        QCOMPARE(textoDe(nombre), QStringLiteral("previo"));
    }
};

QTEST_GUILESS_MAIN(TestPortapapelesSecreto)
#include "TestPortapapelesSecreto.moc"
