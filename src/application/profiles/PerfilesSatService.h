#pragma once

#include "application/common/Errores.h"
#include "application/profiles/PerfilResumen.h"
#include "domain/common/Resultado.h"
#include "domain/perfiles/PerfilId.h"
#include "ports/persistence/ErrorPersistencia.h"

#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>

#include <optional>

namespace satcfdi {

// Contrato de perfiles SAT consumido por presentacion (T003, ampliado en
// T005.1 DA1).
//
// Asincronia/hilo: se llama desde el hilo grafico; los futures se completan
// tras una continuacion en el hilo grafico. Las senales se emiten en el hilo
// grafico, despues del COMMIT y antes de completar el future.
// Sin estado de credencial: la preparacion la combina ConsultaPreparacionPerfiles.
class PerfilesSatService : public QObject {
    Q_OBJECT

public:
    using ResultadoLista = Resultado<QList<PerfilResumen>, ErrorPersistencia>;
    // obtener(): nullopt si el id es nulo, no existe o esta eliminado.
    using ResultadoPerfil = Resultado<std::optional<PerfilResumen>, ErrorPersistencia>;
    using ResultadoCrear = Resultado<PerfilResumen, ErrorCrearPerfil>;
    using ResultadoActualizar = Resultado<PerfilResumen, ErrorActualizarPerfil>;

    using QObject::QObject;
    ~PerfilesSatService() override = default;

    // Perfiles no eliminados, activos E inactivos, ordenados por RFC.
    virtual QFuture<ResultadoLista> listarNoEliminados() = 0;

    // Perfil no eliminado por id (activo o inactivo).
    virtual QFuture<ResultadoPerfil> obtener(const PerfilId& id) = 0;

    // Crea un perfil ACTIVO sin credencial. Normaliza y valida RFC
    // (rfc::normalizarYValidar) y nombre (trim no vacio) fuera de
    // transaccion; inserta en una transaccion. Devuelve el perfil creado (RFC
    // normalizado). Errores: Validacion por campo, RfcDuplicado, Persistencia.
    // Exito: emite perfilesCambiaron().
    virtual QFuture<ResultadoCrear> crear(const QString& rfc, const QString& nombre) = 0;

    // Cambia SOLO el nombre (trim no vacio) de un perfil no eliminado, activo
    // o inactivo; el RFC es inmutable. Errores: Validacion (NombreRequerido),
    // PerfilInexistente, Persistencia. Exito: emite perfilesCambiaron().
    virtual QFuture<ResultadoActualizar> actualizarNombre(const PerfilId& id, const QString& nombre) = 0;

signals:
    // El conjunto de perfiles no eliminados o alguno de sus datos cambio.
    void perfilesCambiaron();
};

} // namespace satcfdi
