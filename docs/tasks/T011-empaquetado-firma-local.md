# T011: Empaquetado y firma local

## Estado

Pendiente (borrador creado en el refinamiento de T010, 2026-10-04; refinar con `/task-refinement T011` antes de implementar).

## Prioridad y tamano

- Prioridad: Alta (bloquea T010).
- Tamano: Mediana.

## Objetivo

Producir un `SAT CFDI Downloader.app` autocontenido, sin dependencias de Homebrew, firmado con Developer ID y un perfil de larga vigencia, e instalado en `/Applications`, sobre el que T010 ejecuta la aceptacion del MVP.

## Contexto

Hoy el bundle depende del Qt y del OpenSSL de Homebrew y se firma de forma ad-hoc, u opcionalmente con un perfil de cuenta gratuita que vence cada 7 dias (`docs/development.md`). Al vencer ese perfil, lo esperable (no verificado) es que macOS impida el arranque de una app con `keychain-access-groups`. Para un worker desatendido que se usara durante anos eso no es viable. El usuario decidio inscribirse al Apple Developer Program.

## Decisiones

| ID | Decision | Estado | Fuente y motivo |
| --- | --- | --- | --- |
| D1 | Firma con certificado Developer ID Application y perfil Developer ID que incluya `keychain-access-groups`, ambos de la membresia del Apple Developer Program. Firma con `--timestamp`. | Aprobada por usuario | Refinamiento de T010. Evita la renovacion semanal y no cambia ADR 0010 ni T005. |
| D2 | El empaquetado es una tarea propia, previa a T010. | Aprobada por usuario | Refinamiento de T010. Separa la construccion del artefacto de su aceptacion. |
| D3 | `macdeployqt` con `-qmldir`; se copian `libssl` y `libcrypto` a `Contents/Frameworks` con los install names reescritos; `libxml2` queda del sistema. El empaquetado corre en un script o target aparte (`tools/package-macos.sh` o `satcfdi_package`) sobre una copia en staging, no en el POST_BUILD. | Recomendacion tecnica | `qt-platform-engineer`. No muta el bundle que usa `ctest`. |
| D4 | Firma por componente de adentro hacia afuera (dylibs, frameworks, plugins y QML, y por ultimo la app con entitlements, hardened runtime y perfil), sin `--deep` al firmar. Se retira `disable-library-validation` si todo queda firmado por el mismo Team; se conserva `allow-jit`. | Recomendacion tecnica | `qt-platform-engineer`. |
| D5 | Sin notarizacion ni DMG: build local instalada copiando a `/Applications`, con una sola copia registrada en Launch Services. | Recomendacion tecnica | `qt-platform-engineer`. No se distribuye a terceros. |

## Alcance

### Incluye

- Script o target de empaquetado y firma, documentado en `docs/development.md`.
- Verificacion de ausencia de rutas de Homebrew (`otool -L` recursivo sobre `Contents/`) y de carga en ejecucion (`QT_DEBUG_PLUGINS`, `DYLD_PRINT_LIBRARIES`).
- Instalacion en `/Applications` y limpieza de copias de build en Launch Services (`lsregister -u`; nunca `resetbtm`).
- Cierre de las pruebas que T004 dejo pendientes de un bundle firmado: notificacion con permiso concedido y Login Item real.

### No incluye

- Notarizacion, DMG, actualizacion automatica o distribucion a terceros.
- Cambios de comportamiento de la app.

## Dependencias

- Precondicion del usuario: membresia activa del Apple Developer Program, certificado Developer ID Application, App ID `mx.adenium.satcfdi-downloader` y perfil Developer ID con `keychain-access-groups`.
- `T004`, `T005`, y `T009` (el bundle final incluye el flujo SAT).

## Criterios de aceptacion

- [ ] `otool -L` recursivo sobre `Contents/` no muestra `/opt/homebrew` ni `/usr/local`, y en ejecucion ninguna biblioteca ni plugin se carga desde esas rutas.
- [ ] `codesign --verify --deep --strict` pasa. La app tiene Team ID, hardened runtime y los entitlements esperados (sin `disable-library-validation` si D4 se cumple), y el perfil embebido vence en mas de un ano.
- [ ] La app instalada en `/Applications` abre, guarda y lee una e.firma de prueba en el Keychain sin dialogos.
- [ ] El Login Item se registra para la copia de `/Applications`. Tras cerrar sesion y volver a entrar, la app arranca sin ventana.
- [ ] Una notificacion de prueba se entrega con el permiso concedido.
- [ ] El empaquetado es reproducible desde un commit limpio con los comandos documentados.

## Verificacion

Los comandos de `docs/development.md` (seccion de empaquetado) y la evidencia en `docs/reports/T011-empaquetado-AAAA-MM-DD.md`.

## Resultado

Pendiente.

## Riesgos y notas

- No esta verificado que pasa con el perfil Developer ID si se deja de pagar la membresia; se documenta como verificacion pendiente.
- El soporte de `keychain-access-groups` en perfiles Developer ID se confirma al crear el perfil. Si no lo admite, se requiere una decision nueva (ADR 0010).

## Referencias

- `docs/development.md`, `ADR 0009`, `ADR 0010`, `T004`, `T005`, `T010`
- `docs/meetings/T010-refinamiento/ronda-01-plataforma-sintesis.md`
