#pragma once

#include "application/operaciones/OperacionesSat.h"
#include "application/persistence/PuertosPersistencia.h"

#include <QHash>
#include <QMutex>

#include <chrono>
#include <cstdint>
#include <memory>

namespace satcfdi {

class AccesoCredencialSat;
class PackageStorage;
class SatGateway;
class TokenSat;

struct OpcionesOperacionesSat {
    // Un token se reutiliza solo si sigue vigente en ahora + margen (D6), para
    // no usarlo justo antes de su Expires (TTL observado en T006: 300 s).
    std::chrono::seconds margenToken{30};
};

// OperacionesSat productivo (T009 D5-D8, D10) sobre SatGateway,
// AccesoCredencialSat y PackageStorage.
//
// Hilo: las operaciones del puerto corren SOLO en el hilo del
// OperacionExecutor (sincronas y bloqueantes; una a la vez). invalidarSesion
// y cerrarSesiones son seguras desde cualquier hilo (p. ej. el grafico, al
// recibir credencialCambio).
//
// Por operacion: estadoEnHiloDeTrabajo (distinto de Lista -> Preparacion, sin
// red), materialEnHiloDeTrabajo, token de la sesion del perfil (o Autentica),
// operacion del gateway y traduccion a resultado/FallaOperacion (tabla D8) con
// los mensajes del catalogo D10. MaterialFirma vive solo durante la operacion.
//
// Sesion de token (D6): en memoria, por perfil. Se reutiliza mientras
// vigenteEn(reloj() + margenToken); se descarta por expiracion, falla de
// autenticacion, tokenRechazado (inferido por el gateway), invalidarSesion
// (cambio de credencial) o cerrarSesiones/destructor. Sin reintento
// automatico tras tokenRechazado: la siguiente operacion autentica de nuevo.
// Un token obtenido mientras se invalidaba la sesion no se conserva.
//
// Descarga: el receptor transmite el ZIP a PackageStorage::guardarAtomico
// (solo con CodEstatus 5000). La ubicacion se valida antes de la red.
//
// Seguridad: no registra nada; los mensajes son del catalogo D10 mas el
// diagnostico tecnico saneado del gateway. Sin token, firma, RFC completo,
// Mensaje SAT crudo ni bytes.
//
// Ownership: referencias NO propietarias; el composition root crea gateway,
// credenciales y almacenamiento antes y los destruye despues.
class OperacionesSatProductivo final : public OperacionesSat {
public:
    OperacionesSatProductivo(SatGateway& gateway, AccesoCredencialSat& credenciales, PackageStorage& almacenamiento,
                             RelojUtc reloj = relojSistema(), OpcionesOperacionesSat opciones = {});
    ~OperacionesSatProductivo() override;

    OperacionesSatProductivo(const OperacionesSatProductivo&) = delete;
    OperacionesSatProductivo& operator=(const OperacionesSatProductivo&) = delete;

    Resultado<ResultadoEnvio, FallaOperacion> enviar(const ContextoEnvio& contexto) override;
    Resultado<ResultadoVerificacion, FallaOperacion> verificar(const ContextoVerificacion& contexto) override;
    Resultado<ResultadoDescarga, FallaOperacion> descargar(const ContextoDescarga& contexto) override;
    Resultado<ResultadoArchivoFinal, FallaOperacion> existeArchivoFinal(const ContextoArchivoFinal& contexto) override;
    Resultado<EstadoCredencial, FallaOperacion> obtenerEstadoCredencial(const PerfilId& perfil) override;

    // Cambio de credencial del perfil (credencialCambio): descarta su token.
    void invalidarSesion(const PerfilId& perfil);
    // Cierre: descarta todos los tokens.
    void cerrarSesiones();
    // Para pruebas: hay token guardado para el perfil (sin evaluar vigencia).
    bool tieneSesion(const PerfilId& perfil) const;

private:
    struct Preparada;

    Resultado<Preparada, FallaOperacion> preparar(const PerfilId& perfil, const SenalCancelacion& cancelacion);
    void descartarSiIgual(const PerfilId& perfil, const std::shared_ptr<const TokenSat>& token);

    SatGateway& m_gateway;
    AccesoCredencialSat& m_credenciales;
    PackageStorage& m_almacenamiento;
    RelojUtc m_reloj;
    OpcionesOperacionesSat m_opciones;

    mutable QMutex m_mutex;
    QHash<QString, std::shared_ptr<const TokenSat>> m_tokens; // por PerfilId::texto()
    QHash<QString, std::uint64_t> m_generaciones;             // sube al invalidar el perfil
    std::uint64_t m_epoca = 0;                                // sube en cerrarSesiones
};

} // namespace satcfdi
