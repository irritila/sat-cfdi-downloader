#pragma once

#include "application/profiles/DemoPerfilesSatService.h"
#include "application/requests/DemoSolicitudesService.h"
#include "presentation/viewmodels/PresentacionViewModels.h"

#include <QList>
#include <QQmlApplicationEngine>

namespace satcfdi {

// Composition root de satcfdi_app: unico lugar que elige implementaciones
// concretas y arma el grafo servicios -> view models -> QML.
//
// DEMO (T002): los servicios son DemoPerfilesSatService y
// DemoSolicitudesService, en memoria. T003 los reemplaza aqui por servicios
// persistidos (y mueve este armado a satcfdi_app_core); vistas y view models
// no cambian.
//
// Orden de vida: los miembros se destruyen en orden inverso a su declaracion,
// asi que el engine (y con el los objetos QML que referencian view models) se
// destruye primero, despues los view models y al final los servicios.
class AppCompositionRoot {
public:
    AppCompositionRoot();

    AppCompositionRoot(const AppCompositionRoot&) = delete;
    AppCompositionRoot& operator=(const AppCompositionRoot&) = delete;

    // Inyecta las propiedades iniciales y carga SatCfdiDownloader/Main.
    // Devuelve false si el engine no produjo ningun objeto raiz.
    bool cargar();

private:
    // Catalogo demo compartido por ambos servicios (perfilId y perfilRfc).
    const QList<PerfilResumen> m_perfiles;
    DemoPerfilesSatService m_perfilesService;
    DemoSolicitudesService m_solicitudesService;
    PresentacionViewModels m_viewModels;
    QQmlApplicationEngine m_engine;
};

} // namespace satcfdi
