#pragma once

// T024.2 (D9): datos declarados de los fakes sinteticos comunes de
// tools/capturas-manual y tools/video-tutorial. Solo RFC genericos del SAT
// (XAXX010101000, XEXX010101000), nombres inventados y rutas /Users/usuario.
// Identificadores fijos (nunca generar()): dos ejecuciones dan los mismos
// datos.

#include "application/cfdi/HistorialDescargas.h"
#include "domain/cfdi/CoberturaMensual.h"
#include "domain/cfdi/MesCalendario.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/SolicitudId.h"
#include "ports/repositories/CatalogoMensualRepository.h"
#include "ports/secrets/SecretStoreTypes.h"

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>

#include <optional>

namespace materiales {

using namespace satcfdi;

inline const QString kRfcPerfil = QStringLiteral("XAXX010101000");
inline const QString kRfcOtro = QStringLiteral("XEXX010101000");
inline const QString kNombrePerfil = QStringLiteral("Contribuyente de ejemplo");
inline const QString kNombreOtro = QStringLiteral("Proveedor de ejemplo");

// UUID fijo con el patron de la herramienta de capturas: 8 veces `c`, etc.
PerfilId perfilIdFijo(char c);
// 00000000-0000-4000-8000-<n en 12 digitos>.
SolicitudId solicitudIdFija(int n);

// Estado de disponibilidad declarado de un mes (la franja de Facturas).
enum class EstadoMesSintetico {
    Disponibles,
    DisponiblesParcialmente, // emitidas completas; recibidas sin cobertura
    Preparando,              // huecos reservados por solicitudes en curso
    RequiereAtencion,        // causa en `causa` / `direccionCausa`
    SinDescargar,
};

struct MesDeclarado {
    EstadoMesSintetico estado = EstadoMesSintetico::SinDescargar;
    int emitidas = 0;
    int recibidas = 0;
    CausaAtencion causa = CausaAtencion::ZipIlegible;
    TipoDescarga direccionCausa = TipoDescarga::Recibidos;
};

struct PerfilDeclarado {
    PerfilId id;
    QString rfc;
    QString nombre;
    std::optional<EstadoCredencial> credencial;
    // Clave: anio * 100 + mes. Un mes ausente es SinDescargar sin facturas.
    QMap<int, MesDeclarado> meses;
};

// Confirmacion por direccion (T023.3): direcciones cuyos candidatos llegan
// como RequiereConfirmacion al planificar la accion del mes.
struct PreplanDeclarado {
    QList<TipoDescarga> porConfirmar;
};

struct DatosFacturas {
    QDateTime ahoraUtc;
    QList<PerfilDeclarado> perfiles;
    PreplanDeclarado preplan;
    // nullopt: ejecutar() queda pendiente (la franja sigue "ejecutando").
    std::optional<int> solicitudesCreadasAlEjecutar;
};

inline int claveMes(int anio, int mes) { return anio * 100 + mes; }

// Conjunto de ejemplo: reloj 2026-10-15 12:00 Centro (18:00 UTC); perfil
// principal XAXX010101000 con enero a agosto Disponibles, septiembre
// Disponibles y octubre (mes en curso) Sin descargar; perfil sin e.firma
// XEXX010101000 sin datos.
DatosFacturas datosFacturasDeEjemplo();

// Facturas sinteticas de un mes y direccion, ordenadas por fecha descendente
// (como la app). Deterministas por (rfc, anio, mes, direccion, i).
QList<CfdiMensual> cfdisDelMes(const PerfilDeclarado& perfil, int anio, int mes, TipoDescarga direccion,
                               int cuantas);

// Historial de ejemplo (T023.5) del perfil: un lote por estado de resultado
// mas una entrada "Anterior".
QList<EntradaHistorial> historialDeEjemplo(const PerfilId& perfil);

} // namespace materiales
