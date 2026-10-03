#include "TestDominio.h"

#include "domain/common/UuidCanonico.h"
#include "domain/paquetes/EstadoDescarga.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/EstadoResumen.h"
#include "domain/solicitudes/SolicitudId.h"

#include <QSet>
#include <QTest>

using namespace satcfdi;

void TestDominio::uuidCanonicoAceptaSoloFormaCanonica_data()
{
    QTest::addColumn<QString>("texto");
    QTest::addColumn<bool>("valido");
    QTest::newRow("canonico") << "0f8fad5b-d9cb-469f-a165-70867728950e" << true;
    QTest::newRow("mayusculas") << "0F8FAD5B-D9CB-469F-A165-70867728950E" << false;
    QTest::newRow("llaves") << "{0f8fad5b-d9cb-469f-a165-70867728950e}" << false;
    QTest::newRow("sin guiones") << "0f8fad5bd9cb469fa16570867728950e" << false;
    QTest::newRow("guion movido") << "0f8fad5bd-9cb-469f-a165-70867728950e" << false;
    QTest::newRow("no hex") << "0f8fad5b-d9cb-469f-a165-70867728950g" << false;
    QTest::newRow("vacio") << "" << false;
    QTest::newRow("espacios") << " 0f8fad5b-d9cb-469f-a165-70867728950e" << false;
}

void TestDominio::uuidCanonicoAceptaSoloFormaCanonica()
{
    QFETCH(QString, texto);
    QFETCH(bool, valido);
    QCOMPARE(uuid::esCanonico(texto), valido);
    QCOMPARE(SolicitudId::desdeTexto(texto).has_value(), valido);
    QCOMPARE(PerfilId::desdeTexto(texto).has_value(), valido);
    if (valido) {
        QCOMPARE(SolicitudId::desdeTexto(texto)->texto(), texto);
    }
}

void TestDominio::idsGeneradosSonCanonicosYUnicos()
{
    QSet<SolicitudId> ids;
    for (int i = 0; i < 100; ++i) {
        const SolicitudId id = SolicitudId::generar();
        QVERIFY(!id.esNulo());
        QVERIFY2(uuid::esCanonico(id.texto()), qPrintable(id.texto()));
        ids.insert(id);
    }
    QCOMPARE(ids.size(), 100);
    QVERIFY(uuid::esCanonico(PerfilId::generar().texto()));
}

void TestDominio::idNuloPorDefecto()
{
    QVERIFY(SolicitudId().esNulo());
    QVERIFY(PerfilId().esNulo());
    QCOMPARE(SolicitudId(), SolicitudId());
    QVERIFY(SolicitudId() != SolicitudId::generar());
}

void TestDominio::estadoResumenDerivaDeSatOLocal_data()
{
    QTest::addColumn<int>("local");
    QTest::addColumn<int>("sat"); // -1 = sin estado SAT
    QTest::addColumn<QString>("clave");

    using L = EstadoLocal;
    using S = EstadoSolicitudSat;
    QTest::newRow("Creada") << int(L::Creada) << -1 << "Creada";
    QTest::newRow("Enviando") << int(L::Enviando) << -1 << "Enviando";
    QTest::newRow("Enviada") << int(L::Enviada) << -1 << "Enviada";
    QTest::newRow("EnvioFallido") << int(L::EnvioFallido) << -1 << "EnvioFallido";
    QTest::newRow("EnvioIncierto") << int(L::EnvioIncierto) << -1 << "EnvioIncierto";
    QTest::newRow("Aceptada") << int(L::Enviada) << int(S::Aceptada) << "Aceptada";
    QTest::newRow("EnProceso") << int(L::Enviada) << int(S::EnProceso) << "EnProceso";
    QTest::newRow("Terminada") << int(L::Enviada) << int(S::Terminada) << "Terminada";
    QTest::newRow("Error->ErrorSat") << int(L::Enviada) << int(S::Error) << "ErrorSat";
    QTest::newRow("Rechazada") << int(L::Enviada) << int(S::Rechazada) << "Rechazada";
    QTest::newRow("Vencida") << int(L::Enviada) << int(S::Vencida) << "Vencida";
    // El estado SAT tiene prioridad sobre cualquier estado local.
    QTest::newRow("sat prevalece") << int(L::Creada) << int(S::Terminada) << "Terminada";
}

void TestDominio::estadoResumenDerivaDeSatOLocal()
{
    QFETCH(int, local);
    QFETCH(int, sat);
    QFETCH(QString, clave);
    std::optional<EstadoSolicitudSat> estadoSat;
    if (sat >= 0) {
        estadoSat = EstadoSolicitudSat(sat);
    }
    QCOMPARE(claveEstable(derivarEstadoResumen(EstadoLocal(local), estadoSat)), clave);
}

void TestDominio::clavesEstablesDeEnums()
{
    QCOMPARE(claveEstable(EstadoLocal::EnvioIncierto), QStringLiteral("EnvioIncierto"));
    // La clave del estado SAT conserva el nombre de dominio; solo el resumen usa ErrorSat.
    QCOMPARE(claveEstable(EstadoSolicitudSat::Error), QStringLiteral("Error"));
    QCOMPARE(claveEstable(EstadoResumen::ErrorSat), QStringLiteral("ErrorSat"));
    QCOMPARE(claveEstable(TipoDescarga::Recibidos), QStringLiteral("Recibidos"));
    QCOMPARE(claveEstable(EstadoDescarga::Vencido), QStringLiteral("Vencido"));
}

void TestDominio::tipoDescargaDesdeClave()
{
    QCOMPARE(satcfdi::tipoDescargaDesdeClave(u"Emitidos"), TipoDescarga::Emitidos);
    QCOMPARE(satcfdi::tipoDescargaDesdeClave(u"Recibidos"), TipoDescarga::Recibidos);
    QVERIFY(!satcfdi::tipoDescargaDesdeClave(u"emitidos").has_value());
    QVERIFY(!satcfdi::tipoDescargaDesdeClave(u"").has_value());
}
