#pragma once

#include "application/estadoagregado/EstadoAgregado.h"

#include <QFuture>
#include <QObject>

#include <functional>
#include <optional>

namespace satcfdi {

class ConsultaPreparacionPerfiles;
class PaqueteSolicitudRepository;
class PersistenceDispatcher;
class SolicitudMasivaRepository;
struct InstantaneaWorker;

// Consulta ligera de solo lectura (una tarea del dispatcher): true si alguna
// solicitud visible requiere atencion (solicitudRequiereAtencion) o tiene
// algun paquete visible en Error. Solo lista paquetes de las solicitudes con
// pendientes de descarga (Disponible + Error, por conteo agrupado) y corta en
// el primer hallazgo. nullopt si la lectura falla o se cancela. Sin migracion.
QFuture<std::optional<bool>> consultarAtencionSolicitudes(PersistenceDispatcher& dispatcher,
                                                          SolicitudMasivaRepository& solicitudes,
                                                          PaqueteSolicitudRepository& paquetes);

// true si algun perfil no eliminado esta activo con e.firma no Lista
// (perfilRequiereAtencion), sobre ConsultaPreparacionPerfiles::listarVerificados
// (metadata, sin descifrar). nullopt si la lista falla o se cancela.
QFuture<std::optional<bool>> consultarAtencionPerfiles(ConsultaPreparacionPerfiles& consulta);

// T014.4 D2: mantiene las entradas del estado agregado y emite
// estadoCambiado SOLO cuando el estado calculado cambia (la primera
// publicacion, en iniciar(), siempre se emite).
//
// - alCambiarWorker: fase Ejecutando -> trabajando; Pausado -> pausado. Sin
//   consultas.
// - refrescarSolicitudes/refrescarPerfiles: relanzan la consulta respectiva.
//   Coalescen: con una consulta en curso solo marcan "sucia" y se relanza una
//   vez al terminar. Un resultado nullopt conserva el ultimo valor conocido.
// - detener(): (salida) ignora resultados pendientes y no emite mas.
//
// Hilo grafico. Las consultas son inyectables (pruebas) y de solo lectura:
// no tocan el worker, el ejecutor ni el puerto SAT.
class MonitorEstadoAgregado final : public QObject {
    Q_OBJECT

public:
    using Consulta = std::function<QFuture<std::optional<bool>>()>;

    MonitorEstadoAgregado(Consulta solicitudes, Consulta perfiles, QObject* parent = nullptr);

    // Publica el estado con las entradas actuales y lanza ambas consultas.
    // Idempotente.
    void iniciar();
    void detener();

    EstadoAgregado estado() const noexcept { return calcularEstadoAgregado(m_entradas); }
    const EntradasEstadoAgregado& entradas() const noexcept { return m_entradas; }
    // Consultas en curso (pruebas).
    bool consultando() const noexcept { return m_solicitudes.enCurso || m_perfiles.enCurso; }

public slots:
    void alCambiarWorker(const satcfdi::InstantaneaWorker& instantanea);
    void refrescarSolicitudes();
    void refrescarPerfiles();

signals:
    void estadoCambiado(satcfdi::EstadoAgregado estado);

private:
    struct Fuente {
        Consulta consulta;
        bool EntradasEstadoAgregado::*campo = nullptr;
        bool enCurso = false;
        bool sucia = false;
    };
    void refrescar(Fuente& fuente);
    void lanzar(Fuente& fuente);
    void terminar(Fuente& fuente, std::optional<bool> valor);
    void publicar();

    Fuente m_solicitudes;
    Fuente m_perfiles;
    EntradasEstadoAgregado m_entradas;
    std::optional<EstadoAgregado> m_publicado;
    bool m_iniciado = false;
    bool m_detenido = false;
};

} // namespace satcfdi
