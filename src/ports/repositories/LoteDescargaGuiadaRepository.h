#pragma once

#include "domain/cfdi/LoteDescargaGuiada.h"
#include "domain/cfdi/MesCalendario.h"
#include "domain/common/Resultado.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/EstadoResumen.h"
#include "domain/solicitudes/EstadosSolicitud.h"
#include "domain/solicitudes/SolicitudId.h"
#include "ports/persistence/ErrorPersistencia.h"
#include "ports/persistence/Exito.h"

#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

namespace satcfdi {

// Tramo SAT exacto de una direccion del lote (inclusivo, por segundos, hora
// Centro sin offset; igual a fecha_inicial_sat/fecha_final_sat de la
// solicitud vinculada).
struct TramoLoteGuiado {
    TipoDescarga direccion = TipoDescarga::Emitidos;
    SolicitudId solicitud;
    QString fechaInicialSat;
    QString fechaFinalSat;

    friend bool operator==(const TramoLoteGuiado&, const TramoLoteGuiado&) = default;
};

// INSERT de un lote (T023.3 lo escribe en la transaccion que crea sus
// solicitudes). Uno o mas tramos; varios por direccion (uno por hueco, ADR
// 0020) sin traslape entre los de una misma direccion.
struct NuevoLoteGuiado {
    QString id; // UUID canonico
    PerfilId perfil;
    MesCalendario mes = *MesCalendario::crear(2000, 1);
    QDateTime creadoEn;
    QList<TramoLoteGuiado> tramos;
};

struct TramoLoteConsultado {
    TramoLoteGuiado tramo;
    bool eliminada = false;               // la solicitud vinculada fue eliminada localmente
    std::optional<EstadoResumen> estado;  // solo si no esta eliminada
};

// Lote para el historial (derivado; ADR 0019). Conserva mes y fecha aunque se
// eliminen sus solicitudes; `resultado` se recalcula sin las eliminadas.
struct LoteGuiado {
    QString id;
    PerfilId perfil;
    MesCalendario mes = *MesCalendario::crear(2000, 1);
    QDateTime creadoEn;
    QList<TramoLoteConsultado> tramos;   // emitidos antes que recibidos; por fecha inicial
    bool eliminacionParcial = false;     // alguna solicitud del lote fue eliminada
    ResultadoLote resultado = ResultadoLote::SinSolicitudesVigentes;
};

// Lotes de descarga guiada (migracion 007). Hilo: tareas del
// PersistenceDispatcher (conexion por hilo).
class LoteDescargaGuiadaRepository {
public:
    virtual ~LoteDescargaGuiadaRepository() = default;

    // Tx: requerida. Revalida dentro de la transaccion que el perfil sea
    // visible y que cada solicitud sea visible, canonica del mes del lote, de
    // la misma direccion y con el mismo tramo SAT, y que los tramos de una
    // misma direccion no se traslapen; si no, Integridad sin escribir nada.
    // Errores: Integridad, Unicidad (solicitud ya vinculada a otro lote),
    // Transaccion, Ocupado, Almacenamiento.
    virtual Resultado<Exito, ErrorPersistencia> insertar(const NuevoLoteGuiado& lote) = 0;

    // Lotes del perfil (de un mes o de todos), creado_en DESC, id DESC.
    // Perfil eliminado (o inexistente) -> lista vacia: los lotes se ocultan
    // con su perfil y no se reasignan a un perfil recreado con el mismo RFC.
    // Tx: opcional. Errores: Almacenamiento, Interno (fila ilegible).
    virtual Resultado<QList<LoteGuiado>, ErrorPersistencia> listar(const PerfilId& perfil,
                                                                   const std::optional<MesCalendario>& mes) = 0;
};

} // namespace satcfdi
