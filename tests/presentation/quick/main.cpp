// satcfdi_presentation_quicktests (T005.1, DA7): Qt Quick Test de
// PerfilesSatPage y EFirmaDialogo sobre el modulo QML real, con view models
// sobre fakes de promesas manuales (FixturePerfiles, context property
// "fixture"). Los tst_*.qml viven junto a este archivo.

#include "FixtureAvisoRaiz.h"
#include "FixtureExplorador.h"
#include "FixtureFacturas.h"
#include "FixturePerfiles.h"
#include "FixtureRespaldo.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QtQuickTest>

class Setup : public QObject {
    Q_OBJECT

public slots:
    void qmlEngineAvailable(QQmlEngine* engine)
    {
        engine->rootContext()->setContextProperty(QStringLiteral("fixture"), new FixturePerfiles(engine));
        // T015.3: explorador de CFDI.
        engine->rootContext()->setContextProperty(QStringLiteral("fixtureExplorador"), new FixtureExplorador(engine));
        // T019 D8: aviso de la carpeta de paquetes.
        engine->rootContext()->setContextProperty(QStringLiteral("fixtureAvisoRaiz"), new FixtureAvisoRaiz(engine));
        // T023.4: pantalla Facturas.
        engine->rootContext()->setContextProperty(QStringLiteral("fixtureFacturas"), new FixtureFacturas(engine));
        // T025.6: pagina Respaldo y franja de avisos de restauracion.
        engine->rootContext()->setContextProperty(QStringLiteral("fixtureRespaldo"), new FixtureRespaldo(engine));
    }
};

QUICK_TEST_MAIN_WITH_SETUP(satcfdi_presentation_quicktests, Setup)

#include "main.moc"
