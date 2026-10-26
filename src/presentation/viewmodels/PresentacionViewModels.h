#pragma once

#include "application/persistence/PuertosPersistencia.h"
#include "AvisoRaizPaquetesViewModel.h"
#include "PuertosRespaldoPresentacion.h"

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
class ExportacionFacturasService;
class ExportacionFacturasViewModel;
class FacturasViewModel;
class FuenteHistorial;
class HistorialViewModel;
class ResolucionDestinoNotificacion;
class FuenteFacturasMes;
class PreferenciasFacturas;
class NuevaSolicitudViewModel;
class PerfilesSatService;
class PerfilesSatViewModel;
class RespaldoViewModel;
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
    // T023.5: fuera de toda ruta de la app (sin pagina ni propiedad inicial).
    // Se conserva solo para tools/video-tutorial, tools/capturas-manual y
    // FixturePerfiles; T024 la elimina al regrabar manual y video.
    NuevaSolicitudViewModel* nuevaSolicitud() const { return m_nuevaSolicitud; }
    SolicitudDetailViewModel* detalle() const { return m_detalle; }
    PerfilesSatViewModel* perfiles() const { return m_perfiles; }
    EFirmaFormViewModel* eFirma() const { return m_eFirma; }
    ExploradorCfdiViewModel* explorador() const { return m_explorador; }
    // T023.4: pantalla Facturas (siempre existe; sin fuente queda vacia).
    FacturasViewModel* facturas() const { return m_facturas; }
    // T023.5: pantalla Historial (siempre existe; sin fuente queda vacia).
    HistorialViewModel* historial() const { return m_historial; }
    // T019 D8: aviso de la carpeta de paquetes; lo fija el composition root
    // antes de cargar Main.qml (sin aviso queda inactivo).
    AvisoRaizPaquetesViewModel* avisoRaizPaquetes() const { return m_avisoRaizPaquetes; }
    // T025.6: pagina Respaldo y franja de avisos de restauracion (siempre
    // existe; sin puertos queda "no disponible").
    RespaldoViewModel* respaldo() const { return m_respaldo; }
    // T026.3: exportacion de facturas desde Facturas (siempre existe; sin
    // servicio queda "no disponible"). Sigue el perfil, mes y pestana de
    // facturas(). Main.qml lo pasa a FacturasPage.
    ExportacionFacturasViewModel* exportacionFacturas() const { return m_exportacionFacturas; }

    // {"appViewModel", "solicitudesModel",
    //  "detalleViewModel", "perfilesViewModel", "eFirmaViewModel",
    //  "exploradorViewModel", "avisoRaizPaquetesViewModel",
    //  "facturasViewModel", "historialViewModel", "respaldoViewModel",
    //  "exportacionFacturasViewModel"} para
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
    // T023.5: datos del Historial y destino de notificaciones de solicitud.
    // No propietarios; nullptr = sin Historial / sin resolucion.
    void setFuenteHistorial(FuenteHistorial* fuente);
    void setResolucionDestinos(ResolucionDestinoNotificacion* resolucion);
    // T025.6 (C2): puertos del respaldo, la restauracion y los datos previos.
    // No propietarios; deben vivir mas que este objeto. nullptr = la parte
    // correspondiente de la pagina Respaldo queda "no disponible". Con el
    // control de respaldo, AppViewModel::mostrarRespaldo() navega.
    void setControlRespaldo(ControlRespaldoPresentacion* control);
    void setControlRestauracion(ControlRestauracionPresentacion* control);
    void setDatosPreviosRestauracion(DatosPreviosRestauracionPresentacion* datos);
    // C4: impresion de la clave de recuperacion (vacia = sin "Imprimir…").
    void setImpresionTexto(ImpresionTexto impresion);
    // H1: copia de la clave de recuperacion (vacia = sin "Copiar").
    void setCopiaTextoSecreto(CopiaTextoSecreto copia);
    // T026.3: servicio de exportacion (no propietario; debe vivir mas que
    // este objeto) y reloj de Facturas (mes en curso y fecha del nombre
    // sugerido). nullptr = exportacion no disponible.
    void setExportacionFacturas(ExportacionFacturasService* servicio, RelojUtc reloj = relojSistema());

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
    HistorialViewModel* m_historial;
    AppViewModel* m_app;
    AvisoRaizPaquetesViewModel* m_avisoRaizPaquetes;
    RespaldoViewModel* m_respaldo;
    ExportacionFacturasViewModel* m_exportacionFacturas;
};

} // namespace satcfdi
