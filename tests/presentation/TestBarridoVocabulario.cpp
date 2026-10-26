// T024.1 (criterio 1): barrido de vocabulario de los textos que no viven en
// el arbol QML: notificaciones (ServicioNotificaciones), menu bar
// (menubar::entradas y estados de MacOSMapeos) y literales tr/qsTr de las
// fuentes del recorrido normal (QML y view models, incluido CatalogoMensajes).
// El arbol de items de las vistas lo recorre satcfdi_presentation_quicktests
// con el mismo BarridoVocabulario. Diagnostico queda excluido
// (vocabulario::archivosDiagnostico) y las excepciones viven en
// vocabulario-excepciones.tsv.

#include "BarridoVocabulario.h"

#include "application/notificaciones/ServicioNotificaciones.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/SolicitudId.h"
#ifdef SATCFDI_CON_MENUBAR
#include "infrastructure/os/MenuBarDefinicion.h"
#include "infrastructure/os/macos/MacOSMapeos.h"
#endif

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTest>
#include <QTimeZone>

#include <utility>

using namespace satcfdi;
using namespace satcfdi::pruebas::vocabulario;

class TestBarridoVocabulario : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void normalizaMayusculasAcentosYPlurales();
    void pruebaNegativaDetectaLiteralProhibido();
    void excepcionSoloAplicaASuOrigenYLiteral();
    void inventarioBienFormado();
    void extraeLiteralesDeFuentes();
    void pruebaNegativaLiteralesSinTr();
    void notificacionesTodasLasTransicionesYDirecciones();
    void notificacionesDeEFirma();
    void menuBarYEstadosDelIcono();
    void literalesDeLasFuentesDelRecorridoNormal();

private:
    void revisar(const QString& origen, const QString& texto);
    void verificarSinFallas(const char* contexto);

    QList<Excepcion> m_excepciones;
    QStringList m_fallas;
    int m_revisados = 0;
};

void TestBarridoVocabulario::initTestCase()
{
    QString error;
    m_excepciones = cargarExcepciones(QStringLiteral(SATCFDI_EXCEPCIONES_VOCABULARIO), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!m_excepciones.isEmpty());
}

void TestBarridoVocabulario::revisar(const QString& origen, const QString& texto)
{
    ++m_revisados;
    for (const QString& t : terminosProhibidos(texto, {origen}, m_excepciones)) {
        m_fallas << QStringLiteral("%1: \"%2\" -> %3").arg(origen, texto, t);
    }
}

void TestBarridoVocabulario::verificarSinFallas(const char* contexto)
{
    const QStringList fallas = std::exchange(m_fallas, {});
    QVERIFY2(m_revisados > 0, contexto);
    m_revisados = 0;
    QVERIFY2(fallas.isEmpty(), qPrintable(QString::fromLatin1(contexto) + QStringLiteral(":\n")
                                          + fallas.join(QLatin1Char('\n'))));
}

void TestBarridoVocabulario::normalizaMayusculasAcentosYPlurales()
{
    QCOMPARE(normalizar(QStringLiteral("  VERIFICACIÓN   de  Paquetes ")), QStringLiteral("verificacion de paquetes"));
    for (const char* t : {"Solicitud", "SOLICITUDES", "paquete", "Paquetes", "Verificación", "verificaciones",
                          "Verificando"}) {
        QCOMPARE(terminosProhibidos(QString::fromUtf8(t), {}, {}).size(), 1);
    }
    // Palabras que solo comparten raiz no cuentan.
    for (const char* t : {"Verificar respaldo", "Se verifica todo", "empaquetado", "solicitado"}) {
        QVERIFY2(terminosProhibidos(QString::fromUtf8(t), {}, {}).isEmpty(), t);
    }
}

void TestBarridoVocabulario::pruebaNegativaDetectaLiteralProhibido()
{
    // Un literal prohibido en una fuente QML sintetica se detecta tanto en el
    // barrido de literales como al revisar el texto con su origen.
    const QString qml = QStringLiteral("Label { text: qsTr(\"Crear solicitud\"); "
                                       "Accessible.name: qsTr(\"3 paquetes listos\") }");
    const QStringList literales = literalesQml(qml);
    QCOMPARE(literales, (QStringList{QStringLiteral("Crear solicitud"), QStringLiteral("3 paquetes listos")}));
    for (const QString& l : literales) {
        revisar(QStringLiteral("FacturasPage.qml"), l);
    }
    revisar(QStringLiteral("RespaldoPage.qml"), QStringLiteral("Cancelar verificación"));
    QCOMPARE(std::exchange(m_fallas, {}).size(), 3);
    m_revisados = 0;
}

void TestBarridoVocabulario::excepcionSoloAplicaASuOrigenYLiteral()
{
    const QList<Excepcion> e{{QStringLiteral("RespaldoPage.qml"), QStringLiteral("Cancelar verificación"),
                              QStringLiteral("prueba")}};
    QVERIFY(terminosProhibidos(QStringLiteral("Cancelar verificación"), {QStringLiteral("RespaldoPage.qml")}, e)
                .isEmpty());
    // Otro origen: falla.
    QCOMPARE(terminosProhibidos(QStringLiteral("Cancelar verificación"), {QStringLiteral("FacturasPage.qml")}, e),
             QStringList{QStringLiteral("verificacion")});
    // Mismo origen, otro termino en el mismo texto: falla.
    QCOMPARE(terminosProhibidos(QStringLiteral("Cancelar verificación de 2 paquetes"),
                                {QStringLiteral("RespaldoPage.qml")}, e),
             QStringList{QStringLiteral("paquetes")});
}

void TestBarridoVocabulario::inventarioBienFormado()
{
    // Cada fila: origen, literal con un termino prohibido y motivo.
    QString error;
    const QString ruta = QDir::temp().filePath(QStringLiteral("vocabulario-excepciones-invalido.tsv"));
    QFile f(ruta);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("# comentario\nRespaldoPage.qml\tVerificar respaldo\tno contiene termino prohibido\n");
    f.close();
    QVERIFY(cargarExcepciones(ruta, &error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("fila 2")));
    QFile::remove(ruta);
    for (const Excepcion& x : std::as_const(m_excepciones)) {
        QVERIFY(!x.origen.isEmpty() && !x.motivo.isEmpty());
        QVERIFY2(!archivosDiagnostico().contains(x.origen), qPrintable(x.origen)); // Diagnostico ya esta excluido
    }
}

void TestBarridoVocabulario::extraeLiteralesDeFuentes()
{
    // C++: todo literal que parezca visible (espacio o mayuscula inicial),
    // con o sin tr; concatena adyacentes; fuera comentarios, #include, char
    // literales y claves internas en minuscula de una palabra.
    const QString cpp = QStringLiteral(
        "#include \"domain/solicitudes/Solicitud.h\"\n"
        "return tr(\"Uno \"\n \"dos\"); x = trn(\"%n archivo\", \"%n archivos\", n); "
        "y = textoMenu(\"Ver facturas\"); z = QStringLiteral(\"paquetes\"); c = '\"'; "
        "// QStringLiteral(\"Solicitud en comentario\")\n"
        "/* \"Paquetes en bloque\" */ w = QStringLiteral(\"3 paquetes\"); v = QLatin1String(\"Paquetes\");");
    QCOMPARE(literalesCpp(cpp), (QStringList{QStringLiteral("Uno dos"), QStringLiteral("%n archivo"),
                                             QStringLiteral("%n archivos"), QStringLiteral("Ver facturas"),
                                             QStringLiteral("paquetes"), QStringLiteral("3 paquetes"),
                                             QStringLiteral("Paquetes")}));
    // QML/JS: literales directos y qsTr, comillas dobles, simples y
    // plantillas; objectName y claves en minuscula quedan fuera.
    const QString qml = QStringLiteral(
        "Label { objectName: \"listaSolicitudes\"; text: \"Crear solicitud\" } // \"Comentario solicitud\"\n"
        "Item { Accessible.name: qsTr(\"Paquetes\"); property string k: 'Otra solicitud'; "
        "property string t: `Hay ${n} paquetes` }");
    QCOMPARE(literalesQml(qml), (QStringList{QStringLiteral("Crear solicitud"), QStringLiteral("Paquetes"),
                                             QStringLiteral("Otra solicitud"), QStringLiteral("Hay ${n} paquetes")}));
}

void TestBarridoVocabulario::pruebaNegativaLiteralesSinTr()
{
    // T024.1 (revision): QStringLiteral en C++ y literal directo en QML.
    for (const QString& l : literalesCpp(QStringLiteral("return QStringLiteral(\"3 paquetes\");"))) {
        revisar(QStringLiteral("CatalogoMensajes.cpp"), l);
    }
    for (const QString& l : literalesQml(QStringLiteral("Label { text: \"Crear solicitud\" }"))) {
        revisar(QStringLiteral("FacturasPage.qml"), l);
    }
    const QStringList fallas = std::exchange(m_fallas, {});
    m_revisados = 0;
    QCOMPARE(fallas.size(), 2);
    QVERIFY(fallas.at(0).endsWith(QStringLiteral("-> paquetes")));
    QVERIFY(fallas.at(1).endsWith(QStringLiteral("-> solicitud")));
}

void TestBarridoVocabulario::notificacionesTodasLasTransicionesYDirecciones()
{
    const QString origen = QStringLiteral("ServicioNotificaciones.cpp");
    int compuestas = 0;
    for (TipoTransicionNotificable tipo :
         {TipoTransicionNotificable::Terminada, TipoTransicionNotificable::DescargaCompleta,
          TipoTransicionNotificable::ErrorSat, TipoTransicionNotificable::Rechazada,
          TipoTransicionNotificable::SinFacturas, TipoTransicionNotificable::Vencida}) {
        for (TipoDescarga direccion : {TipoDescarga::Emitidos, TipoDescarga::Recibidos}) {
            for (int paquetes : {0, 3}) {
                TransicionNotificable t;
                t.solicitudId = SolicitudId::generar();
                t.tipo = tipo;
                t.tipoDescarga = direccion;
                t.rfcSolicitante = QStringLiteral("XAXX010101000");
                t.fechaInicialSat = QStringLiteral("2026-09-01T00:00:00");
                t.fechaFinalSat = QStringLiteral("2026-09-30T23:59:59");
                t.paquetes = paquetes;
                t.descargados = paquetes;
                const auto n = ServicioNotificaciones::componer(t);
                if (!n) {
                    continue; // Terminada con archivos no se notifica (D4).
                }
                ++compuestas;
                revisar(origen, n->titulo);
                revisar(origen, n->cuerpo);
            }
        }
    }
    QCOMPARE(compuestas, 22); // 6 tipos x 2 direcciones x 2 conteos, menos Terminada con archivos
    verificarSinFallas("componer");
}

void TestBarridoVocabulario::notificacionesDeEFirma()
{
    const QString origen = QStringLiteral("ServicioNotificaciones.cpp");
    const PerfilId perfil = PerfilId::generar();
    for (EstadoCredencial estado : kEstadosCredencial) {
        if (const auto n = ServicioNotificaciones::componerCredencial(perfil, estado)) {
            revisar(origen, n->titulo);
            revisar(origen, n->cuerpo);
        }
    }
    for (int dias : {0, 1, 7, 30}) {
        const Notificacion n = ServicioNotificaciones::componerVencimiento(
            perfil, QStringLiteral("XAXX010101000"), QDateTime(QDate(2026, 12, 1), QTime(12, 0), QTimeZone::UTC), 30,
            dias);
        revisar(origen, n.titulo);
        revisar(origen, n.cuerpo);
    }
    verificarSinFallas("e.firma");
}

void TestBarridoVocabulario::menuBarYEstadosDelIcono()
{
#ifdef SATCFDI_CON_MENUBAR
    using Fase = OSIntegration::EstadoMonitoreo::Fase;
    using Actividad = OSIntegration::EstadoMonitoreo::Actividad;
    using Login = OSIntegration::LoginItemStatus;
    using Notif = OSIntegration::NotificationStatus;
    const QString origen = QStringLiteral("MenuBarDefinicion.h");
    for (bool pausado : {false, true}) {
        for (Fase fase : {Fase::Pausado, Fase::ActivoEnEspera, Fase::Ejecutando, Fase::Deteniendo, Fase::Detenido}) {
            for (Actividad act : {Actividad::Ninguna, Actividad::Enviando, Actividad::Verificando,
                                  Actividad::Descargando, Actividad::Otra}) {
                for (Login login : {Login::Disabled, Login::Enabled, Login::RequiresApproval, Login::Rejected,
                                    Login::Unavailable}) {
                    for (Notif notif : {Notif::NotDetermined, Notif::Granted, Notif::Denied, Notif::Unavailable}) {
                        menubar::EstadoMenu s;
                        s.monitoreoPausado = pausado;
                        s.monitoreo = {fase, act, 2};
                        s.preferenciaLoginItem = pausado;
                        s.loginItem = login;
                        s.notificaciones = notif;
                        for (const menubar::Entrada& e : menubar::entradas(s)) {
                            revisar(origen, e.texto);
                        }
                    }
                }
            }
        }
    }
    const QString mapeos = QStringLiteral("MacOSMapeos.cpp");
    for (auto icono : {OSIntegration::EstadoIcono::Normal, OSIntegration::EstadoIcono::Trabajando,
                       OSIntegration::EstadoIcono::Pausado, OSIntegration::EstadoIcono::Atencion}) {
        revisar(mapeos, macos::textoEstadoIcono(icono));
    }
    for (auto accion : {OSIntegration::AccionNotificacion::Abrir, OSIntegration::AccionNotificacion::MostrarEnFinder,
                        OSIntegration::AccionNotificacion::AbrirPerfiles}) {
        revisar(mapeos, macos::textoAccionNotificacion(accion));
    }
    verificarSinFallas("menu bar");
#else
    QSKIP("MenuBarDefinicion/MacOSMapeos solo se compilan en macOS");
#endif
}

void TestBarridoVocabulario::literalesDeLasFuentesDelRecorridoNormal()
{
    // Todos los QML y view models de presentacion, salvo Diagnostico, mas los
    // textos fijos del menu bar. Un archivo nuevo entra solo al barrido.
    const QString raiz = QStringLiteral(SATCFDI_FUENTES);
    struct Fuente {
        QString directorio;
        QStringList filtros;
    };
    const QList<Fuente> fuentes{
        {QStringLiteral("presentation/qml"), {QStringLiteral("*.qml"), QStringLiteral("*.js")}},
        {QStringLiteral("presentation/viewmodels"), {QStringLiteral("*.cpp"), QStringLiteral("*.h")}},
        {QStringLiteral("infrastructure/os"), {QStringLiteral("MenuBarDefinicion.h"), QStringLiteral("MacOSMapeos.cpp")}},
        {QStringLiteral("application/notificaciones"), {QStringLiteral("ServicioNotificaciones.cpp")}},
    };
    int archivos = 0;
    int literales = 0;
    for (const Fuente& f : fuentes) {
        QDirIterator it(raiz + QLatin1Char('/') + f.directorio, f.filtros, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString ruta = it.next();
            const QString nombre = it.fileInfo().fileName();
            if (archivosDiagnostico().contains(nombre)) {
                continue; // Diagnostico: excluido de forma explicita.
            }
            QFile archivo(ruta);
            QVERIFY(archivo.open(QIODevice::ReadOnly));
            const QString contenido = QString::fromUtf8(archivo.readAll());
            const bool qml = nombre.endsWith(QLatin1String(".qml")) || nombre.endsWith(QLatin1String(".js"));
            const QStringList textos = qml ? literalesQml(contenido) : literalesCpp(contenido);
            for (const QString& t : textos) {
                revisar(nombre, t);
            }
            ++archivos;
            literales += int(textos.size());
        }
    }
    QVERIFY2(archivos > 60, qPrintable(QString::number(archivos)));
    QVERIFY2(literales > 500, qPrintable(QString::number(literales)));
    verificarSinFallas("literales de las fuentes");
}

QTEST_GUILESS_MAIN(TestBarridoVocabulario)

#include "TestBarridoVocabulario.moc"
