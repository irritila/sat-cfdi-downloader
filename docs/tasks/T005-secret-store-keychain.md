# T005: SecretStore con Keychain

## Estado

Pendiente

## Prioridad y tamano

- Prioridad: Alta.
- Tamano: Grande.

## Objetivo

Implementar el contrato `SecretStore` para importar, validar y proteger localmente la e.firma mediante Keychain de macOS.

## Contexto

La app necesita usar certificado, llave privada y contrasena para autenticarse ante SAT. Ese material es altamente sensible y no debe guardarse en SQLite, logs ni archivos comunes. La app es personal y local; no requiere una solucion de custodia multiusuario o empresarial.

## Alcance

### Incluye

- Importacion/copia controlada de `.cer` y `.key`.
- Proteccion de referencias y secretos mediante `MacOSSecretStore`.
- Validacion de contrasena, correspondencia certificado/llave, RFC y vigencia.
- Una credencial activa por `PerfilSat`.
- Reemplazo de una credencial existente.
- Entrega de material de firma solo en memoria para una operacion.
- Manejo de errores de Keychain y limpieza controlada tras una importacion fallida.
- Pruebas con material de certificado controlado.

### No incluye

- Token SAT persistente.
- Rotacion o recuperacion empresarial.
- Exportacion o migracion de credenciales entre equipos.
- Multiusuario, roles o permisos de aplicacion.
- CSD para timbrado.
- Integracion completa del flujo SAT; corresponde a `T009`.

## Dependencias

- `T003-persistencia-local.md`.
- ADR 0006 y ADR 0010.
- `docs/architecture.md`.

Esta tarea no depende de `T006`. El spike SAT puede recomendar una libreria criptografica adicional, pero debe usar material de prueba independiente y no bloquear la custodia basica en Keychain.

## Trabajo esperado

1. Definir los items de Keychain y referencias que guardara `CredencialSat`.
2. Implementar `MacOSSecretStore` detras de `SecretStore`.
3. Importar/copiar certificado y llave al almacenamiento controlado por la app.
4. Validar contrasena, correspondencia criptografica, RFC y vigencia antes de activar la credencial.
5. Persistir o reemplazar la referencia de credencial dentro de una operacion consistente.
6. Eliminar o invalidar la credencial anterior solo despues de validar la nueva.
7. Devolver material de firma en memoria y limpiar buffers cuando sea posible.
8. Traducir errores tecnicos a errores visibles sin incluir secretos.
9. Implementar un fake `SecretStore` para tests de aplicacion.

## Decisiones que debe cerrar esta tarea

- Identificadores y nombres de los items Keychain.
- Que material se guarda directamente en Keychain y que material se guarda en archivo cifrado controlado por la app.
- Politica cuando el usuario cancela o deniega acceso a Keychain.
- Politica de reemplazo si la nueva e.firma es invalida.
- Politica de limpieza si la importacion falla a mitad del proceso.
- Forma de comprobar vigencia y correspondencia sin registrar material sensible.

## Restricciones tecnicas

- `SecretStore` es el contrato; la aplicacion no debe depender directamente de Keychain.
- SQLite solo guarda referencias no secretas y metadata del perfil.
- La contrasena nunca se guarda en SQLite ni en logs.
- El token SAT no se guarda en Keychain en el MVP; vive solo en memoria.
- Los logs deben registrar codigo y categoria del error, nunca llave, contrasena, token, firma ni contenido completo del certificado.
- Una credencial no se considera lista hasta completar todas las validaciones requeridas.

## Criterios de aceptacion

- [ ] Una e.firma valida puede importarse y asociarse a un `PerfilSat`.
- [ ] Una contrasena incorrecta es rechazada sin activar la credencial.
- [ ] Una llave y certificado que no corresponden son rechazados.
- [ ] Un certificado con RFC distinto al perfil es rechazado.
- [ ] Una e.firma vencida o invalida es rechazada.
- [ ] El reemplazo conserva la credencial anterior si la nueva importacion falla.
- [ ] El reemplazo deja una sola credencial activa cuando la nueva importacion es valida.
- [ ] Una referencia de credencial puede recuperarse desde SQLite sin revelar secretos.
- [ ] `obtenerMaterialFirma` entrega material solo durante la operacion solicitada.
- [ ] Los errores de Keychain, cancelacion y acceso denegado son distinguibles para la UI.
- [ ] Ningun secreto aparece en SQLite, logs, mensajes visibles o archivos temporales de pruebas.
- [ ] El fake `SecretStore` permite probar servicios sin usar Keychain real.
- [ ] La tarea no introduce soporte para CSD, multiusuario o migracion de credenciales.

## Verificacion

1. Ejecutar tests con certificado y llave validos de prueba.
2. Probar contrasena incorrecta.
3. Probar certificado y llave incompatibles.
4. Probar RFC distinto y certificado vencido.
5. Simular cancelacion o acceso denegado de Keychain.
6. Probar reemplazo valido y reemplazo fallido.
7. Inspeccionar SQLite y `LogSolicitud` para confirmar ausencia de secretos.
8. Ejecutar `ctest --test-dir build`.

## Definicion de terminado

- `MacOSSecretStore` implementa el contrato definido.
- Una credencial valida puede registrarse, consultarse y reemplazarse.
- Los fallos no dejan una credencial parcialmente activa.
- Los tests cubren validaciones y errores principales.
- `T009` puede solicitar material de firma sin conocer Keychain ni rutas internas.

## Resultado

Pendiente.

## Riesgos y notas

- Esta tarea protege credenciales del usuario personal; no resuelve respaldo o migracion entre Macs.
- La compatibilidad de la firma SAT se validara en `T006` con un fixture controlado.
- Los paquetes ZIP no pertenecen a `SecretStore` aunque tambien sean informacion sensible.

## Referencias

- `T003-persistencia-local.md`
- `docs/architecture.md`
- ADR 0006
- ADR 0010
