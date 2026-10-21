#pragma once

#include "domain/cfdi/CoberturaMensual.h"
#include "domain/cfdi/MesCalendario.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/EstadosSolicitud.h"
#include "domain/solicitudes/SolicitudId.h"
#include "ports/repositories/CatalogoMensualRepository.h"
#include "ports/repositories/LoteDescargaGuiadaRepository.h"

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QLatin1StringView>
#include <QList>
#include <QString>

#include <optional>

namespace satcfdi {

// Historial de descargas de un perfil (T023.5, T023 D6/D14, ADR 0019/0020).
// Una entrada por lote guiado (accion del usuario sobre un mes, ambas
// direcciones) y una por solicitud previa a T023 sin lote ("Descarga
// anterior", solo diagnostico). Valor derivado: nunca se persiste.

// Resultado visible de una entrada, derivado de la evaluacion del mes
// (evaluarCoberturaMensual) restringida a las solicitudes vigentes del lote:
// - RequiereAtencion: alguna solicitud vigente del lote tiene una causa
//   ACTIVA del mes (CoberturaMensual::causas).
// - EnPreparacion: sin causas activas y alguna solicitud vigente del lote en
//   curso (solicitudesEnCurso: envio, SAT, paquetes o indexacion).
// - NoCompletada: sin causas activas ni trabajo, pero alguna solicitud
//   vigente tiene una causa SUPERADA (ADR 0020: otra descarga posterior cubrio
//   su tramo). Sin Reintentar.
// - SinFacturas: todas las vigentes acreditan y el lote no aporta ningun CFDI
//   al catalogo (conjunto vacio confirmado por el SAT).
// - Completa: todas las vigentes acreditan.
// - Eliminada: todas las solicitudes del lote se eliminaron.
// - Anterior: solicitud previa a T023 sin lote.
// Precedencia: RequiereAtencion > EnPreparacion > NoCompletada > SinFacturas/Completa.
enum class ResultadoHistorial {
    Completa,
    EnPreparacion,
    RequiereAtencion,
    SinFacturas,
    NoCompletada,
    Eliminada,
    Anterior,
};

QLatin1StringView claveEstable(ResultadoHistorial resultado);

struct EntradaHistorial {
    enum class Tipo {
        Guiada,   // lote de descarga guiada
        Anterior, // solicitud previa a T023 sin lote
    };

    Tipo tipo = Tipo::Guiada;
    QString loteId; // vacio en Anterior
    PerfilId perfil;
    // Mes del lote. En Anterior: el mes si el rango SAT cae en un solo mes;
    // si no, nullopt y la presentacion usa desde/hasta.
    std::optional<MesCalendario> mes;
    QDate desde; // Anterior: dias del rango SAT; Guiada: primer y ultimo dia del mes
    QDate hasta;
    QDateTime creadoEn; // UTC; orden del historial (mas reciente primero)
    ResultadoHistorial resultado = ResultadoHistorial::Completa;

    // CFDI que la entrada aporta al catalogo mensual (misma deduplicacion que
    // CatalogoMensualRepository: cada UUID cuenta en la entrada de su fuente
    // canonica, asi la suma de las entradas de un mes coincide con el mes).
    int emitidas = 0;
    int recibidas = 0;
    // Direcciones con al menos una solicitud vigente en la entrada.
    bool tieneEmitidas = false;
    bool tieneRecibidas = false;

    bool eliminacionParcial = false; // alguna solicitud del lote se elimino
    // Hay causa activa y ofertaDescargaGuiada(evaluacion del mes) ofrece una
    // accion: Reintentar lleva a Facturas del mes y la ejecuta.
    bool puedeReintentar = false;
    // Solicitud a abrir en Diagnostico por direccion (nullopt si no hay
    // vigente): la de la causa activa de mayor prioridad; si no, la vigente
    // mas reciente (fecha inicial mayor) de esa direccion.
    std::optional<SolicitudId> diagnosticoEmitidas;
    std::optional<SolicitudId> diagnosticoRecibidas;
    // Anterior: la solicitud tenia filtros (contraparte, tipo, complemento).
    bool conFiltros = false;
};

// Funciones puras (pruebas unitarias). `conteos`: CFDI por solicitud fuente
// canonica. `evaluacion`: la del mes del lote (sin
// revalidar); nullptr si no se pudo evaluar (resultado por estados de
// solicitud: resultadoLote).
EntradaHistorial entradaDeLote(const LoteGuiado& lote, const EvaluacionCoberturaMensual* evaluacion,
                               const QHash<SolicitudId, int>& conteos);
EntradaHistorial entradaAnterior(const PerfilId& perfil, const SolicitudSinLote& solicitud,
                                 const QHash<SolicitudId, int>& conteos);

} // namespace satcfdi
