#pragma once

#include "domain/common/Resultado.h"
#include "domain/credenciales/CredencialSat.h"
#include "domain/perfiles/PerfilId.h"
#include "ports/persistence/ErrorPersistencia.h"
#include "ports/persistence/Exito.h"
#include "ports/secrets/SecretStoreTypes.h"

#include <QList>

#include <optional>

namespace satcfdi {

// Repositorio de credencial_sat (T005, DA3). Sincrono.
//
// Hilo: solo desde tareas de PersistenceDispatcher (o el ejecutor serial
// con su propia conexion). No es QObject.
// Transaccion: lecturas dentro o fuera de UnitOfWork; escrituras dentro de
// begin()/commit() (Transaccion si no hay una activa).
// Guarda solo referencias opacas y metadata no secreta.
class CredencialSatRepository {
public:
    virtual ~CredencialSatRepository() = default;

    // INSERT de la fila completa. Exige metadataCompleta() (si no: Integridad,
    // restriccion "credencial_sat.metadata"). Errores: Unicidad
    // (ux_credencial_sat_perfil) si el perfil ya tiene credencial;
    // Integridad (FK perfil, CHECK); Transaccion; Ocupado; Almacenamiento.
    virtual Resultado<Exito, ErrorPersistencia> insertar(const CredencialSat& credencial) = 0;

    // UPDATE de la fila del perfil: tres referencias, metadata y
    // actualizada_en de `nueva`; conserva id y registrada_en. Exige
    // metadataCompleta(). Errores: NoEncontrado si el perfil no tiene fila;
    // Integridad; Transaccion; Ocupado; Almacenamiento.
    virtual Resultado<Exito, ErrorPersistencia> reemplazar(const PerfilId& perfilId,
                                                           const CredencialSat& nueva) = 0;

    // Fila del perfil o nullopt. Errores: Almacenamiento; Interno (fila
    // ilegible).
    virtual Resultado<std::optional<CredencialSat>, ErrorPersistencia>
    obtenerPorPerfil(const PerfilId& perfilId) = 0;

    // DELETE fisico. true si borro una fila. Errores: Transaccion; Ocupado;
    // Almacenamiento.
    virtual Resultado<bool, ErrorPersistencia> eliminarPorPerfil(const PerfilId& perfilId) = 0;

    // Generaciones referenciadas por TODAS las filas (para reconciliar).
    // Fail-safe: si alguna fila tiene referencias que no forman una
    // CredencialRef valida, devuelve Interno (nunca una lista parcial, para no
    // autorizar el borrado de una generacion en uso).
    virtual Resultado<QList<CredencialRef>, ErrorPersistencia> listarReferenciasVigentes() = 0;
};

} // namespace satcfdi
