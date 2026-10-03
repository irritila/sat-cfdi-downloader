#pragma once

#include "application/profiles/PerfilResumen.h"
#include "application/requests/SolicitudesService.h"
#include "domain/solicitudes/DedupKey.h"

#include <QList>

#include <optional>

namespace satcfdi {

// DEMO (T002, adaptado al contrato T003): implementacion en memoria SOLO para
// el shell y pruebas de presentacion. No es persistencia; T003 la reemplaza
// en el composition root por SolicitudesServicePersistido. No usa hilos,
// timers, SQL, red ni archivos.
//
// - Futures ya completados (QtFuture::makeReadyValueFuture); senales
//   sincronas, emitidas antes de devolver el future.
// - Datos::Representativos: 11 solicitudes, una por cada clave de
//   estadoResumen; con y sin estadoSat; con y sin paquetes (los 5 estados de
//   descarga aparecen al menos una vez).
// - Datos::Vacio: sin solicitudes, para el estado vacio de la lista.
// - Validacion y dedup_key v1 reales (prepararSolicitud + SolicitudCanonica)
//   y matriz de duplicados de dominio sobre los datos en memoria.
// - crearLocal() exitoso agrega una solicitud `Creada` al inicio y emite
//   listaCambiada() y solicitudActualizada(id).
// - eliminar() marca en memoria; listar/obtener ya no la ven; emite
//   listaCambiada() y solicitudEliminada(id) solo si cambio.
// - No implementa transiciones de estado ni limites SAT.
class DemoSolicitudesService final : public SolicitudesService {
    Q_OBJECT

public:
    enum class Datos {
        Representativos,
        Vacio,
    };

    // `perfiles` es el catalogo usado para validar perfilId y resolver
    // perfilRfc (normalmente DemoPerfilesSatService::perfilesDemo()).
    explicit DemoSolicitudesService(QList<PerfilResumen> perfiles,
                                    Datos datos = Datos::Representativos,
                                    QObject* parent = nullptr);

    // Reemplaza el catalogo de perfiles (p. ej. tras crear() o sembrar() en
    // DemoPerfilesSatService). No altera solicitudes existentes.
    void setPerfiles(QList<PerfilResumen> perfiles);

    QFuture<ResultadoLista> listar() override;
    QFuture<ResultadoDetalle> obtener(const SolicitudId& id) override;
    QFuture<ResultadoEvaluarDuplicado> evaluarDuplicado(const NuevaSolicitudRequest& request) override;
    QFuture<ResultadoCrear> crearLocal(const NuevaSolicitudRequest& request,
                                       ConfirmacionDuplicado confirmacion) override;
    QFuture<ResultadoEliminar> eliminar(const SolicitudId& id) override;

    // Registro interno (publico solo para el armado de datos demo).
    struct Registro {
        SolicitudDetalle detalle;
        DedupKey dedupKey;
        std::optional<QDateTime> eliminadoEn;
    };

private:
    std::optional<QString> rfcPerfilActivo(const PerfilId& id) const;
    Resultado<EvaluacionDuplicado, ErrorCrear> evaluar(const NuevaSolicitudRequest& request,
                                                       std::optional<SolicitudCanonica>* canonica) const;

    QList<PerfilResumen> m_perfiles;
    QList<Registro> m_registros; // mas recientes primero, incluye eliminadas
};

} // namespace satcfdi
