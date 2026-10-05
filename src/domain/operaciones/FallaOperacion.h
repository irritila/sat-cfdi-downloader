#pragma once

#include <QString>
#include <QStringView>

#include <array>
#include <optional>

namespace satcfdi {

// Fase en la que fallo una operacion externa (T007 D3; ADR 0017). La fase
// decide la transicion (D7, D8, D6) y forma parte de la clave de falla de
// verificacion. Sin SOAP, token ni bytes: solo semantica.
// - Preparacion: local, antes de autenticar (material, credencial no Lista,
//   archivo, configuracion; tambien el adaptador nulo sin T009).
// - Autenticacion: cualquier falla de Autentica.
// - AntesDeEnvio: la peticion de la operacion no salio (T006 D5).
// - DespuesDeEnvio: la peticion salio y no hubo respuesta completa (incierto).
// - RespuestaExplicita: SAT respondio (HTTP completo) con un codigo de rechazo,
//   Fault o un resultado no exitoso; `codigo` lleva el CodEstatus/faultcode.
// - Almacenamiento: falla local al guardar el ZIP (T008 D12); ver CausaAlmacenamiento.
enum class FaseOperacion {
    Preparacion,
    Autenticacion,
    AntesDeEnvio,
    DespuesDeEnvio,
    RespuestaExplicita,
    Almacenamiento,
};

inline constexpr std::array<FaseOperacion, 6> kFasesOperacion = {
    FaseOperacion::Preparacion,    FaseOperacion::Autenticacion,      FaseOperacion::AntesDeEnvio,
    FaseOperacion::DespuesDeEnvio, FaseOperacion::RespuestaExplicita, FaseOperacion::Almacenamiento,
};

// Detalle de una falla de Almacenamiento (solo con esa fase).
enum class CausaAlmacenamiento {
    ColisionDestino,   // ya existe un archivo final distinto en el destino
    EscrituraFallida,  // E/S, permisos, fsync o renombrado
    EspacioInsuficiente,
};

QString claveEstable(FaseOperacion fase);
QString claveEstable(CausaAlmacenamiento causa);
std::optional<FaseOperacion> faseOperacionDesdeClave(QStringView clave);

// Resultado no exitoso de una operacion de OperacionesSat.
struct FallaOperacion {
    FaseOperacion fase = FaseOperacion::Preparacion;
    std::optional<QString> codigo;     // CodEstatus/faultcode/codigo local (sin datos personales)
    QString diagnosticoSanitizado;     // ya saneado por el adaptador (LogSanitizer aplica de nuevo)
    bool cancelada = false;            // termino por cancelacion cooperativa (D12)
    std::optional<CausaAlmacenamiento> causaAlmacenamiento; // solo fase Almacenamiento

    static FallaOperacion de(FaseOperacion fase, std::optional<QString> codigo = std::nullopt,
                             QString diagnostico = {})
    {
        FallaOperacion f;
        f.fase = fase;
        f.codigo = std::move(codigo);
        f.diagnosticoSanitizado = std::move(diagnostico);
        return f;
    }

    friend bool operator==(const FallaOperacion&, const FallaOperacion&) = default;
};

// Clave de falla de verificacion (D8): "<fase>" o "<fase>:<codigo>", p. ej.
// "RespuestaExplicita:300". Se persiste en ultima_clave_falla_verificacion.
QString claveFalla(const FallaOperacion& falla);

// Texto de ultimo_error para una falla (T007; formato estable desde T009):
//   "<claveFalla>[/<causa>][ (cancelada)][: <diagnosticoSanitizado>]"
// p. ej. "Autenticacion (cancelada): No se pudo conectar ... [AntesDeEnvio]".
// Con el adaptador de T009, diagnosticoSanitizado = "<mensaje D10>[ [<detalle tecnico>]]".
QString textoUltimoError(const FallaOperacion& falla);

// Desglose de un ultimo_error escrito con textoUltimoError (sin migracion: la
// fase ya se persiste como prefijo). nullopt si el texto no empieza con una
// fase conocida (p. ej. "CodEstatus 301" de un rechazo explicito o el texto
// de envio interrumpido por cierre).
struct UltimoErrorDesglosado {
    FaseOperacion fase = FaseOperacion::Preparacion;
    std::optional<QString> codigo;
    std::optional<CausaAlmacenamiento> causaAlmacenamiento;
    bool cancelada = false;
    QString mensaje;        // mensaje visible (D10), sin el detalle tecnico
    QString detalleTecnico; // desde el primer " [" hasta el "]" final, si lo hay

    friend bool operator==(const UltimoErrorDesglosado&, const UltimoErrorDesglosado&) = default;
};
std::optional<UltimoErrorDesglosado> desglosarUltimoError(QStringView texto);

} // namespace satcfdi
