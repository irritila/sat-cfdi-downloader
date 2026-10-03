#pragma once

#include "domain/common/Resultado.h"
#include "domain/perfiles/PerfilId.h"
#include "ports/persistence/ErrorPersistencia.h"
#include "ports/secrets/SecretStoreTypes.h"

#include <QFuture>
#include <QObject>
#include <QString>

#include <optional>

namespace satcfdi {

// Error visible de CredencialesSatService (T005). La presentacion traduce por
// `tipo`/`codigoPerfil`/`categoria`; `mensaje` es un texto fijo por categoria
// SIN RFC, rutas, OSStatus ni material. `causa` (persistencia) solo para
// diagnostico.
struct ErrorCredencialSat {
    enum class Tipo {
        PerfilInvalido,      // `codigoPerfil`
        CredencialExistente, // importar() sobre un perfil que ya tiene credencial
        Almacen,             // `categoria` (catalogo cerrado de SecretStore)
        Persistencia,        // `causa`
        HiloNoPermitido,     // obtenerMaterialFirma() llamado desde el hilo
                             // grafico (verificacion en runtime, tambien en
                             // release); no se toco SQLite ni el SecretStore
    };

    enum class CodigoPerfil {
        PerfilInexistente, // id nulo, desconocido o eliminado
        PerfilInactivo,    // importar/reemplazar exigen perfil activo
    };

    Tipo tipo = Tipo::Almacen;
    std::optional<CodigoPerfil> codigoPerfil;
    std::optional<ErrorSecretStore::Categoria> categoria;
    std::optional<ErrorPersistencia> causa;
    QString mensaje;

    static ErrorCredencialSat perfil(CodigoPerfil codigo);
    static ErrorCredencialSat existente();
    // Descarta `diagnostico` y `codigoNativo` del error del puerto.
    static ErrorCredencialSat almacen(ErrorSecretStore::Categoria categoria);
    static ErrorCredencialSat persistencia(ErrorPersistencia causa);
    static ErrorCredencialSat hiloNoPermitido();
};

// Texto visible fijo por categoria (sin datos variables).
QString mensajeVisible(ErrorSecretStore::Categoria categoria);

// Resultado no secreto de importar()/reemplazar().
struct CredencialImportada {
    EstadoCredencial estado = EstadoCredencial::Lista;
    MetadataCredencial metadata;
    // reemplazar(): true si la generacion anterior no se pudo eliminar tras
    // el commit; queda para la reconciliacion del siguiente arranque. La
    // nueva credencial sigue activa.
    bool limpiezaPendiente = false;
};

// Resultado de eliminar().
struct CredencialEliminada {
    bool habiaCredencial = false;
    bool limpiezaPendiente = false; // igual que en CredencialImportada
};

// Casos de uso de credencial e.firma por perfil (T005, DA2).
//
// Asincronia/hilo: importar, reemplazar, obtenerEstado, eliminar y reconciliar
// se llaman desde el hilo grafico y devuelven QFuture completado tras la
// continuacion en el hilo grafico; el trabajo (SQLite + SecretStore) corre en
// el hilo de PersistenceDispatcher, en serie. Las senales se emiten en el
// hilo grafico SOLO despues del COMMIT y antes de completar el future.
//
// Los tipos sensibles (EntradaEFirma, MaterialFirma) nunca viajan en QFuture
// ni en senales. obtenerMaterialFirma() es SINCRONO y solo se invoca dentro de
// una operacion de trabajo (tarea del dispatcher o ejecutor serial), nunca
// desde el hilo grafico ni desde QML.
class CredencialesSatService : public QObject {
    Q_OBJECT

public:
    using ResultadoImportacion = Resultado<CredencialImportada, ErrorCredencialSat>;
    using ResultadoEstado = Resultado<EstadoCredencial, ErrorCredencialSat>;
    using ResultadoEliminacion = Resultado<CredencialEliminada, ErrorCredencialSat>;
    using ResultadoMaterial = Resultado<MaterialFirma, ErrorCredencialSat>;
    using ResultadoReconciliacion = Resultado<ResumenReconciliacion, ErrorCredencialSat>;

    using QObject::QObject;
    ~CredencialesSatService() override = default;

    // Perfil existente y activo SIN credencial. Valida y escribe la generacion
    // (SecretStore) FUERA de transaccion; luego BEGIN IMMEDIATE, INSERT,
    // COMMIT y confirmar(). Cualquier fallo previo al commit descarta la
    // candidata. Errores: PerfilInvalido, CredencialExistente, Almacen,
    // Persistencia.
    virtual QFuture<ResultadoImportacion> importar(const PerfilId& perfilId, EntradaEFirma&& entrada) = 0;

    // Perfil existente y activo CON credencial (sin ella: Almacen
    // CredencialNoEncontrada). Prepara la candidata fuera de transaccion;
    // BEGIN IMMEDIATE; relee la anterior; UPDATE; COMMIT; confirmar(); elimina
    // la generacion anterior. Fallo antes del commit: la anterior sigue activa
    // y la candidata se descarta. Fallo de limpieza tras el commit: exito con
    // limpiezaPendiente = true.
    virtual QFuture<ResultadoImportacion> reemplazar(const PerfilId& perfilId, EntradaEFirma&& entrada) = 0;

    // SinCredencial si el perfil no tiene fila; Validando mientras una
    // importacion/reemplazo de ese perfil esta en curso; si no, el estado del
    // SecretStore con el reloj inyectado. Perfil existente (activo o no).
    virtual QFuture<ResultadoEstado> obtenerEstado(const PerfilId& perfilId) = 0;

    // Borra la fila (COMMIT) y despues la generacion. Idempotente.
    virtual QFuture<ResultadoEliminacion> eliminar(const PerfilId& perfilId) = 0;

    // Arranque (DA5): lista referencias vigentes y pide al SecretStore borrar
    // huerfanas. Un error no borra nada. Las operaciones despachadas despues
    // esperan a que termine (dispatcher serial).
    virtual QFuture<ResultadoReconciliacion> reconciliar() = 0;

    // SINCRONO, solo en un hilo de trabajo (dispatcher o ejecutor serial de
    // T009). Desde el hilo grafico devuelve HiloNoPermitido sin abortar.
    // Serializado con importar/reemplazar/eliminar/reconciliar: observa la
    // credencial anterior o la nueva completa, nunca un estado intermedio
    // (puede esperar a que termine una de esas operaciones). No valida
    // vigencia. No se debe invocar desde dentro de una de esas operaciones.
    // Errores: HiloNoPermitido; PerfilInvalido; Almacen (CredencialNoEncontrada
    // sin fila, y las del puerto); Persistencia.
    virtual ResultadoMaterial obtenerMaterialFirma(const PerfilId& perfilId) = 0;

signals:
    // La credencial del perfil cambio (importada, reemplazada o eliminada).
    void credencialCambio(const QString& perfilId);
};

} // namespace satcfdi
