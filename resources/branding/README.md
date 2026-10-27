# Branding de Archivo Fiscal (T020)

Fuentes SVG del logo "Carpeta con pestana" y sus derivados. La geometria la
entrega el lider conceptual (`docs/meetings/T020-desarrollo/svg/`, ver su
`NOTAS.md`); aqui se copia sin cambios. El wordmark y el lockup se generan.

| Carpeta | Contenido | viewBox |
| --- | --- | --- |
| `icono/` | Capas del icono de app a sangre (fondo, carpeta, pestana, hoja posterior, hoja anterior, frente), `icono-compuesto.svg` y `icono-oscuro-fondo.svg`. Fuente del `.icon` (corte 2). | `0 0 1024 1024` |
| `video/` | `marca-video.svg` y sus cinco capas (D7, D9). Solo video. | `176 196 648 624` |
| `menu/` | Plantillas de menu bar, normal y tres estados, 18 y 36 px; negro con alfa (D6). Propiedad de qt-interface-engineer desde C2. | `0 0 18 18`, `0 0 36 36` |
| `marca/` | `wordmark.svg` (marfil `#F4F1E8` con la pestana de Fiscal en `#C9A227`), `wordmark-mono.svg` (todo `#0E1F18`) y `lockup-video.svg` (vertical, marca arriba). Texto a trazos (D8, propuesta c). **Generados.** | ver abajo |
| `fonts/Inter/` | Inter SemiBold 4.1 y `OFL.txt`; version y hashes en `VERSION.md` (D11). | |

`marca/` usa el sistema de coordenadas de `marca-video.svg`: el lockup
contiene la geometria literal de la marca (mismas coordenadas) y amplia el
viewBox hacia los lados y hacia abajo para el texto; el viewBox de
`wordmark*.svg` es su caja de tinta en ese mismo sistema (indica donde va).
Escala de `05-motion.md`: carpeta 496 unidades = 180 logicos (k = 496/180).

Nombre: propuesta c "pestana tipografica" elegida por el usuario (bitacora C2;
`docs/meetings/T020-desarrollo/svg-nombre/PROPUESTAS.md`), en logicos:

- Inter SemiBold 40 en "Archivo" y "Fiscal"; tracking -0.015 em (-0.60) y
  -0.020 em (-0.80); 14 entre cajas de tinta; linea base comun.
- El punto de cada "i" se sustituye por una pestana 8.8 x 3.2, radio 0.8
  (2.75:1, como la pestana de la carpeta): marfil en Archivo y `#C9A227` en
  Fiscal. En mono, todo `#0E1F18`.
- Caja del nombre 28 bajo la carpeta (y = 756 + 28k) y centrada en x = 500.

## Restricciones

`satcfdi_branding_tests` (ctest, label `branding`) recorre todos los SVG:
solo `svg`, `g` y formas; sin `text`, filtros, imagenes, gradientes,
mascaras, `href`, `url(...)`, `style` ni referencias a fuentes; colores
planos; menu solo negro (atributos y render); viewBox por carpeta. El
wordmark claro es marfil con exactamente un acento dorado (una pestana `rect`
2.75:1, <= 1 % del viewBox); el mono, un solo color; en el lockup el dorado
son solo las dos pestanas. El lockup contiene la marca y el wordmark
literales. `menu-18*.svg`/`menu-36*.svg` reciben las reglas de su tamano;
cualquier otro SVG nuevo sin regla falla.

## Regenerar

Herramienta `tools/branding` (`branding_render`, QSvgRenderer; se compila
siempre). ctest `satcfdi_branding_wordmark` regenera `marca/*.svg` en un
temporal y exige bytes iguales (si falla tras actualizar Qt o Inter,
regenerar y revisar el diff). Los targets que escriben archivos requieren la
opcion:

```bash
cmake -S . -B build-branding -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt \
      -DSATCFDI_BUILD_BRANDING=ON
cmake --build build-branding --target branding_wordmark   # reescribe marca/*.svg
cmake --build build-branding --target branding_hoja       # build-branding/branding/hoja-contactos.png
```

`-DSATCFDI_BRANDING_HOJA=<png>` cambia la salida de la hoja. Un PNG suelto:
`QT_QPA_PLATFORM=offscreen build-branding/tools/branding/branding_render render <svg> <alto> <png> [--fondo '#052E1C']`.

La conversion a trazos usa Qt (`QTextLayout` para la forma y el kerning,
`QRawFont::pathForGlyph` para los contornos) con el TTF versionado; falla si Qt
resuelve otra fuente. Tras regenerar, revisar el diff y correr ctest.
