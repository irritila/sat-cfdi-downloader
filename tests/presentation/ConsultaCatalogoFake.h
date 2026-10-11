#pragma once

// Fake de ConsultaCatalogoCfdi (T015.3, solo tests/presentation): cada
// listar/resumen crea una promesa pendiente que la prueba resuelve, falla o
// cancela en el orden que quiera (respuestas tardias y desordenadas). Registra
// los filtros pedidos y las revisiones. Datos fiscales ficticios.

#include "fakes/PromesasPendientes.h"

#include "ConsultaCatalogoCfdi.h"

#include "domain/cfdi/ImporteCfdi.h"

#include <QFuture>
#include <QList>

namespace fakes {

class ConsultaCatalogoFake final : public satcfdi::ConsultaCatalogoCfdi {
    Q_OBJECT

public:
    using ConsultaCatalogoCfdi::ConsultaCatalogoCfdi;

    QFuture<ResultadoLista> listar(const satcfdi::SolicitudId& solicitud,
                                   const satcfdi::FiltroListaCfdi& filtro) override
    {
        solicitudesListadas.append(solicitud);
        filtros.append(filtro);
        return listas.nueva();
    }
    QFuture<ResultadoResumen> resumen(const satcfdi::SolicitudId& solicitud) override
    {
        solicitudesResumidas.append(solicitud);
        return resumenes.nueva();
    }
    QFuture<void> revisarSolicitud(const satcfdi::SolicitudId& solicitud) override
    {
        revisadas.append(solicitud);
        return QtFuture::makeReadyVoidFuture();
    }

    void resolverLista(int indice, const QList<satcfdi::CfdiListado>& filas)
    {
        listas.resolver(indice, ResultadoLista::exito(filas));
    }
    void fallarLista(int indice)
    {
        listas.resolver(indice, ResultadoLista::fallo(satcfdi::ErrorPersistencia{}));
    }
    void resolverResumen(int indice, const satcfdi::ResumenIndiceSolicitud& r)
    {
        resumenes.resolver(indice, ResultadoResumen::exito(r));
    }
    void fallarResumen(int indice)
    {
        resumenes.resolver(indice, ResultadoResumen::fallo(satcfdi::ErrorPersistencia{}));
    }

    PromesasPendientes<ResultadoLista> listas;
    PromesasPendientes<ResultadoResumen> resumenes;
    QList<satcfdi::FiltroListaCfdi> filtros;
    QList<satcfdi::SolicitudId> solicitudesListadas;
    QList<satcfdi::SolicitudId> solicitudesResumidas;
    QList<satcfdi::SolicitudId> revisadas;
};

// CFDI sintetico para las pruebas.
struct DatosCfdi {
    QString paquete = QStringLiteral("PAQ_01");
    int posicion = 0;
    satcfdi::TipoComprobante tipo = satcfdi::TipoComprobante::Ingreso;
    QString fecha = QStringLiteral("2026-09-03T10:22:31");
    QString serie = QStringLiteral("A");
    QString folio = QStringLiteral("12");
    QString emisorRfc = QStringLiteral("XAXX010101000");
    QString emisorNombre = QStringLiteral("Empresa Ejemplo");
    QString receptorRfc = QStringLiteral("XEXX010101000");
    QString receptorNombre = QStringLiteral("Cliente Ejemplo");
    QString total = QStringLiteral("1160.00");
    QString moneda = QStringLiteral("MXN");
    QString metodoPago = QStringLiteral("PUE");
    QString uuid = QStringLiteral("0A1B2C3D-0000-4000-8000-000000000001");
    satcfdi::EstadoParseo estado = satcfdi::EstadoParseo::Completo;
};

inline satcfdi::CfdiListado cfdi(const DatosCfdi& d)
{
    satcfdi::CfdiListado c;
    c.paqueteId = d.paquete;
    c.posicion = d.posicion;
    c.resumen.estado = d.estado;
    c.resumen.version = QStringLiteral("4.0");
    c.resumen.tipo = d.tipo;
    c.resumen.fecha = d.fecha;
    c.resumen.serie = d.serie;
    c.resumen.folio = d.folio;
    c.resumen.emisor.rfc = d.emisorRfc;
    c.resumen.emisor.nombre = d.emisorNombre;
    c.resumen.receptor.rfc = d.receptorRfc;
    c.resumen.receptor.nombre = d.receptorNombre;
    if (!d.total.isEmpty()) {
        c.resumen.total = satcfdi::ImporteCfdi::desdeTexto(d.total);
    }
    c.resumen.moneda = d.moneda;
    c.resumen.metodoPago = d.metodoPago;
    c.resumen.uuid = d.uuid;
    return c;
}

// `n` CFDI distintos (posiciones 0..n-1 del mismo paquete).
inline QList<satcfdi::CfdiListado> cfdis(int n, const QString& paquete = QStringLiteral("PAQ_01"))
{
    QList<satcfdi::CfdiListado> r;
    for (int i = 0; i < n; ++i) {
        DatosCfdi d;
        d.paquete = paquete;
        d.posicion = i;
        d.folio = QString::number(100 + i);
        d.uuid = QStringLiteral("0A1B2C3D-0000-4000-8000-%1").arg(i, 12, 10, QLatin1Char('0'));
        r.append(cfdi(d));
    }
    return r;
}

} // namespace fakes
