#include "TestDemoPerfilesSatService.h"

#include "application/profiles/DemoPerfilesSatService.h"

#include <QTest>

using namespace satcfdi;

void TestDemoPerfilesSatService::listarActivosExcluyeInactivosYOrdenaPorRfc()
{
    const QList<PerfilResumen> catalogo = DemoPerfilesSatService::perfilesDemo();
    QVERIFY(std::any_of(catalogo.cbegin(), catalogo.cend(),
                        [](const PerfilResumen& p) { return !p.activo; }));

    DemoPerfilesSatService servicio;
    auto future = servicio.listarActivos();
    QVERIFY(future.isFinished());
    const auto resultado = future.result();
    QVERIFY(resultado.esExito());

    const QList<PerfilResumen>& activos = resultado.valor();
    QCOMPARE(activos.size(), 2);
    for (const PerfilResumen& p : activos) {
        QVERIFY(p.activo);
        QVERIFY(!p.id.esNulo());
        QVERIFY(!p.rfc.isEmpty());
        QVERIFY(!p.razonSocial.isEmpty());
    }
    QVERIFY(activos.at(0).rfc < activos.at(1).rfc);
}

void TestDemoPerfilesSatService::listarActivosVacio()
{
    DemoPerfilesSatService servicio({});
    const auto resultado = servicio.listarActivos().result();
    QVERIFY(resultado.esExito());
    QVERIFY(resultado.valor().isEmpty());
}
