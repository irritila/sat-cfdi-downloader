#include "TestResultado.h"

#include "application/common/Resultado.h"

#include <QString>
#include <QTest>

#include <memory>

using namespace satcfdi;

void TestResultado::exitoContieneValor()
{
    const auto r = Resultado<int, QString>::exito(42);
    QVERIFY(r.esExito());
    QVERIFY(static_cast<bool>(r));
    QCOMPARE(r.valor(), 42);
}

void TestResultado::falloContieneError()
{
    const auto r = Resultado<int, QString>::fallo(QStringLiteral("x"));
    QVERIFY(!r.esExito());
    QCOMPARE(r.error(), QStringLiteral("x"));
}

void TestResultado::valorMovible()
{
    auto r = Resultado<std::unique_ptr<int>, QString>::exito(std::make_unique<int>(7));
    std::unique_ptr<int> p = std::move(r).valor();
    QVERIFY(p);
    QCOMPARE(*p, 7);
}

void TestResultado::mismoTipoEnValorYError()
{
    const auto ok = Resultado<QString, QString>::exito(QStringLiteral("v"));
    const auto ko = Resultado<QString, QString>::fallo(QStringLiteral("e"));
    QVERIFY(ok.esExito());
    QCOMPARE(ok.valor(), QStringLiteral("v"));
    QVERIFY(!ko.esExito());
    QCOMPARE(ko.error(), QStringLiteral("e"));
}
