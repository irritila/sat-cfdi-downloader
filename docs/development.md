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

Modulos Qt usados en T002: Core, Gui, Widgets, Qml, Quick, QuickControls2, Test
y QuickTest. Qt Sql entra con T003 y Qt Network con las tareas SAT.

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
ctest --test-dir build -L unit
ctest --test-dir build -L presentation
```

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

Al cerrar la ventana el proceso termina; T002 no tiene icono en menu bar.

El bundle generado asi depende del Qt instalado en el equipo. Empaquetar un
bundle autocontenido (`macdeployqt`, firma, notarizacion) queda fuera de T002.

## Notas de plataforma

- Version minima de macOS: sin `-DCMAKE_OSX_DEPLOYMENT_TARGET`, el binario y
  `LSMinimumSystemVersion` toman la version mayor del SDK. Con el Qt de Homebrew
  (compilado para macOS 26) el bundle local requiere macOS 26 o superior; fijar
  un minimo menor produce advertencias de `ld` y no garantiza ejecucion.
- QtNetwork: el proyecto no lo declara en `find_package` ni en
  `target_link_libraries` (prohibido en T002). Aun asi `otool -L satcfdi_app`
  lo lista, porque el target `Qt6::Qml` de Homebrew declara `Qt6::Network` en
  su `INTERFACE_LINK_LIBRARIES` (QtQml depende de QtNetwork) y CMake lo propaga
  a la linea de enlace. Es una dependencia transitiva de Qt y no implica que la
  app use la red.
- clangd: la raiz activa `CMAKE_EXPORT_COMPILE_COMMANDS`, que genera
  `build/compile_commands.json`. Para clangd, enlazarlo en la raiz
  (`ln -s build/compile_commands.json .`) o configurar `--compile-commands-dir`.
  El enlace y el indice `.cache/` estan ignorados por git.

## Directorios de build

Todos los directorios `build*/` estan ignorados por git. Durante ciclos de
desarrollo con varios agentes se usan directorios separados (`build-core`,
`build-platform`, `build-interface`); `build/` es el directorio canonico.
