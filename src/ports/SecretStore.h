#pragma once

#include "domain/common/Resultado.h"
#include "ports/persistence/Exito.h"
#include "ports/secrets/SecretStoreTypes.h"

#include <QDateTime>
#include <QList>
#include <QStringView>

namespace satcfdi {

// Puerto de custodia de la e.firma por generacion (T005, DA1; ADR 0006,
// ADR 0010). Implementacion productiva: MacOSSecretStore (Keychain + contenedor
// cifrado autenticado), en infrastructure/secrets/macos. Pruebas de
// aplicacion: tests/fakes/FakeSecretStore.h.
//
// Hilo y bloqueo:
// - No es QObject. Todas las llamadas pueden bloquear (E/S, Keychain,
//   criptografia): SOLO desde el hilo de PersistenceDispatcher o del ejecutor
//   serial de operaciones (T007/T009). NUNCA desde el hilo grafico.
// - La implementacion es reentrante entre generaciones distintas; el
//   llamador serializa operaciones sobre la misma generacion.
// - SIN prompts: ninguna llamada muestra UI de autenticacion (Keychain con
//   interaccion deshabilitada). Un almacen bloqueado devuelve AlmacenBloqueado
//   y el llamador decide reintentar en un ciclo posterior; nunca en bucle.
//
// Reloj: el puerto no lee la hora; el servicio pasa `ahoraUtc` (RelojUtc
// inyectable). Vigencia local: vigenteDesde <= ahoraUtc < vigenteHasta.
//
// Errores: solo ErrorSecretStore con su catalogo cerrado. Ninguna categoria
// ni diagnostico incluye rutas, RFC ni material.
class SecretStore {
public:
    virtual ~SecretStore() = default;

    // Lee .cer/.key desde las rutas efimeras de `entrada`, valida formato,
    // contrasena, correspondencia certificado/llave, RFC (OID 2.5.4.45) igual a
    // `rfcEsperado` (ya normalizado), admisibilidad como e.firma y vigencia
    // local, y escribe una generacion NUEVA aun no activa. Consume `entrada`
    // (su contrasena se limpia al destruirse). No toca generaciones previas.
    // Fallo: no deja residuos (o los deja solo para reconciliacion).
    // Errores: ArchivoIlegible, FormatoInvalido, ContrasenaIncorrecta,
    // ParejaIncompatible, RfcNoCoincide, NoEsEFirma, Vencida, NoVigenteAun,
    // AlmacenBloqueado, AccesoDenegado, CanceladoPorUsuario,
    // AlmacenMalConfigurado, AlmacenNoDisponible, FalloEscritura, Interno.
    virtual Resultado<CredencialPreparada, ErrorSecretStore>
    prepararEFirma(EntradaEFirma&& entrada, QStringView rfcEsperado, const QDateTime& ahoraUtc) = 0;

    // Estado de una generacion persistida: Lista, Vencida, NoVigenteAun,
    // MaterialFaltante (algun item/contenedor ausente) o MaterialDanado
    // (contenedor ilegible o autenticacion fallida). Nunca devuelve
    // SinCredencial ni Validando. Errores: AlmacenBloqueado, AccesoDenegado,
    // AlmacenMalConfigurado, AlmacenNoDisponible, Interno.
    virtual Resultado<EstadoCredencial, ErrorSecretStore>
    obtenerEstado(const CredencialRef& referencia, const QDateTime& ahoraUtc) = 0;

    // Descifra y devuelve el material de la generacion para UNA operacion.
    // No valida vigencia. Errores: CredencialNoEncontrada, CredencialDanada,
    // AlmacenBloqueado, AccesoDenegado, AlmacenMalConfigurado,
    // AlmacenNoDisponible, Interno.
    virtual Resultado<MaterialFirma, ErrorSecretStore>
    obtenerMaterialFirma(const CredencialRef& referencia) = 0;

    // Elimina todos los items/archivos de la generacion. Idempotente: una
    // generacion inexistente es exito. Errores: AlmacenBloqueado,
    // AccesoDenegado, AlmacenMalConfigurado, AlmacenNoDisponible,
    // FalloEscritura, Interno.
    virtual Resultado<Exito, ErrorSecretStore> eliminar(const CredencialRef& referencia) = 0;

    // Elimina toda generacion propia que NO este en `vigentes` (huerfanas de
    // cierres inesperados, descartes o limpiezas fallidas). Nunca toca una
    // vigente. Si no puede enumerar con certeza, falla sin borrar nada.
    // Fallos individuales de borrado se cuentan en fallosLimpieza (exito).
    // Errores: AlmacenBloqueado, AccesoDenegado, AlmacenMalConfigurado,
    // AlmacenNoDisponible, Interno.
    virtual Resultado<ResumenReconciliacion, ErrorSecretStore>
    reconciliar(const QList<CredencialRef>& vigentes) = 0;
};

} // namespace satcfdi
