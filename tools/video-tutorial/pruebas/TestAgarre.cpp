// T022 D7 (C2): contrato de conservaAgarre con items QML sinteticos.

#include "Agarre.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTest>

#include <memory>

class TestAgarre : public QObject {
    Q_OBJECT

private:
    QQmlEngine m_engine;

    // Arbol: raiz > fila > objetivo > interno; raiz > otro.
    std::unique_ptr<QQuickItem> crear(const QByteArray& propiedadesObjetivo = {})
    {
        QQmlComponent c(&m_engine);
        c.setData("import QtQuick\n"
                  "Item {\n"
                  "  Item { objectName: 'fila'\n"
                  "    Item { objectName: 'objetivo'; " + propiedadesObjetivo + "\n"
                  "      Item { objectName: 'interno' } } }\n"
                  "  Item { objectName: 'otro' }\n"
                  "}\n",
                  QUrl());
        auto raiz = std::unique_ptr<QQuickItem>(qobject_cast<QQuickItem*>(c.create()));
        if (!raiz) {
            qWarning() << c.errorString();
        }
        return raiz;
    }

    static QQuickItem* item(QQuickItem* raiz, const char* nombre)
    {
        return raiz->findChild<QQuickItem*>(QString::fromLatin1(nombre));
    }

private slots:
    void grabberRelacionadoConserva_data()
    {
        QTest::addColumn<QString>("grabber");
        QTest::newRow("objetivo") << QStringLiteral("objetivo");
        QTest::newRow("descendiente") << QStringLiteral("interno");
        QTest::newRow("ancestro") << QStringLiteral("fila");
    }

    void grabberRelacionadoConserva()
    {
        QFETCH(QString, grabber);
        auto raiz = crear();
        QVERIFY(raiz);
        QVERIFY(video::conservaAgarre(item(raiz.get(), "objetivo"), item(raiz.get(), qPrintable(grabber))));
    }

    void grabberAjenoFalla()
    {
        auto raiz = crear();
        QVERIFY(raiz);
        QVERIFY(!video::conservaAgarre(item(raiz.get(), "objetivo"), item(raiz.get(), "otro")));
    }

    void grabberAjenoFallaAunqueSigaPresionado()
    {
        auto raiz = crear("property bool pressed: true");
        QVERIFY(raiz);
        QVERIFY(!video::conservaAgarre(item(raiz.get(), "objetivo"), item(raiz.get(), "otro")));
    }

    void sinGrabberFalla()
    {
        auto raiz = crear("property bool pressed: false; property bool down: false");
        QVERIFY(raiz);
        QVERIFY(!video::conservaAgarre(item(raiz.get(), "objetivo"), nullptr));
    }

    void sinGrabberPresionadoConserva_data()
    {
        QTest::addColumn<QByteArray>("propiedad");
        QTest::newRow("pressed") << QByteArray("property bool pressed: true");
        QTest::newRow("down") << QByteArray("property bool down: true");
    }

    void sinGrabberPresionadoConserva()
    {
        QFETCH(QByteArray, propiedad);
        auto raiz = crear(propiedad);
        QVERIFY(raiz);
        QVERIFY(video::conservaAgarre(item(raiz.get(), "objetivo"), nullptr));
    }

    void sinObjetivoFalla()
    {
        auto raiz = crear();
        QVERIFY(raiz);
        QVERIFY(!video::conservaAgarre(nullptr, item(raiz.get(), "fila")));
    }
};

QTEST_MAIN(TestAgarre)
#include "TestAgarre.moc"
