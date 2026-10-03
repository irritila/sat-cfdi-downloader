// satcfdi_presentation_quicktests (T005.1, DA7): Qt Quick Test de
// PerfilesSatPage y EFirmaDialogo sobre el modulo QML real, con view models
// sobre fakes de promesas manuales (FixturePerfiles, context property
// "fixture"). Los tst_*.qml viven junto a este archivo.

#include "FixturePerfiles.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QtQuickTest>

class Setup : public QObject {
    Q_OBJECT

public slots:
    void qmlEngineAvailable(QQmlEngine* engine)
    {
        engine->rootContext()->setContextProperty(QStringLiteral("fixture"), new FixturePerfiles(engine));
    }
};

QUICK_TEST_MAIN_WITH_SETUP(satcfdi_presentation_quicktests, Setup)

#include "main.moc"
