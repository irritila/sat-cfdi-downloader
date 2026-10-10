#pragma once

#include "application/persistence/PuertosPersistencia.h"
#include "application/profiles/ConsultaPreparacionPerfiles.h"

#include <QFuture>
#include <QObject>

#include <functional>
#include <optional>

namespace satcfdi {

class AvisoVencimientoRepository;
class PersistenceDispatcher;
class Programador;
class ServicioNotificaciones;
class UnitOfWork;

// Aviso de vencimiento de e.firma (T014.3 D2, D3). QObject del hilo grafico.
//
// - iniciar(): evalua de inmediato y luego cada 24 h con el Programador y el
//   reloj inyectados (sin timers propios). detener() cancela lo programado.
// - evaluar(): lista perfiles verificados (`listarPerfiles`, p. ej.
//   ConsultaPreparacionPerfiles::listarVerificados); para cada perfil con
//   preparacion Lista y vigenteHasta dentro de 30 dias toma el umbral mas
//   urgente alcanzado (30 o 7), registra el dedupe persistido (perfil,
//   vigencia, umbral) en una tarea del PersistenceDispatcher y SOLO si es nuevo
//   notifica por ServicioNotificaciones (tipo efirma_por_vencer). Una e.firma
//   vencida o no Lista no avisa. El resultado de la entrega no vuelve: un
//   permiso denegado no cambia nada mas.
// - alCambiarCredencial(): reevalua (un reemplazo cambia la vigencia y por
//   tanto la clave del dedupe).
//
// Ownership: referencias NO propietarias; el composition root lo crea despues
// del dispatcher y del ServicioNotificaciones y lo destruye antes.
class AvisoVencimientoEFirma : public QObject {
    Q_OBJECT

public:
    using ListarPerfiles = std::function<QFuture<ConsultaPreparacionPerfiles::ResultadoLista>()>;

    AvisoVencimientoEFirma(ListarPerfiles listarPerfiles, PersistenceDispatcher& dispatcher,
                           AvisoVencimientoRepository& avisos, UnitOfWork& unidadDeTrabajo,
                           ServicioNotificaciones& notificaciones, Programador& programador, RelojUtc reloj,
                           QObject* parent = nullptr);
    ~AvisoVencimientoEFirma() override;

    void iniciar();
    void detener();

    // Evalua ahora. El future (hilo grafico) entrega cuantos avisos nuevos se
    // notificaron.
    QFuture<int> evaluar();

public slots:
    void alCambiarCredencial();

signals:
    // Fin de cada evaluacion (hilo grafico), con los avisos nuevos notificados.
    void evaluacionTerminada(int avisosNuevos);

private:
    void programarSiguiente();

    ListarPerfiles m_listar;
    PersistenceDispatcher& m_dispatcher;
    AvisoVencimientoRepository& m_avisos;
    UnitOfWork& m_unidadDeTrabajo;
    ServicioNotificaciones& m_notificaciones;
    Programador& m_programador;
    RelojUtc m_reloj;
    std::optional<quint64> m_programado;
    bool m_activo = false;
};

} // namespace satcfdi
