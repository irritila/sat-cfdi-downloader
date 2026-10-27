# T020: Logo de la aplicacion

## Estado

Completada (2026-10-26).

## Prioridad y tamano

Media. Mediana: tres cortes (activos SVG, icono compilado, menu bar).

## Objetivo

Dar a "Archivo Fiscal" su logo "Carpeta con pestana" y sus derivados: icono de app de macOS 26 con capas y variantes, icono de menu bar de plantilla con sus estados, y los SVG de marca y wordmark para el video.

## Contexto

Refinamiento con GPT-6-Astra como lider conceptual (decision del usuario) y revisiones de especialista-macos y especialista-motion-design. Tres rondas de conceptos; el usuario eligio "Carpeta con pestana" (ronda 03). Hoy el bundle no tiene icono propio y el menu bar dibuja un documento con flecha en `IconoMenuBar.cpp`. Sesion: `docs/meetings/T020-refinamiento/` (hoja de contactos y SVG `svg/r03-*.svg`).

## Decisiones

| ID | Decision | Estado | Fuente |
|---|---|---|---|
| D1 | GPT-6-Astra (Codex `gpt-6-astra`) lidera conceptualmente el logo, como rol provisional; sustituye a `especialista-identidad-visual` en Fable. | Aprobada por usuario | 2026-10-26 |
| D2 | El logo comunica "documentos guardados"; se conserva el acento dorado `#C9A227` (<= 5 %). | Aprobada por usuario | 2026-10-26 |
| D3 | Concepto "Carpeta con pestana": carpeta verde con dos hojas marfil, frente oblicuo y pestana dorada, sobre fondo `#052E1C`. Base: `respuestas/03-pulido.md` y `svg/r03-*.svg`. | Aprobada por usuario | 2026-10-26 (ronda 03) |
| D4 | Icono de app como `.icon` de Icon Composer versionado (capas SVG), compilado con `xcrun actool` a `Assets.car` + `AppIcon.icns` de respaldo. Fondo a sangre `#052E1C` sin squircle propio (lo aplica macOS 26). Capas: fondo, carpeta, pestana, hoja posterior, hoja anterior, frente. Variantes: clara, oscura (fondo hacia `#0E1F18`), tintada y transparente. El `.icon` lo escriben los agentes como archivos (sin manejar Icon Composer); el paquete versionado es la fuente. El usuario lo abre en Icon Composer para la revision visual inicial y tras cada cambio de capas. | Recomendacion tecnica | `respuestas/04-macos.md` |
| D5 | El icono compilado (`Assets.car`, `AppIcon.icns`) se versiona; un script lo regenera con `actool` cuando cambia el logo. El build normal no depende de Xcode. Un manifiesto versionado registra hash de cada fuente (`AppIcon.icon` y SVG), comando, `xcodebuild -version` y hash de cada salida; la prueba del build normal falla si las fuentes no coinciden con el manifiesto (icono desactualizado). | Aprobada por usuario (versionar); manifiesto: recomendacion tecnica | 2026-10-26; revision de calidad 06 |
| D6 | Menu bar: se reemplaza el glifo de `IconoMenuBar.cpp` por la carpeta (plantilla negra con alfa, 18 px @1x y 36 px @2x, coordenadas enteras) y se redisenan las insignias de estado (Trabajando, En pausa, Requiere atencion) para no recortar el frente. Se mantiene `QSystemTrayIcon` y `textoEstadoIcono`. | Recomendacion tecnica | `respuestas/04-macos.md` (bloqueante resuelto aqui) |
| D7 | La marca sin contenedor (`r03-marca`) solo se usa en el video, nunca como icono de app (se confundiria con una carpeta verde de Finder). | Recomendacion tecnica | `respuestas/04-macos.md` |
| D8 | Wordmark "Archivo Fiscal" en Inter SemiBold (OFL) convertido a trazos, `#F4F1E8`, lockup vertical (marca arriba) solo para intro y cierre del video; version monocroma `#0E1F18`. SF Pro no se convierte por licencia. | Aprobada por usuario | 2026-10-26; `respuestas/05-motion.md` |
| D9 | SVG de video: `marca-video.svg` con `viewBox="176 196 648 624"`, cinco SVG de capa con el mismo viewBox, sin filtros ni degradados; halo y atenuacion del fondo viven en la herramienta. Sobre fondo generativo la carpeta no baja de 300 px de alto en 1440p. | Recomendacion tecnica | `respuestas/05-motion.md` |
| D11 | Inter SemiBold y su `OFL.txt` se versionan en `resources/branding/fonts/Inter/` con version y hash; la conversion a trazos la hace un script versionado. | Recomendacion tecnica | Revision de calidad 06 |
| D12 | Las capturas de Dock, Finder y Cmd-Tab son evidencia manual del usuario en su equipo (ningun agente manipula Dock, Finder ni preferencias). El video (T024.4) no usa estas capturas: sigue con ilustraciones rotuladas. | Recomendacion tecnica | Revision de calidad 06; AGENTS |
| D13 | Desarrollo: el menu bar usa la propuesta A de Astra (carpeta en silueta, insignias en esquina), renderizada desde SVG con `QSvgRenderer`; el wordmark usa el tratamiento c (Inter SemiBold con tracking ajustado y los puntos de las "i" como pestanas, la de "Fiscal" en dorado). La conversion a trazos la hace una herramienta Qt (`QRawFont`) en lugar de fontTools. | Aprobada por usuario | 2026-10-26 (desarrollo) |
| D10 | La animacion de portada y cierre (capas, 120 + 150 cuadros, atenuacion del fondo, presupuesto dentro de 8:00) se implementa en T024.4, no en T020. | Recomendacion tecnica | `respuestas/05-motion.md`; T024 D7 |

## Alcance

### Incluye

- Corte 1, activos: SVG finales en `resources/branding/` (maestro, capas del icono, `marca-video.svg`, capas de video, wordmark y lockup), Inter y su licencia (D11), script de conversion a trazos, herramienta minima con `QSvgRenderer` para PNG de control y prueba de restricciones SVG.
- Corte 2, icono compilado: `resources/macos/AppIcon.icon`, script `actool` con `--check` y manifiesto (D5), `Assets.car` y `AppIcon.icns` versionados, `Info.plist.in` con `CFBundleIconName` y `CFBundleIconFile`, copia a Resources antes de la firma.
- Corte 3, menu bar: glifo y cuatro estados en `IconoMenuBar.cpp` (o render del SVG con `QSvgRenderer`), con goldens.

### No incluye

- Animacion e integracion en el video (T024.4).
- Pantallas de la app con el logo (barra de titulo, primer uso).

## Dependencias

- T019 (nombre y bundle). Xcode 26 solo para regenerar el icono compilado.
- T011 para verificar el icono en la firma Developer ID; mientras tanto se verifica con la firma de desarrollo de `build-signed`.

## Trabajo esperado

- Lider conceptual (Astra): ajustes finales de forma, capas y wordmark.
- qt-platform-engineer: `.icon`, script `actool`, CMake/plist, herramienta de render.
- qt-interface-engineer o qt-engineer: menu bar e insignias.
- especialista-macos: revision de capturas y variantes.

## Criterios de aceptacion

- [x] Los SVG de marca no tienen `<text>`, filtros ni imagenes externas; una prueba lo comprueba.
- [x] El wordmark y el lockup no contienen `<text>` ni referencias a fuentes; Inter y `OFL.txt` estan versionados con su version y hash.
- [x] `resources/macos/AppIcon.icon` esta versionado con capas SVG. `regenerar_app_icon --check` regenera en un directorio temporal: `AppIcon.icns` debe ser identico en bytes; `Assets.car` se compara por `assetutil --info` normalizado (actool incrusta UUID y fecha), junto con, variantes esperadas y los PNG extraidos del `.icns`. Dos regeneraciones con la misma herramienta quedan registradas (coinciden o no, sin presentarlo como garantia entre versiones).
- [x] Una prueba del build normal (sin Xcode) falla si el hash de alguna fuente no coincide con el manifiesto.
- [x] El bundle contiene `Assets.car` y `AppIcon.icns`; `plutil -p` muestra `CFBundleIconName` y `CFBundleIconFile`; `assetutil --info` lista variantes Aqua, DarkAqua y Tintable; los recursos se copian antes de firmar y `codesign --verify --deep --strict` pasa sobre `build-signed` conservando Team ID, hardened runtime y entitlements. La verificacion con Developer ID queda en T011.
- [x] El build sin Xcode completo sigue funcionando. Evidencia sustituta: ningun paso del build invoca `actool`, `iconutil`, `xcodebuild` ni `assetutil`; no probado en un equipo solo con Command Line Tools.
- [x] Evidencia manual del usuario (D12): capturas en Dock, Finder (16, 32 y grande) y Cmd-Tab en modo claro, oscuro, tintado y transparente, sin contenedor gris, y junto a una carpeta verde en un directorio temporal; se registran version de macOS, pantalla y escala, hash del bundle y pasos de refresco de Launch Services. Lo no probado se marca "No probado".
- [x] Los cuatro estados del menu bar se renderizan a 18 y 36 px solo en negro con alfa, con goldens de mascara alfa a 18 y 36 px (alfa parcial solo en curvas y diagonal, RGB siempre negro) y una asercion de que los pixeles reservados del frente siguen visibles en cada estado; capturas en apariencia clara y oscura; tooltip y nombre accesible siguen saliendo de `textoEstadoIcono`.
- [x] `marca-video.svg`, capas de video, wordmark y lockup existen con el viewBox de D9 y texto a trazos.
- [x] Build `-Werror`, `ctest` y `git diff --check` pasan.
- [x] El usuario aprueba el icono final en todos sus tamanos y el menu bar.

## Verificacion

Pruebas de restricciones SVG y render de menu bar; script `actool` con comparacion; `plutil`, `assetutil`, `codesign`; capturas revisadas por especialista-macos y aprobadas por el usuario.

## Resultado

Activos en `resources/branding/` (icono por capas, marca y capas de video, wordmark y lockup a trazos, Inter con OFL, menu bar en 18 y 36 px con sus cuatro estados), herramienta `tools/branding/` con pruebas de restricciones SVG y regresion del wordmark. Icono de app en `resources/macos/AppIcon.icon` compilado a `AppIcon-compilado/` con `tools/app-icon/regenerar_app_icon` y manifiesto verificado en el build normal; el bundle copia el icono antes de firmar (`LINK_DEPENDS` fuerza refirma si cambia). `IconoMenuBar` renderiza los SVG con goldens de alfa (tolerancia +-2, frente exacto). Build `-Werror`, `ctest` 52/52, `codesign --verify --deep --strict` OK. Revisiones: especialista-macos apto con cambios; calidad aprobado. El usuario probo Icon Composer, Dock, Finder, Cmd-Tab y menu bar y aprobo (macOS 26.3). Riesgos: variantes Clear/Tinted con translucidez apagada, `.icns` hasta 256 px, build solo con CLT no probado; Developer ID en T011.

## Referencias

- `docs/meetings/T020-refinamiento/`, `docs/tasks/T018-roles-produccion-video.md` (D5), `docs/tasks/T019-renombrar-archivo-fiscal.md`, `docs/tasks/T024.4-video-tutorial-nuevo.md`.
