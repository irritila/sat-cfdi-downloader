#pragma once

#include "application/profiles/PerfilResumen.h"
#include "ports/secrets/SecretStoreTypes.h"

#include <QDateTime>
#include <QString>

#include <array>
#include <optional>

namespace satcfdi {

// Preparacion de un perfil para la UI (T005.1, DA2). Catalogo cerrado de 8.
// - Verificando: persistido, aun sin respuesta de CredencialesSatService (o
//   importacion/reemplazo en curso).
// - SinCredencial ... MaterialDanado: EstadoCredencial verificado.
// - EstadoNoDisponible: no se pudo consultar (almacen bloqueado, persistencia,
//   perfil desaparecido, cancelacion). Reintentable; NO significa credencial
//   invalida.
enum class PreparacionPerfil {
    Verificando,
    SinCredencial,
    Lista,
    Vencida,
    NoVigenteAun,
    MaterialFaltante,
    MaterialDanado,
    EstadoNoDisponible,
};

inline constexpr std::array<PreparacionPerfil, 8> kPreparacionesPerfil = {
    PreparacionPerfil::Verificando,     PreparacionPerfil::SinCredencial,
    PreparacionPerfil::Lista,           PreparacionPerfil::Vencida,
    PreparacionPerfil::NoVigenteAun,    PreparacionPerfil::MaterialFaltante,
    PreparacionPerfil::MaterialDanado,  PreparacionPerfil::EstadoNoDisponible,
};

// Clave estable PascalCase (roles/logs/pruebas; la UI traduce).
QString claveEstable(PreparacionPerfil preparacion);

// Validando -> Verificando; el resto 1:1.
PreparacionPerfil preparacionDesde(EstadoCredencial estado);

// UNICA regla de elegibilidad: activo && Lista. QML no la recalcula.
constexpr bool esListoParaSolicitudes(bool activo, PreparacionPerfil preparacion) noexcept
{
    return activo && preparacion == PreparacionPerfil::Lista;
}

// Perfil + preparacion derivada. Construir solo con componer() para que
// `listoParaSolicitudes` sea siempre coherente.
struct PerfilConPreparacion {
    PerfilResumen perfil;
    PreparacionPerfil preparacion = PreparacionPerfil::Verificando;
    bool listoParaSolicitudes = false;
    // notAfter de la metadata 002 (sin descifrar); nullopt si no hay
    // credencial, la fila es previa a 002 o el estado no esta disponible.
    std::optional<QDateTime> vigenteHasta;

    static PerfilConPreparacion componer(PerfilResumen perfil, PreparacionPerfil preparacion,
                                         std::optional<QDateTime> vigenteHasta = std::nullopt)
    {
        PerfilConPreparacion r;
        r.listoParaSolicitudes = esListoParaSolicitudes(perfil.activo, preparacion);
        r.perfil = std::move(perfil);
        r.preparacion = preparacion;
        r.vigenteHasta = std::move(vigenteHasta);
        return r;
    }

    friend bool operator==(const PerfilConPreparacion&, const PerfilConPreparacion&) = default;
};

} // namespace satcfdi
