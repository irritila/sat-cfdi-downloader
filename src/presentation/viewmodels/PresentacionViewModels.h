#pragma once

#include <QObject>
#include <QVariantMap>

namespace satcfdi {

class AppViewModel;
class NuevaSolicitudViewModel;
class PerfilesSatService;
class SolicitudDetailViewModel;
class SolicitudesListModel;
class SolicitudesService;

// Arma el grafo de view models del shell a partir de servicios abstractos y
// produce las propiedades iniciales `required` de Main.qml. No conoce
// implementaciones concretas (Demo* o persistidas): las elige el composition
// root. Los servicios deben vivir mas que este objeto, y este objeto mas que
// el QQmlApplicationEngine que carga Main.qml.
class PresentacionViewModels : public QObject {
    Q_OBJECT

public:
    PresentacionViewModels(SolicitudesService* solicitudes,
                           PerfilesSatService* perfiles,
                           QObject* parent = nullptr);

    AppViewModel* app() const { return m_app; }
    SolicitudesListModel* solicitudes() const { return m_solicitudes; }
    NuevaSolicitudViewModel* nuevaSolicitud() const { return m_nuevaSolicitud; }
    SolicitudDetailViewModel* detalle() const { return m_detalle; }

    // {"appViewModel", "solicitudesModel", "nuevaSolicitudViewModel",
    //  "detalleViewModel"} para QQmlApplicationEngine::setInitialProperties.
    QVariantMap initialProperties() const;

private:
    SolicitudesListModel* m_solicitudes;
    NuevaSolicitudViewModel* m_nuevaSolicitud;
    SolicitudDetailViewModel* m_detalle;
    AppViewModel* m_app;
};

} // namespace satcfdi
