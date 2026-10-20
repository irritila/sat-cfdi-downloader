#pragma once

#include "application/common/Errores.h"
#include "application/persistence/PuertosPersistencia.h"
#include "domain/cfdi/MesCalendario.h"
#include "domain/cfdi/PlanDescargaGuiada.h"
#include "domain/common/Resultado.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/DedupKey.h"
#include "domain/solicitudes/Duplicados.h"
#include "domain/solicitudes/SolicitudCanonica.h"
#include "domain/solicitudes/SolicitudId.h"
#include "ports/persistence/ErrorPersistencia.h"
#include "ports/secrets/SecretStoreTypes.h"

#include <QDateTime>
#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

namespace satcfdi {

class CatalogoMensualRepository;
class LoteDescargaGuiadaRepository;
class PersistenceDispatcher;
class ProgresoIndexacionCfdi;

// Solicitud nueva propuesta por el plan para un hueco libre, con su
// clasificacion de duplicados (ADR 0019: evaluarDuplicado solo sobre tramos
// nuevos, nunca sobre los reutilizados).
struct CandidatoGuiado {
    TipoDescarga direccion = TipoDescarga::Emitidos;
    QString fechaInicialSat; // tramo exacto, hora Centro sin offset, inclusivo
    QString fechaFinalSat;
    DedupKey dedupKey;
    EvaluacionDuplicado duplicado;
    // Filtros normalizados con los que se crearia (derivados; no participan
    // en la comparacion).
    std::optional<SolicitudCanonica> canonica;

    friend bool operator==(const CandidatoGuiado& a, const CandidatoGuiado& b)
    {
        return a.direccion == b.direccion && a.fechaInicialSat == b.fechaInicialSat
               && a.fechaFinalSat == b.fechaFinalSat && a.dedupKey == b.dedupKey
               && a.duplicado.clasificacion == b.duplicado.clasificacion
               && a.duplicado.solicitudReferencia == b.duplicado.solicitudReferencia;
    }
};

// Resultado de planificar(): lo que la UI muestra y confirma antes de ejecutar.
// Sin transaccion abierta mientras la UI pregunta.
struct PreplanGuiado {
    PerfilId perfil;
    MesCalendario mes = *MesCalendario::crear(2000, 1);
    AccionGuiada accion = AccionGuiada::DescargarFacturas;
    QDateTime instante; // UTC, capturado UNA vez al iniciar la accion (D19)
    QList<SolicitudId> indexando; // solicitudes con indexacion programada al planificar
    EvaluacionCoberturaMensual evaluacion;
    OfertaDescargaGuiada oferta;
    PlanDescargaGuiada plan;
    QList<CandidatoGuiado> candidatos; // en orden: emitidos y luego recibidos, por tramo
    // Solicitudes reutilizadas por el plan que siguen en Creada (sin envio
    // iniciado): ejecutar() las vuelve a encolar, p. ej. tras un cierre entre
    // el commit y el encolado.
    QList<SolicitudId> reutilizadasSinEnviar;

    // Direcciones con algun candidato RequiereConfirmacion (una confirmacion
    // por direccion).
    QList<TipoDescarga> direccionesPorConfirmar() const;
    // Hay al menos un candidato que se crearia (no Bloqueado).
    bool creaAlgo() const;
};

enum class DecisionConfirmacion {
    Confirmar,
    Cancelar,
};

// Decision por direccion para sus candidatos RequiereConfirmacion. Cancelar
// una direccion no afecta a la otra.
struct DecisionesGuiadas {
    std::optional<DecisionConfirmacion> emitidos;
    std::optional<DecisionConfirmacion> recibidos;

    std::optional<DecisionConfirmacion> de(TipoDescarga d) const
    {
        return d == TipoDescarga::Emitidos ? emitidos : recibidos;
    }
};

struct EjecucionGuiada {
    std::optional<QString> loteId;     // solo si se creo alguna solicitud
    QList<SolicitudId> creadas;        // en orden de candidatos
    QList<SolicitudId> reutilizadas;   // en curso del plan y referencias Bloqueado
    QList<TipoDescarga> canceladas;    // direcciones cuya confirmacion se cancelo
    QList<SolicitudId> encoladas;      // creadas + reutilizadas en Creada
};

struct ErrorDescargaGuiada {
    enum class Tipo {
        EFirmaNoLista,         // `estadoCredencial` (nullopt si la consulta fallo)
        PerfilNoDisponible,    // perfil inexistente, eliminado o inactivo
        MesFuturo,
        AccionNoDisponible,    // la accion pedida no es la ofrecida: `preplanActual` (evaluacion y oferta)
        PlanDesactualizado,    // la evidencia o los duplicados cambiaron: `preplanActual`
        ConfirmacionPendiente, // falta la decision de `direcciones`
        Creacion,              // `creacion` (validacion, duplicado o persistencia)
        Persistencia,          // `causa`
    };
    Tipo tipo = Tipo::Persistencia;
    std::optional<EstadoCredencial> estadoCredencial;
    QList<TipoDescarga> direcciones;
    std::optional<PreplanGuiado> preplanActual;
    std::optional<ErrorCrear> creacion;
    std::optional<ErrorPersistencia> causa;
    QString mensaje;
};

// Puertos de la descarga guiada. Referencias no propietarias; las funciones
// las provee el composition root sobre OperacionExecutor y WorkerLocal.
struct PuertosDescargaGuiada {
    PersistenceDispatcher& dispatcher;
    PuertosPersistencia persistencia; // perfiles, solicitudes, logs, UoW, sanitizer
    CatalogoMensualRepository& catalogo;
    LoteDescargaGuiadaRepository& lotes;
    ProgresoIndexacionCfdi& indexacion;
    // Gate de e.firma (D9): estado del perfil en el hilo del ejecutor;
    // nullopt si la consulta fallo. Se completa en cualquier hilo.
    std::function<QFuture<std::optional<EstadoCredencial>>(const PerfilId&)> estadoCredencial;
    // Encola el envio de una solicitud Creada (WorkerLocal::enviar). Hilo grafico.
    std::function<void(const SolicitudId&)> encolarEnvio;
    RelojUtc reloj;
};

// Caso de uso "Descargar facturas" / "Actualizar mes" / "Completar mes"
// (T023.3, ADR 0019 y 0020).
//
// planificar(): captura el instante una vez; consulta la e.firma; lee la
// evidencia, evalua, planifica y clasifica duplicados de los candidatos (solo
// lectura). ejecutar(preplan, decisiones): consulta la e.firma; en UNA
// transaccion BEGIN IMMEDIATE relee y recalcula el plan con el MISMO instante;
// si cambio -> PlanDesactualizado (rollback, con el plan nuevo); si falta una
// confirmacion -> ConfirmacionPendiente; crea las solicitudes (nucleo de
// crearLocal), vincula SOLO las nuevas en un lote y confirma; despues, en el
// hilo grafico, encola el envio de las nuevas y de las reutilizadas en Creada.
//
// Idempotencia: tras crear, esas solicitudes reservan sus huecos y un plan
// posterior las reutiliza; una segunda ejecucion del mismo preplan detecta el
// cambio (PlanDesactualizado) y no crea nada. El worker no crea solicitudes.
//
// Ownership/hilo: QObject del hilo grafico, creado por el composition root
// despues del dispatcher, el ejecutor, el worker y el indexador.
class DescargaGuiadaService final : public QObject {
    Q_OBJECT

public:
    using ResultadoPreplan = Resultado<PreplanGuiado, ErrorDescargaGuiada>;
    using ResultadoEjecucion = Resultado<EjecucionGuiada, ErrorDescargaGuiada>;

    explicit DescargaGuiadaService(PuertosDescargaGuiada puertos, QObject* parent = nullptr);

    QFuture<ResultadoPreplan> planificar(const PerfilId& perfil, MesCalendario mes, AccionGuiada accion);
    QFuture<ResultadoEjecucion> ejecutar(const PreplanGuiado& preplan, const DecisionesGuiadas& decisiones);

signals:
    // Se crearon solicitudes del mes (despues del commit).
    void mesActualizado(const satcfdi::PerfilId& perfil, const QString& mes);

private:
    PuertosDescargaGuiada m_p;
};

} // namespace satcfdi
