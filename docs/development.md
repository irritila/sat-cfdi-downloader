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

Modulos Qt usados: Core, Gui, Widgets, Qml, Quick, QuickControls2, Sql, Test y
QuickTest. Sql (T003) solo lo enlazan `satcfdi_infrastructure` y sus pruebas.
Qt Network entra con las tareas SAT.

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
```

Las pruebas de infraestructura e integracion usan bases en `QTemporaryDir` y
`QStandardPaths` en modo prueba; no tocan los datos reales del usuario.

Opciones de CMake del proyecto:

- `-DSATCFDI_WARNINGS_AS_ERRORS=ON`: trata advertencias como errores en los
  targets del proyecto (desactivado por defecto).
- `-DSATCFDI_BUNDLE_IDENTIFIER=<id>`: cambia el `CFBundleIdentifier` del bundle
  (por defecto `mx.adenium.satcfdi`).

## Abrir la app

`satcfdi_app` se genera como bundle macOS dentro del directorio de build:

```bash
open build/src/app/satcfdi_app.app
```

Para ver la salida de consola, ejecutar el binario dentro del bundle:

```bash
./build/src/app/satcfdi_app.app/Contents/MacOS/satcfdi_app
```

Al cerrar la ventana el proceso termina; todavia no hay icono en menu bar (T004).

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
(modo WAL). T003 no crea carpeta de paquetes ZIP.

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
- QtNetwork: el proyecto no lo declara en `find_package` ni en
  `target_link_libraries` (prohibido hasta las tareas SAT). Aun asi `otool -L satcfdi_app`
  lo lista, porque el target `Qt6::Qml` de Homebrew declara `Qt6::Network` en
  su `INTERFACE_LINK_LIBRARIES` (QtQml depende de QtNetwork) y CMake lo propaga
  a la linea de enlace. Es una dependencia transitiva de Qt y no implica que la
  app use la red.
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

## Directorios de build

Todos los directorios `build*/` estan ignorados por git. Durante ciclos de
desarrollo con varios agentes se usan directorios separados (`build-core`,
`build-platform`, `build-interface`); `build/` es el directorio canonico.
