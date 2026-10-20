# Prototipo T023 (T023.1)

Prototipo navegable del recorrido Facturas por perfil y mes (T023, D5). No es
codigo productivo: usa view models falsos en QML/JS (`qml/VmPrototipo.qml`) con
datos sinteticos (`qml/DatosSinteticos.js`, RFC `XAXX010101000` y
`XEXX010101000`, reloj fijo 19 oct 2026 10:42). No enlaza SQLite, SAT, Keychain,
worker ni rutas reales; CMake falla al configurar si llegara a hacerlo.

Reutiliza `Theme` y los componentes reales (`EncabezadoPagina`, `EstadoBadge`,
`BotonAccion`, `FilaCfdi`, `SelectorSegmentado`, `CampoTexto`, `CampoCombo`,
`AvisoEnLinea`, `EstadoVacio`, `BotonCopiar`, `Icono`) importando el modulo
`SatCfdiDownloader` desde `satcfdi_presentation`. El PDF y la notificacion son
simulados.

## Construir

```bash
cmake -S . -B build-prototipo -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt \
      -DSATCFDI_BUILD_PROTOTIPO_T023=ON -DSATCFDI_WARNINGS_AS_ERRORS=ON
cmake --build build-prototipo --target satcfdi_prototipo_t023
```

La opcion esta apagada por omision: la build normal no contiene el target.

## Ejecutar

```bash
build-prototipo/prototypes/T023/satcfdi_prototipo_t023 [--tema claro|oscuro] [--escenario <clave>]
```

- ⌘⇧E: panel de escenarios (reinicia los datos y fija un estado; cambia tema y ancho 960/720).
- ⌘1 Facturas, ⌘2 Perfiles SAT, ⌘3 Historial, ⌘R accion del mes, ⌘F buscar, ⌘↑/⌘↓ mes anterior/siguiente.
- Menu Ir > Diagnóstico. Escape regresa desde Historial, Perfiles y Diagnóstico.
- "Descargar facturas", "Completar mes", "Actualizar mes" y "Reintentar" simulan
  Preparando (tres etapas), la lista llenandose y Disponibles, con una
  notificacion que abre el mes.

## Capturas

```bash
build-prototipo/prototypes/T023/satcfdi_prototipo_t023 --capturas <dir> [--solo <clave>]
# o: cmake --build build-prototipo --target prototipo_t023_capturas
```

Genera `NN-<escenario>-<960|720>-<claro|oscuro>.png` (17 escenarios, 68 PNG).
Requiere sesion grafica (cocoa).
