# Icono de la app (T020 D4, D5)

- Fuente: `resources/macos/AppIcon.icon`, paquete de Icon Composer escrito como
  archivos (`icon.json` y `Assets/`). Las capas de `Assets/` son copias de
  `resources/branding/icono/icono-{carpeta,pestana,hoja-posterior,hoja-anterior,frente}.svg`;
  el script las sincroniza.
- Composicion (`icon.json`, grupos de delante hacia atras): `frente`;
  `hojas` (hoja anterior, hoja posterior); `carpeta` (pestana, carpeta).
  Fondo como relleno del documento: `#052E1C` y, en apariencia oscura,
  `#0E1F18`. Tintado y transparente los deriva el sistema del mismo `.icon`
  (sombra neutra, translucidez desactivada para conservar los planos). Ajustes
  finos de vidrio o de cada apariencia: abrir el `.icon` en Icon Composer y
  regenerar.
- Compilado y versionado: `resources/macos/AppIcon-compilado/{Assets.car,AppIcon.icns}`.
  `src/app` los copia a `Contents/Resources` (antes de la firma) e
  `Info.plist.in` declara `CFBundleIconName`/`CFBundleIconFile` = `AppIcon`.
  El build normal no necesita Xcode.
- Manifiesto: `resources/macos/AppIcon.manifiesto` (hash de cada fuente y
  salida, comando y `xcodebuild -version`). ctest `satcfdi_app_icon_manifiesto`
  (label `branding`, sin Xcode) falla si algo no coincide.

## Revisar o ajustar en Icon Composer

El formato de `icon.json` no es un contrato documentado: Icon Composer puede
reescribirlo (orden de claves, valores por omision, nombres de `Assets/`) al
guardar.

1. Para revisar (Default, Dark, Clear claro/oscuro, Tinted), abrir
   `resources/macos/AppIcon.icon` y cerrar **sin guardar**. `git status` debe
   quedar limpio; si Icon Composer guardo solo, descartar con
   `git checkout -- resources/macos/AppIcon.icon` (y borrar archivos nuevos).
2. Si se acepta un ajuste, guardar desde Icon Composer: su `icon.json` (y los
   `Assets/` que escriba) pasan a ser la fuente. Despues:
   - las capas de `Assets/` deben seguir llamandose `icono-<capa>.svg` y ser
     iguales a `resources/branding/icono/` (el script las vuelve a copiar; un
     cambio de geometria se hace en `resources/branding/icono/`, no en el
     `.icon`);
   - ejecutar `tools/app-icon/regenerar_app_icon` y `--check`;
   - versionar juntos `AppIcon.icon/`, `AppIcon-compilado/` y
     `AppIcon.manifiesto`, y correr ctest (`-L branding`).

## Regenerar (requiere Xcode 26)

```bash
tools/app-icon/regenerar_app_icon            # escribe AppIcon-compilado/ y el manifiesto
tools/app-icon/regenerar_app_icon --check    # regenera en un temporal y compara
```

El minimo de macOS (`--minimum-deployment-target`) es el
`SATCFDI_MACOS_MINIMUM_VERSION` del build: `--minimo-macos 26.0`, la variable
de entorno `SATCFDI_MACOS_MINIMUM_VERSION`, o el `LSMinimumSystemVersion` del
bundle de `--build-dir` (por omision `build/`, que debe estar compilado). Se
registra como `minimo-macos` en el manifiesto; `satcfdi_app_icon_manifiesto`
falla si no coincide con el del build que corre ctest, o si faltan `xcode`,
`minimo-macos` o `comando`.

`actool` de Xcode 26.2 no produce `Assets.car` byte a byte reproducible
(nombres de rendition con UUID y `Timestamp`); `AppIcon.icns` si. Por eso
`--check` con la misma version exige bytes iguales en el `.icns` y, para
`Assets.car`, bytes iguales o `assetutil --info` normalizado igual (sin
`Timestamp`, UUID ni el digest de esas renditions). Con otra version de
Xcode compara la estructura de `assetutil --info` y los PNG extraidos del
`.icns` con `iconutil`.

## Registro de regeneraciones

| Fecha (UTC) | Xcode | Assets.car | AppIcon.icns | Resultado |
| --- | --- | --- | --- | --- |
| 2026-10-27T01:03:42Z | 26.2 (17C52) | bytes distintos; assetutil normalizado igual | bytes iguales | `--check` OK |
| 2026-10-27T01:03:45Z | 26.2 (17C52) | bytes distintos; assetutil normalizado igual | bytes iguales | `--check` OK |
| 2026-10-27 (regeneracion con `minimo-macos`) | 26.2 (17C52) | nuevo `Assets.car` (bytes distintos, mismo contenido normalizado) | bytes iguales | `--check` OK |

No es una garantia entre versiones de Xcode.
