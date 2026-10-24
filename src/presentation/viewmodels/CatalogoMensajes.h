#pragma once

#include "application/profiles/PerfilConPreparacion.h"
#include "application/requests/SolicitudDtos.h"
#include "PuertosRespaldoPresentacion.h"

#include <QString>
#include <QStringList>

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

} // namespace satcfdi::catalogo
