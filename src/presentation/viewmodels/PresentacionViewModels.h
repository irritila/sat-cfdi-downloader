#pragma once

#include "application/persistence/PuertosPersistencia.h"
#include "AvisoRaizPaquetesViewModel.h"

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
class FacturasViewModel;
class FuenteFacturasMes;
class PreferenciasFacturas;
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
    // T023.4: pantalla Facturas (siempre existe; sin fuente queda vacia).
    FacturasViewModel* facturas() const { return m_facturas; }
    // T019 D8: aviso de la carpeta de paquetes; lo fija el composition root
    // antes de cargar Main.qml (sin aviso queda inactivo).
    AvisoRaizPaquetesViewModel* avisoRaizPaquetes() const { return m_avisoRaizPaquetes; }

    // {"appViewModel", "solicitudesModel", "nuevaSolicitudViewModel",
    //  "detalleViewModel", "perfilesViewModel", "eFirmaViewModel",
    //  "exploradorViewModel", "avisoRaizPaquetesViewModel",
    //  "facturasViewModel"} para
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
    // T023.4: datos de Facturas y preferencia del ultimo perfil. No
    // propietarios. Con fuente, Facturas es la pagina inicial y la que abre
    // AppViewModel::mostrarFacturas(); nullptr = sin pantalla Facturas.
    void setFuenteFacturas(FuenteFacturasMes* fuente, PreferenciasFacturas* preferencias);

private:
    void conectarPreparacionDetalle(PerfilesSatService* perfiles, CredencialesSatService* credenciales,
                                    RelojUtc reloj);

    SolicitudesListModel* m_solicitudes;
    NuevaSolicitudViewModel* m_nuevaSolicitud;
    SolicitudDetailViewModel* m_detalle;
    PerfilesSatViewModel* m_perfiles;
    EFirmaFormViewModel* m_eFirma;
    ExploradorCfdiViewModel* m_explorador;
    FacturasViewModel* m_facturas;
    AppViewModel* m_app;
    AvisoRaizPaquetesViewModel* m_avisoRaizPaquetes;
};

} // namespace satcfdi
