# Desarrollo local

Instrucciones para configurar, compilar, probar y abrir la app en macOS.

## Prerrequisitos

| Herramienta | Version | Notas |
| --- | --- | --- |
| macOS | Apple Silicon o Intel | Plataforma unica soportada por ahora. |
| Xcode Command Line Tools | Actual | `xcode-select --install`. Provee clang y el SDK de macOS. |
| Qt | 6.8 LTS o superior | Baseline del proyecto: 6.8. Probado con Qt 6.10.2 de Homebrew. No usar APIs posteriores a 6.8. |
| CMake | 3.21 o superior | Probado con CMake 4.4.3. |
| Ninja | Cualquiera reciente | Recomendado como generador. Probado con Ninja 1.13.2. |

Instalacion con Homebrew:

```bash
brew install qt cmake ninja
```

Modulos Qt usados: Core, Gui, Widgets, Qml, Quick, QuickControls2, Sql, Network,
Test y QuickTest. Sql (T003) solo lo enlazan `satcfdi_infrastructure` y sus
pruebas. Network (T004) solo lo enlaza `satcfdi_app_core` para la instancia
unica con `QLocalServer`/`QLocalSocket` (IPC local, sin red); el cliente HTTP
del SAT llega con su tarea.

## OpenSSL 3 (T005)

La validacion de e.firma y el cifrado de contenedores (`satcfdi_crypto`) usan
OpenSSL 3 como dependencia directa (`find_package(OpenSSL 3 COMPONENTS Crypto)`):

```bash
brew install openssl@3
```

En macOS, si no se indica `OPENSSL_ROOT_DIR`, la configuracion usa
`/opt/homebrew/opt/openssl@3` (o `/usr/local/opt/openssl@3` en Intel). Para
otra instalacion:

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt \
      -DOPENSSL_ROOT_DIR=/ruta/a/openssl@3
```

Empaquetar `libcrypto` dentro del bundle queda para la distribucion.

## Encontrar Qt (`CMAKE_PREFIX_PATH`)

Qt de Homebrew no esta en las rutas de busqueda por defecto de CMake. Indicarlo
al configurar:

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
```

Alternativas equivalentes:

- Exportar la variable una vez por sesion:
  `export CMAKE_PREFIX_PATH=/opt/homebrew/opt/qt`.
- Con el instalador oficial de Qt, apuntar al kit de macOS, por ejemplo
  `-DCMAKE_PREFIX_PATH=$HOME/Qt/6.8.3/macos`.

## Configurar, compilar y probar

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build
ctest --test-dir build --output-on-failure
```

Filtrar pruebas por label:

```bash
ctest --test-dir build -L unit            # satcfdi_tests: dominio y aplicacion
ctest --test-dir build -L infrastructure  # satcfdi_infrastructure_tests: SQLite real
ctest --test-dir build -L integration     # satcfdi_integration_tests: arranque y composition root
ctest --test-dir build -L presentation    # satcfdi_presentation_tests: QML y view models
ctest --test-dir build -L secrets         # crypto y SecretStore macOS (Keychain falso)
```

Las pruebas de infraestructura e integracion usan bases en `QTemporaryDir` y
`QStandardPaths` en modo prueba; no tocan los datos reales del usuario.

Opciones de CMake del proyecto:

- `-DSATCFDI_WARNINGS_AS_ERRORS=ON`: trata advertencias como errores en los
  targets del proyecto (desactivado por defecto).
- `-DSATCFDI_KEYCHAIN_REAL_TESTS=ON` (T005, macOS): compila y registra
  `satcfdi_keychain_real_tests` (label `keychain-real`) contra el Keychain real.
  Apagado por defecto. Requiere sesion grafica y un ejecutable firmado con
  entitlements para el data protection keychain; sin ellos la prueba hace
  QSKIP (`errSecMissingEntitlement`).
- `-DSATCFDI_BUNDLE_IDENTIFIER=<id>`: cambia el `CFBundleIdentifier` del bundle
  (por defecto `mx.adenium.satcfdi-downloader`, desde T004). Es una variable de
  cache: si un directorio de build existente tiene exactamente el id anterior
  `mx.adenium.satcfdi`, la configuracion lo migra al nuevo y lo informa con un
  mensaje `STATUS`; un id personalizado distinto se respeta.

## Abrir la app

`satcfdi_app` se genera como bundle macOS dentro del directorio de build:

```bash
open build/src/app/satcfdi_app.app
```

Para ver la salida de consola, ejecutar el binario dentro del bundle:

```bash
./build/src/app/satcfdi_app.app/Contents/MacOS/satcfdi_app
```

## Ciclo de vida (T004)

- Instancia unica: al arrancar, la app abre el canal local
  `mx.adenium.satcfdi-downloader.<uid>.instance-v1` (socket Unix en el
  directorio temporal del usuario) ANTES de tocar SQLite. Si ya hay una
  instancia, la nueva le envia `ActivateWindow` (la primaria muestra y enfoca
  su ventana) y termina con codigo 0, sin crear base ni grafo. Si el socket
  quedo de un proceso terminado (canal obsoleto), se retira y la nueva
  instancia asume el rol primario. La adquisicion y la retirada del canal
  obsoleto se serializan entre procesos con un `QLockFile` junto al socket
  (`<canal>.lock`), de modo que arranques simultaneos dejan una sola primaria.
- Cerrar la ventana la oculta: el proceso y el menu bar siguen activos
  (`setQuitOnLastWindowClosed(false)`). La ventana se vuelve a mostrar desde el
  menu bar o abriendo la app otra vez. Solo Salir (menu bar, Cmd-Q) termina:
  registra `ultimo_cierre_en`, retira el menu bar y sale.
- Arranque manual: muestra y enfoca la ventana. Arranque por Login Item: crea el
  menu bar sin mostrar la ventana.
- Probar la instancia unica:

```bash
B=./build/src/app/satcfdi_app.app/Contents/MacOS/satcfdi_app
$B --data-dir /tmp/satcfdi-prueba &   # primaria
$B; echo $?                            # secundaria: activa la primaria y sale con 0
```

- Probar el Login Item (requiere el adaptador `MacOSIntegration`): abrir el
  bundle con `open build/src/app/satcfdi_app.app`, activar "Iniciar con la
  sesion" en el menu bar y revisar Ajustes del Sistema > General > Elementos de
  inicio. `SMAppService` puede reportar `RequiresApproval` hasta que se apruebe
  ahi. Para probar el arranque sin ventana, cerrar sesion y volver a entrar. Sin
  identidad de firma valida el registro puede fallar o quedar `Unavailable`; la
  verificacion completa requiere un bundle firmado. Desactivar la opcion al
  terminar.

## Datos locales y `--data-dir`

Al arrancar, la app crea (si falta) el directorio de datos, crea o abre la base
`satcfdi.sqlite3` y aplica las migraciones embebidas antes de cargar la UI. La
inicializacion SQLite corre en un hilo temporal que se une antes de crear la UI;
el hilo grafico no ejecuta SQL:

- Por defecto: `QStandardPaths::AppDataLocation`, que en macOS es
  `~/Library/Application Support/Adenium/SAT CFDI Downloader/satcfdi.sqlite3`.
- Con `--data-dir <dir>` (o `--data-dir=<dir>`): `<dir>/satcfdi.sqlite3`. Util
  para pruebas manuales sin tocar los datos reales:

```bash
./build/src/app/satcfdi_app.app/Contents/MacOS/satcfdi_app --data-dir /tmp/satcfdi-prueba
```

Junto a la base pueden aparecer `satcfdi.sqlite3-wal` y `satcfdi.sqlite3-shm`
(modo WAL) y, desde T008, la carpeta `paquetes/` (ver "Paquetes ZIP locales").

Credenciales e.firma (T005): los contenedores cifrados viven en
`<directorio de datos>/credentials/` y la contrasena y la clave de envoltura en
el Keychain (servicios `<bundle id>.efirma.v1.*`). El directorio se crea con
permisos 0700 en la primera importacion, no al arrancar. Al arrancar, la app
encola una reconciliacion que elimina generaciones huerfanas (residuos de
fallos previos) sin bloquear la UI; si falla, no borra nada y la app sigue.

Si la base no se puede abrir o migrar (por ejemplo, una base creada por una
version mas nueva de la app), la app muestra un dialogo de error, no carga la
UI, no modifica la base y termina con codigo distinto de cero.

El bundle generado asi depende del Qt instalado en el equipo. Empaquetar un
bundle autocontenido (`macdeployqt`, firma, notarizacion) queda fuera del alcance actual.

## Notas de plataforma

- Version minima de macOS: sin `-DCMAKE_OSX_DEPLOYMENT_TARGET`, el binario y
  `LSMinimumSystemVersion` toman la version mayor del SDK. Con el Qt de Homebrew
  (compilado para macOS 26) el bundle local requiere macOS 26 o superior; fijar
  un minimo menor produce advertencias de `ld` y no garantiza ejecucion.
- QtNetwork: desde T004 `satcfdi_app_core` lo enlaza solo para
  `QLocalServer`/`QLocalSocket` (socket Unix local, sin trafico de red). Ademas
  QtQml de Homebrew ya dependia de QtNetwork, por lo que `otool -L` lo listaba
  desde T002.
- QtSql y driver SQLite: `otool -L satcfdi_app` lista QtSql porque
  `satcfdi_infrastructure` es una biblioteca estatica y su codigo SQL queda
  dentro del ejecutable; ninguna otra capa usa la API de QtSql. El driver
  `libqsqlite.dylib` es un plugin que Qt carga en tiempo de ejecucion desde el
  Qt instalado (`/opt/homebrew/opt/qt/share/qt/plugins/sqldrivers/`); el bundle
  local no lo copia. Una distribucion autocontenida (`macdeployqt` con
  `Contents/PlugIns/sqldrivers/libqsqlite.dylib`, firma y notarizacion) queda
  fuera de T003.
- clangd: la raiz activa `CMAKE_EXPORT_COMPILE_COMMANDS`, que genera
  `build/compile_commands.json`. Para clangd, enlazarlo en la raiz
  (`ln -s build/compile_commands.json .`) o configurar `--compile-commands-dir`.
  El enlace y el indice `.cache/` estan ignorados por git.

## Firma con entitlements (opcional, T005)

El data protection keychain que usa `MacOSSecretStore` exige un ejecutable
firmado con entitlements (`com.apple.application-identifier`,
`keychain-access-groups`). Sin firma, el almacen devuelve
`AlmacenMalConfigurado` y no se pueden guardar credenciales e.firma. Por
defecto el build usa la firma ad-hoc del linker y no requiere nada de esto.

Tres opciones de CMake, vacias por defecto (definir las tres o ninguna):

- `SATCFDI_CODESIGN_IDENTITY`: identidad de `codesign`. Usar el hash SHA-1 que
  muestra `security find-identity -v -p codesigning`, no el nombre: si hay
  certificados con el mismo nombre (por ejemplo uno vencido), firmar por
  nombre es ambiguo.
- `SATCFDI_PROVISIONING_PROFILE`: ruta del `.provisionprofile` cuyo
  `application-identifier` sea `<TEAM>.mx.adenium.satcfdi-downloader`
  (Xcode los guarda en `~/Library/Developer/Xcode/UserData/Provisioning Profiles/`).
- `SATCFDI_TEAM_ID`: Team ID de la cuenta.

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt \
      -DSATCFDI_CODESIGN_IDENTITY=<sha1> \
      -DSATCFDI_PROVISIONING_PROFILE=<ruta al .provisionprofile> \
      -DSATCFDI_TEAM_ID=<team id>
cmake --build build --target satcfdi_app
```

Las excepciones del hardened runtime viven en la plantilla de entitlements
(compartida por la app y por `satcfdi_keychain_real_tests`). El paso de firma
de la app falla si a los entitlements EFECTIVOS les falta `allow-jit`,
`disable-library-validation` o `keychain-access-groups`.

Tras enlazar, un paso POST_BUILD copia el perfil a
`Contents/embedded.provisionprofile`, firma el bundle con
`resources/macos/satcfdi.entitlements.in` (generado en el directorio de build),
`--options runtime --timestamp=none` y verifica con `codesign --verify --strict`.
Los valores solo viven en la cache de CMake; no se versionan.

Entitlements generados:

- `com.apple.application-identifier` y `com.apple.developer.team-identifier`.
- `keychain-access-groups = [<TEAM>.<bundle id>]`. Es el grupo por defecto de
  SecItem* (el primero de la lista); `KeychainApiMacOS` no fija
  `kSecAttrAccessGroup`.
- `com.apple.security.cs.allow-jit`: Qt compila con JIT (memoria `MAP_JIT`)
  las expresiones de `QRegularExpression`/PCRE2 y el JavaScript del motor QML
  (V4). Con hardened runtime y sin esta excepcion, el proceso muere con
  `EXC_BREAKPOINT` en `pthread_jit_write_protect_np`; por ejemplo al abrir el
  FileDialog nativo (`QPlatformFileDialogHelper::cleanFilterList`). No hace
  falta `allow-unsigned-executable-memory`.
- `com.apple.security.cs.disable-library-validation`: el hardened runtime
  activa library validation y dyld rechaza las bibliotecas de Homebrew (Qt,
  OpenSSL), firmadas ad-hoc o por otro equipo ("different Team IDs"). Una
  distribucion que copie y firme sus dependencias dentro del bundle podria
  retirarla.

Comprobar:

```bash
codesign --verify --strict --verbose=2 build/src/app/satcfdi_app.app
codesign -dvv build/src/app/satcfdi_app.app            # Authority, TeamIdentifier, flags=runtime
codesign -d --entitlements - --xml build/src/app/satcfdi_app.app | plutil -p -
security cms -D -i build/src/app/satcfdi_app.app/Contents/embedded.provisionprofile | plutil -p -
```

Perfil de cuenta gratuita: dura 7 dias (`ExpirationDate` en la salida de
`security cms -D`). Para renovarlo, abrir en Xcode un proyecto macOS vacio con
el mismo bundle id y equipo y compilarlo: Xcode genera un perfil nuevo en la
misma carpeta (puede cambiar el nombre del archivo). Reconfigurar con la nueva
ruta y volver a compilar `satcfdi_app`.

### Prueba contra el Keychain real

Con `-DSATCFDI_KEYCHAIN_REAL_TESTS=ON` y las tres opciones de firma,
`satcfdi_keychain_real_tests` se construye como bundle con el mismo bundle id
que la app (el perfil esta atado a ese App ID), embebe el perfil y se firma con
los mismos entitlements. Usa servicios aislados y aleatorios
(`<bundle id>.test-<aleatorio>.*`), no toca los items de la app y borra todo lo
que crea. Sin las opciones de firma, la prueba hace QSKIP
(`errSecMissingEntitlement`, -34018).

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt \
      -DSATCFDI_KEYCHAIN_REAL_TESTS=ON \
      -DSATCFDI_CODESIGN_IDENTITY=<sha1> \
      -DSATCFDI_PROVISIONING_PROFILE=<ruta al .provisionprofile> \
      -DSATCFDI_TEAM_ID=<team id>
cmake --build build --target satcfdi_keychain_real_tests
ctest --test-dir build -R keychain --output-on-failure
```

Requiere sesion grafica con el llavero desbloqueado. Si macOS muestra un
dialogo, no debe aceptarse a ciegas: la prueba no deberia pedir nada. Nota:
`security find-generic-password` solo consulta los llaveros de archivo; no ve
los items del data protection keychain. La ausencia de residuos la comprueba la
propia prueba (lista vacia tras borrar).

## Flujo SAT integrado (T009)

- `satcfdi_app` enlaza `satcfdi_sat`: el root crea `SatGatewayProductivo`, que
  solo se invoca desde el hilo del ejecutor. La CLI del spike sigue fuera de la
  app (la configuracion falla si `satcfdi_app` enlazara `satcfdi_sat_spike_core`).
- Notificaciones: `OSIntegration::notificar(NotificacionLocal{id, tipo, titulo,
  cuerpo})` responde con `notificacionTerminada(id, resultado)`. Nunca pide
  permiso; con permiso denegado o no disponible solo reporta el resultado, no
  cambia estados ni logs, y la lista de solicitudes muestra un aviso. El mismo
  `id` reemplaza la notificacion en macOS. El cambio de credencial de un perfil
  a un estado no `Lista` tambien notifica (texto del catalogo, sin RFC).
- Detalle: `Enviar` aparece en solicitudes `Creada` y solo se habilita con la
  credencial del perfil `Lista` (si no, muestra el motivo); los mensajes del
  catalogo D10 describen rechazos, envio incierto y paquetes vencidos o con
  maximo de descargas (5008, sin `Reintentar descarga`).
- Las pruebas usan fakes de red (`FakeSatGateway`, `FakeOperacionesSat`); el humo
  real lo ejecuta solo el usuario.

## Paquetes ZIP locales (T008)

- Raiz de paquetes (D8): con `--data-dir <dir>`, `<dir>/paquetes`; sin
  `--data-dir`, `~/SAT-CFDI-Downloader/paquetes` (no `Application Support`).
  `ruta_local` se guarda relativa a esa raiz.
- La app crea la raiz al ARRANCAR, en el hilo de E/S del bootstrap (no en el
  hilo grafico), con permisos 0700 (los componentes que falten;
  no cambia permisos de carpetas que ya existian). El adaptador
  (`FilesystemPackageStorage`) tambien la crearia en el primer guardado; se crea
  antes para que sea visible y quede privada desde el inicio.
- Dentro: `<RFC>/<AAAA-MM>/<id local de la solicitud>/<IdPaquete>.zip`. Al
  arrancar, la recuperacion escanea la raiz: un temporal propio se elimina, un
  final sin solicitud asociable se conserva y solo deja un diagnostico en el log
  de la app (categoria `satcfdi.recuperacion.archivos`).
- El detalle muestra, por cada paquete `Descargado`, si el archivo local esta
  presente, no se encontro o no se pudo comprobar; nunca cambia el estado.
- Prueba manual: `satcfdi_app --data-dir /tmp/satcfdi-prueba` y comprobar que
  existe `/tmp/satcfdi-prueba/paquetes` (0700).
- Acceso desde la app (T009.1): `Mostrar en Finder` por paquete `Descargado`
  (solo si el archivo local esta presente), `Abrir carpeta de la solicitud` en
  el detalle y `Abrir carpeta de paquetes` en la lista y en el menu bar. La ruta
  se resuelve en el hilo del ejecutor y el adaptador de macOS la revalida justo
  antes de `NSWorkspace`. Ninguna accion cambia estados, SQLite ni logs, no crea
  carpetas ni abre el ZIP. Si el destino no existe o Finder falla, se muestra un
  aviso accesible ("Archivo local no encontrado", "Carpeta de paquetes no
  encontrada", "No se pudo abrir Finder"); desde el menu bar el aviso aparece en
  la lista de la ventana, que se trae al frente.

## Monitoreo local: worker y ejecutor serial (T007)

- `AppCompositionRoot` crea `OperacionExecutor` (hilo y conexion SQLite
  propios, distintos de `PersistenceDispatcher`), `WorkerLocal` y el puerto
  `OperacionesSat`. Hasta T009 el puerto es `OperacionesSatNulo`: toda operacion
  falla en `Preparacion` ("Integracion SAT no disponible en esta version") y la
  solicitud vuelve a `Creada` con su error visible; nunca hay exito simulado.
- Al arrancar, el worker encola la recuperacion y arranca pausado o activo segun
  la configuracion persistida. El menu bar muestra una linea no seleccionable:
  `Monitoreo activo`, `Monitoreo pausado` o `Trabajando: enviando/verificando/
  descargando...`, y `Pendientes: N` si hay intenciones guardadas por la pausa.
- Crear una solicitud encola su envio (tambien con el monitoreo pausado). El
  detalle ofrece `Verificar ahora` (solicitud enviada sin estado SAT final) y
  `Reintentar descarga` (paquetes disponibles o con error); con pausa quedan
  pendientes.
- Salir (menu bar o Cmd-Q): el worker deja de programar, el ejecutor rechaza
  operaciones nuevas y espera la actual hasta 10 s; despues pide la cancelacion
  cooperativa. Luego se registra el ultimo cierre y se retira el menu bar.
- Prueba manual (tarea T007): abrir la app con `--data-dir` temporal, crear una
  solicitud (falla `Preparacion` con el adaptador nulo y queda `Creada`),
  pausar y reanudar desde el menu bar observando la linea de estado, y
  comprobar que la ventana sigue respondiendo.

## Spike SAT (T006): CLI manual `satcfdi_sat_spike`

La biblioteca `satcfdi_sat` (sobres SOAP/WS-Security, C14N con libxml2 del
SDK, firma RSA-SHA1 con OpenSSL 3, parser y cliente HTTP) y sus pruebas sin red
(`ctest -L sat-spike`) se compilan siempre. La CLI manual es opcional, queda
fuera del bundle y `satcfdi_app` no enlaza `satcfdi_sat` (la configuracion
falla si alguna vez lo hiciera).

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt -DSATCFDI_BUILD_SAT_SPIKE=ON
cmake --build build --target satcfdi_sat_spike
ctest --test-dir build -L sat-spike --output-on-failure   # tests/sat (siempre) y satcfdi_sat_spike_cli_tests
```

Reglas de la CLI:

- `--cer` y `--key` deben estar FUERA del repositorio (se rechazan rutas
  dentro de el). `.cer`, `.key`, `.pfx`, `.zip` y `sat-spike/` estan en
  `.gitignore` como red de seguridad.
- La contrasena solo se pide por prompt sin eco en una terminal interactiva
  (termios verificado; si no se puede apagar el eco, no se pide). Nunca por
  argumento ni variable de entorno; un argumento no reconocido se rechaza sin
  reproducirlo. La terminal se restaura ante SIGINT, SIGTERM, SIGHUP, SIGQUIT y
  SIGTSTP. Si no se puede fijar `RLIMIT_CORE=0`, la CLI aborta. (El target
  `satcfdi_sat_spike_prueba` lee stdin sin TTY solo para las pruebas.)
- Si la e.firma no valida (contrasena, pareja, no es e.firma, vigencia), termina
  con codigo 3 sin red ni archivos.
- Cada operacion real muestra la operacion, el RFC enmascarado y el rango o Id,
  y exige escribir `yes`. `solicita`, `verifica` y `descarga` autentican antes
  dentro del mismo proceso (el token vive solo en memoria), con su propia
  confirmacion.
- Directorio de salida (`--salida`, por defecto
  `~/Library/Application Support/Adenium/SAT CFDI Downloader/sat-spike/`, 0700):
  `ledger.jsonl` (0600, verificado con `stat`; enlaces simbolicos o permisos
  que no se confirman fallan cerrado) registra cada operacion ANTES de enviarla (operacion,
  rango, hash del criterio y del sobre) y su resultado; puede contener RFC e
  Ids reales, nunca contrasenas, tokens, sobres ni paquetes. La CLI rechaza un
  criterio ya registrado y mas de 3 solicitudes (una que termino `AntesDeEnvio`
  no llego al SAT y no cuenta). Una solicitud `DespuesDeEnvio` queda marcada
  como incierta y no se reenvia. Maximo 10 verificaciones por `IdSolicitud`,
  separadas al menos 15 minutos (la CLI indica cuanto falta), y una sola
  descarga por `IdPaquete`. Toda la operacion real (leer, validar, registrar,
  enviar, anotar) ocurre bajo `flock` exclusivo de `ledger.lock`; si otro
  proceso lo tiene, la CLI falla de inmediato. Un ledger corrupto falla
  cerrado. Los ZIP van a `paquetes/paquete-<hash>.zip` (0600); el `IdPaquete`
  real solo queda en el ledger. El directorio de salida no puede estar dentro
  del repo (se comprueba antes de pedir la contrasena).
- Ids sin copiarlos: `verifica --ultima` usa el IdSolicitud de la ultima
  solicitud aceptada (`CodEstatus=5000`) del ledger y `descarga --paquete N` el
  N-esimo IdPaquete de la ultima verificacion con paquetes; solo se muestran
  enmascarados. `--id` (UUID) e `--id-paquete` (`UUID_NN`) siguen disponibles,
  pero un valor con otro formato se rechaza antes de pedir la contrasena y sin
  red (sin reproducirlo).
- Solicitud: solo `TipoSolicitud=CFDI` y `EstadoComprobante=Vigente`, un dia
  completo cerrado (`--desde AAAA-MM-DD`, 00:00:00 a 23:59:59).
- La consola solo muestra texto enmascarado (`EnmascaradorEvidencia`).
- Firma configurable para la corrida: `--c14n exclusiva|inclusiva`,
  `--issuer docsat|rfc4514` (defaults de `OpcionesFirma::porDefecto` por
  operacion) y `--ws-addressing` (agrega `To`/`Action` al Header de
  `Autentica`). `--c14n-declarada exclusiva|inclusiva` es EXPERIMENTAL: declara
  una C14N distinta de la calculada (no conforme a XMLDSig); la consola lo
  advierte y la evidencia debe registrarlo aparte.

Secuencia de la corrida real (la ejecuta solo el usuario, en su Mac; ver
`docs/tasks/T006-spike-sat.md`, "Reglas de seguridad y operacion"):

```bash
B=./build/tools/sat_spike/satcfdi_sat_spike
E="--cer <ruta fuera del repo>.cer --key <ruta fuera del repo>.key"
$B --dry-run $E --desde <dia>                         # 1. sin red: revisar sobres enmascarados
$B autentica $E                                       # 2. token: formato y TTL
$B solicita --tipo emitidos --desde <dia> $E          # 3. una solicitud (o --tipo recibidos)
$B verifica --ultima $E                               # 4. cada 15-30 min, maximo 10 veces
$B descarga --paquete 1 $E                            # 5. una vez por paquete (1, 2, ...)
```

`<dia>`: un dia cerrado de hace mas de 7 dias, con entre 1 y 50 CFDI conocidos y
no solicitado antes por otro medio. Un rechazo 301-305 obliga a usar otro dia.
Solo la salida de consola (ya enmascarada) se comparte para la evidencia; el
ledger y los ZIP quedan locales.

## Manual de usuario: capturas (T012)

Las imagenes de `docs/manual/img/` se generan con la herramienta
`satcfdi_manual_capturas` (`tools/capturas-manual/`). Usa los view models de
presentacion sobre fakes deterministas: sin SQLite, worker, SAT ni Keychain, y
solo datos sinteticos (RFC genericos `XAXX010101000`/`XEXX010101000`, rutas
`/Users/usuario/...`, reloj fijo 2026-10-01 10:00 UTC). La herramienta fuerza
tema claro, locale `es_MX`, zona UTC y ventana de 960x640; cada PNG se normaliza
a 1920x1280 sin metadatos variables. Las ilustraciones (selector de archivos,
Finder, menu bar, notificacion) se alimentan de `menubar::entradas` y de
`ServicioNotificaciones`, asi que reflejan los textos reales.

```bash
cmake -S . -B build -G Ninja -DSATCFDI_BUILD_MANUAL_CAPTURAS=ON
cmake --build build --target manual_capturas      # regenera las 12 imagenes
# Una sola imagen o a otro directorio:
QT_QPA_PLATFORM=cocoa ./build/tools/capturas-manual/satcfdi_manual_capturas \
    --salida docs/manual/img --solo 08-detalle-terminada.png
```

- Requiere una sesion grafica de macOS: abre ventanas cocoa breves. No forma
  parte de `ctest` y no usa `screencapture`.
- Guarda PNG sin perdida con compresion zlib maxima. Imprime
  `OK`/`FALLA <archivo> <tipo> <WxH> <bytes>` por imagen y termina con codigo 0
  solo si todas miden 1920x1280 y ninguna pasa de 1 MB.
- El determinismo es de contenido, no de bytes: dos corridas producen los mismos
  nombres, dimensiones y escenarios, con diferencias minimas de antialiasing.
- La prueba `satcfdi_manual_enlaces` (label `docs`, `ctest -L docs`) verifica que
  cada `![...](img/X.png)` de `docs/manual/usuario.md` exista y que no haya PNG
  sin referenciar.

## Directorios de build

Todos los directorios `build*/` estan ignorados por git. Durante ciclos de
desarrollo con varios agentes se usan directorios separados (`build-core`,
`build-platform`, `build-interface`); `build/` es el directorio canonico.
