#pragma once

// Fakes de la previsualizacion de CFDI (T015.4, solo tests/presentation):
// cada generar/guardarComo crea una promesa pendiente que la prueba resuelve,
// falla o cancela en el orden que quiera; registra lo pedido. El Finder de
// Vista Previa tambien responde con promesas manuales. Rutas ficticias.

#include "fakes/PromesasPendientes.h"

#include "AccionesFinder.h"
#include "PrevisualizacionCfdi.h"

#include <QFuture>
#include <QList>
#include <QStringList>

namespace fakes {

class PrevisualizacionFake final : public satcfdi::PrevisualizacionCfdi {
    Q_OBJECT

public:
    using PrevisualizacionCfdi::PrevisualizacionCfdi;

    QFuture<ResultadoGenerar> generar(const satcfdi::SolicitudId& solicitud, const QString& paqueteId,
                                      int posicion) override
    {
        solicitudes.append(solicitud);
        pedidos.append(QStringLiteral("%1:%2").arg(paqueteId).arg(posicion));
        return generaciones.nueva();
    }
    QFuture<ResultadoGuardar> guardarComo(const QString& rutaPdf, const QString& destinoAbsoluto) override
    {
        guardados.append(rutaPdf + QStringLiteral(" -> ") + destinoAbsoluto);
        return copias.nueva();
    }

    void resolverPdf(int indice, const QString& ruta = QStringLiteral("/tmp/cache/cfdi-pdf/a.pdf"),
                     const QString& nombre = QStringLiteral("CFDI-A12-0A1B2C3D.pdf"))
    {
        generaciones.resolver(indice, ResultadoGenerar::exito(satcfdi::PdfPrevisualizado{ruta, nombre}));
    }
    void fallarPdf(int indice, satcfdi::MotivoPrevisualizacion motivo)
    {
        generaciones.resolver(indice, ResultadoGenerar::fallo(motivo));
    }
    void resolverGuardado(int indice) { copias.resolver(indice, ResultadoGuardar::exito(true)); }
    void fallarGuardado(int indice, satcfdi::MotivoPrevisualizacion motivo = satcfdi::MotivoPrevisualizacion::Fallo)
    {
        copias.resolver(indice, ResultadoGuardar::fallo(motivo));
    }

    PromesasPendientes<ResultadoGenerar> generaciones;
    PromesasPendientes<ResultadoGuardar> copias;
    QList<satcfdi::SolicitudId> solicitudes;
    QStringList pedidos;
    QStringList guardados;
};

// Finder con Vista Previa por promesas; el resto responde Mostrado.
class FinderVistaPreviaFake final : public satcfdi::AccionesFinder {
public:
    using Resultado = satcfdi::ResultadoAccionFinder;

    QFuture<Resultado> mostrarPaquete(const satcfdi::SolicitudId&, const QString&) override { return mostrado(); }
    QFuture<Resultado> abrirCarpetaSolicitud(const satcfdi::SolicitudId&) override { return mostrado(); }
    QFuture<Resultado> abrirCarpetaPaquetes() override { return mostrado(); }
    QFuture<Resultado> abrirEnVistaPrevia(const QString& rutaAbsoluta) override
    {
        rutas.append(rutaAbsoluta);
        return vistasPrevias.nueva();
    }

    void resolver(int indice, Resultado::Estado estado, const QString& mensaje = {})
    {
        vistasPrevias.resolver(indice, Resultado{estado, mensaje});
    }

    PromesasPendientes<Resultado> vistasPrevias;
    QStringList rutas;

private:
    static QFuture<Resultado> mostrado()
    {
        return QtFuture::makeReadyValueFuture(Resultado{Resultado::Estado::Mostrado, {}});
    }
};

} // namespace fakes
