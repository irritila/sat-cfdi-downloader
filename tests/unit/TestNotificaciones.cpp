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

void TestNotificaciones::textosPorTransicion()
{
    const SolicitudId id = SolicitudId::generar();
    struct Caso {
        TipoTransicionNotificable tipo;
        QString clave;
        QString titulo;
        QString resultado;
    };
    const QList<Caso> casos = {
        {TipoTransicionNotificable::Terminada, u"terminada"_s, u"Solicitud terminada"_s,
         u"El SAT terminó la solicitud con 3 paquetes."_s},
        {TipoTransicionNotificable::DescargaCompleta, u"descarga_completa"_s, u"Descarga completa"_s,
         u"3 de 3 paquetes descargados."_s},
        {TipoTransicionNotificable::ErrorSat, u"error_sat"_s, u"Error en el SAT"_s,
         u"El SAT reportó un error en la solicitud."_s},
        {TipoTransicionNotificable::Rechazada, u"rechazada"_s, u"Solicitud rechazada"_s,
         u"El SAT rechazó la solicitud."_s},
        {TipoTransicionNotificable::Vencida, u"vencida"_s, u"Solicitud vencida"_s,
         u"La solicitud venció en el SAT; los paquetes pueden ya no estar disponibles."_s},
    };
    // Periodo de un mes (rango D5 con guion largo).
    const QString contexto = u"Recibidos · 1–30 sep 2026 · RFC ***3C9"_s;
    for (const Caso& c : casos) {
        const Notificacion n = ServicioNotificaciones::componer(transicion(c.tipo, id, 3, 3));
        QCOMPARE(n.tipo, c.clave);
        QCOMPARE(n.id, id.texto() + QLatin1Char(':') + c.clave);
        QCOMPARE(n.titulo, c.titulo);
        QCOMPARE(n.cuerpo, c.resultado + QLatin1Char('\n') + contexto);
        // Sin RFC completo ni Id local en lo visible.
        QVERIFY(!n.titulo.contains(kRfc) && !n.cuerpo.contains(kRfc));
        QVERIFY(!n.cuerpo.contains(id.texto()));
    }
    QCOMPARE(ServicioNotificaciones::componer(transicion(TipoTransicionNotificable::Terminada, id, 1)).cuerpo.section(
                 QLatin1Char('\n'), 0, 0),
             u"El SAT terminó la solicitud con 1 paquete."_s);

    // Texto de referencia de UX-39 (un solo dia).
    TransicionNotificable t = transicion(TipoTransicionNotificable::DescargaCompleta, id, 1, 1);
    t.rfcSolicitante = u"XAXX010101000"_s;
    t.fechaInicialSat = u"2026-09-03T00:00:00"_s;
    t.fechaFinalSat = u"2026-09-03T23:59:59"_s;
    QCOMPARE(ServicioNotificaciones::componer(t).cuerpo,
             u"1 de 1 paquetes descargados.\nRecibidos · 3 sep 2026 · RFC ***000"_s);
    QCOMPARE(ServicioNotificaciones::rfcEnmascarado(QString()), QString());

    // Formateador de fechas de aplicacion (equivalente a FormatoFechas).
    using namespace satcfdi::fechaslegibles;
    QCOMPARE(fecha(QDate(2026, 9, 3)), u"3 sep 2026"_s);
    QCOMPARE(rango(QDate(2026, 9, 28), QDate(2026, 10, 2)), u"28 sep – 2 oct 2026"_s);
    QCOMPARE(rango(QDate(2026, 12, 28), QDate(2027, 1, 2)), u"28 dic 2026 – 2 ene 2027"_s);
    QCOMPARE(fecha(QDate()), QString());
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
