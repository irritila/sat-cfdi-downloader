#pragma once

#include <QObject>
#include <QVariantMap>

namespace satcfdi {

class AccionesSolicitud;
class ConsultaExistenciaPaquetes;
class AppViewModel;
class CredencialesSatService;
class EFirmaFormViewModel;
class NuevaSolicitudViewModel;
class PerfilesSatService;
class PerfilesSatViewModel;
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
    // Los tres servicios son obligatorios (T005.1 DA6: el root productivo
    // pasa los persistidos; las pruebas, fakes). Construye las consultas de
    // preparacion que necesitan NuevaSolicitudViewModel y PerfilesSatViewModel.
    PresentacionViewModels(SolicitudesService* solicitudes,
                           PerfilesSatService* perfiles,
                           CredencialesSatService* credenciales,
                           QObject* parent = nullptr);

    AppViewModel* app() const { return m_app; }
    SolicitudesListModel* solicitudes() const { return m_solicitudes; }
    NuevaSolicitudViewModel* nuevaSolicitud() const { return m_nuevaSolicitud; }
    SolicitudDetailViewModel* detalle() const { return m_detalle; }
    PerfilesSatViewModel* perfiles() const { return m_perfiles; }
    EFirmaFormViewModel* eFirma() const { return m_eFirma; }

    // {"appViewModel", "solicitudesModel", "nuevaSolicitudViewModel",
    //  "detalleViewModel", "perfilesViewModel", "eFirmaViewModel"} para
    //  QQmlApplicationEngine::setInitialProperties.
    QVariantMap initialProperties() const;

    // T007: acciones de operacion (envio tras crear, Verificar ahora,
    // Reintentar descarga). No propietario; debe vivir mas que este objeto.
    // nullptr = sin acciones (las pruebas de presentacion y el shell sin worker).
    void setAccionesSolicitud(AccionesSolicitud* acciones);
    // T008 D11: existencia de ZIP en el detalle. No propietario; nullptr = no consulta.
    void setConsultaExistencia(ConsultaExistenciaPaquetes* consulta);

private:
    void conectarPreparacionDetalle(PerfilesSatService* perfiles, CredencialesSatService* credenciales);

    SolicitudesListModel* m_solicitudes;
    NuevaSolicitudViewModel* m_nuevaSolicitud;
    SolicitudDetailViewModel* m_detalle;
    PerfilesSatViewModel* m_perfiles;
    EFirmaFormViewModel* m_eFirma;
    AppViewModel* m_app;
};

} // namespace satcfdi
