#pragma once

// T025.4 (D4, D18): respaldo diario evaluado al arrancar, al despertar el
// sistema y al cambiar el estado del gestor (configuracion o fin de un
// respaldo). Reloj y Programador inyectables (pruebas con FakeReloj y
// FakeProgramador).
//
// Vencido = diario activado y (sin ningun exito o >= `intervalo` desde el
// ultimo exito). Si vencio, lanza UNA ejecucion: el gestor marca enCurso de
// forma sincrona, asi que rafagas de despertares o un respaldo manual en curso
// no duplican. Si falta carpeta o contrasena no intenta nada (solo informa el
// pendiente al gestor para la UI). Tras un intento fallido espera
// `esperaTrasFallo` antes de volver a intentar (no reintenta en bucle). Si no
// vencio, programa la proxima evaluacion para cuando venza.
//
// Hilo grafico.

#include "application/operaciones/Programador.h"
#include "application/persistence/PuertosPersistencia.h"
#include "application/respaldo/GestorRespaldo.h"

#include <QObject>

#include <chrono>
#include <optional>

namespace satcfdi::respaldo {

class ProgramadorRespaldoDiario : public QObject {
    Q_OBJECT

public:
    struct Opciones {
        std::chrono::seconds intervalo = std::chrono::hours(24);
        std::chrono::seconds esperaTrasFallo = std::chrono::hours(1);
    };

    ProgramadorRespaldoDiario(GestorRespaldo& gestor, Programador& programador, RelojUtc reloj, Opciones opciones,
                              QObject* parent = nullptr);
    ~ProgramadorRespaldoDiario() override;

    // Al arrancar (despues de GestorRespaldo::iniciar()), en cada
    // OSIntegration::sistemaDesperto y en cada GestorRespaldo::estadoCambiado.
    void evaluar();

    int ejecucionesLanzadas() const noexcept { return m_lanzadas; }
    std::optional<QDateTime> proximaEvaluacionUtc() const { return m_proxima; }

signals:
    void respaldoDiarioLanzado();

private:
    void programarEn(const QDateTime& instante);

    GestorRespaldo& m_gestor;
    Programador& m_programador;
    RelojUtc m_reloj;
    Opciones m_opciones;
    std::optional<Programador::Id> m_id;
    std::optional<QDateTime> m_proxima;
    int m_lanzadas = 0;
};

} // namespace satcfdi::respaldo
