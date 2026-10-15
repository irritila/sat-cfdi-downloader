#pragma once

#include "application/persistence/PuertosPersistencia.h"

#include <QObject>
#include <QVariantMap>

namespace satcfdi {

class AccionesSolicitud;
class AccionesFinder;
class ConsultaCatalogoCfdi;
class ConsultaExistenciaPaquetes;
class ConsultaPrimerUso;
class PrevisualizacionCfdi;
class AppViewModel;
class CredencialesSatService;
class EFirmaFormViewModel;
class ExploradorCfdiViewModel;
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
    // `reloj` (T014.3): el del aviso de vencimiento, para que diasParaVencer
    // (badge) y la notificacion usen la misma hora.
    PresentacionViewModels(SolicitudesService* solicitudes,
                           PerfilesSatService* perfiles,
                           CredencialesSatService* credenciales,
                           QObject* parent = nullptr,
                           RelojUtc reloj = relojSistema());

    AppViewModel* app() const { return m_app; }
    SolicitudesListModel* solicitudes() const { return m_solicitudes; }
    NuevaSolicitudViewModel* nuevaSolicitud() const { return m_nuevaSolicitud; }
    SolicitudDetailViewModel* detalle() const { return m_detalle; }
    PerfilesSatViewModel* perfiles() const { return m_perfiles; }
    EFirmaFormViewModel* eFirma() const { return m_eFirma; }
    ExploradorCfdiViewModel* explorador() const { return m_explorador; }

    // {"appViewModel", "solicitudesModel", "nuevaSolicitudViewModel",
    //  "detalleViewModel", "perfilesViewModel", "eFirmaViewModel",
    //  "exploradorViewModel"} para
    //  QQmlApplicationEngine::setInitialProperties.
    QVariantMap initialProperties() const;

    // T007: acciones de operacion (envio tras crear, Verificar ahora,
    // Reintentar descarga). No propietario; debe vivir mas que este objeto.
    // nullptr = sin acciones (las pruebas de presentacion y el shell sin worker).
    void setAccionesSolicitud(AccionesSolicitud* acciones);
    // T008 D11: existencia de ZIP en el detalle. No propietario; nullptr = no consulta.
    void setConsultaExistencia(ConsultaExistenciaPaquetes* consulta);
    // T009.1: Finder (detalle y lista). No propietario; nullptr = sin acciones.
    void setAccionesFinder(AccionesFinder* acciones);
    // T014.3 D1: guia de primer uso en la lista. No propietario; nullptr = sin guia.
    void setConsultaPrimerUso(ConsultaPrimerUso* consulta);
    // T015.3: indice de CFDI ("Ver facturas" y explorador). No propietario;
    // nullptr = sin explorador.
    void setConsultaCatalogo(ConsultaCatalogoCfdi* consulta);
    // T015.4: PDF del CFDI en el explorador. No propietario; nullptr = sin
    // visor (Enter solo emite la intencion).
    void setPrevisualizacionCfdi(PrevisualizacionCfdi* previsualizacion);

private:
    void conectarPreparacionDetalle(PerfilesSatService* perfiles, CredencialesSatService* credenciales,
                                    RelojUtc reloj);

    SolicitudesListModel* m_solicitudes;
    NuevaSolicitudViewModel* m_nuevaSolicitud;
    SolicitudDetailViewModel* m_detalle;
    PerfilesSatViewModel* m_perfiles;
    EFirmaFormViewModel* m_eFirma;
    ExploradorCfdiViewModel* m_explorador;
    AppViewModel* m_app;
};

} // namespace satcfdi
