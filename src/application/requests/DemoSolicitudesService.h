#pragma once

#include "application/profiles/PerfilResumen.h"
#include "application/requests/SolicitudesService.h"

#include <QList>

namespace satcfdi {

// DEMO (T002): implementacion en memoria SOLO para el shell. No es
// persistencia ni comportamiento productivo; T003 la reemplaza en el
// composition root. No usa hilos, timers, SQL, red ni archivos.
//
// - Futures ya completados (QtFuture::makeReadyValueFuture).
// - Datos::Representativos: 11 solicitudes, una por cada clave de
//   estadoResumen; con y sin estadoSat; con y sin paquetes (los 5 estados de
//   descarga aparecen al menos una vez).
// - Datos::Vacio: sin solicitudes, para el estado vacio de la lista.
// - crear(): validacion superficial (perfil requerido y activo en `perfiles`,
//   fechas requeridas, fechaFinal >= fechaInicial). Si es valida, agrega una
//   solicitud `Creada` sin estado SAT ni paquetes al inicio de la lista y emite
//   solicitudActualizada(id) de forma sincrona, antes de devolver el future.
// - No implementa transiciones de estado, deduplicacion ni limites SAT.
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

    QFuture<ResultadoLista> listar() override;
    QFuture<ResultadoDetalle> obtener(const SolicitudId& id) override;
    QFuture<ResultadoCrear> crear(const NuevaSolicitudRequest& request) override;

private:
    const PerfilResumen* perfilActivo(const PerfilId& id) const;

    QList<PerfilResumen> m_perfiles;
    QList<SolicitudDetalle> m_solicitudes; // mas recientes primero
};

} // namespace satcfdi
