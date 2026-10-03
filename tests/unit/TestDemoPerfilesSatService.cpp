#include "TestDemoPerfilesSatService.h"

#include "application/profiles/DemoPerfilesSatService.h"

#include <QTest>

using namespace satcfdi;

void TestDemoPerfilesSatService::listarNoEliminadosIncluyeInactivosYOrdenaPorRfc()
{
    const QList<PerfilResumen> catalogo = DemoPerfilesSatService::perfilesDemo();
    QVERIFY(std::any_of(catalogo.cbegin(), catalogo.cend(),
                        [](const PerfilResumen& p) { return !p.activo; }));

    DemoPerfilesSatService servicio;
    auto future = servicio.listarNoEliminados();
    QVERIFY(future.isFinished());
    const auto resultado = future.result();
    QVERIFY(resultado.esExito());

    const QList<PerfilResumen>& perfiles = resultado.valor();
    QCOMPARE(perfiles.size(), catalogo.size());
    for (const PerfilResumen& p : perfiles) {
        QVERIFY(!p.id.esNulo());
        QVERIFY(!p.rfc.isEmpty());
        QVERIFY(!p.nombre.isEmpty());
    }
    QVERIFY(std::any_of(perfiles.cbegin(), perfiles.cend(), [](const PerfilResumen& p) { return !p.activo; }));
    for (qsizetype i = 1; i < perfiles.size(); ++i) {
        QVERIFY(perfiles.at(i - 1).rfc < perfiles.at(i).rfc);
    }
}

void TestDemoPerfilesSatService::listarNoEliminadosVacio()
{
    DemoPerfilesSatService servicio({});
    const auto resultado = servicio.listarNoEliminados().result();
    QVERIFY(resultado.esExito());
    QVERIFY(resultado.valor().isEmpty());
}
