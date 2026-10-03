#pragma once

#include "application/common/Errores.h"
#include "application/common/Resultado.h"
#include "application/profiles/PerfilResumen.h"

#include <QFuture>
#include <QList>
#include <QObject>

namespace satcfdi {

// Contrato de perfiles SAT consumido por presentacion. En T002 solo expone la
// lista de perfiles activos para el selector de nueva solicitud; T003 agrega
// crearPerfilSimulado (docs/tasks/T003-persistencia-local.md).
//
// Mismas reglas de asincronia, ownership e hilo que SolicitudesService.
class PerfilesSatService : public QObject {
    Q_OBJECT

public:
    using ResultadoLista = Resultado<QList<PerfilResumen>, ErrorPersistencia>;

    using QObject::QObject;
    ~PerfilesSatService() override = default;

    // Perfiles con activo == true, ordenados por RFC.
    virtual QFuture<ResultadoLista> listarActivos() = 0;
};

} // namespace satcfdi
