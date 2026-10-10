#pragma once

// Repositorios y UnitOfWork falsos en memoria (solo tests/unit). Emulan las
// reglas de SQLite que importan a los servicios: transaccion con snapshot y
// rollback, FK de perfil, unicidad de RFC vigente e indice parcial DM6.
// Permiten inyectar fallos por operacion y bloquear listarVisibles().
//
// Hilo: se invocan desde el hilo de PersistenceDispatcher; la prueba solo lee
// el Almacen despues de que el future correspondiente termino.

#include "domain/operaciones/PoliticasOperacion.h"
#include "domain/solicitudes/Duplicados.h"
#include "ports/repositories/ConfiguracionAppRepository.h"
#include "ports/repositories/CredencialSatRepository.h"
#include "ports/repositories/LogSolicitudRepository.h"
#include "ports/repositories/OperacionesSolicitudRepository.h"
#include "ports/repositories/PaqueteSolicitudRepository.h"
#include "ports/repositories/PerfilSatRepository.h"
#include "ports/repositories/SolicitudMasivaRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <QHash>
#include <QMutex>
#include <QSemaphore>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QWaitCondition>

#include <algorithm>
#include <atomic>

namespace fakes {

using namespace satcfdi;

struct Almacen {
    QList<PerfilSat> perfiles;
    QList<SolicitudPersistida> solicitudes;
    QList<PaquetePersistido> paquetes;
    QList<LogPersistido> logs;
    // Fila unica configuracion_app (T004); valores por defecto de la migracion.
    std::optional<ConfiguracionApp> configuracion = ConfiguracionApp{};
    // credencial_sat (T005).
    QList<CredencialSat> credenciales;
    // Racha de fallas de verificacion (T007, migracion 003): clave y contador.
    QHash<SolicitudId, std::pair<QString, int>> rachas;

    // Traza de operaciones ("begin", "commit", "rollback", "insertarCreada", ...).
    QStringList eventos;
    QSet<QThread*> hilos;
    // Fallos inyectados por nombre de operacion.
    QHash<QString, ErrorPersistencia> fallos;

    // Bloqueo de listarVisibles() para la prueba de no bloqueo.
    bool bloquearListar = false;
    std::atomic<bool> listarBloqueado{false};
    QMutex mutex;
    QWaitCondition condicion;
    bool liberado = false;

    void liberar()
    {
        {
            QMutexLocker l(&mutex);
            liberado = true;
            condicion.wakeAll();
        }
        soltar(); // nunca deja una barrera de operacion pendiente
    }

    // Barrera por operacion: la siguiente llamada a registrar(op) se detiene
    // en el hilo del dispatcher hasta soltar(). Sirve para garantizar que la
    // tarea NO ha terminado cuando el servicio encadena su .then(this, ...):
    // si la tarea termina antes, Qt ejecuta la continuacion en el acto (dentro
    // de la llamada al servicio) y una prueba que conecta despues no ve la
    // senal. Se arma antes de invocar el servicio y se suelta despues; el
    // orden soltar()/llegada es indiferente (semaforo). Un solo uso por armado.
    void bloquearEn(const QString& op)
    {
        bloqueoOperacion = op;
    }
    void soltar() { barreraOperacion.release(); }
    bool detenidoEnBarrera() const { return enBarrera.load(); }

    // Snapshot de transaccion.
    struct Estado {
        QList<PerfilSat> perfiles;
        QList<SolicitudPersistida> solicitudes;
        QList<PaquetePersistido> paquetes;
        QList<LogPersistido> logs;
        std::optional<ConfiguracionApp> configuracion;
        QList<CredencialSat> credenciales;
        QHash<SolicitudId, std::pair<QString, int>> rachas;
    };
    std::optional<Estado> snapshot;

    QString bloqueoOperacion; // lo escribe la prueba antes de despachar; lo limpia el dispatcher
    QSemaphore barreraOperacion;
    std::atomic<bool> enBarrera{false};

    std::optional<ErrorPersistencia> registrar(const QString& op)
    {
        if (!bloqueoOperacion.isEmpty() && op == bloqueoOperacion) {
            bloqueoOperacion.clear();
            enBarrera = true;
            barreraOperacion.acquire();
            enBarrera = false;
        }
        eventos.append(op);
        hilos.insert(QThread::currentThread());
        if (fallos.contains(op)) {
            return fallos.value(op);
        }
        return std::nullopt;
    }
};

inline ErrorPersistencia error(ErrorPersistencia::Tipo t, const char* restriccion = "")
{
    return ErrorPersistencia::de(t, QStringLiteral("fallo inyectado"), QString::fromLatin1(restriccion));
}

class FakeUnitOfWork final : public UnitOfWork {
public:
    explicit FakeUnitOfWork(Almacen& a) : m_a(a) {}

    Resultado<Exito, ErrorPersistencia> begin() override
    {
        if (auto e = m_a.registrar(QStringLiteral("begin"))) {
            return Resultado<Exito, ErrorPersistencia>::fallo(*e);
        }
        if (m_a.snapshot) {
            return Resultado<Exito, ErrorPersistencia>::fallo(error(ErrorPersistencia::Tipo::Transaccion));
        }
        m_a.snapshot =
            Almacen::Estado{m_a.perfiles, m_a.solicitudes, m_a.paquetes, m_a.logs, m_a.configuracion,
                           m_a.credenciales, m_a.rachas};
        return Resultado<Exito, ErrorPersistencia>::exito({});
    }

    Resultado<Exito, ErrorPersistencia> commit() override
    {
        if (auto e = m_a.registrar(QStringLiteral("commit"))) {
            return Resultado<Exito, ErrorPersistencia>::fallo(*e);
        }
        if (!m_a.snapshot) {
            return Resultado<Exito, ErrorPersistencia>::fallo(error(ErrorPersistencia::Tipo::Transaccion));
        }
        m_a.snapshot.reset();
        return Resultado<Exito, ErrorPersistencia>::exito({});
    }

    Resultado<Exito, ErrorPersistencia> rollback() override
    {
        m_a.registrar(QStringLiteral("rollback"));
        if (m_a.snapshot) {
            m_a.perfiles = m_a.snapshot->perfiles;
            m_a.solicitudes = m_a.snapshot->solicitudes;
            m_a.paquetes = m_a.snapshot->paquetes;
            m_a.logs = m_a.snapshot->logs;
            m_a.configuracion = m_a.snapshot->configuracion;
            m_a.credenciales = m_a.snapshot->credenciales;
            m_a.rachas = m_a.snapshot->rachas;
            m_a.snapshot.reset();
        }
        return Resultado<Exito, ErrorPersistencia>::exito({});
    }

private:
    Almacen& m_a;
};

class FakePerfiles final : public PerfilSatRepository {
public:
    explicit FakePerfiles(Almacen& a) : m_a(a) {}

    Resultado<PerfilSat, ErrorPersistencia> insertar(const NuevoPerfilSat& n) override
    {
        using R = Resultado<PerfilSat, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("insertarPerfil"))) {
            return R::fallo(*e);
        }
        for (const PerfilSat& p : m_a.perfiles) {
            if (!p.eliminadoEn && p.rfc == n.rfc) {
                return R::fallo(error(ErrorPersistencia::Tipo::Unicidad, "ux_perfil_sat_rfc_vigente"));
            }
        }
        PerfilSat p{n.id, n.rfc, n.nombre, n.activo, n.creadoEn, n.actualizadoEn, std::nullopt};
        m_a.perfiles.append(p);
        return R::exito(p);
    }

    Resultado<std::optional<PerfilSat>, ErrorPersistencia> obtener(const PerfilId& id) override
    {
        using R = Resultado<std::optional<PerfilSat>, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("obtenerPerfil"))) {
            return R::fallo(*e);
        }
        for (const PerfilSat& p : m_a.perfiles) {
            if (p.id == id && !p.eliminadoEn) {
                return R::exito(p);
            }
        }
        return R::exito(std::nullopt);
    }

    Resultado<std::optional<PerfilSat>, ErrorPersistencia> obtenerVigentePorRfc(QStringView rfc) override
    {
        using R = Resultado<std::optional<PerfilSat>, ErrorPersistencia>;
        m_a.registrar(QStringLiteral("obtenerPerfilPorRfc"));
        for (const PerfilSat& p : m_a.perfiles) {
            if (p.rfc == rfc && !p.eliminadoEn) {
                return R::exito(p);
            }
        }
        return R::exito(std::nullopt);
    }

    Resultado<QList<PerfilSat>, ErrorPersistencia> listarActivosVisibles() override
    {
        using R = Resultado<QList<PerfilSat>, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("listarPerfiles"))) {
            return R::fallo(*e);
        }
        QList<PerfilSat> r;
        for (const PerfilSat& p : m_a.perfiles) {
            if (p.activo && !p.eliminadoEn) {
                r.append(p);
            }
        }
        std::sort(r.begin(), r.end(), [](const PerfilSat& x, const PerfilSat& y) { return x.rfc < y.rfc; });
        return R::exito(r);
    }

    // T005.1: activos e inactivos no eliminados, por RFC.
    Resultado<QList<PerfilSat>, ErrorPersistencia> listarVisibles() override
    {
        using R = Resultado<QList<PerfilSat>, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("listarPerfilesVisibles"))) {
            return R::fallo(*e);
        }
        QList<PerfilSat> r;
        for (const PerfilSat& p : m_a.perfiles) {
            if (!p.eliminadoEn) {
                r.append(p);
            }
        }
        std::sort(r.begin(), r.end(), [](const PerfilSat& x, const PerfilSat& y) { return x.rfc < y.rfc; });
        return R::exito(r);
    }

    // T005.1: solo nombre y actualizadoEn; exige transaccion.
    Resultado<std::optional<PerfilSat>, ErrorPersistencia>
    actualizarNombreVisible(const PerfilId& id, const QString& nombre, const QDateTime& en) override
    {
        using R = Resultado<std::optional<PerfilSat>, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("actualizarNombrePerfil"))) {
            return R::fallo(*e);
        }
        if (!m_a.snapshot) {
            return R::fallo(error(ErrorPersistencia::Tipo::Transaccion));
        }
        for (PerfilSat& p : m_a.perfiles) {
            if (p.id == id && !p.eliminadoEn) {
                p.nombre = nombre;
                p.actualizadoEn = en;
                return R::exito(p);
            }
        }
        return R::exito(std::nullopt);
    }

private:
    Almacen& m_a;
};

// Predicado del indice parcial DM6.
inline bool esBloqueanteDm6(const SolicitudPersistida& s)
{
    if (s.eliminadoEn) {
        return false;
    }
    if (s.estadoLocal == EstadoLocal::Creada || s.estadoLocal == EstadoLocal::Enviando) {
        return true;
    }
    return s.estadoLocal == EstadoLocal::Enviada
           && (!s.estadoSolicitudSat || *s.estadoSolicitudSat == EstadoSolicitudSat::Aceptada
               || *s.estadoSolicitudSat == EstadoSolicitudSat::EnProceso);
}

class FakeSolicitudes final : public SolicitudMasivaRepository {
public:
    explicit FakeSolicitudes(Almacen& a) : m_a(a) {}

    Resultado<QList<SolicitudPersistida>, ErrorPersistencia> listarVisibles() override
    {
        using R = Resultado<QList<SolicitudPersistida>, ErrorPersistencia>;
        if (m_a.bloquearListar) {
            QMutexLocker l(&m_a.mutex);
            m_a.listarBloqueado = true;
            while (!m_a.liberado) {
                m_a.condicion.wait(&m_a.mutex);
            }
        }
        if (auto e = m_a.registrar(QStringLiteral("listarSolicitudes"))) {
            return R::fallo(*e);
        }
        QList<SolicitudPersistida> r;
        for (const SolicitudPersistida& s : m_a.solicitudes) {
            if (!s.eliminadoEn) {
                r.append(s);
            }
        }
        std::sort(r.begin(), r.end(), [](const SolicitudPersistida& x, const SolicitudPersistida& y) {
            return x.creadaEn > y.creadaEn;
        });
        return R::exito(r);
    }

    Resultado<std::optional<SolicitudPersistida>, ErrorPersistencia> obtenerVisible(const SolicitudId& id) override
    {
        using R = Resultado<std::optional<SolicitudPersistida>, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("obtenerSolicitud"))) {
            return R::fallo(*e);
        }
        for (const SolicitudPersistida& s : m_a.solicitudes) {
            if (s.id == id && !s.eliminadoEn) {
                return R::exito(s);
            }
        }
        return R::exito(std::nullopt);
    }

    Resultado<EvaluacionDuplicado, ErrorPersistencia> clasificarDuplicado(const DedupKey& clave) override
    {
        using R = Resultado<EvaluacionDuplicado, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("clasificarDuplicado"))) {
            return R::fallo(*e);
        }
        QList<CoincidenciaDuplicado> coincidencias;
        for (const SolicitudPersistida& s : m_a.solicitudes) {
            if (s.dedupKey != clave) {
                continue;
            }
            CoincidenciaDuplicado c;
            c.id = s.id;
            c.estadoLocal = s.estadoLocal;
            c.estadoSat = s.estadoSolicitudSat;
            c.eliminada = s.eliminadoEn.has_value();
            for (const PaquetePersistido& p : m_a.paquetes) {
                if (p.solicitudMasivaId == s.id && !p.eliminadoEn) {
                    c.paquetesNoEliminados.append(p.estadoDescarga);
                }
            }
            coincidencias.append(c);
        }
        return R::exito(clasificarCoincidencias(clave, coincidencias));
    }

    Resultado<Exito, ErrorPersistencia> insertarCreada(const SolicitudNuevaPersistida& n) override
    {
        using R = Resultado<Exito, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("insertarCreada"))) {
            return R::fallo(*e);
        }
        const bool perfilExiste = std::any_of(m_a.perfiles.cbegin(), m_a.perfiles.cend(),
                                              [&](const PerfilSat& p) { return p.id == n.perfilSatId; });
        if (!perfilExiste) {
            return R::fallo(error(ErrorPersistencia::Tipo::Integridad, "fk_perfil"));
        }
        for (const SolicitudPersistida& s : m_a.solicitudes) {
            if (s.dedupKey == n.canonica.dedupKey() && esBloqueanteDm6(s)) {
                return R::fallo(error(ErrorPersistencia::Tipo::DedupBloqueado,
                                      "ux_solicitud_masiva_dedup_bloqueante"));
            }
        }
        const SolicitudCanonica& c = n.canonica;
        SolicitudPersistida s;
        s.id = n.id;
        s.perfilSatId = n.perfilSatId;
        s.tipoCfdi = c.tipoDescarga();
        s.operacionSat = c.operacionSat();
        s.rfcSolicitante = c.rfcSolicitante();
        s.rfcEmisor = c.rfcEmisor();
        s.rfcReceptor = c.rfcReceptor();
        s.rfcReceptores = c.rfcReceptores();
        s.fechaInicialSat = c.fechaInicialSat();
        s.fechaFinalSat = c.fechaFinalSat();
        s.tipoComprobante = c.tipoComprobante();
        s.complemento = c.complemento();
        s.dedupKey = c.dedupKey();
        s.estadoLocal = EstadoLocal::Creada;
        s.creadaEn = n.creadaEn;
        m_a.solicitudes.append(s);
        return R::exito({});
    }

    Resultado<bool, ErrorPersistencia> marcarEliminadaVisible(const SolicitudId& id, const QDateTime& en) override
    {
        using R = Resultado<bool, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("marcarEliminada"))) {
            return R::fallo(*e);
        }
        for (SolicitudPersistida& s : m_a.solicitudes) {
            if (s.id == id && !s.eliminadoEn) {
                s.eliminadoEn = en;
                return R::exito(true);
            }
        }
        return R::exito(false);
    }

private:
    Almacen& m_a;
};

class FakePaquetes final : public PaqueteSolicitudRepository {
public:
    explicit FakePaquetes(Almacen& a) : m_a(a) {}

    Resultado<QList<PaquetePersistido>, ErrorPersistencia>
    listarVisiblesPorSolicitud(const SolicitudId& id) override
    {
        m_a.registrar(QStringLiteral("listarPaquetes"));
        QList<PaquetePersistido> r;
        for (const PaquetePersistido& p : m_a.paquetes) {
            if (p.solicitudMasivaId == id && !p.eliminadoEn) {
                r.append(p);
            }
        }
        return Resultado<QList<PaquetePersistido>, ErrorPersistencia>::exito(r);
    }

    Resultado<QHash<SolicitudId, int>, ErrorPersistencia> contarVisiblesPorSolicitud() override
    {
        m_a.registrar(QStringLiteral("contarPaquetes"));
        QHash<SolicitudId, int> r;
        for (const PaquetePersistido& p : m_a.paquetes) {
            if (!p.eliminadoEn) {
                ++r[p.solicitudMasivaId];
            }
        }
        return Resultado<QHash<SolicitudId, int>, ErrorPersistencia>::exito(r);
    }

    Resultado<QHash<SolicitudId, ConteoPaquetes>, ErrorPersistencia> contarVisiblesPorSolicitudYEstado() override
    {
        using R = Resultado<QHash<SolicitudId, ConteoPaquetes>, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("contarPaquetesPorEstado"))) {
            return R::fallo(*e);
        }
        QHash<SolicitudId, ConteoPaquetes> r;
        for (const PaquetePersistido& p : m_a.paquetes) {
            if (p.eliminadoEn) {
                continue;
            }
            ConteoPaquetes& c = r[p.solicitudMasivaId];
            ++c.total;
            if (p.estadoDescarga == EstadoDescarga::Descargado) {
                ++c.descargados;
            } else if (p.estadoDescarga == EstadoDescarga::Disponible || p.estadoDescarga == EstadoDescarga::Error) {
                ++c.pendientesDescarga;
            }
        }
        return R::exito(r);
    }

    Resultado<int, ErrorPersistencia> marcarEliminadosPorSolicitud(const SolicitudId& id,
                                                                   const QDateTime& en) override
    {
        if (auto e = m_a.registrar(QStringLiteral("marcarPaquetes"))) {
            return Resultado<int, ErrorPersistencia>::fallo(*e);
        }
        int n = 0;
        for (PaquetePersistido& p : m_a.paquetes) {
            if (p.solicitudMasivaId == id && !p.eliminadoEn) {
                p.eliminadoEn = en;
                ++n;
            }
        }
        return Resultado<int, ErrorPersistencia>::exito(n);
    }

private:
    Almacen& m_a;
};

class FakeLogs final : public LogSolicitudRepository {
public:
    explicit FakeLogs(Almacen& a) : m_a(a) {}

    Resultado<QList<LogPersistido>, ErrorPersistencia> listarVisiblesPorSolicitud(const SolicitudId& id) override
    {
        m_a.registrar(QStringLiteral("listarLogs"));
        QList<LogPersistido> r;
        for (const LogPersistido& l : m_a.logs) {
            if (l.solicitudMasivaId == id && !l.eliminadoEn) {
                r.append(l);
            }
        }
        return Resultado<QList<LogPersistido>, ErrorPersistencia>::exito(r);
    }

    Resultado<Exito, ErrorPersistencia> agregar(const LogEntradaSaneada& e) override
    {
        if (auto f = m_a.registrar(QStringLiteral("agregarLog"))) {
            return Resultado<Exito, ErrorPersistencia>::fallo(*f);
        }
        LogPersistido l;
        static_cast<LogEntradaSaneada&>(l) = e;
        m_a.logs.append(l);
        return Resultado<Exito, ErrorPersistencia>::exito({});
    }

    Resultado<int, ErrorPersistencia> marcarEliminadosPorSolicitud(const SolicitudId& id,
                                                                   const QDateTime& en) override
    {
        if (auto e = m_a.registrar(QStringLiteral("marcarLogs"))) {
            return Resultado<int, ErrorPersistencia>::fallo(*e);
        }
        int n = 0;
        for (LogPersistido& l : m_a.logs) {
            if (l.solicitudMasivaId == id && !l.eliminadoEn) {
                l.eliminadoEn = en;
                ++n;
            }
        }
        return Resultado<int, ErrorPersistencia>::exito(n);
    }

private:
    Almacen& m_a;
};

// configuracion_app en memoria (T004). Las escrituras exigen transaccion
// activa (snapshot), como SqliteConfiguracionAppRepository.
class FakeConfiguracion final : public ConfiguracionAppRepository {
public:
    explicit FakeConfiguracion(Almacen& a) : m_a(a) {}

    Resultado<ConfiguracionApp, ErrorPersistencia> obtener() override
    {
        using R = Resultado<ConfiguracionApp, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("obtenerConfiguracion"))) {
            return R::fallo(*e);
        }
        if (!m_a.configuracion) {
            return R::fallo(error(ErrorPersistencia::Tipo::NoEncontrado));
        }
        return R::exito(*m_a.configuracion);
    }

    Resultado<ConfiguracionApp, ErrorPersistencia> actualizarInicioAutomatico(bool v, const QDateTime& en) override
    {
        return escribir(QStringLiteral("actualizarInicioAutomatico"), en,
                        [v](ConfiguracionApp& c) { c.inicioAutomaticoHabilitado = v; });
    }

    Resultado<ConfiguracionApp, ErrorPersistencia> actualizarMonitoreoPausado(bool v, const QDateTime& en) override
    {
        return escribir(QStringLiteral("actualizarMonitoreoPausado"), en,
                        [v](ConfiguracionApp& c) { c.monitoreoPausado = v; });
    }

    Resultado<ConfiguracionApp, ErrorPersistencia> registrarUltimoCierre(const QDateTime& en) override
    {
        return escribir(QStringLiteral("registrarUltimoCierre"), en,
                        [en](ConfiguracionApp& c) { c.ultimoCierreEn = en; });
    }

private:
    template <typename F>
    Resultado<ConfiguracionApp, ErrorPersistencia> escribir(const QString& op, const QDateTime& en, F aplicar)
    {
        using R = Resultado<ConfiguracionApp, ErrorPersistencia>;
        if (auto e = m_a.registrar(op)) {
            return R::fallo(*e);
        }
        if (!m_a.snapshot) {
            return R::fallo(error(ErrorPersistencia::Tipo::Transaccion));
        }
        if (!m_a.configuracion) {
            return R::fallo(error(ErrorPersistencia::Tipo::NoEncontrado));
        }
        aplicar(*m_a.configuracion);
        m_a.configuracion->actualizadaEn = en;
        return R::exito(*m_a.configuracion);
    }

    Almacen& m_a;
};

// credencial_sat en memoria (T005): escrituras exigen transaccion, FK de
// perfil, unicidad por perfil, metadata completa; listarReferenciasVigentes
// es fail-safe ante referencias ilegibles. Operaciones: "insertarCredencial",
// "reemplazarCredencial", "obtenerCredencial", "eliminarCredencial",
// "listarReferencias".
class FakeCredenciales final : public CredencialSatRepository {
public:
    explicit FakeCredenciales(Almacen& a) : m_a(a) {}

    Resultado<Exito, ErrorPersistencia> insertar(const CredencialSat& c) override
    {
        using R = Resultado<Exito, ErrorPersistencia>;
        if (auto e = escritura(QStringLiteral("insertarCredencial"), c)) {
            return R::fallo(*e);
        }
        const bool perfilExiste = std::any_of(m_a.perfiles.cbegin(), m_a.perfiles.cend(),
                                              [&](const PerfilSat& p) { return p.id == c.perfilSatId; });
        if (!perfilExiste) {
            return R::fallo(error(ErrorPersistencia::Tipo::Integridad, "foreign_key"));
        }
        for (const CredencialSat& x : m_a.credenciales) {
            if (x.perfilSatId == c.perfilSatId) {
                return R::fallo(error(ErrorPersistencia::Tipo::Unicidad, "ux_credencial_sat_perfil"));
            }
        }
        m_a.credenciales.append(c);
        return R::exito({});
    }

    Resultado<Exito, ErrorPersistencia> reemplazar(const PerfilId& perfilId, const CredencialSat& n) override
    {
        using R = Resultado<Exito, ErrorPersistencia>;
        if (auto e = escritura(QStringLiteral("reemplazarCredencial"), n)) {
            return R::fallo(*e);
        }
        for (CredencialSat& x : m_a.credenciales) {
            if (x.perfilSatId == perfilId) {
                const QString id = x.id;
                const QDateTime registrada = x.registradaEn;
                x = n;
                x.id = id;
                x.perfilSatId = perfilId;
                x.registradaEn = registrada;
                return R::exito({});
            }
        }
        return R::fallo(error(ErrorPersistencia::Tipo::NoEncontrado));
    }

    Resultado<std::optional<CredencialSat>, ErrorPersistencia> obtenerPorPerfil(const PerfilId& perfilId) override
    {
        using R = Resultado<std::optional<CredencialSat>, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("obtenerCredencial"))) {
            return R::fallo(*e);
        }
        for (const CredencialSat& x : m_a.credenciales) {
            if (x.perfilSatId == perfilId) {
                return R::exito(x);
            }
        }
        return R::exito(std::nullopt);
    }

    Resultado<bool, ErrorPersistencia> eliminarPorPerfil(const PerfilId& perfilId) override
    {
        using R = Resultado<bool, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("eliminarCredencial"))) {
            return R::fallo(*e);
        }
        if (!m_a.snapshot) {
            return R::fallo(error(ErrorPersistencia::Tipo::Transaccion));
        }
        const auto n = m_a.credenciales.removeIf([&](const CredencialSat& x) { return x.perfilSatId == perfilId; });
        return R::exito(n > 0);
    }

    Resultado<QList<CredencialRef>, ErrorPersistencia> listarReferenciasVigentes() override
    {
        using R = Resultado<QList<CredencialRef>, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("listarReferencias"))) {
            return R::fallo(*e);
        }
        QList<CredencialRef> refs;
        for (const CredencialSat& x : m_a.credenciales) {
            const auto r = CredencialRef::desdeReferencias(x.certificadoRef, x.llavePrivadaRef, x.contrasenaRef);
            if (!r) {
                return R::fallo(error(ErrorPersistencia::Tipo::Interno));
            }
            refs.append(*r);
        }
        return R::exito(refs);
    }

private:
    std::optional<ErrorPersistencia> escritura(const QString& op, const CredencialSat& c)
    {
        if (auto e = m_a.registrar(op)) {
            return e;
        }
        if (!m_a.snapshot) {
            return error(ErrorPersistencia::Tipo::Transaccion);
        }
        if (!c.metadataCompleta()) {
            return error(ErrorPersistencia::Tipo::Integridad, "credencial_sat.metadata");
        }
        return std::nullopt;
    }

    Almacen& m_a;
};

// Repositorio de operaciones del ejecutor/worker en memoria (T007). Emula el
// contrato de OperacionesSolicitudRepository: escrituras con transaccion,
// revalidacion de eliminado_en y del estado de origen (false sin cambios si
// no aplica), limpieza condicional de intenciones (D13), paquetes nuevos sin
// duplicar ni desplazar su vencimiento (D10) y racha de fallas (migracion 003).
// Operaciones registradas con el nombre del metodo (fallos inyectables).
class FakeOperacionesSolicitud final : public OperacionesSolicitudRepository {
public:
    explicit FakeOperacionesSolicitud(Almacen& a) : m_a(a) {}

    Resultado<QList<PerfilId>, ErrorPersistencia> listarPerfilesConTrabajo(const QDateTime& ahora) override
    {
        using R = Resultado<QList<PerfilId>, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("listarPerfilesConTrabajo"))) {
            return R::fallo(*e);
        }
        QList<PerfilId> r;
        auto agregar = [&](const PerfilId& p) {
            if (!r.contains(p)) {
                r.append(p);
            }
        };
        for (const SolicitudPersistida& s : m_a.solicitudes) {
            if (s.eliminadoEn) {
                continue;
            }
            if (verificacionDebida(s, ahora) || s.verificacionPendiente || s.descargaPendiente) {
                agregar(s.perfilSatId);
            }
        }
        for (const PaquetePersistido& p : m_a.paquetes) {
            const SolicitudPersistida* s = solicitud(p.solicitudMasivaId);
            if (s && !p.eliminadoEn && p.estadoDescarga == EstadoDescarga::Disponible
                && s->estadoSolicitudSat == EstadoSolicitudSat::Terminada) {
                agregar(s->perfilSatId);
            }
        }
        return R::exito(r);
    }

    Resultado<QList<PaqueteDescargable>, ErrorPersistencia> listarVencimientosEstimados(const QDateTime& ahora,
                                                                                        int limite) override
    {
        using R = Resultado<QList<PaqueteDescargable>, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("listarVencimientosEstimados"))) {
            return R::fallo(*e);
        }
        QList<PaqueteDescargable> r;
        for (const PaquetePersistido& p : m_a.paquetes) {
            if (politicas::vencimientoEstimadoAlcanzado(p, ahora) && solicitud(p.solicitudMasivaId)) {
                r.append(descargable(p));
            }
        }
        std::sort(r.begin(), r.end(), [](const PaqueteDescargable& x, const PaqueteDescargable& y) {
            return *x.paquete.vencimientoEstimadoEn < *y.paquete.vencimientoEstimadoEn;
        });
        return R::exito(r.mid(0, limite));
    }

    Resultado<QList<IntencionPendiente>, ErrorPersistencia> listarIntencionesPendientes(const QList<PerfilId>& perfiles,
                                                                                       int limite) override
    {
        using R = Resultado<QList<IntencionPendiente>, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("listarIntencionesPendientes"))) {
            return R::fallo(*e);
        }
        QList<IntencionPendiente> r;
        for (const SolicitudPersistida& s : m_a.solicitudes) {
            if (!s.eliminadoEn && s.accionPendienteEn && perfiles.contains(s.perfilSatId)) {
                r.append(IntencionPendiente{s.id, s.perfilSatId, s.verificacionPendiente, s.descargaPendiente,
                                            *s.accionPendienteEn});
            }
        }
        std::sort(r.begin(), r.end(), [](const IntencionPendiente& x, const IntencionPendiente& y) {
            return x.accionPendienteEn < y.accionPendienteEn;
        });
        return R::exito(r.mid(0, limite));
    }

    Resultado<QList<SolicitudPersistida>, ErrorPersistencia>
    listarVerificacionesDebidas(const QList<PerfilId>& perfiles, const QDateTime& ahora, int limite) override
    {
        using R = Resultado<QList<SolicitudPersistida>, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("listarVerificacionesDebidas"))) {
            return R::fallo(*e);
        }
        QList<SolicitudPersistida> r;
        for (const SolicitudPersistida& s : m_a.solicitudes) {
            if (!s.eliminadoEn && perfiles.contains(s.perfilSatId) && verificacionDebida(s, ahora)) {
                r.append(s);
            }
        }
        std::sort(r.begin(), r.end(), [](const SolicitudPersistida& x, const SolicitudPersistida& y) {
            return *x.siguienteVerificacionEn < *y.siguienteVerificacionEn;
        });
        return R::exito(r.mid(0, limite));
    }

    Resultado<QList<PaqueteDescargable>, ErrorPersistencia> listarDescargasAutomaticas(const QList<PerfilId>& perfiles,
                                                                                       int limite) override
    {
        using R = Resultado<QList<PaqueteDescargable>, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("listarDescargasAutomaticas"))) {
            return R::fallo(*e);
        }
        QList<PaqueteDescargable> r;
        for (const PaquetePersistido& p : m_a.paquetes) {
            const SolicitudPersistida* s = solicitud(p.solicitudMasivaId);
            if (s && !p.eliminadoEn && p.estadoDescarga == EstadoDescarga::Disponible
                && s->estadoSolicitudSat == EstadoSolicitudSat::Terminada && perfiles.contains(s->perfilSatId)) {
                r.append(descargable(p));
            }
        }
        std::sort(r.begin(), r.end(), [](const PaqueteDescargable& x, const PaqueteDescargable& y) {
            return x.paquete.disponibleEn < y.paquete.disponibleEn;
        });
        return R::exito(r.mid(0, limite));
    }

    Resultado<std::optional<PaqueteDescargable>, ErrorPersistencia> obtenerPaquete(const QString& paqueteId) override
    {
        using R = Resultado<std::optional<PaqueteDescargable>, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("obtenerPaquete"))) {
            return R::fallo(*e);
        }
        const PaquetePersistido* p = paquete(paqueteId);
        if (!p || !solicitud(p->solicitudMasivaId)) {
            return R::exito(std::nullopt);
        }
        return R::exito(descargable(*p));
    }

    Resultado<QList<PaqueteDescargable>, ErrorPersistencia> listarPaquetesReintentables(const SolicitudId& id) override
    {
        using R = Resultado<QList<PaqueteDescargable>, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("listarPaquetesReintentables"))) {
            return R::fallo(*e);
        }
        QList<PaqueteDescargable> r;
        if (solicitud(id)) {
            for (const PaquetePersistido& p : m_a.paquetes) {
                if (p.solicitudMasivaId == id && !p.eliminadoEn
                    && (p.estadoDescarga == EstadoDescarga::Disponible || p.estadoDescarga == EstadoDescarga::Error)) {
                    r.append(descargable(p));
                }
            }
        }
        return R::exito(r);
    }

    Resultado<TrabajoInterrumpido, ErrorPersistencia> listarInterrumpidos() override
    {
        using R = Resultado<TrabajoInterrumpido, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("listarInterrumpidos"))) {
            return R::fallo(*e);
        }
        TrabajoInterrumpido t;
        for (const SolicitudPersistida& s : m_a.solicitudes) {
            if (!s.eliminadoEn && s.estadoLocal == EstadoLocal::Enviando) {
                t.solicitudesEnviando.append(s.id);
            }
        }
        for (const PaquetePersistido& p : m_a.paquetes) {
            if (!p.eliminadoEn && p.estadoDescarga == EstadoDescarga::Descargando && solicitud(p.solicitudMasivaId)) {
                t.paquetesDescargando.append(descargable(p));
            }
        }
        return R::exito(t);
    }

    Resultado<std::optional<RachaVerificacion>, ErrorPersistencia> leerRachaVerificacion(const SolicitudId& id) override
    {
        using R = Resultado<std::optional<RachaVerificacion>, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("leerRachaVerificacion"))) {
            return R::fallo(*e);
        }
        const SolicitudPersistida* s = solicitud(id);
        if (!s) {
            return R::exito(std::nullopt);
        }
        RachaVerificacion r;
        r.verificacionesSinCambio = s->verificacionesSinCambio;
        if (m_a.rachas.contains(id)) {
            r.ultimaClaveFalla = m_a.rachas.value(id).first;
            r.fallasIguales = m_a.rachas.value(id).second;
        }
        return R::exito(r);
    }

    Resultado<bool, ErrorPersistencia> marcarEnviando(const SolicitudId& id, const QDateTime& ahora) override
    {
        return escribir(QStringLiteral("marcarEnviando"), [&]() {
            SolicitudPersistida* s = solicitudMutable(id);
            if (!s || s->estadoLocal != EstadoLocal::Creada) {
                return false;
            }
            s->estadoLocal = EstadoLocal::Enviando;
            s->envioIniciadoEn = ahora;
            return true;
        });
    }

    Resultado<bool, ErrorPersistencia> aplicarEnvio(const AplicacionEnvio& a) override
    {
        return escribir(QStringLiteral("aplicarEnvio"), [&]() {
            SolicitudPersistida* s = solicitudMutable(a.solicitudId);
            if (!s || s->estadoLocal != EstadoLocal::Enviando) {
                return false;
            }
            s->estadoLocal = a.destino;
            s->ultimoError = a.ultimoError;
            if (a.destino == EstadoLocal::Creada) {
                s->envioIniciadoEn.reset();
                s->codEstatusSolicitud.reset();
                s->mensajeSolicitudSat.reset();
            } else {
                s->codEstatusSolicitud = a.codEstatus;
                s->mensajeSolicitudSat = a.mensaje;
            }
            if (a.destino == EstadoLocal::Enviada) {
                s->idSolicitudSat = a.idSolicitudSat;
                s->enviadaEn = a.enviadaEn;
                s->siguienteVerificacionEn = a.siguienteVerificacionEn;
            }
            return true;
        });
    }

    Resultado<ResultadoAplicacionVerificacion, ErrorPersistencia> aplicarVerificacion(const AplicacionVerificacion& a) override
    {
        using R = Resultado<ResultadoAplicacionVerificacion, ErrorPersistencia>;
        if (auto e = m_a.registrar(QStringLiteral("aplicarVerificacion"))) {
            return R::fallo(*e);
        }
        if (!m_a.snapshot) {
            return R::fallo(error(ErrorPersistencia::Tipo::Transaccion));
        }
        ResultadoAplicacionVerificacion r;
        SolicitudPersistida* s = solicitudMutable(a.solicitudId);
        if (!s || s->estadoLocal != EstadoLocal::Enviada) {
            return R::exito(r);
        }
        r.aplicada = true;
        s->estadoSolicitudSat = a.estadoSolicitudSat;
        s->codigoEstadoSolicitud = a.codigoEstadoSolicitud;
        s->mensajeVerificacionSat = a.mensajeVerificacion;
        s->numeroCfdi = a.numeroCfdi;
        s->ultimaVerificacionEn = a.verificadaEn;
        s->siguienteVerificacionEn = a.siguienteVerificacionEn;
        s->verificacionesSinCambio = a.verificacionesSinCambio;
        s->ultimoError.reset();
        m_a.rachas.remove(a.solicitudId);
        for (const PaqueteNuevo& n : a.paquetesNuevos) {
            const bool existe = std::any_of(m_a.paquetes.cbegin(), m_a.paquetes.cend(), [&](const PaquetePersistido& p) {
                return p.solicitudMasivaId == a.solicitudId && p.idPaqueteSat == n.idPaqueteSat;
            });
            if (existe) {
                continue;
            }
            PaquetePersistido p;
            p.id = n.id;
            p.solicitudMasivaId = a.solicitudId;
            p.idPaqueteSat = n.idPaqueteSat;
            p.disponibleEn = n.disponibleEn;
            p.vencimientoEstimadoEn = n.vencimientoEstimadoEn;
            m_a.paquetes.append(p);
            r.paquetesInsertados.append(n.id);
        }
        if (a.vencerNoDescargados) {
            for (PaquetePersistido& p : m_a.paquetes) {
                if (p.solicitudMasivaId == a.solicitudId && !p.eliminadoEn
                    && p.estadoDescarga != EstadoDescarga::Descargado && p.estadoDescarga != EstadoDescarga::Vencido) {
                    p.estadoDescarga = EstadoDescarga::Vencido;
                    p.vencidoEn = a.verificadaEn;
                    p.motivoVencimiento = MotivoVencimiento::SolicitudExpirada;
                    p.origenVencimiento = OrigenVencimiento::Sat;
                    r.paquetesVencidos.append(p.id);
                }
            }
        }
        return R::exito(r);
    }

    Resultado<bool, ErrorPersistencia> aplicarFallaVerificacion(const AplicacionFallaVerificacion& a) override
    {
        return escribir(QStringLiteral("aplicarFallaVerificacion"), [&]() {
            SolicitudPersistida* s = solicitudMutable(a.solicitudId);
            if (!s || s->estadoLocal != EstadoLocal::Enviada) {
                return false;
            }
            s->ultimoError = a.ultimoError;
            s->siguienteVerificacionEn = a.siguienteVerificacionEn;
            m_a.rachas.insert(a.solicitudId, {a.claveFalla, a.fallasIguales});
            return true;
        });
    }

    Resultado<bool, ErrorPersistencia> marcarDescargando(const QString& paqueteId, const QDateTime& ahora,
                                                         bool permitirError) override
    {
        return escribir(QStringLiteral("marcarDescargando"), [&]() {
            PaquetePersistido* p = paqueteMutable(paqueteId);
            if (!p || !solicitud(p->solicitudMasivaId)) {
                return false;
            }
            const bool desde = p->estadoDescarga == EstadoDescarga::Disponible
                               || (permitirError && p->estadoDescarga == EstadoDescarga::Error);
            if (!desde) {
                return false;
            }
            p->estadoDescarga = EstadoDescarga::Descargando;
            p->descargaIniciadaEn = ahora;
            return true;
        });
    }

    Resultado<bool, ErrorPersistencia> aplicarDescarga(const AplicacionDescarga& a) override
    {
        return escribir(QStringLiteral("aplicarDescarga"), [&]() {
            PaquetePersistido* p = paqueteMutable(a.paqueteId);
            if (!p || !solicitud(p->solicitudMasivaId) || p->estadoDescarga != EstadoDescarga::Descargando) {
                return false;
            }
            p->estadoDescarga = a.destino;
            p->codigoDescargaSat = a.codigoDescargaSat;
            p->mensajeDescargaSat = a.mensajeDescargaSat;
            p->ultimoError = a.ultimoError;
            if (a.destino == EstadoDescarga::Descargado) {
                p->rutaLocal = a.rutaFinal;
                p->descargadoEn = a.aplicadaEn;
            }
            if (a.destino == EstadoDescarga::Vencido) {
                p->vencidoEn = a.aplicadaEn;
                p->motivoVencimiento = a.motivoVencimiento;
                p->origenVencimiento = a.origenVencimiento;
            }
            if (a.destino == EstadoDescarga::Disponible) {
                p->descargaIniciadaEn.reset();
            }
            if (a.reconciliado) {
                p->reconciliadoEn = a.aplicadaEn;
            }
            return true;
        });
    }

    Resultado<bool, ErrorPersistencia> vencerPaqueteEstimado(const QString& paqueteId, const QDateTime& ahora) override
    {
        return escribir(QStringLiteral("vencerPaqueteEstimado"), [&]() {
            PaquetePersistido* p = paqueteMutable(paqueteId);
            if (!p || !solicitud(p->solicitudMasivaId) || !politicas::vencimientoEstimadoAlcanzado(*p, ahora)) {
                return false;
            }
            p->estadoDescarga = EstadoDescarga::Vencido;
            p->vencidoEn = ahora;
            p->motivoVencimiento = MotivoVencimiento::VencimientoEstimado;
            p->origenVencimiento = OrigenVencimiento::EstimacionLocal;
            return true;
        });
    }

    Resultado<bool, ErrorPersistencia> registrarIntencion(const SolicitudId& id, TipoIntencion tipo,
                                                          const QDateTime& ahora) override
    {
        return escribir(QStringLiteral("registrarIntencion"), [&]() {
            SolicitudPersistida* s = solicitudMutable(id);
            if (!s) {
                return false;
            }
            (tipo == TipoIntencion::Verificacion ? s->verificacionPendiente : s->descargaPendiente) = true;
            s->accionPendienteEn = ahora;
            return true;
        });
    }

    Resultado<bool, ErrorPersistencia> consumirIntencion(const SolicitudId& id, TipoIntencion tipo,
                                                         const QDateTime& capturadaEn) override
    {
        return escribir(QStringLiteral("consumirIntencion"), [&]() {
            SolicitudPersistida* s = solicitudMutable(id);
            if (!s || s->accionPendienteEn != std::optional<QDateTime>(capturadaEn)) {
                return false;
            }
            (tipo == TipoIntencion::Verificacion ? s->verificacionPendiente : s->descargaPendiente) = false;
            if (!s->verificacionPendiente && !s->descargaPendiente) {
                s->accionPendienteEn.reset();
            }
            return true;
        });
    }

private:
    static bool verificacionDebida(const SolicitudPersistida& s, const QDateTime& ahora)
    {
        const bool verificable = !s.estadoSolicitudSat || *s.estadoSolicitudSat == EstadoSolicitudSat::Aceptada
                                 || *s.estadoSolicitudSat == EstadoSolicitudSat::EnProceso;
        return s.estadoLocal == EstadoLocal::Enviada && verificable && s.siguienteVerificacionEn
               && *s.siguienteVerificacionEn <= ahora;
    }

    const SolicitudPersistida* solicitud(const SolicitudId& id) const
    {
        for (const SolicitudPersistida& s : m_a.solicitudes) {
            if (s.id == id && !s.eliminadoEn) {
                return &s;
            }
        }
        return nullptr;
    }
    SolicitudPersistida* solicitudMutable(const SolicitudId& id)
    {
        return const_cast<SolicitudPersistida*>(solicitud(id));
    }
    const PaquetePersistido* paquete(const QString& id) const
    {
        for (const PaquetePersistido& p : m_a.paquetes) {
            if (p.id == id && !p.eliminadoEn) {
                return &p;
            }
        }
        return nullptr;
    }
    PaquetePersistido* paqueteMutable(const QString& id) { return const_cast<PaquetePersistido*>(paquete(id)); }

    PaqueteDescargable descargable(const PaquetePersistido& p) const
    {
        const SolicitudPersistida* s = solicitud(p.solicitudMasivaId);
        return PaqueteDescargable{p, s ? s->perfilSatId : PerfilId(), s ? s->rfcSolicitante : QString(),
                                  s && s->idSolicitudSat ? *s->idSolicitudSat : QString()};
    }

    template <typename F>
    Resultado<bool, ErrorPersistencia> escribir(const QString& op, F aplicar)
    {
        using R = Resultado<bool, ErrorPersistencia>;
        if (auto e = m_a.registrar(op)) {
            return R::fallo(*e);
        }
        if (!m_a.snapshot) {
            return R::fallo(error(ErrorPersistencia::Tipo::Transaccion));
        }
        return R::exito(aplicar());
    }

    Almacen& m_a;
};

} // namespace fakes
