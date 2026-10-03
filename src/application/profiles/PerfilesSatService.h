#pragma once

#include "application/common/Errores.h"
#include "application/profiles/NuevoPerfilSimuladoRequest.h"
#include "application/profiles/PerfilResumen.h"
#include "domain/common/Resultado.h"
#include "domain/perfiles/PerfilId.h"
#include "ports/persistence/ErrorPersistencia.h"

#include <QFuture>
#include <QList>
#include <QObject>

namespace satcfdi {

// Contrato de perfiles SAT consumido por presentacion (T003).
//
// Mismas reglas de asincronia, ownership, hilo y senales que
// SolicitudesService.
class PerfilesSatService : public QObject {
    Q_OBJECT

public:
    using ResultadoLista = Resultado<QList<PerfilResumen>, ErrorPersistencia>;
    using ResultadoCrearPerfil = Resultado<PerfilId, ErrorCrearPerfil>;

    using QObject::QObject;
    ~PerfilesSatService() override = default;

    // Perfiles visibles con activo == true, ordenados por RFC.
    virtual QFuture<ResultadoLista> listarActivos() = 0;

    // Crea un perfil sin credencial (accion visible "Crear perfil simulado";
    // nunca se siembra en silencio). Normaliza y valida RFC y razon social,
    // inserta en una transaccion. RFC vigente duplicado ->
    // ErrorCrearPerfil::Integridad. Exito: emite perfilesCambiaron() antes de
    // completar el future.
    virtual QFuture<ResultadoCrearPerfil>
    crearPerfilSimulado(const NuevoPerfilSimuladoRequest& request) = 0;

signals:
    // El conjunto de perfiles activos cambio.
    void perfilesCambiaron();
};

} // namespace satcfdi
