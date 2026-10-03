#pragma once

#include "infrastructure/secrets/macos/DirectorioCredenciales.h"
#include "infrastructure/secrets/macos/KeychainApi.h"
#include "ports/SecretStore.h"

#include <QString>
#include <QStringView>

#include <memory>

namespace satcfdi {

// SecretStore productivo de macOS (T005, ADR 0010, DA6, DS1-DS6).
//
// Por generacion (CredencialRef = UUID):
// - Keychain (via KeychainApi):
//     <bundleId>.efirma.v1.password      cuenta = uuid -> contrasena SAT
//     <bundleId>.efirma.v1.wrapping-key  cuenta = uuid -> clave AES-256 (32 B)
// - Archivo `<directorio>/<nombre>.scc`: contenedor v1 (AES-256-GCM) con el
//   .cer DER y el .key PKCS#8 DER original. El nombre es un hash del UUID que
//   solo este adaptador sabe derivar (no aparece en SQLite ni en logs).
//
// Construccion (composition root): el directorio de credenciales (p. ej.
// QStandardPaths::AppDataLocation + "/credentials"), el bundle id efectivo y
// la KeychainApi (KeychainApiMacOS en produccion). Se crea el directorio con
// 0700 de forma perezosa en la primera escritura.
//
// Hilo: no es QObject; todas las llamadas bloquean (E/S, Keychain, OpenSSL):
// SOLO desde el hilo del dispatcher o del ejecutor serial. Sin estado mutable
// compartido: reentrante entre generaciones. reconciliar() no debe correr en
// paralelo con prepararEFirma() (el servicio lo serializa en el dispatcher).
//
// Las CredencialPreparada devueltas capturan `this` en su descartador: el
// adaptador debe vivir mas que ellas (lo posee el root junto al dispatcher).
class MacOSSecretStore final : public SecretStore {
public:
    MacOSSecretStore(QString directorioCredenciales, QString bundleId, std::shared_ptr<secrets::KeychainApi> keychain);
    ~MacOSSecretStore() override;

    MacOSSecretStore(const MacOSSecretStore&) = delete;
    MacOSSecretStore& operator=(const MacOSSecretStore&) = delete;

    // Atajo de produccion con KeychainApiMacOS.
    static std::unique_ptr<MacOSSecretStore> conKeychainDelSistema(QString directorioCredenciales, QString bundleId);

    Resultado<CredencialPreparada, ErrorSecretStore>
    prepararEFirma(EntradaEFirma&& entrada, QStringView rfcEsperado, const QDateTime& ahoraUtc) override;

    Resultado<EstadoCredencial, ErrorSecretStore> obtenerEstado(const CredencialRef& referencia,
                                                               const QDateTime& ahoraUtc) override;

    Resultado<MaterialFirma, ErrorSecretStore> obtenerMaterialFirma(const CredencialRef& referencia) override;

    Resultado<Exito, ErrorSecretStore> eliminar(const CredencialRef& referencia) override;

    Resultado<ResumenReconciliacion, ErrorSecretStore> reconciliar(const QList<CredencialRef>& vigentes) override;

    // Nombres de servicio Keychain (T005).
    QString servicioContrasena() const;
    QString servicioClaveEnvoltura() const;

    // Detalle interno expuesto solo para pruebas del adaptador: nombre del
    // contenedor de una generacion y patron de temporales.
    static QString nombreContenedor(QStringView uuid);
    static bool esNombreContenedor(QStringView nombre);
    static bool esNombreTemporal(QStringView nombre);

private:
    struct Apertura;
    Apertura abrir(const CredencialRef& referencia, bool verificarContrasena);

    secrets::DirectorioCredenciales m_directorio;
    QString m_bundleId;
    std::shared_ptr<secrets::KeychainApi> m_keychain;
};

} // namespace satcfdi
