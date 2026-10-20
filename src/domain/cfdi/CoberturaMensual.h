#pragma once

#include "domain/cfdi/IndiceCfdi.h"
#include "domain/cfdi/MesCalendario.h"
#include "domain/paquetes/EstadoDescarga.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/EstadosSolicitud.h"
#include "domain/solicitudes/SolicitudId.h"
#include "domain/solicitudes/SolicitudPersistida.h"

#include <QDateTime>
#include <QLatin1StringView>
#include <QList>
#include <QString>

#include <optional>

namespace satcfdi {

// Regla pura de cobertura y estado del mes (T023.2, ADR 0019, T023 D11/D19).
// Sin reloj, SQL, archivos ni QML: recibe la evidencia ya leida y el instante
// ya capturado.

// Estados del mes, en orden de precedencia (ADR 0019).
enum class EstadoMes {
    RequiereAtencion,
    Preparando,
    Disponibles,
    DisponiblesParcialmente,
    SinDescargar,
};

// Causa de `RequiereAtencion`, en orden de prioridad para elegir la causa
// principal. Los codigos 5002 y 5005 preceden al estado de la solicitud que
// los porta (p. ej. EnvioFallido con 5002) porque cambian la accion ofrecida:
// 5002 no permite recrear la direccion y 5005 conduce al diagnostico.
enum class CausaAtencion {
    Codigo5002,
    Codigo5005,
    EnvioIncierto,
    EnvioFallido,
    ErrorSat,
    Rechazada,
    Vencida,
    PaqueteError,
    PaqueteVencido,
    ZipNoDisponible,        // ZIP faltante u origen no disponible (indice OrigenNoDisponible)
    ZipIlegible,            // el lector rechazo el ZIP (indice Ilegible)
    IndiceIlegible,         // la fila de indice no se pudo interpretar
    IndexacionSinProgramar, // paquete Descargado sin indice vigente y sin trabajo
};

// Clase de mes respecto del instante de evaluacion (en Centro).
enum class TipoMes {
    Cerrado,
    EnCurso,
    Futuro,
};

// Claves estables (nombre del enumerador); la presentacion las traduce.
QLatin1StringView claveEstable(EstadoMes estado);
QLatin1StringView claveEstable(CausaAtencion causa);
QLatin1StringView claveEstable(TipoMes tipo);

// Paquete visible de una solicitud del mes con el estado de su indice.
struct EvidenciaPaqueteMensual {
    QString paqueteId;
    EstadoDescarga estadoDescarga = EstadoDescarga::Disponible;
    // Solo para Descargado: estado de indice; nullopt = sin fila (Pendiente).
    std::optional<EstadoIndicePaquete> indice;
    bool indiceVigente = false; // version de parser vigente
    bool indiceLegible = true;  // false: la fila de indice no se pudo interpretar
};

// Solicitud visible del perfil cuyo rango intersecta el mes.
struct EvidenciaSolicitudMensual {
    SolicitudPersistida solicitud;
    QList<EvidenciaPaqueteMensual> paquetes; // paquetes visibles
    // Hay trabajo de indexacion en curso o en cola que alcanza a la solicitud
    // (lo completa la aplicacion con el indexador).
    bool indexacionProgramada = false;
};

struct CausaMensual {
    CausaAtencion causa = CausaAtencion::IndexacionSinProgramar;
    TipoDescarga direccion = TipoDescarga::Emitidos;
    SolicitudId solicitud;

    friend bool operator==(const CausaMensual&, const CausaMensual&) = default;
};

// Tramo inclusivo por segundos, en reloj de pared Centro (ver MesCalendario.h).
struct TramoMensual {
    QDateTime inicio;
    QDateTime fin;

    friend bool operator==(const TramoMensual&, const TramoMensual&) = default;
};

// Tramo de una solicitud canonica en curso: lo reserva (ADR 0020).
struct TramoEnCurso {
    SolicitudId solicitud;
    TramoMensual tramo;

    friend bool operator==(const TramoEnCurso&, const TramoEnCurso&) = default;
};

// Hueco maximo sin acreditar dentro del objetivo de la direccion (ADR 0020).
struct HuecoCobertura {
    TramoMensual tramo;
    // Solicitudes en curso cuyo tramo intersecta el hueco: T023.3 las
    // reutiliza en lugar de crear una solicitud solapada.
    QList<SolicitudId> reservadoPor;

    friend bool operator==(const HuecoCobertura&, const HuecoCobertura&) = default;
};

// Cobertura de una direccion (ADR 0019 modificado por ADR 0020).
struct CoberturaDireccion {
    TipoDescarga direccion = TipoDescarga::Emitidos;
    // Ultimo segundo cubierto contiguamente desde el primer segundo del mes
    // ("disponibles hasta"); nullopt = el primer segundo no esta cubierto.
    std::optional<QDateTime> hasta;
    bool alcanzaFrontera = false;
    // Union de TODOS los tramos acreditados del mes, en componentes maximos
    // ordenados.
    QList<TramoMensual> tramos;
    // Complemento de `tramos` en componentes maximos (inicial, internos y
    // final) dentro del objetivo de planeacion: [inicio del mes, fin del mes]
    // en un mes cerrado; [inicio del mes, instante evaluado] en el mes en
    // curso. Vacio si alcanzaFrontera (el mes en curso cubierto hoy no se
    // actualiza el mismo dia, ADR 0019) o en un mes futuro.
    QList<HuecoCobertura> huecos;
    int tramosAcreditados = 0; // tramos de solicitudes acreditadas (antes de unir)
    int tramosTrasHueco = 0;   // acreditados que no extienden `hasta`
    // Solicitudes canonicas en curso (Preparando) y sus tramos reservados.
    QList<SolicitudId> solicitudesEnCurso;
    QList<TramoEnCurso> enCurso;
    // Solicitudes que acreditan cobertura (contigua o no).
    QList<SolicitudId> solicitudesAcreditadas;
};

struct EvaluacionCoberturaMensual {
    MesCalendario mes = *MesCalendario::crear(2000, 1);
    TipoMes tipoMes = TipoMes::Cerrado;
    EstadoMes estado = EstadoMes::SinDescargar;
    // Causa principal (solo con RequiereAtencion) y todas las causas ACTIVAS,
    // ordenadas por prioridad, direccion (Emitidos primero), creada_en e id.
    std::optional<CausaMensual> causa;
    QList<CausaMensual> causas;
    // Causas superadas (ADR 0020): su solicitud quedo cubierta integramente
    // por solicitudes canonicas acreditadas posteriores (creada_en, id) de la
    // misma direccion. Solo para diagnostico e historial: no fuerzan
    // RequiereAtencion. Mismo orden que `causas`.
    QList<CausaMensual> causasSuperadas;
    CoberturaDireccion emitidos;
    CoberturaDireccion recibidos;
    // Reloj de pared Centro: frontera objetivo (fin del mes cerrado o inicio
    // de hoy en el mes en curso) e instante evaluado.
    QDateTime frontera;
    QDateTime instanteCentro;
    // Solicitudes del perfil con filtros o rango no canonico (aportan CFDI al
    // catalogo pero no participan en cobertura ni estado).
    int solicitudesNoCanonicas = 0;

    const CoberturaDireccion& cobertura(TipoDescarga d) const
    {
        return d == TipoDescarga::Emitidos ? emitidos : recibidos;
    }
};

// Solicitud canonica del mes (ADR 0019): mismo perfil y direccion, CFDI y
// Vigente, sin filtros adicionales (contraparte, tipo de comprobante,
// complemento), rango SAT valido dentro del mes y no eliminada.
bool esSolicitudCanonicaDelMes(const SolicitudPersistida& solicitud, const PerfilId& perfil, MesCalendario mes);

// Evalua cobertura y estado del mes con precedencia ADR 0019:
// RequiereAtencion (causas activas) > Preparando > Disponibles (ambas
// direcciones alcanzan la frontera) > DisponiblesParcialmente (algun tramo
// acreditado, aunque no empiece el dia 1; ADR 0020) > SinDescargar (cero
// tramos acreditados en ambas direcciones). `instante`: el capturado por el reloj inyectable al iniciar
// la accion (cualquier zona; se convierte a Centro). Solo considera las
// solicitudes canonicas de `perfil` y `mes`; el resto se cuenta en
// solicitudesNoCanonicas. Determinista: no depende del orden de `evidencia`.
EvaluacionCoberturaMensual evaluarCoberturaMensual(const PerfilId& perfil, MesCalendario mes,
                                                   const QDateTime& instante,
                                                   const QList<EvidenciaSolicitudMensual>& evidencia);

} // namespace satcfdi
