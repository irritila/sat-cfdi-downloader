#include "application/respaldo/ProgramadorRespaldoDiario.h"

namespace satcfdi::respaldo {

using Pendiente = EstadoRespaldoGestor::PendienteDiario;

ProgramadorRespaldoDiario::ProgramadorRespaldoDiario(GestorRespaldo& gestor, Programador& programador,
                                                     RelojUtc reloj, Opciones opciones, QObject* parent)
    : QObject(parent), m_gestor(gestor), m_programador(programador), m_reloj(std::move(reloj)),
      m_opciones(opciones)
{
    // Encolada: evaluar() puede cambiar el pendiente (estadoCambiado) sin
    // reentrar.
    connect(&m_gestor, &GestorRespaldo::estadoCambiado, this, &ProgramadorRespaldoDiario::evaluar,
            Qt::QueuedConnection);
}

ProgramadorRespaldoDiario::~ProgramadorRespaldoDiario()
{
    if (m_id) {
        m_programador.cancelar(*m_id);
    }
}

void ProgramadorRespaldoDiario::programarEn(const QDateTime& instante)
{
    if (m_proxima && *m_proxima == instante && m_id) {
        return;
    }
    if (m_id) {
        m_programador.cancelar(*m_id);
    }
    m_proxima = instante;
    m_id = m_programador.programar(instante, [this] {
        m_id.reset();
        m_proxima.reset();
        evaluar();
    });
}

void ProgramadorRespaldoDiario::evaluar()
{
    const EstadoRespaldoGestor e = m_gestor.estado();
    if (!e.cargado || !e.arranqueCompleto) {
        return; // barrera: hasta que iniciar() termine (incluida la limpieza de .partial)
    }
    if (!e.diarioActivado) {
        if (m_id) {
            m_programador.cancelar(*m_id);
            m_id.reset();
            m_proxima.reset();
        }
        m_gestor.fijarPendienteDiario(Pendiente::Ninguno);
        return;
    }
    if (!e.carpetaConfigurada) {
        m_gestor.fijarPendienteDiario(Pendiente::FaltaCarpeta);
        return;
    }
    if (!e.contrasenaConfigurada) {
        m_gestor.fijarPendienteDiario(Pendiente::FaltaContrasena);
        return;
    }
    m_gestor.fijarPendienteDiario(Pendiente::Ninguno);
    if (e.enCurso) {
        return; // al terminar, estadoCambiado vuelve a evaluar
    }

    const QDateTime ahora = m_reloj().toUTC();
    QDateTime vence = e.ultimoExitoUtc ? e.ultimoExitoUtc->addSecs(m_opciones.intervalo.count()) : ahora;
    const bool ultimoFallo = e.ultimoIntentoUtc && (!e.ultimoExitoUtc || *e.ultimoIntentoUtc > *e.ultimoExitoUtc);
    if (ultimoFallo) {
        const QDateTime reintento = e.ultimoIntentoUtc->addSecs(m_opciones.esperaTrasFallo.count());
        if (reintento > vence) {
            vence = reintento;
        }
    }
    if (vence > ahora) {
        programarEn(vence);
        return;
    }
    if (m_id) {
        m_programador.cancelar(*m_id);
        m_id.reset();
        m_proxima.reset();
    }
    ++m_lanzadas;
    emit respaldoDiarioLanzado();
    (void)m_gestor.respaldarAhora(); // marca enCurso de inmediato (D18)
}

} // namespace satcfdi::respaldo
