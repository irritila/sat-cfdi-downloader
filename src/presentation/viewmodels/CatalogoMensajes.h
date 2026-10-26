#pragma once

#include "application/cfdi/ExportacionFacturasService.h"
#include "application/profiles/PerfilConPreparacion.h"
#include "application/requests/SolicitudDtos.h"
#include "HorizonteFacturas.h"
#include "PuertosRespaldoPresentacion.h"

#include <QString>
#include <QStringList>

#include <optional>

namespace satcfdi::catalogo {

// Catalogo de mensajes visibles de T009 (D10). Textos fijos: sin RFC
// completo, Ids, token, rutas ni Mensaje SAT crudo. Vacio = sin mensaje.

// Por que no se puede enviar con la credencial del perfil (vacio si Lista y
// perfil activo).
QString motivoCredencial(PreparacionPerfil preparacion, bool perfilActivo);

// Estado de la solicitud segun estado local, codigos de creacion y
// verificacion (conservados por separado).
QString mensajeSolicitud(const SolicitudDetalle& detalle);

// Paquete: vencido, maximo de descargas (5008) o error reintentable.
QString mensajePaquete(const PaqueteResumen& paquete);

// 5008: el paquete alcanzo el maximo de descargas; no se ofrece reintentar.
bool esMaximoDescargas(const PaqueteResumen& paquete);


// ---- T025.6: respaldo y restauracion ----
// Textos fijos: nunca "subido" (D1: "copiado a la carpeta"), sin diagnostico,
// rutas, nombres de archivo, RFC, contrasenas ni claves.

// Mensaje visible: variante de AvisoEnLinea ("exito", "advertencia",
// "error", "neutro" o "progreso"), titulo y descripcion.
struct MensajeVisible {
    QString variante;
    QString titulo;
    QString descripcion;
};

// Causa de un respaldo fallido (vacio con exito).
QString causaRespaldo(const ResultadoRespaldoPresentacion& resultado);
// Titulo/variante/descripcion del resultado (sin fecha: la agrega el view model).
MensajeVisible mensajeRespaldo(const ResultadoRespaldoPresentacion& resultado);
// Advertencias de un respaldo valido (faltantes, omitidos, depuracion o
// registro) y, en cualquier resultado, que el resumen no se pudo guardar
// (avisoPersistencia). Las copias sin registro van aparte
// (avisoCopiasSinRegistro).
QStringList advertenciasRespaldo(const ResultadoRespaldoPresentacion& resultado);
// D20: aviso de borrado manual (vacio si no hay copias sin registro).
QString avisoCopiasSinRegistro(int copias);

QString mensajeErrorGestor(respaldo::ErrorGestorRespaldo::Categoria categoria);
QString mensajeErrorRestauracion(const respaldo::ErrorRestauracion& error);
MensajeVisible mensajeSolicitudRestauracion(const respaldo::ResultadoSolicitudRestauracion& resultado);
MensajeVisible mensajeDescarte(const ResultadoDescartePresentacion& resultado);
// `fecha`: fecha del respaldo restaurado ya formateada (vacia si no se conoce).
MensajeVisible mensajeArranque(ResultadoArranquePresentacion::Estado estado, const QString& fecha);
MensajeVisible avisoDatosPrevios();

QString textoFase(respaldo::RespaldoService::Fase fase);
QString textoFase(respaldo::RestauracionService::Fase fase);
QString textoPendienteDiario(EstadoRespaldoPresentacion::PendienteDiario pendiente);


// ---- T026.3: exportacion de facturas (C5) ----
// Textos fijos sin "solicitud", "paquete" ni "verificacion", y sin datos
// fiscales, rutas ni nombres de archivo (D20): el adaptador solo recibe
// estado, conteos y codigos. `nombrePublicado` lo agrega el view model en la
// ventana de exportacion, nunca este catalogo.
struct ResumenExportacion {
    ResultadoExportacion::Estado estado = ResultadoExportacion::Estado::Error;
    std::optional<ErrorExportacion::Codigo> error;
    std::optional<exportacion::ErrorDestinoExportacion::Codigo> destino;
    int exportadas = 0;
    int pdfExportados = 0;
    int sinPdf = 0;
    int omitidas = 0;
    int lineasMes = 0;
    bool advertenciaDurabilidad = false;
};
// Copia solo estado, conteos y codigos (descarta nombre, tamano y errno).
ResumenExportacion resumenExportacion(const ResultadoExportacion& resultado);

// D10: aviso fijo del dialogo de exportar.
QString avisoExportacion();
// D16: "Se exportarán N facturas." o, con 0, "No hay facturas para exportar".
QString textoConteoPrevisto(int conteo);
// Facturas del plan que no se exportaran (no disponibles en este equipo o ya
// no disponibles) y meses con descargas pendientes o cobertura incompleta
// (D9). Vacio si no hay ninguno.
QStringList avisosPlanExportacion(int omitidas, int lineasMes);
QString textoErrorRango(horizonte_facturas::ErrorRango error);
// Error al preparar el plan (Planificacion, Detenido, ...).
QString textoErrorPlanExportacion(ErrorExportacion::Codigo codigo);
// publicarComo(): Estado (el servicio ya no espera un nombre) tiene texto
// propio; Detenido reutiliza el del plan.
QString textoErrorPublicarExportacion(ErrorExportacion::Codigo codigo);
// DestinoExistente al reservar o al publicar: pedir otro nombre.
QString textoDestinoExistente();
// Error del destino que el usuario corrige en el dialogo (otra carpeta u otro
// nombre); vacio si el codigo no es corregible ahi.
QString textoErrorDestinoCorregible(exportacion::ErrorDestinoExportacion::Codigo codigo);
QString textoFase(FaseExportacion fase);
// Resultado: variante, titulo y descripcion (D9, D22: texto propio con
// exportadas == 0).
MensajeVisible mensajeExportacion(const ResumenExportacion& resumen);
// Lineas de detalle de un ZIP publicado: sin PDF, omitidas y meses incompletos.
QStringList detallesExportacion(const ResumenExportacion& resumen);
// Advertencia de durabilidad (vacia si no aplica).
QString advertenciaExportacion(const ResumenExportacion& resumen);

} // namespace satcfdi::catalogo
