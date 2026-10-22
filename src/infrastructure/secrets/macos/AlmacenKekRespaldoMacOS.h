#pragma once

#include "infrastructure/secrets/macos/KeychainApi.h"
#include "ports/respaldo/AlmacenKekRespaldo.h"

#include <QString>

#include <memory>

namespace satcfdi::secrets {

// AlmacenKekRespaldo sobre Keychain (T025.4, D4; ADR 0010, ADR 0021).
//
// - Un item kSecClassGenericPassword por KEK: servicio
//   `<bundleId>.respaldo.v1.kek.<ambito>` (distinto de los de e.firma; el
//   ambito es el id de instalacion) y cuenta = la referencia (UUID). Cada
//   instalacion solo ve y borra su servicio. Ambito invalido: ninguna llamada
//   toca Keychain (AlmacenMalConfigurado, "ambito.invalido"). KeychainApiMacOS fija data protection keychain,
//   kSecAttrSynchronizable = false y
//   kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly, sin prompts.
// - Dato (68 bytes, big endian), construido y leido en BufferSecreto:
//     "AFK1" | tipo u8 (1 contrasena, 2 recuperacion) | 3 bytes 0 |
//     memoriaKib u32 | iteraciones u32 | paralelismo u32 | sal[16] | kek[32]
//   En una KEK de recuperacion los parametros y la sal van en cero.
// - Nunca la contrasena ni la clave de recuperacion. Nada se registra en logs.
//
// Hilo: como KeychainApi, nunca el hilo grafico.
class AlmacenKekRespaldoMacOS final : public respaldo::AlmacenKekRespaldo {
public:
    // `ambito`: id de instalacion en forma canonica (respaldo::esAmbitoKekValido).
    AlmacenKekRespaldoMacOS(QString bundleId, QString ambito, std::shared_ptr<KeychainApi> keychain);

    // Atajo de produccion con KeychainApiMacOS.
    static std::unique_ptr<AlmacenKekRespaldoMacOS> conKeychainDelSistema(QString bundleId, QString ambito);

    bool ambitoValido() const noexcept { return m_ambitoValido; }

    Resultado<respaldo::ReferenciaKek, respaldo::ErrorAlmacenKek> guardar(const respaldo::KekRespaldo& kek) override;
    Resultado<respaldo::KekRespaldo, respaldo::ErrorAlmacenKek> cargar(const respaldo::ReferenciaKek& referencia) override;
    Resultado<Exito, respaldo::ErrorAlmacenKek> borrar(const respaldo::ReferenciaKek& referencia) override;
    Resultado<QList<respaldo::ReferenciaKek>, respaldo::ErrorAlmacenKek> listar() override;

    // Vacio si el ambito es invalido.
    QString servicio() const;

    static constexpr std::size_t kTamanoDato = 68;

private:
    QString m_bundleId;
    QString m_ambito;
    bool m_ambitoValido = false;
    std::shared_ptr<KeychainApi> m_keychain;
};

} // namespace satcfdi::secrets
