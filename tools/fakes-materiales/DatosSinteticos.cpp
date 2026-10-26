#include "DatosSinteticos.h"

#include <QTimeZone>

#include <algorithm>
#include <array>

namespace materiales {

namespace {

// Contrapartes inventadas (sin relacion con personas o empresas reales).
constexpr std::array<const char*, 10> kContrapartes = {
    "Papelería Ejemplo",          "Servicios Demo",          "Transportes Ficticios",
    "Comercializadora Imaginaria", "Arrendamientos Demo",     "Distribuidora Imaginaria",
    "Refacciones Ficticias",      "Telecomunicaciones Ejemplo", "Consultoría Muestra",
    "Alimentos de Ejemplo",
};

} // namespace

PerfilId perfilIdFijo(char c)
{
    const QString t = QString(8, QLatin1Char(c)) + QStringLiteral("-") + QString(4, QLatin1Char(c))
                      + QStringLiteral("-4") + QString(3, QLatin1Char(c)) + QStringLiteral("-8")
                      + QString(3, QLatin1Char(c)) + QStringLiteral("-") + QString(12, QLatin1Char(c));
    return PerfilId::desdeTexto(t).value();
}

SolicitudId solicitudIdFija(int n)
{
    return SolicitudId::desdeTexto(QStringLiteral("00000000-0000-4000-8000-%1").arg(n, 12, 10, QLatin1Char('0')))
        .value();
}

DatosFacturas datosFacturasDeEjemplo()
{
    DatosFacturas d;
    // 12:00 en Centro (UTC-6): mes en curso octubre 2026.
    d.ahoraUtc = QDateTime(QDate(2026, 10, 15), QTime(18, 0), QTimeZone::UTC);

    PerfilDeclarado principal;
    principal.id = perfilIdFijo('1');
    principal.rfc = kRfcPerfil;
    principal.nombre = kNombrePerfil;
    principal.credencial = EstadoCredencial::Lista;
    for (int m = 1; m <= 9; ++m) {
        MesDeclarado mes;
        mes.estado = EstadoMesSintetico::Disponibles;
        mes.emitidas = 14 + (m * 5) % 9;
        mes.recibidas = 22 + (m * 7) % 13;
        principal.meses.insert(claveMes(2026, m), mes);
    }
    // Diciembre del anio anterior, para el combo del anio.
    MesDeclarado diciembre;
    diciembre.estado = EstadoMesSintetico::Disponibles;
    diciembre.emitidas = 19;
    diciembre.recibidas = 31;
    principal.meses.insert(claveMes(2025, 12), diciembre);

    PerfilDeclarado otro;
    otro.id = perfilIdFijo('2');
    otro.rfc = kRfcOtro;
    otro.nombre = kNombreOtro;
    otro.credencial = EstadoCredencial::SinCredencial;

    d.perfiles = {principal, otro};
    return d;
}

QList<CfdiMensual> cfdisDelMes(const PerfilDeclarado& perfil, int anio, int mes, TipoDescarga direccion,
                               int cuantas)
{
    QList<CfdiMensual> r;
    const QDate inicio(anio, mes, 1);
    const int dias = inicio.daysInMonth();
    const bool emitida = direccion == TipoDescarga::Emitidos;
    const QString contraparteRfc = perfil.rfc == kRfcPerfil ? kRfcOtro : kRfcPerfil;
    // Paquete por mes y direccion; la solicitud fuente es fija por mes.
    const QString paquete = QStringLiteral("PAQ-%1-%2-%3")
                                .arg(anio)
                                .arg(mes, 2, 10, QLatin1Char('0'))
                                .arg(emitida ? QLatin1Char('E') : QLatin1Char('R'));
    const SolicitudId solicitud = solicitudIdFija(anio * 1000 + mes * 10 + (emitida ? 1 : 2));
    for (int i = 0; i < cuantas; ++i) {
        CfdiListado c;
        c.paqueteId = paquete;
        c.posicion = i;
        ResumenCfdi& x = c.resumen;
        x.estado = EstadoParseo::Completo;
        x.version = QStringLiteral("4.0");
        x.tipo = i % 9 == 8 ? TipoComprobante::Egreso : i % 11 == 10 ? TipoComprobante::Pago : TipoComprobante::Ingreso;
        const int dia = 1 + (i * 7 + mes) % dias;
        const QTime hora(8 + (i * 5) % 11, (i * 17) % 60, (i * 31) % 60);
        x.fecha = QDate(anio, mes, dia).toString(Qt::ISODate) + QLatin1Char('T') + hora.toString(Qt::ISODate);
        x.serie = emitida ? QStringLiteral("A") : (i % 3 == 0 ? QString() : QStringLiteral("F"));
        x.folio = QString::number(1000 + 37 * i + mes);
        const QString contraparte = QString::fromUtf8(kContrapartes[std::size_t(i + mes) % kContrapartes.size()]);
        x.emisor.rfc = emitida ? perfil.rfc : contraparteRfc;
        x.emisor.nombre = emitida ? perfil.nombre : contraparte;
        x.receptor.rfc = emitida ? contraparteRfc : perfil.rfc;
        x.receptor.nombre = emitida ? contraparte : perfil.nombre;
        if (x.tipo == TipoComprobante::Pago) {
            x.total = ImporteCfdi::desdeTexto(QStringLiteral("0"));
            x.moneda = QStringLiteral("XXX");
        } else {
            x.total = ImporteCfdi::desdeTexto(
                QStringLiteral("%1.%2").arg(350 + (731 * i + 97 * mes) % 18000).arg((13 * i) % 100, 2, 10, QLatin1Char('0')));
            x.moneda = QStringLiteral("MXN");
            x.metodoPago = i % 4 == 3 ? QStringLiteral("PPD") : QStringLiteral("PUE");
        }
        // 8-4-4-4-12 hexadecimales sinteticos, distintos por perfil, mes y direccion.
        x.uuid = QStringLiteral("%1%2%3-%4-4000-8000-%5")
                     .arg(emitida ? QLatin1Char('E') : QLatin1Char('F'))
                     .arg(mes, 2, 16, QLatin1Char('0'))
                     .arg(0x3A5C7 + 7919 * i, 5, 16, QLatin1Char('0'))
                     .arg(anio, 4, 10, QLatin1Char('0'))
                     .arg(i + (perfil.rfc == kRfcPerfil ? 0 : 500000), 12, 10, QLatin1Char('0'))
                     .toUpper();
        r.append(CfdiMensual{c, direccion, solicitud, true});
    }
    std::stable_sort(r.begin(), r.end(), [](const CfdiMensual& a, const CfdiMensual& b) {
        return a.cfdi.resumen.fecha > b.cfdi.resumen.fecha;
    });
    return r;
}

QList<EntradaHistorial> historialDeEjemplo(const PerfilId& perfil)
{
    QList<EntradaHistorial> r;
    int siguiente = 900;
    auto entrada = [&](int mes, int dia, ResultadoHistorial resultado, int emitidas, int recibidas) {
        EntradaHistorial e;
        e.loteId = QStringLiteral("lote-2026-%1").arg(mes, 2, 10, QLatin1Char('0'));
        e.perfil = perfil;
        e.mes = MesCalendario::crear(2026, mes);
        e.desde = QDate(2026, mes, 1);
        e.hasta = e.desde.addMonths(1).addDays(-1);
        e.creadoEn = QDateTime(QDate(2026, mes == 12 ? 12 : mes + 1, dia), QTime(17, 12), QTimeZone::UTC);
        e.resultado = resultado;
        e.emitidas = emitidas;
        e.recibidas = recibidas;
        e.tieneEmitidas = true;
        e.tieneRecibidas = true;
        e.diagnosticoEmitidas = solicitudIdFija(++siguiente);
        e.diagnosticoRecibidas = solicitudIdFija(++siguiente);
        return e;
    };
    r.append(entrada(9, 3, ResultadoHistorial::Completa, 18, 28));
    r.append(entrada(8, 4, ResultadoHistorial::Completa, 17, 22));
    EntradaHistorial parcial = entrada(7, 5, ResultadoHistorial::Completa, 21, 34);
    parcial.eliminacionParcial = true;
    r.append(parcial);
    EntradaHistorial atencion = entrada(6, 2, ResultadoHistorial::RequiereAtencion, 15, 0);
    atencion.puedeReintentar = true;
    r.append(atencion);
    EntradaHistorial preparacion = entrada(5, 10, ResultadoHistorial::EnPreparacion, 0, 0);
    r.append(preparacion);
    r.append(entrada(2, 4, ResultadoHistorial::SinFacturas, 0, 0));
    EntradaHistorial anterior;
    anterior.tipo = EntradaHistorial::Tipo::Anterior;
    anterior.perfil = perfil;
    anterior.desde = QDate(2026, 1, 1);
    anterior.hasta = QDate(2026, 1, 31);
    anterior.mes = MesCalendario::crear(2026, 1);
    anterior.creadoEn = QDateTime(QDate(2026, 2, 3), QTime(16, 0), QTimeZone::UTC);
    anterior.resultado = ResultadoHistorial::Anterior;
    anterior.emitidas = 12;
    anterior.tieneEmitidas = true;
    anterior.diagnosticoEmitidas = solicitudIdFija(++siguiente);
    r.append(anterior);
    return r;
}

} // namespace materiales
