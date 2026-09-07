# ADR 0010: Usar Keychain para secretos y token SAT solo en memoria

## Estado

Accepted

## Contexto

El MVP corre en el Mac personal del usuario. La app debe importar e.firma, proteger certificado, llave privada y contrasena, y evitar que secretos terminen en base local, logs o archivos comunes.

El SAT recomienda usar el almacen local de llaves criptograficas cuando se usa el propio equipo. Apple Keychain esta disenado para guardar secretos pequenos, contrasenas, llaves y certificados en almacenamiento cifrado del sistema.

No hay una necesidad clara de persistir el token SAT entre ejecuciones. El worker verifica cada 10 minutos y sube a 30 minutos con backoff; autenticar de nuevo por ciclo es aceptable para una app personal.

## Decision

Implementar `SecretStore` en macOS mediante un adaptador inicial `MacOSSecretStore`.

Reglas:

- `MacOSSecretStore` usa Keychain para secretos pequenos, llaves de cifrado y contrasenas.
- Al registrar e.firma, la app importa/copia `.cer` y `.key` al almacenamiento controlado por la app.
- Antes de marcar el perfil como listo para crear solicitudes, la app debe validar que la contrasena abre la llave privada, que certificado y llave corresponden, que el RFC del certificado corresponde al perfil y que la e.firma esta vigente.
- La llave privada importada debe quedar cifrada en reposo. La llave o secreto que permite descifrarla vive en Keychain, no en la base local.
- La contrasena de e.firma se guarda en Keychain, nunca en la base local ni en logs.
- `CredencialSat` guarda solo referencias no secretas a los items administrados por `SecretStore`.
- `obtenerMaterialFirma()` entrega material sensible solo en memoria y solo para la operacion actual.
- El token SAT no se persiste. Puede existir solo en memoria del proceso hasta expirar, fallar autenticacion, terminar el ciclo en curso o cerrar la app.
- `SecretStore` no expone metodos de cache persistente de token para el MVP.
- Si en el futuro se confirma un token SAT con TTL largo y valor real de reutilizacion, se debe crear un ADR nuevo antes de persistirlo.

## Consecuencias

- No se introduce contrasena maestra propia en el MVP.
- La seguridad de secretos se apoya en macOS Keychain y en archivos controlados por la app.
- Copiar la base local y la carpeta de paquetes no basta para migrar credenciales a otro equipo; el usuario debera reimportar e.firma o se debera disenar un flujo futuro de exportacion segura.
- Puede haber prompts o rechazos del sistema al usar Keychain; la UI debe mostrar errores accionables.
- El worker autentica contra SAT cuando necesita operar, en lugar de depender de tokens persistidos.
- La futura migracion a Windows debe implementar el mismo contrato con otro adaptador, por ejemplo usando mecanismos seguros del sistema operativo.

## Referencias

- `docs/requirements.md`
- `docs/architecture.md`
- `docs/web-service.md`
- Apple: Keychain services: https://developer.apple.com/documentation/security/keychain-services
- Apple: Storing Keys in the Keychain: https://developer.apple.com/documentation/security/storing-keys-in-the-keychain
- Apple: Keychain items: https://developer.apple.com/documentation/security/keychain-items
