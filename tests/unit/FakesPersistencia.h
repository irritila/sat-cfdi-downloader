#pragma once

// Repositorios y UnitOfWork falsos en memoria (solo tests/unit). Emulan las
// reglas de SQLite que importan a los servicios: transaccion con snapshot y
// rollback, FK de perfil, unicidad de RFC vigente e indice parcial DM6.
// Permiten inyectar fallos por operacion y bloquear listarVisibles().
//
// Hilo: se invocan desde el hilo de PersistenceDispatcher; la prueba solo lee
// el Almacen despues de que el future correspondiente termino.

#include "domain/solicitudes/Duplicados.h"
#include "ports/repositories/ConfiguracionAppRepository.h"
#include "ports/repositories/CredencialSatRepository.h"
#include "ports/repositories/LogSolicitudRepository.h"
#include "ports/repositories/PaqueteSolicitudRepository.h"
#include "ports/repositories/PerfilSatRepository.h"
#include "ports/repositories/SolicitudMasivaRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <QHash>
#include <QMutex>
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
        QMutexLocker l(&mutex);
        liberado = true;
        condicion.wakeAll();
    }

    // Snapshot de transaccion.
    struct Estado {
        QList<PerfilSat> perfiles;
        QList<SolicitudPersistida> solicitudes;
        QList<PaquetePersistido> paquetes;
        QList<LogPersistido> logs;
        std::optional<ConfiguracionApp> configuracion;
        QList<CredencialSat> credenciales;
    };
    std::optional<Estado> snapshot;

    std::optional<ErrorPersistencia> registrar(const QString& op)
    {
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
                           m_a.credenciales};
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

} // namespace fakes
