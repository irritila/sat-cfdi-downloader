#include "TestNotificaciones.h"

#include "application/notificaciones/ServicioNotificaciones.h"
#include "application/common/FechasLegibles.h"

#include <QTest>

using namespace satcfdi;
using namespace Qt::StringLiterals;

namespace {

const QString kRfc = QStringLiteral("EKU9003173C9");

// Notificador falso: registra lo pedido. `denegado` simula permiso denegado
// (la entrega no ocurre), sin ningun canal de vuelta hacia el servicio.
class NotificadorFalso final : public Notificador {
public:
    void notificar(const Notificacion& n) override
    {
        pedidas.append(n);
        if (!denegado) {
            mostradas.append(n);
        }
    }
    bool denegado = false;
    QList<Notificacion> pedidas;
    QList<Notificacion> mostradas;
};

TransicionNotificable transicion(TipoTransicionNotificable tipo, const SolicitudId& id, int paquetes = 0,
                                 int descargados = 0)
{
    TransicionNotificable t;
    t.solicitudId = id;
    t.tipo = tipo;
    t.tipoDescarga = TipoDescarga::Recibidos;
    t.rfcSolicitante = kRfc;
    t.fechaInicialSat = QStringLiteral("2026-09-01T00:00:00");
    t.fechaFinalSat = QStringLiteral("2026-09-30T23:59:59");
    t.paquetes = paquetes;
    t.descargados = descargados;
    return t;
}

} // namespace

// T024.1 (D4): una fila por renglon de la matriz (direccion recibidas y
// emitidas). Titulo, direccion en el cuerpo, destino sin cambio e id/tipo
// iguales a los anteriores (dedupe). La ausencia de "Mostrar en Finder" se
// prueba en la adaptacion (TestNotificacionesAccionables).
void TestNotificaciones::textosPorTransicion_data()
{
    QTest::addColumn<TipoTransicionNotificable>("tipo");
    QTest::addColumn<int>("paquetes");
    QTest::addColumn<TipoDescarga>("direccion");
    QTest::addColumn<QString>("clave");
    QTest::addColumn<QString>("titulo");
    QTest::addColumn<QString>("resultado");
    using T = TipoTransicionNotificable;
    struct Fila {
        const char* nombre;
        T tipo;
        int paquetes;
        QString clave;
        QString titulo;
        QString resultado; // %1 = direccion
    };
    const QList<Fila> filas = {
        {"descarga completa", T::DescargaCompleta, 3, u"descarga_completa"_s, u"Facturas descargadas"_s,
         u"Las facturas %1 ya están en tu equipo."_s},
        {"terminada sin archivos", T::Terminada, 0, u"terminada"_s, u"Sin facturas en el periodo"_s,
         u"El SAT no tiene facturas %1 en este periodo."_s},
        {"sin facturas 5004", T::SinFacturas, 0, u"sin_facturas"_s, u"Sin facturas en el periodo"_s,
         u"El SAT no tiene facturas %1 en este periodo."_s},
        {"error sat", T::ErrorSat, 0, u"error_sat"_s, u"No se pudieron descargar las facturas"_s,
         u"El SAT reportó un error al preparar las facturas %1."_s},
        {"rechazada", T::Rechazada, 0, u"rechazada"_s, u"El SAT no aceptó preparar las facturas"_s,
         u"No se descargaron las facturas %1."_s},
        {"vencida", T::Vencida, 0, u"vencida"_s, u"Facturas no descargadas"_s,
         u"Venció el plazo del SAT para descargar las facturas %1."_s},
    };
    for (const Fila& f : filas) {
        for (TipoDescarga d : {TipoDescarga::Recibidos, TipoDescarga::Emitidos}) {
            const QString dir = d == TipoDescarga::Emitidos ? u"emitidas"_s : u"recibidas"_s;
            QTest::addRow("%s %s", f.nombre, qPrintable(dir))
                << f.tipo << f.paquetes << d << f.clave << f.titulo << f.resultado.arg(dir);
        }
    }
}

void TestNotificaciones::textosPorTransicion()
{
    QFETCH(TipoTransicionNotificable, tipo);
    QFETCH(int, paquetes);
    QFETCH(TipoDescarga, direccion);
    QFETCH(QString, clave);
    QFETCH(QString, titulo);
    QFETCH(QString, resultado);
    const SolicitudId id = SolicitudId::generar();
    TransicionNotificable t = transicion(tipo, id, paquetes, paquetes);
    t.tipoDescarga = direccion;
    const std::optional<Notificacion> n = ServicioNotificaciones::componer(t);
    QVERIFY(n);
    // Claves de dedupe y destino sin cambio.
    QCOMPARE(n->tipo, clave);
    QCOMPARE(n->id, id.texto() + QLatin1Char(':') + clave);
    QCOMPARE(n->destino, (DestinoNotificacion{DestinoNotificacion::Tipo::Solicitud, id.texto()}));
    QCOMPARE(n->titulo, titulo);
    // Cuerpo: resultado con la direccion; periodo (rango D5) y perfil (RFC enmascarado).
    QCOMPARE(n->cuerpo, resultado + u"\n1–30 sep 2026 · RFC ***3C9"_s);
    const QString otra = direccion == TipoDescarga::Emitidos ? u"recibidas"_s : u"emitidas"_s;
    QVERIFY(!n->cuerpo.contains(otra));
    // Sin RFC completo, Id local ni vocabulario del servicio web (D3).
    for (const QString& texto : {n->titulo, n->cuerpo}) {
        QVERIFY(!texto.contains(kRfc));
        QVERIFY(!texto.contains(id.texto()));
        for (const QString& prohibida : {u"solicitud"_s, u"paquete"_s, u"verificaci"_s, u"Emitidos"_s, u"Recibidos"_s}) {
            QVERIFY2(!texto.contains(prohibida, Qt::CaseInsensitive), qPrintable(texto));
        }
    }
    if (clave == u"sin_facturas"_s || clave == u"terminada"_s) {
        // T023.6 (D22): informativo, sin "rechazó" ni "error".
        for (const QString& texto : {n->titulo, n->cuerpo}) {
            QVERIFY2(!texto.contains(u"rechaz"_s, Qt::CaseInsensitive) && !texto.contains(u"error"_s, Qt::CaseInsensitive),
                     qPrintable(texto));
        }
    }
}

void TestNotificaciones::terminadaConArchivosNoNotifica()
{
    const SolicitudId id = SolicitudId::generar();
    for (int paquetes : {1, 3}) {
        QVERIFY(!ServicioNotificaciones::componer(transicion(TipoTransicionNotificable::Terminada, id, paquetes)));
    }
    NotificadorFalso notificador;
    ServicioNotificaciones servicio(notificador);
    servicio.alConfirmarTransicion(transicion(TipoTransicionNotificable::Terminada, id, 2));
    QCOMPARE(notificador.pedidas.size(), 0);

    // Texto de referencia (un solo dia, RFC generico).
    TransicionNotificable t = transicion(TipoTransicionNotificable::DescargaCompleta, id, 1, 1);
    t.rfcSolicitante = u"XAXX010101000"_s;
    t.fechaInicialSat = u"2026-09-03T00:00:00"_s;
    t.fechaFinalSat = u"2026-09-03T23:59:59"_s;
    QCOMPARE(ServicioNotificaciones::componer(t)->cuerpo,
             u"Las facturas recibidas ya están en tu equipo.\n3 sep 2026 · RFC ***000"_s);
    QCOMPARE(ServicioNotificaciones::rfcEnmascarado(QString()), QString());

    // Formateador de fechas de aplicacion (equivalente a FormatoFechas).
    using namespace satcfdi::fechaslegibles;
    QCOMPARE(fecha(QDate(2026, 9, 3)), u"3 sep 2026"_s);
    QCOMPARE(rango(QDate(2026, 9, 28), QDate(2026, 10, 2)), u"28 sep – 2 oct 2026"_s);
    QCOMPARE(rango(QDate(2026, 12, 28), QDate(2027, 1, 2)), u"28 dic 2026 – 2 ene 2027"_s);
    QCOMPARE(fecha(QDate()), QString());
}

// T024.1 (D4): un mes guiado con archivos en ambas direcciones (Terminada y
// Descarga completa por cada una) produce exactamente 2 notificaciones; las
// transiciones repetidas (reintento, verificar de nuevo) no duplican y un
// servicio nuevo (reinicio) compone las mismas claves de dedupe.
void TestNotificaciones::mesGuiadoProduceDosNotificaciones()
{
    NotificadorFalso notificador;
    ServicioNotificaciones servicio(notificador);
    const SolicitudId emitidas = SolicitudId::generar();
    const SolicitudId recibidas = SolicitudId::generar();
    const auto de = [](TipoTransicionNotificable tipo, const SolicitudId& id, TipoDescarga d, int paq, int desc) {
        TransicionNotificable t = transicion(tipo, id, paq, desc);
        t.tipoDescarga = d;
        return t;
    };
    const QList<TransicionNotificable> mes = {
        de(TipoTransicionNotificable::Terminada, emitidas, TipoDescarga::Emitidos, 2, 0),
        de(TipoTransicionNotificable::Terminada, recibidas, TipoDescarga::Recibidos, 1, 0),
        de(TipoTransicionNotificable::DescargaCompleta, recibidas, TipoDescarga::Recibidos, 1, 1),
        de(TipoTransicionNotificable::DescargaCompleta, emitidas, TipoDescarga::Emitidos, 2, 2),
    };
    for (const auto& t : mes) {
        servicio.alConfirmarTransicion(t);
    }
    QCOMPARE(notificador.pedidas.size(), 2);
    QCOMPARE(notificador.pedidas.at(0).id, recibidas.texto() + u":descarga_completa"_s);
    QCOMPARE(notificador.pedidas.at(1).id, emitidas.texto() + u":descarga_completa"_s);
    QVERIFY(notificador.pedidas.at(0).cuerpo.contains(u"recibidas"_s));
    QVERIFY(notificador.pedidas.at(1).cuerpo.contains(u"emitidas"_s));
    for (const auto& n : notificador.pedidas) {
        QCOMPARE(n.titulo, u"Facturas descargadas"_s);
    }

    // Reintento: las mismas transiciones otra vez no duplican.
    for (const auto& t : mes) {
        servicio.alConfirmarTransicion(t);
    }
    QCOMPARE(notificador.pedidas.size(), 2);

    // Reinicio: otro servicio compone exactamente las mismas claves (el SO
    // reemplaza una notificacion con el mismo id; el ejecutor solo publica
    // transiciones persistidas, asi que tras reiniciar no se repiten).
    NotificadorFalso otro;
    ServicioNotificaciones reiniciado(otro);
    for (const auto& t : mes) {
        reiniciado.alConfirmarTransicion(t);
    }
    QCOMPARE(otro.pedidas.size(), 2);
    QCOMPARE(otro.pedidas.at(0).id, notificador.pedidas.at(0).id);
    QCOMPARE(otro.pedidas.at(1).id, notificador.pedidas.at(1).id);
}

void TestNotificaciones::dedupePorSolicitudYTransicion()
{
    NotificadorFalso notificador;
    ServicioNotificaciones servicio(notificador);
    const SolicitudId a = SolicitudId::generar();
    const SolicitudId b = SolicitudId::generar();
    servicio.alConfirmarTransicion(transicion(TipoTransicionNotificable::Terminada, a, 0));
    servicio.alConfirmarTransicion(transicion(TipoTransicionNotificable::Terminada, a, 0)); // repetida
    servicio.alConfirmarTransicion(transicion(TipoTransicionNotificable::DescargaCompleta, a, 2, 2));
    servicio.alConfirmarTransicion(transicion(TipoTransicionNotificable::DescargaCompleta, a, 2, 2));
    servicio.alConfirmarTransicion(transicion(TipoTransicionNotificable::Terminada, b, 0));
    QCOMPARE(notificador.pedidas.size(), 3);
    QCOMPARE(notificador.pedidas.at(0).tipo, QStringLiteral("terminada"));
    QCOMPARE(notificador.pedidas.at(1).tipo, QStringLiteral("descarga_completa"));
    QCOMPARE(notificador.pedidas.at(2).id, b.texto() + QStringLiteral(":terminada"));
}

void TestNotificaciones::credencialPorPerfil()
{
    NotificadorFalso notificador;
    ServicioNotificaciones servicio(notificador);
    const PerfilId p1 = PerfilId::generar();
    const PerfilId p2 = PerfilId::generar();
    servicio.alCambiarEstadoCredencial(p1, EstadoCredencial::Lista); // primera observacion: nada
    QCOMPARE(notificador.pedidas.size(), 0);
    servicio.alCambiarEstadoCredencial(p1, EstadoCredencial::Vencida);
    servicio.alCambiarEstadoCredencial(p1, EstadoCredencial::Vencida); // sin cambio
    QCOMPARE(notificador.pedidas.size(), 1);
    const Notificacion n = notificador.pedidas.constFirst();
    QCOMPARE(n.tipo, QStringLiteral("credencial"));
    QCOMPARE(n.id, QStringLiteral("credencial:") + p1.texto() + QStringLiteral(":Vencida"));
    QCOMPARE(n.titulo, u"e.firma no disponible"_s);
    QCOMPARE(n.cuerpo, u"La e.firma está vencida. El monitoreo de ese perfil está en pausa.\nReemplázala en Perfiles SAT."_s);
    QVERIFY(!n.cuerpo.contains(p1.texto()));

    servicio.alCambiarEstadoCredencial(p1, EstadoCredencial::Lista); // no notifica, rearma
    servicio.alCambiarEstadoCredencial(p1, EstadoCredencial::Vencida);
    servicio.alCambiarEstadoCredencial(p2, EstadoCredencial::MaterialDanado);
    QCOMPARE(notificador.pedidas.size(), 3);
    QVERIFY(notificador.pedidas.at(2).cuerpo.startsWith(u"No se pudo leer la e.firma guardada. "_s));
    QVERIFY(!ServicioNotificaciones::componerCredencial(p1, EstadoCredencial::Validando));
}

void TestNotificaciones::permisoDenegadoNoReintenta()
{
    NotificadorFalso notificador;
    notificador.denegado = true;
    ServicioNotificaciones servicio(notificador);
    const SolicitudId id = SolicitudId::generar();
    servicio.alConfirmarTransicion(transicion(TipoTransicionNotificable::Rechazada, id));
    servicio.alConfirmarTransicion(transicion(TipoTransicionNotificable::Rechazada, id));
    // Se pidio una sola vez, no se mostro y no se reintenta.
    QCOMPARE(notificador.pedidas.size(), 1);
    QCOMPARE(notificador.mostradas.size(), 0);
    // Con el permiso concedido despues, la misma transicion no se repite.
    notificador.denegado = false;
    servicio.alConfirmarTransicion(transicion(TipoTransicionNotificable::Rechazada, id));
    QCOMPARE(notificador.pedidas.size(), 1);
}
