#pragma once

// Servicios fijos MUTABLES del video (T017 D6): perfiles, credenciales,
// solicitudes, transiciones y lista los gobierna el guion (sin SQLite,
// worker, Programador ni OperacionExecutor). El explorador y el PDF usan el
// lector ZIP (LectorZipLibzip), el parser (parsearCfdi), el calculo del
// indice (calcularIndicePaquete) y el renderer (RenderizadorPdfCfdi) reales
// sobre ZIP sinteticos generados antes de grabar en el directorio temporal.
// Reutiliza los fakes inmutables de tools/capturas-manual/ServiciosFijos.h y
// (T024.2, T024 D9) los fakes sinteticos comunes de tools/fakes-materiales:
// Facturas, Historial, Exportacion y Respaldo.

#include "ExportacionSintetica.h" // tools/fakes-materiales
#include "FuentesSinteticas.h"
#include "Guion.h"
#include "RespaldoSintetico.h"
#include "ServiciosFijos.h" // tools/capturas-manual

#include "application/primeruso/ConsultaPrimerUso.h"

#include "infrastructure/cfdi/pdf/RenderizadorPdfCfdi.h"
#include "infrastructure/cfdi/zip/LectorZipLibzip.h"

#include <QDateTime>
#include <QHash>
#include <QPromise>
#include <QSet>

#include <map>
#include <memory>
#include <vector>

namespace video {

using namespace satcfdi;

// Solicitudes en memoria. crearLocal agrega una solicitud Creada ("nuevaN").
class SolicitudesGuion final : public SolicitudesService {
public:
    QList<SolicitudDetalle> detalles;
    QDateTime ahora;
    int creadas = 0;
    QHash<QString, SolicitudId> porClave;
    QSet<QString> ocultas; // solicitudId: fuera de la lista y del detalle
    std::function<QString(const QString&)> nombrePerfilPorRfc;
    QHash<QString, QString> rfcPorPerfilId;

    QFuture<ResultadoLista> listar() override;
    QFuture<ResultadoDetalle> obtener(const SolicitudId& id) override;
    QFuture<ResultadoEvaluarDuplicado> evaluarDuplicado(const NuevaSolicitudRequest&) override;
    QFuture<ResultadoCrear> crearLocal(const NuevaSolicitudRequest& request, ConfirmacionDuplicado) override;
    QFuture<ResultadoEliminar> eliminar(const SolicitudId&) override;

    SolicitudDetalle* buscar(const SolicitudId& id);
};

// Perfiles en memoria. crear(rfc, nombre) solo acepta un perfil declarado
// en el guion con "visible": false y ese RFC (D2): lo hace visible con el
// nombre escrito en la UI. Cualquier otro RFC queda en `violacion`.
class PerfilesGuion final : public PerfilesSatService {
public:
    QList<PerfilResumen> perfiles; // todos, en orden de declaracion
    QSet<QString> ocultos;         // perfilId
    QString violacion;             // no vacio = la grabacion debe fallar

    QFuture<ResultadoLista> listarNoEliminados() override;
    QFuture<ResultadoPerfil> obtener(const PerfilId& id) override;
    QFuture<ResultadoCrear> crear(const QString& rfc, const QString& nombre) override;
    QFuture<ResultadoActualizar> actualizarNombre(const PerfilId& id, const QString& nombre) override;
};

// Credenciales en memoria. importar/reemplazar dejan la operacion pendiente
// (la UI muestra "Validando…") hasta que el guion la complete
// (completarEFirma). Nunca leen los archivos ni la contrasena.
class CredencialesGuion final : public CredencialesSatService {
public:
    QHash<QString, ResumenCredencial> resumenes; // perfilId -> resumen (falta = SinCredencial)

    QFuture<ResultadoImportacion> importar(const PerfilId& perfil, EntradaEFirma&& entrada) override;
    QFuture<ResultadoImportacion> reemplazar(const PerfilId& perfil, EntradaEFirma&& entrada) override;
    QFuture<ResultadoEstado> obtenerEstado(const PerfilId& perfil) override;
    QFuture<ResultadoResumen> obtenerResumen(const PerfilId& perfil) override;
    QFuture<ResultadoEliminacion> eliminar(const PerfilId& perfil) override;
    QFuture<ResultadoReconciliacion> reconciliar() override;
    ResultadoMaterial obtenerMaterialFirma(const PerfilId&) override;

    bool hayPendiente(const QString& perfilId) const { return m_pendientes.count(perfilId) != 0; }
    // Termina con exito la importacion pendiente del perfil. Falso si no hay.
    bool completar(const PerfilId& perfil, const ResumenCredencial& resumen);
    // Cambia la credencial sin operacion pendiente (cambiarEFirma).
    void fijar(const PerfilId& perfil, const std::optional<ResumenCredencial>& resumen);

private:
    QFuture<ResultadoImportacion> pendiente(const PerfilId& perfil);
    std::map<QString, std::unique_ptr<QPromise<ResultadoImportacion>>> m_pendientes;
};

// T014.3 D1: pasos de "Empieza en tres pasos" derivados en vivo de los
// servicios del guion (perfiles visibles, alguna e.firma Lista, solicitudes
// visibles). La app refresca la guia con las senales de esos servicios.
class PrimerUsoGuion final : public ConsultaPrimerUso {
public:
    PrimerUsoGuion(const PerfilesGuion& perfiles, const CredencialesGuion& credenciales,
                   const SolicitudesGuion& solicitudes);
    QFuture<ResultadoPrimerUso> consultar() override;

private:
    const PerfilesGuion& m_perfiles;
    const CredencialesGuion& m_credenciales;
    const SolicitudesGuion& m_solicitudes;
};

// Indice de CFDI por solicitud: solo visible con la solicitud "descargada".
class CatalogoGuion final : public ConsultaCatalogoCfdi {
public:
    QHash<QString, QList<CfdiListado>> indexados; // solicitudId -> CFDI del indice real
    QHash<QString, ResumenIndiceSolicitud> resumenIndexado;
    QSet<QString> visibles;

    QFuture<ResultadoLista> listar(const SolicitudId& solicitud, const FiltroListaCfdi& filtro) override;
    QFuture<ResultadoResumen> resumen(const SolicitudId& solicitud) override;
    QFuture<void> revisarSolicitud(const SolicitudId&) override { return QtFuture::makeReadyVoidFuture(); }
};

// PDF con el lector ZIP, el parser y el renderer reales. Modos de prueba
// (criterio de la barrera, D6): Fallo -> documentoError; SinRespuesta -> el
// future nunca termina (timeout del guion).
class PrevisualizacionZip final : public PrevisualizacionCfdi {
public:
    enum class Modo { Normal, Fallo, SinRespuesta };
    Modo modo = Modo::Normal;
    QHash<QString, QString> zipPorPaquete; // paqueteId -> ruta absoluta
    QString dirPdf;

    QFuture<ResultadoGenerar> generar(const SolicitudId&, const QString& paqueteId, int posicion) override;
    QFuture<ResultadoGuardar> guardarComo(const QString&, const QString&) override;

private:
    LectorZipLibzip m_lector;
    RenderizadorPdfCfdi m_renderer;
    std::vector<std::unique_ptr<QPromise<ResultadoGenerar>>> m_pendientes;
};

class DemoVideo {
public:
    DemoVideo(const DatosGuion& datos, QString dirTemporal);

    // Genera los ZIP sinteticos, los indexa con el codigo real y arma el
    // estado inicial. Error con la solicitud o vacio.
    QString preparar();
    // Transicion gobernada por el guion; `verificadaEn` (ISO, opcional) fija
    // la ultima verificacion: el resultado de "Verificar ahora". Error o vacio.
    QString avanzar(const QString& clave, const QString& estado, const QString& verificadaEn = {});
    // Credencial del perfil (lista, porVencer, sinEFirma) y su vencimiento.
    QString cambiarEFirma(const QString& perfil, const QString& efirma, const QString& vigenteHasta);
    // Reemplaza la lista visible por estas solicitudes (salto temporal).
    QString mostrarSolicitudes(const QStringList& claves);
    // Termina "Validando…" de la e.firma del perfil (vigencia por omision: 3 anos).
    QString completarEFirma(const QString& perfil, const QString& vigenteHasta);
    // Notificaciones para IlustracionNotificacion con los textos reales de
    // ServicioNotificaciones y las acciones de notificacionLocalDe (copia).
    // parametros.notificaciones: [{tipo: descargaCompleta, solicitud} |
    // {tipo: efirmaPorVencer, perfil}].
    QVariantList notificaciones(const QVariantMap& parametros, QString& error) const;

    QDateTime ahora() const { return m_ahora; }

    SolicitudesGuion solicitudes;
    PerfilesGuion perfiles;
    CredencialesGuion credenciales;
    capturas::AccionesNulas acciones;
    capturas::ExistenciaFija existencia;
    CatalogoGuion catalogo;
    PrevisualizacionZip previsualizacion;
    PrimerUsoGuion primerUso{perfiles, credenciales, solicitudes};
    // T024.2 (D9): Facturas (pantalla principal desde T023) e Historial con
    // los fakes comunes. Facturas toma los perfiles visibles del guion con su
    // e.firma (sin meses declarados: catalogo vacio y meses Sin descargar,
    // como T022 D6); Historial, una entrada "Anterior" por solicitud visible.
    materiales::FuenteFacturasSintetica facturas; // reloj del guion
    materiales::PreferenciasFacturasMemoria preferenciasFacturas;
    materiales::HistorialSintetico historial;
    // Exportacion (servicio real sobre puertos en memoria; sin retener: el
    // resultado se espera con la barrera esperarExportacion) y Respaldo
    // gobernados por datos. Las escenas que los usan son de T024.4.
    materiales::ExportacionSintetica exportacion;
    materiales::ControlRespaldoSintetico respaldo;
    materiales::ControlRestauracionSintetico restauracion;
    materiales::DatosPreviosSinteticos datosPrevios;

private:
    static materiales::DatosFacturas datosFacturas(const DatosGuion& datos);
    static materiales::DatosExportacion datosExportacion();
    QList<PerfilFacturas> perfilesFacturas() const;
    QList<EntradaHistorial> historialDe(const PerfilId& perfil) const;
    void aplicarEstado(SolicitudDetalle& d, const QString& estado, const QString& clave);

    DatosGuion m_datos;
    QString m_dir;
    QDateTime m_ahora;
    QHash<QString, QString> m_paquetePorSolicitud; // clave -> idPaqueteSat
    QHash<QString, PerfilId> m_perfilId;           // clave -> id
};

} // namespace video
