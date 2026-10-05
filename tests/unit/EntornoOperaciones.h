#pragma once

// Entorno de pruebas de T007 (OperacionExecutor y WorkerLocal) sobre fakes en
// memoria: Almacen + repositorios falsos, FakeOperacionesSat, reloj logico y
// FakeProgramador. Sin tiempo real: las esperas son por condicion
// (QTest::qWaitFor) y el tiempo avanza con el FakeProgramador.

#include "FakesPersistencia.h"
#include "fakes/FakeConfiguracionAppService.h"
#include "fakes/FakeOperacionesSat.h"
#include "fakes/FakePackageStorage.h"
#include "fakes/FakeProgramador.h"

#include "application/logging/RegexLogSanitizer.h"
#include "application/operaciones/OperacionExecutor.h"
#include "application/operaciones/WorkerLocal.h"
#include "domain/common/UuidCanonico.h"

#include <QTest>
#include <QTimeZone>

#include <atomic>
#include <memory>

namespace pruebasT007 {

using namespace satcfdi;

inline const QDateTime kInicio = QDateTime(QDate(2026, 10, 5), QTime(12, 0), QTimeZone(QTimeZone::UTC));

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, 30000) || f.isCanceled() || f.resultCount() == 0) {
        return std::nullopt;
    }
    return f.result();
}

inline bool esperarVoid(QFuture<void> f)
{
    return QTest::qWaitFor([&] { return f.isFinished(); }, 30000);
}

struct Entorno {
    fakes::Almacen almacen;
    fakes::FakeSolicitudes solicitudes{almacen};
    fakes::FakeLogs logs{almacen};
    fakes::FakeOperacionesSolicitud operaciones{almacen};
    fakes::FakeUnitOfWork uow{almacen};
    fakes::FakePaquetes paquetesRepo{almacen};
    fakes::FakePackageStorage storage;
    RegexLogSanitizer sanitizer;
    fakes::FakeOperacionesSat sat;
    fakes::FakeReloj reloj{kInicio};
    fakes::FakeProgramador programador{reloj};
    std::atomic<int> cierresConexion{0};
    std::atomic<QThread*> hiloCierre{nullptr};
    std::unique_ptr<OperacionExecutor> ejecutor;
    PerfilId perfil = PerfilId::generar();

    Entorno() { crearEjecutor(); }

    ~Entorno()
    {
        sat.liberar();
        ejecutor.reset();
        almacen.liberar();
    }

    void crearEjecutor()
    {
        ejecutor = std::make_unique<OperacionExecutor>(
            PuertosEjecutor{solicitudes, logs, operaciones, uow, sanitizer, sat,
                            [this]() {
                                hiloCierre = QThread::currentThread();
                                ++cierresConexion;
                            },
                            &paquetesRepo, &storage},
            reloj.funcion(), programador);
    }

    SolicitudPersistida& sembrar(EstadoLocal estado, std::optional<EstadoSolicitudSat> estadoSat = std::nullopt)
    {
        SolicitudPersistida s;
        s.id = SolicitudId::generar();
        s.perfilSatId = perfil;
        s.rfcSolicitante = QStringLiteral("EKU9003173C9");
        s.estadoLocal = estado;
        s.creadaEn = kInicio;
        s.fechaInicialSat = QStringLiteral("2026-09-01T00:00:00");
        s.fechaFinalSat = QStringLiteral("2026-09-30T23:59:59");
        if (estado != EstadoLocal::Creada) {
            s.envioIniciadoEn = kInicio;
        }
        if (estado == EstadoLocal::Enviada) {
            s.idSolicitudSat = QStringLiteral("SAT-") + s.id.texto().left(8);
            s.enviadaEn = kInicio;
            s.codEstatusSolicitud = QStringLiteral("5000");
            s.estadoSolicitudSat = estadoSat;
            if (estadoSat) {
                s.ultimaVerificacionEn = kInicio;
            }
        }
        almacen.solicitudes.append(s);
        return almacen.solicitudes.last();
    }

    PaquetePersistido& sembrarPaquete(const SolicitudId& s, EstadoDescarga estado,
                                      std::optional<QDateTime> vencimiento = std::nullopt)
    {
        PaquetePersistido p;
        p.id = uuid::generarCanonico();
        p.solicitudMasivaId = s;
        p.idPaqueteSat = QStringLiteral("PAQ_") + QString::number(almacen.paquetes.size() + 1);
        p.estadoDescarga = estado;
        p.disponibleEn = kInicio;
        p.vencimientoEstimadoEn = vencimiento.value_or(kInicio.addSecs(72 * 3600));
        if (estado == EstadoDescarga::Descargando) {
            p.descargaIniciadaEn = kInicio;
        }
        if (estado == EstadoDescarga::Descargado) {
            p.descargadoEn = kInicio;
            p.rutaLocal = QStringLiteral("paquetes/x.zip");
        }
        almacen.paquetes.append(p);
        return almacen.paquetes.last();
    }

    const SolicitudPersistida* solicitud(const SolicitudId& id) const
    {
        for (const auto& s : almacen.solicitudes) {
            if (s.id == id) {
                return &s;
            }
        }
        return nullptr;
    }
    const PaquetePersistido* paquete(const QString& id) const
    {
        for (const auto& p : almacen.paquetes) {
            if (p.id == id) {
                return &p;
            }
        }
        return nullptr;
    }

    int logsDe(TipoEventoLog tipo, std::optional<SolicitudId> s = std::nullopt) const
    {
        int n = 0;
        for (const auto& l : almacen.logs) {
            if (l.tipoEvento == tipo && (!s || l.solicitudMasivaId == *s)) {
                ++n;
            }
        }
        return n;
    }

    // Ruta relativa (T008 D5) del archivo final de un paquete sembrado.
    QString rutaFinal(const PaquetePersistido& p) const
    {
        const SolicitudPersistida* s = solicitud(p.solicitudMasivaId);
        return rutapaquete::derivarRutaRelativa(
                   UbicacionPaquete{s->rfcSolicitante, s->fechaInicialSat, s->id.texto(), p.idPaqueteSat})
            .valor();
    }

    // Espera a que el ejecutor procese todo lo encolado hasta ahora.
    bool drenar()
    {
        return esperarVoid(ejecutor->leer([](OperacionesSolicitudRepository&) {}));
    }
};

inline FallaOperacion falla(FaseOperacion fase, const char* codigo = nullptr)
{
    return FallaOperacion::de(fase, codigo ? std::optional(QString::fromLatin1(codigo)) : std::nullopt,
                              QStringLiteral("diagnostico de prueba"));
}

inline ResultadoVerificacion verificacion(EstadoSolicitudSat estado, QStringList ids = {}, qint64 numero = 0,
                                          const char* codigoEstado = "5000")
{
    ResultadoVerificacion v;
    v.estadoSolicitudSat = estado;
    v.idsPaquetes = std::move(ids);
    v.numeroCfdi = numero;
    v.codigoEstadoSolicitud = QString::fromLatin1(codigoEstado);
    v.mensaje = QStringLiteral("Solicitud Aceptada");
    return v;
}

} // namespace pruebasT007
