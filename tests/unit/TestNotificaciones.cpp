#include "TestNotificaciones.h"

#include "application/notificaciones/ServicioNotificaciones.h"
#include "application/operaciones/MensajesOperacionSat.h"

#include <QTest>

using namespace satcfdi;

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

void TestNotificaciones::textosPorTransicion()
{
    const SolicitudId id = SolicitudId::generar();
    struct Caso {
        TipoTransicionNotificable tipo;
        QString clave;
        QString inicio;
    };
    const QList<Caso> casos = {
        {TipoTransicionNotificable::Terminada, QStringLiteral("terminada"), QStringLiteral("Terminada (3 paquetes). ")},
        {TipoTransicionNotificable::DescargaCompleta, QStringLiteral("descarga_completa"),
         QStringLiteral("Descarga completa: 3 de 3. ")},
        {TipoTransicionNotificable::ErrorSat, QStringLiteral("error_sat"),
         QStringLiteral("El SAT reporto un error en la solicitud. ")},
        {TipoTransicionNotificable::Rechazada, QStringLiteral("rechazada"),
         QStringLiteral("El SAT rechazo la solicitud. ")},
        {TipoTransicionNotificable::Vencida, QStringLiteral("vencida"),
         QStringLiteral("La solicitud vencio en el SAT; los paquetes pueden ya no estar disponibles. ")},
    };
    for (const Caso& c : casos) {
        const Notificacion n = ServicioNotificaciones::componer(transicion(c.tipo, id, 3, 3));
        QCOMPARE(n.tipo, c.clave);
        QCOMPARE(n.id, id.texto() + QLatin1Char(':') + c.clave);
        QVERIFY2(n.cuerpo.startsWith(c.inicio), qPrintable(n.cuerpo));
        QVERIFY(n.cuerpo.endsWith(QStringLiteral("Recibidos del 2026-09-01 al 2026-09-30, RFC ***3C9.")));
        QVERIFY(!n.titulo.isEmpty());
        // Sin RFC completo ni Id local en lo visible.
        QVERIFY(!n.titulo.contains(kRfc) && !n.cuerpo.contains(kRfc));
        QVERIFY(!n.cuerpo.contains(id.texto()));
    }
    QVERIFY(ServicioNotificaciones::componer(transicion(TipoTransicionNotificable::Terminada, id, 1))
                .cuerpo.startsWith(QStringLiteral("Terminada (1 paquete). ")));
    QCOMPARE(ServicioNotificaciones::rfcEnmascarado(QString()), QString());
}

void TestNotificaciones::dedupePorSolicitudYTransicion()
{
    NotificadorFalso notificador;
    ServicioNotificaciones servicio(notificador);
    const SolicitudId a = SolicitudId::generar();
    const SolicitudId b = SolicitudId::generar();
    servicio.alConfirmarTransicion(transicion(TipoTransicionNotificable::Terminada, a, 2));
    servicio.alConfirmarTransicion(transicion(TipoTransicionNotificable::Terminada, a, 2)); // repetida
    servicio.alConfirmarTransicion(transicion(TipoTransicionNotificable::DescargaCompleta, a, 2, 2));
    servicio.alConfirmarTransicion(transicion(TipoTransicionNotificable::DescargaCompleta, a, 2, 2));
    servicio.alConfirmarTransicion(transicion(TipoTransicionNotificable::Terminada, b, 1));
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
    QVERIFY(n.cuerpo.startsWith(mensajessat::credencialVencida()));
    QVERIFY(!n.cuerpo.contains(p1.texto()));

    servicio.alCambiarEstadoCredencial(p1, EstadoCredencial::Lista); // no notifica, rearma
    servicio.alCambiarEstadoCredencial(p1, EstadoCredencial::Vencida);
    servicio.alCambiarEstadoCredencial(p2, EstadoCredencial::MaterialDanado);
    QCOMPARE(notificador.pedidas.size(), 3);
    QVERIFY(notificador.pedidas.at(2).cuerpo.startsWith(mensajessat::credencialIlegible()));
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
