// T024.4: descarga guiada simulada (datos.meses + avanzarMes), Historial por
// lotes, respaldo gobernado por el guion, notificacion facturasDescargadas
// con los textos de ServicioNotificaciones y acciones vigiladas (D10). Sin
// QML ni grabacion: DemoVideo con el guion leido por leerGuion.

#include "DatosDemo.h"

#include "application/notificaciones/ServicioNotificaciones.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace satcfdi;
using namespace video;

namespace {

QString escribir(const QTemporaryDir& dir, const QByteArray& json)
{
    const QString ruta = dir.filePath(QStringLiteral("guion.json"));
    QFile f(ruta);
    if (!f.open(QIODevice::WriteOnly) || f.write(json) != json.size()) {
        return {};
    }
    return ruta;
}

// Guion minimo con datos.meses (una escena de espera).
QByteArray guionConMeses(const QByteArray& meses, const QByteArray& extraDatos = {})
{
    return QByteArrayLiteral(R"({"esquema": 1, "tipo": "ejemplo", "titulo": "Prueba T024.4",
      "presupuestoTotal": {"min": 1, "max": 100000},
      "datos": {"ahora": "2026-10-06T16:00:00Z",
        "perfiles": [{"clave": "principal", "rfc": "XAXX010101000", "nombre": "Contribuyente de ejemplo",
                      "efirma": "lista", "visible": false}],
        "solicitudes": [], "meses": )")
           + meses + extraDatos
           + QByteArrayLiteral(R"(},
      "capitulos": [{"id": "c", "titulo": "C", "escenas": [{"id": "e", "presupuesto": {"min": 1, "max": 900},
        "lineas": [{"id": "l1", "texto": "Una linea corta."}],
        "acciones": [{"tipo": "esperar", "cuadros": 40}]}]}]})");
}

const QByteArray kMeses = QByteArrayLiteral(R"([
    {"perfil": "principal", "mes": "2026-07", "estado": "requiereAtencion", "direccion": "recibidas",
     "causa": "ErrorSat", "emitidas": 6, "recibidas": 0},
    {"perfil": "principal", "mes": "2026-08", "estado": "disponibles", "emitidas": 8, "recibidas": 12,
     "descargadoEn": "2026-09-02T17:00:00Z"},
    {"perfil": "principal", "mes": "2026-09", "estado": "sinDescargar", "emitidas": 7, "recibidas": 5}])");

} // namespace

class TestDemoT0244 : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    Guion m_guion;
    std::unique_ptr<DemoVideo> m_demo;

    void preparar(const QByteArray& json)
    {
        m_guion = Guion{};
        const QString ruta = escribir(m_dir, json);
        QVERIFY(!ruta.isEmpty());
        const QString e = leerGuion(ruta, m_guion);
        QVERIFY2(e.isEmpty(), qPrintable(e));
        m_demo = std::make_unique<DemoVideo>(m_guion.datos, m_dir.filePath(QStringLiteral("demo")));
        const QString p = m_demo->preparar();
        QVERIFY2(p.isEmpty(), qPrintable(p));
    }
    PerfilId perfil() const { return m_demo->perfiles.perfiles.constFirst().id; }
    MesCalendario mes(int m) const { return *MesCalendario::crear(2026, m); }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    // Facturas del perfil oculto (visible:false) desde datos.meses: listas
    // del ZIP sintetico indexado con el codigo real y estado por direccion.
    void mesesDeclarados()
    {
        preparar(guionConMeses(kMeses));
        const auto agosto = m_demo->facturas.facturas(perfil(), mes(8), std::nullopt);
        QCOMPARE(agosto.size(), 20);
        for (const CfdiMensual& c : agosto) {
            QVERIFY(c.fuenteGuiada);
            QVERIFY(m_demo->previsualizacion.zipPorPaquete.contains(c.cfdi.paqueteId));
        }
        QCOMPARE(m_demo->facturas.evaluacion(perfil(), mes(8)).estado, EstadoMes::Disponibles);
        const EvaluacionCoberturaMensual julio = m_demo->facturas.evaluacion(perfil(), mes(7));
        QCOMPARE(julio.estado, EstadoMes::RequiereAtencion);
        QVERIFY(julio.causa.has_value());
        QCOMPARE(m_demo->facturas.facturas(perfil(), mes(7), TipoDescarga::Emitidos).size(), 6);
        QCOMPARE(m_demo->facturas.evaluacion(perfil(), mes(9)).estado, EstadoMes::SinDescargar);
        QVERIFY(m_demo->facturas.facturas(perfil(), mes(9), std::nullopt).isEmpty());
    }

    // avanzarMes: Preparando -> Disponibles por direccion, avisando a
    // Facturas e Historial como el worker.
    void avanzarMesPorDireccion()
    {
        preparar(guionConMeses(kMeses));
        QSignalSpy facturas(&m_demo->facturas, &FuenteFacturasMes::datosCambiados);
        QSignalSpy historial(&m_demo->historial, &FuenteHistorial::datosCambiados);
        QVERIFY(m_demo->avanzarMes(QStringLiteral("principal"), QStringLiteral("2026-09"), {},
                                   QStringLiteral("preparando"), {})
                    .isEmpty());
        QCOMPARE(m_demo->facturas.evaluacion(perfil(), mes(9)).estado, EstadoMes::Preparando);
        QVERIFY(m_demo->avanzarMes(QStringLiteral("principal"), QStringLiteral("2026-09"), QStringLiteral("emitidas"),
                                   QStringLiteral("disponibles"), {})
                    .isEmpty());
        QCOMPARE(m_demo->facturas.evaluacion(perfil(), mes(9)).estado, EstadoMes::Preparando);
        QCOMPARE(m_demo->facturas.facturas(perfil(), mes(9), TipoDescarga::Emitidos).size(), 7);
        QVERIFY(m_demo->facturas.facturas(perfil(), mes(9), TipoDescarga::Recibidos).isEmpty());
        QVERIFY(m_demo->avanzarMes(QStringLiteral("principal"), QStringLiteral("2026-09"), QStringLiteral("recibidas"),
                                   QStringLiteral("disponibles"), {})
                    .isEmpty());
        QCOMPARE(m_demo->facturas.evaluacion(perfil(), mes(9)).estado, EstadoMes::Disponibles);
        QCOMPARE(m_demo->facturas.facturas(perfil(), mes(9), std::nullopt).size(), 12);
        QCOMPARE(facturas.count(), 3);
        QCOMPARE(historial.count(), 3);
        QVERIFY(!m_demo->avanzarMes(QStringLiteral("principal"), QStringLiteral("2026-10"), {},
                                    QStringLiteral("disponibles"), {})
                     .isEmpty());
    }

    // Historial: un lote por mes descargado, coherente con su estado.
    void historialPorLotes()
    {
        preparar(guionConMeses(kMeses));
        auto entradas = m_demo->historial.historial(perfil()).result().valor();
        QCOMPARE(entradas.size(), 2); // julio y agosto; septiembre sin descargar
        QCOMPARE(entradas.at(0).mes, std::optional<MesCalendario>(mes(8)));
        QCOMPARE(entradas.at(0).resultado, ResultadoHistorial::Completa);
        QCOMPARE(entradas.at(0).emitidas + entradas.at(0).recibidas, 20);
        QCOMPARE(entradas.at(0).creadoEn, QDateTime::fromString(QStringLiteral("2026-09-02T17:00:00Z"), Qt::ISODate));
        QCOMPARE(entradas.at(1).resultado, ResultadoHistorial::RequiereAtencion);
        QVERIFY(entradas.at(1).puedeReintentar);
        m_demo->avanzarMes(QStringLiteral("principal"), QStringLiteral("2026-09"), {}, QStringLiteral("preparando"), {});
        entradas = m_demo->historial.historial(perfil()).result().valor();
        QCOMPARE(entradas.size(), 3);
        QCOMPARE(entradas.at(0).mes, std::optional<MesCalendario>(mes(9)));
        QCOMPARE(entradas.at(0).resultado, ResultadoHistorial::EnPreparacion);
    }

    // Toda CausaAtencion del dominio se acepta en datos.meses (Guion.cpp
    // mantiene la lista a mano; esta prueba la sincroniza).
    void causasSincronizadas()
    {
        for (int i = 0; i <= int(CausaAtencion::IndexacionSinProgramar); ++i) {
            const QByteArray clave = QByteArray(claveEstable(CausaAtencion(i)).data(),
                                                claveEstable(CausaAtencion(i)).size());
            Guion g;
            const QString ruta = escribir(m_dir, guionConMeses(QByteArrayLiteral(
                R"([{"perfil": "principal", "mes": "2026-07", "estado": "requiereAtencion", "causa": ")")
                + clave + QByteArrayLiteral(R"("}])")));
            const QString e = leerGuion(ruta, g);
            QVERIFY2(e.isEmpty(), qPrintable(QString::fromLatin1(clave) + QStringLiteral(": ") + e));
        }
    }

    // facturasDescargadas: titulo y cuerpo de ServicioNotificaciones
    // (descarga completa de una direccion del mes).
    void notificacionFacturasDescargadas()
    {
        preparar(guionConMeses(kMeses));
        QString error;
        const QVariantList ns = m_demo->notificaciones(
            {{QStringLiteral("notificaciones"),
              QVariantList{QVariantMap{{QStringLiteral("tipo"), QStringLiteral("facturasDescargadas")},
                                       {QStringLiteral("perfil"), QStringLiteral("principal")},
                                       {QStringLiteral("mes"), QStringLiteral("2026-09")},
                                       {QStringLiteral("direccion"), QStringLiteral("emitidas")}},
                           QVariantMap{{QStringLiteral("tipo"), QStringLiteral("facturasDescargadas")},
                                       {QStringLiteral("perfil"), QStringLiteral("principal")},
                                       {QStringLiteral("mes"), QStringLiteral("2026-09")},
                                       {QStringLiteral("direccion"), QStringLiteral("recibidas")}}}}},
            error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(ns.size(), 2);
        for (int i = 0; i < 2; ++i) {
            TransicionNotificable t;
            t.tipo = TipoTransicionNotificable::DescargaCompleta;
            t.tipoDescarga = i == 0 ? TipoDescarga::Emitidos : TipoDescarga::Recibidos;
            t.rfcSolicitante = QStringLiteral("XAXX010101000");
            t.fechaInicialSat = QStringLiteral("2026-09-01T00:00:00");
            t.fechaFinalSat = QStringLiteral("2026-09-30T23:59:59");
            t.paquetes = 1;
            t.descargados = 1;
            const std::optional<Notificacion> real = ServicioNotificaciones::componer(t);
            QVERIFY(real.has_value());
            const QVariantMap n = ns.at(i).toMap();
            QCOMPARE(n.value(QStringLiteral("titulo")).toString(), real->titulo);
            QCOMPARE(n.value(QStringLiteral("cuerpo")).toString(), real->cuerpo);
            QCOMPARE(real->titulo, QStringLiteral("Facturas descargadas"));
            QVERIFY(real->cuerpo.startsWith(i == 0 ? QStringLiteral("Las facturas emitidas ya están en tu equipo.")
                                                   : QStringLiteral("Las facturas recibidas ya están en tu equipo.")));
        }
    }

    // Respaldo gobernado por el guion: solo con uno en curso.
    void respaldoGobernado()
    {
        preparar(guionConMeses(QByteArrayLiteral("[]")));
        QVERIFY(!m_demo->progresoRespaldo(QStringLiteral("escritura"), 10, 100).isEmpty());
        QVERIFY(!m_demo->resultadoRespaldo(QStringLiteral("exito"), {}, 1).isEmpty());
        (void)m_demo->respaldo.respaldarAhora();
        QVERIFY(m_demo->respaldo.estadoActual.enCurso);
        QSignalSpy progreso(&m_demo->respaldo, &ControlRespaldoPresentacion::progreso);
        QVERIFY(m_demo->progresoRespaldo(QStringLiteral("escritura"), 10, 100).isEmpty());
        QCOMPARE(progreso.count(), 1);
        QVERIFY(m_demo->resultadoRespaldo(QStringLiteral("conAvisos"), QStringLiteral("2026-10-06T16:12:00Z"),
                                          12'400'000)
                    .isEmpty());
        QVERIFY(!m_demo->respaldo.estadoActual.enCurso);
        QVERIFY(m_demo->respaldo.ultimo.has_value());
        QCOMPARE(m_demo->respaldo.ultimo->estado, respaldo::ResultadoRespaldo::Estado::ExitoConAdvertencias);
        QCOMPARE(m_demo->respaldo.ultimo->bytes, quint64(12'400'000));
    }

    // D10: Finder y Vista Previa dejan una violacion.
    void accionesVigiladas()
    {
        preparar(guionConMeses(QByteArrayLiteral("[]")));
        QVERIFY(m_demo->violacion().isEmpty());
        (void)m_demo->acciones.abrirEnVistaPrevia(QStringLiteral("/Users/usuario/a.pdf"));
        QVERIFY(m_demo->violacion().contains(QStringLiteral("Vista Previa")));
    }
};

QTEST_GUILESS_MAIN(TestDemoT0244)
#include "TestDemoT0244.moc"
