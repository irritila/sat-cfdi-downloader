# T005: SecretStore con Keychain

## Estado

Completada

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
- `docs/design/qt-project-structure.md` para la frontera entre aplicacion e
  infraestructura.

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

## Decisiones cerradas del refinamiento

- La vigencia de T005 es exclusivamente local: `notBefore <= ahora <
  notAfter`, usando un `Clock` inyectable. La revocacion y aceptacion final del
  SAT quedan para T006/T009.
- Los items Keychain usan `kSecUseDataProtectionKeychain=true`,
  `kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly` y
  `kSecAttrSynchronizable=false`. El worker puede operar con la pantalla
  bloqueada despues del primer desbloqueo. `errSecInteractionNotAllowed` se
  trata como error transitorio y no genera prompts en segundo plano.
- El `service` es versionado y no contiene RFC ni rutas. Las cuentas se basan
  en un `credentialUUID` opaco por generacion:

  ```text
  <bundle-id>.efirma.v1.password
  <bundle-id>.efirma.v1.wrapping-key
  account = <credentialUUID>
  ```

- La aplicacion guarda certificado y llave privada en un contenedor versionado
  cifrado autenticado dentro de `AppDataLocation/credentials/`, con nombres
  aleatorios y permisos restrictivos. La clave de envoltura vive en Keychain.
  No se crean descifrados temporales persistentes.
- `SecretStore` es un puerto de infraestructura. `CredencialesSatService`
  orquesta importacion, reemplazo, eliminacion y la transaccion SQLite.
- `MaterialFirma` y los buffers de contraseña son tipos move-only, no cruzan
  QML ni señales encoladas y se limpian al destruirse como garantia best effort.
- La importacion valida formato, contraseña, correspondencia criptografica,
  RFC, que el certificado sea admisible como e.firma y vigencia local antes de
  activar una referencia.
- El reemplazo crea una generacion candidata, actualiza SQLite y confirma la
  candidata solo despues del `commit`. La credencial anterior permanece activa
  si falla cualquier paso previo.
- La reconciliacion al arrancar elimina generaciones sin referencia vigente en
  SQLite. Si la limpieza posterior al commit falla, la nueva credencial sigue
  activa y el residuo se limpia en la siguiente reconciliacion.
- El token SAT no pertenece a `SecretStore` y solo puede vivir en memoria.

### Contrato minimo

Casos de uso de `CredencialesSatService`:

```text
importar(perfilId, EntradaEFirma)
reemplazar(perfilId, EntradaEFirma)
obtenerEstado(perfilId)
obtenerMaterialFirma(perfilId)
eliminar(perfilId)
```

El puerto `SecretStore` debe cubrir internamente:

```text
prepararEFirma(EntradaEFirma, rfcEsperado) -> CredencialPreparada
obtenerEstado(CredencialRef) -> EstadoCredencial
obtenerMaterialFirma(CredencialRef) -> MaterialFirma
eliminar(CredencialRef) -> Resultado
reconciliar(referenciasVigentes) -> ResumenReconciliacion
```

`CredencialPreparada` es move-only y descarta su generacion si se destruye sin
confirmacion. `MaterialFirma` es move-only, no tiene conversion a `QString`,
`QByteArray` ni `QDebug`, y solo vive durante la operacion que lo consume.

Estados de credencial:

```text
SinCredencial | Validando | Lista | Vencida | NoVigenteAun |
MaterialFaltante | MaterialDanado
```

Categorias de error visibles:

```text
ArchivoIlegible | FormatoInvalido | ContrasenaIncorrecta |
ParejaIncompatible | RfcNoCoincide | NoEsEFirma | Vencida |
NoVigenteAun | CredencialNoEncontrada | CredencialDanada |
AlmacenBloqueado | AccesoDenegado | CanceladoPorUsuario |
AlmacenMalConfigurado | AlmacenNoDisponible | FalloEscritura | Interno
```

La UI traduce categorias; los detalles de `OSStatus`, rutas y diagnosticos
tecnicos solo pueden aparecer en logs sanitizados.

## Pendientes no bloqueantes

- Confirmar mediante un spike la biblioteca/API concreta para leer los formatos
  PKCS#8 usados por `.key` y extraer el RFC del certificado sin heuristicas.
- Definir la regla verificable para distinguir una e.firma de un CSD cuando el
  formato del certificado lo permita; no se usaran nombres ni extensiones.
- Determinar si los metadatos no secretos `numero_serie`, `vigente_desde` y
  `vigente_hasta` requieren columnas adicionales en `credencial_sat`.
- Unificar el nombre del fake entre `FakeSecretStore` y cualquier referencia
  previa a `InMemorySecretStore`. (Resuelto: `FakeSecretStore`.)

## Restricciones tecnicas

- `SecretStore` es el contrato; la aplicacion no debe depender directamente de Keychain.
- SQLite solo guarda referencias no secretas y metadata del perfil.
- La contrasena nunca se guarda en SQLite ni en logs.
- El token SAT no se guarda en Keychain en el MVP; vive solo en memoria.
- Los logs deben registrar codigo y categoria del error, nunca llave, contrasena, token, firma ni contenido completo del certificado.
- Una credencial no se considera lista hasta completar todas las validaciones requeridas.

## Criterios de aceptacion

- [x] Dado un perfil activo y fixtures validos, cuando se importa una e.firma,
  entonces se crea una referencia opaca y el perfil queda listo.
- [x] Dada una contraseña incorrecta, cuando se importa, entonces devuelve
  `ContrasenaIncorrecta` y no activa ninguna credencial.
- [x] Dado un certificado y una llave incompatibles, cuando se importan,
  entonces devuelve `ParejaIncompatible` sin residuos.
- [x] Dado un RFC de certificado distinto al perfil, cuando se importa,
  entonces devuelve `RfcNoCoincide` sin exponer el RFC en el mensaje visible.
- [x] Dado un certificado vencido o aún no vigente, cuando se importa,
  entonces devuelve la categoria correspondiente usando el `Clock` inyectado.
- [x] Dado un certificado no admisible como e.firma, cuando se importa,
  entonces devuelve `NoEsEFirma` sin inferirlo por nombre o extensión.
- [x] Dada una credencial activa, cuando un reemplazo falla en validacion,
  Keychain, escritura o commit, entonces la credencial anterior sigue usable.
- [x] Dado un reemplazo valido, cuando termina el commit, entonces existe una
  sola generacion activa y la anterior queda pendiente de limpieza.
- [x] Dado un cierre inesperado o fallo de limpieza posterior al commit, cuando
  inicia la app, entonces la reconciliacion elimina generaciones huerfanas sin
  tocar la credencial vigente.
- [x] Dada una referencia recuperada desde SQLite, entonces no contiene
  contraseña, llave, DER, token ni ruta absoluta.
- [x] Dado `obtenerMaterialFirma`, entonces devuelve `MaterialFirma` move-only
  y no persiste ni expone el material fuera de la operacion.
- [x] Dado Keychain cancelado, denegado, bloqueado o no disponible, entonces la
  UI recibe categorias distintas y el worker no genera prompts en bucle.
- [x] Dado cualquier flujo de importacion/reemplazo, entonces ningun secreto
  aparece en SQLite, WAL/SHM, logs, QML, mensajes visibles ni temporales.
- [x] Dado `FakeSecretStore`, entonces los servicios pueden probar exito,
  fallos y reemplazo sin Keychain real.
- [x] La tarea no agrega token persistente, exportacion, CSD productivo,
  multiusuario ni migracion de credenciales.

## Verificacion

1. Ejecutar tests con certificado y llave validos de prueba.
2. Probar contrasena incorrecta.
3. Probar certificado y llave incompatibles.
4. Probar RFC distinto y certificado vencido.
5. Simular cancelacion o acceso denegado de Keychain.
6. Probar reemplazo valido y reemplazo fallido.
7. Inspeccionar SQLite y `LogSolicitud` para confirmar ausencia de secretos.
8. Ejecutar `ctest --test-dir build`.
9. Ejecutar la suite determinista con `FakeSecretStore`, `KeychainApi` falso y
   `Clock` controlable.
10. Ejecutar pruebas de Keychain real solo con un namespace de servicio aislado
    y un bundle firmado; omitirlas con `QSKIP` explicito si el entorno no aplica.
11. Escanear SQLite, WAL/SHM, logs, mensajes y temporales con centinelas de los
    fixtures; el conteo esperado es cero.
12. Ejecutar pruebas manuales de cancelacion, denegacion y bloqueo sin permitir
    prompts desde el worker.

## Definicion de terminado

- `MacOSSecretStore` implementa el contrato definido.
- Una credencial valida puede registrarse, consultarse y reemplazarse.
- Los fallos no dejan una credencial parcialmente activa.
- Los tests cubren validaciones y errores principales.
- `T009` puede solicitar material de firma sin conocer Keychain ni rutas internas.

## Resultado

Completada el 2026-10-04.

- `SecretStore` con tipos move-only; `MacOSSecretStore` con OpenSSL 3 (parseo, validacion, AES-256-GCM) y Security.framework solo para Keychain (data protection keychain, sin prompts).
- `CredencialesSatService` con importar, reemplazar, estado, material, eliminar y reconciliar; exclusion entre operaciones y verificacion de hilo en runtime.
- `CredencialSatRepository` y migracion `002_credencial_metadata.sql` (serie y vigencia); referencias `scs1:<uuid>:<rol>`.
- Regla e.firma/CSD: `OU` no vacio => `NoEsEFirma` (decision del usuario; confirmar en T006).
- Firma opcional del bundle con entitlements de Keychain; prueba contra el Keychain real firmada sin omisiones.
- `ctest`: 8 suites pasan, incluidas centinelas de secretos con control positivo.
- Pendientes: prueba manual con e.firma real (T005.1); `disable-library-validation` solo en desarrollo; perfil gratuito de 7 dias; empaquetado de OpenSSL en distribucion.

## Riesgos y notas

- Esta tarea protege credenciales del usuario personal; no resuelve respaldo o migracion entre Macs.
- La compatibilidad de la firma SAT se validara en `T006` con un fixture controlado.
- Los paquetes ZIP no pertenecen a `SecretStore` aunque tambien sean informacion sensible.

## Referencias

- `T003-persistencia-local.md`
- `docs/architecture.md`
- ADR 0006
- ADR 0010
