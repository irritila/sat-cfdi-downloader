#pragma once

// Fixture de Qt Quick Test para FranjaAvisoRaizPaquetes (T019 D8): crea
// AvisoRaizPaquetesViewModel (no creable desde QML) sin aviso o con un
// motivo. Context property "fixtureAvisoRaiz". Rutas ~/... como las entrega
// el composition root.

#include "AvisoRaizPaquetesViewModel.h"

#include <QObject>

class FixtureAvisoRaiz : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // motivo: AvisoRaizPaquetesViewModel.Motivo (Ninguno = sin aviso).
    // usaNueva: con Error, la sesion usa la raiz nueva (anterior no es carpeta).
    Q_INVOKABLE satcfdi::AvisoRaizPaquetesViewModel* crear(int motivo, bool usaNueva = false)
    {
        using Motivo = satcfdi::AvisoRaizPaquetesViewModel::Motivo;
        auto* vm = new satcfdi::AvisoRaizPaquetesViewModel(this);
        const auto m = static_cast<Motivo>(motivo);
        const QString legada = QStringLiteral("~/SAT-CFDI-Downloader/paquetes");
        const QString nueva = QStringLiteral("~/Archivo-Fiscal/paquetes");
        vm->fijar({m, legada, nueva, (m == Motivo::RaizNoDisponible || usaNueva) ? nueva : legada});
        return vm;
    }
};
