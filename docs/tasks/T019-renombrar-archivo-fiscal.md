# T019: Renombrar la aplicacion a "Archivo Fiscal"

## Estado

Completada (2026-10-18).

## Prioridad y tamano

- Prioridad: Media (conviene antes de que T011 instale la copia definitiva).
- Tamano: Mediano (textos visibles, identidad visible de macOS y migracion de la carpeta de paquetes).

## Objetivo

Que la aplicacion se llame "Archivo Fiscal" en todo lo visible, sin perder datos, credenciales ni permisos del usuario.

## Contexto

El usuario decidio el nombre durante T017 (D18): "SAT CFDI Downloader" suena raro. El guion del video ya usa "Archivo Fiscal" y "Archivo-Fiscal/paquetes", pero la app, sus ilustraciones y sus carpetas siguen con el nombre anterior. El usuario ya tiene datos reales: base y credenciales cifradas en AppDataLocation, ZIP en `~/SAT-CFDI-Downloader/paquetes` y e.firmas en el Keychain del grupo `<TEAM>.mx.adenium.satcfdi-downloader`. Inventario y analisis en `docs/meetings/T019-refinamiento/` (`contexto-base.md`, `ronda-01-macos-identidad.md`).

## Decisiones

| ID | Decision | Estado | Fuente y motivo |
| --- | --- | --- | --- |
| D1 | Se mantiene el bundle id `mx.adenium.satcfdi-downloader` y todo lo que deriva de el: grupo `keychain-access-groups`, prefijo de los servicios Keychain, canal de instancia unica, App ID y perfil de aprovisionamiento. | Aprobada por usuario (2026-10-18) | `especialista-macos` (ronda 01): cambiarlo obliga a migrar secretos y a reautorizar notificaciones e inicio automatico, sin beneficio visible. |
| D2 | Se mantienen `organizationName` "Adenium" y `applicationName` "SAT CFDI Downloader" porque definen la ruta de datos (AppDataLocation con la base, `credentials` y logs). Lo visible usa `QGuiApplication::setApplicationDisplayName("Archivo Fiscal")`. Un comentario y una prueba fijan el `applicationName` interno. | Aprobada por usuario (2026-10-18) | Ronda 01: migrar la carpeta interna arriesga separar la base de las credenciales cifradas; no es visible. |
| D3 | La carpeta de paquetes pasa de `~/SAT-CFDI-Downloader/paquetes` a `~/Archivo-Fiscal/paquetes` con una migracion automatica al arrancar (ver D4). La base guarda rutas relativas, asi que no se reescribe SQLite. | Aprobada por usuario (2026-10-18) | Es visible en Finder y el video ya usa la ruta nueva. |
| D4 | Migracion de paquetes: solo en la instancia primaria, despues de adquirir la instancia unica y antes de que el worker escriba; no aplica con `--data-dir`. Sin marcador: el estado se deduce del sistema de archivos con `lstat` (sin seguir enlaces). Renombra con `renamex_np(..., RENAME_EXCL)`; nunca copia ni borra ZIP. Casos: origen ausente -> crear la raiz nueva (0700); solo origen -> renombrar `~/SAT-CFDI-Downloader` completo si solo contiene `paquetes` (y `.DS_Store`), o crear `~/Archivo-Fiscal` y renombrar solo `paquetes` si contiene otras cosas; destino vacio de un intento previo -> `rmdir` y reintentar; origen y destino con contenido, destino que es archivo o enlace, `EXDEV`, permisos u otro error de `lstat`, listado, `mkdir`, `rmdir` o rename -> no tocar nada, usar la raiz vieja y avisar; ya migrado -> nada. Si `~/SAT-CFDI-Downloader` es un enlace simbolico y su objetivo contiene `paquetes`, se renombra el enlace completo sin listar ni modificar el objetivo; si no contiene `paquetes`, no hay origen y no se toca. Si `paquetes` es un enlace, se renombra el enlace. Justo antes de renombrar el padre completo se vuelve a listar; si cambio, se renombra solo `paquetes` (riesgo residual documentado). Si el padre o `paquetes` no es directorio, alli no puede haber ZIP: se crea y usa la raiz nueva sin tocar el archivo viejo y se avisa. Si falla entre crear el padre y renombrar, se elimina el padre vacio. Si el origen no existe y no se puede crear la raiz nueva, se conserva el comportamiento actual (la app arranca) y se avisa. Permisos 0700 solo en directorios nuevos. Sin symlink con el nombre viejo. | Recomendacion tecnica (ajustada en desarrollo por las revisiones de calidad) | Ronda 01 y `docs/meetings/T019-desarrollo/respuestas/01-calidad.md`. `rename(2)` reemplaza en silencio un directorio vacio; `RENAME_EXCL` lo evita. |
| D8 | Contrato: el bootstrap recibe un migrador inyectable y devuelve en `ArranquePreparado` un `ResolucionRaizPaquetes` (raiz efectiva, estado: `Nueva`, `YaNueva`, `MigradaPadre`, `MigradaPaquetes`, `UsaLegadaPorConflicto`, `UsaLegadaPorError`, y aviso opcional). La politica vive en `app_core`; `lstat`, listado, `mkdir`, `rmdir` y `renamex_np(RENAME_EXCL)` en el adaptador de infraestructura (`FileOps`). La raiz efectiva alimenta almacenamiento, recuperacion de temporales, existencia de ZIP, indexador CFDI, PDF y "Abrir carpeta de paquetes". El aviso lo presenta un view model dedicado (no `mensajeFinder`): visible al abrir la ventana (sin forzarla si arranca por inicio automatico), accesible, no descartable durante la sesion y con rutas como `~/...`. El diagnostico va a una categoria de log de arranque sin rutas absolutas, RFC ni nombres de ZIP; no se escribe `LogSolicitud`. | Recomendacion tecnica | `qt-quality-engineer` (ronda 02). |
| D5 | `CFBundleName` y `CFBundleDisplayName` = "Archivo Fiscal". El target y el ejecutable siguen siendo `satcfdi_app`, y el modulo QML y los prefijos `SATCFDI_*` no cambian. El bundle instalado se llamara `Archivo Fiscal.app`; eso lo aplica T011 (staging), que se actualiza junto con ADR 0009. | Recomendacion tecnica | Ronda 01: Finder muestra el nombre del directorio; renombrar el bundle no invalida la firma. |
| D6 | Se actualizan solo los documentos vivos: `README.md`, `docs/requirements.md`, `docs/architecture.md`, `docs/development.md`, `docs/design/` vigente, `docs/manual/usuario.md`, ADR vigentes que describen comportamiento actual (0004 rutas, 0009 nombre del bundle) y tareas pendientes (T010, T011). Tareas completadas, ADR historicos y bitacoras quedan como estan. | Aprobada por usuario (2026-10-18) | Conserva la historia. |
| D7 | El video tutorial se regraba con el nombre nuevo en ilustraciones (menu bar, notificaciones); la voz no cambia, asi que no hay llamadas a OpenAI. | Recomendacion tecnica | El guion ya dice "Archivo Fiscal". |

## Alcance

### Incluye

- Textos visibles: titulo de ventana (`Main.qml`), "Salir de Archivo Fiscal" (`MenuBarDefinicion.h`), tooltip y estado del icono (`MacOSMapeos.cpp`), notificacion de prueba (`AppLifecycleController.cpp`), titulo de los dialogos fatales (`main.cpp:20` a `applicationDisplayName()`), creador del PDF (`RenderizadorPdfCfdi.cpp`), `SATCFDI_BUNDLE_NAME` y descripcion del proyecto en `CMakeLists.txt`.
- `setApplicationDisplayName` y la prueba que fija `applicationName` (D2).
- Migracion de la carpeta de paquetes (D3, D4) con aviso en la UI ante conflicto o fallo.
- Ilustraciones de `tools/capturas-manual` y `tools/video-tutorial`; capturas del manual que muestran el nombre; regrabacion del video (D7).
- Pruebas que comparan los textos viejos.
- Documentos vivos (D6).

### No incluye

- Cambiar bundle id, Keychain, AppDataLocation, target, ejecutable o modulo QML (D1, D2, D5).
- Logo e icono (T020).
- Instalar `Archivo Fiscal.app` en `/Applications` y re-registrar el login item desde ahi (T011).
- Poner al dia el resto del manual y las capturas no relacionadas con el nombre (T021).

## Dependencias

- T017 completada. Coordinar con T011 (staging y ADR 0009), T020 (logo) y T021 (manual).
- Perfil de aprovisionamiento de `build-signed` vigente para la verificacion firmada (vencia el 2026-10-18).

## Trabajo esperado

1. Identidad visible y textos, con sus pruebas.
2. Migracion de paquetes en el bootstrap con pruebas unitarias sobre un home inyectado.
3. Ilustraciones, capturas que muestran el nombre y regrabacion del video.
4. Documentos vivos.

## Criterios de aceptacion

- [x] `plutil -p` del Info.plist muestra `CFBundleName` y `CFBundleDisplayName` "Archivo Fiscal" y `CFBundleIdentifier` `mx.adenium.satcfdi-downloader`; en la build firmada, `codesign -d --entitlements` conserva el grupo `<TEAM>.mx.adenium.satcfdi-downloader`.
- [x] `QStandardPaths::AppDataLocation` sigue terminando en `Adenium/SAT CFDI Downloader` (prueba unitaria) y `applicationDisplayName()` es "Archivo Fiscal".
- [x] Titulo de ventana, menu de la app, "Salir de Archivo Fiscal", tooltip y estados del icono, notificacion de prueba, dialogos fatales y creador del PDF usan "Archivo Fiscal" (pruebas de `MacOSMapeos`, presentacion y PDF actualizadas).
- [x] Pruebas unitarias de la migracion con home inyectado y `FileOps` simulado: origen ausente (crea raiz nueva; si `mkdir` falla, arranca y avisa); solo origen con `paquetes` y `.DS_Store` (`MigradaPadre`); con otros archivos en el padre (`MigradaPaquetes`, el padre viejo queda sin `paquetes`); destino vacio previo; destino con contenido, archivo o enlace (sin cambios, `UsaLegadaPorConflicto` y aviso); origen enlace simbolico (se renombra el enlace, el objetivo intacto); origen que no es directorio; fallos de `lstat`, listado, `mkdir`, `rmdir`, rename y `EXDEV` (sin cambios, `UsaLegadaPorError`); permisos 0700 solo en directorios nuevos; idempotencia en un segundo arranque.
- [x] En un sistema de archivos real: un ZIP centinela y un `.part` conservan su inode tras migrar, ningun archivo se copia ni se borra, y la recuperacion posterior escanea la raiz efectiva y limpia solo temporales propios.
- [x] Integracion con migracion exitosa y con raiz vieja por conflicto: existencia de ZIP, apertura de PDF y "Abrir carpeta de paquetes" usan la raiz efectiva; con `--data-dir` no se toca ninguna ruta del home.
- [x] El aviso de conflicto o error se ve al abrir la ventana, es accesible, no se descarta durante la sesion y muestra las rutas como `~/...`; el log de arranque no contiene rutas absolutas, RFC ni nombres de ZIP.
- [x] La migracion corre antes de arrancar el worker, solo en la instancia primaria y no con `--data-dir` (prueba de orden en el bootstrap).
- [x] Con los datos reales del usuario (comprobacion manual del usuario): tras el primer arranque existe `~/Archivo-Fiscal/paquetes` y no queda `~/SAT-CFDI-Downloader` (si solo contenia `paquetes`; si tenia otros archivos, queda sin `paquetes`), los perfiles y solicitudes previos se ven igual, un ZIP y un PDF de una solicitud previa se abren, y una e.firma existente sigue lista sin reimportarla ni dialogos del Keychain.
- [x] Build firmada (manual): una notificacion de prueba llega con el permiso existente y remitente "Archivo Fiscal", y el inicio automatico sigue habilitado.
- [x] Una lista versionada de coincidencias permitidas (`applicationName` interno y su prueba, ruta de origen de la migracion y sus pruebas, `tools/sat_spike`, documentos historicos) y un script comprueban que `git grep` de "SAT CFDI Downloader" y "SAT-CFDI-Downloader" no encuentra nada fuera de ella.
- [x] Las ilustraciones del manual y del video muestran "Archivo Fiscal"; el video regrabado pasa la verificacion D16 de T017 (manifiesto regenerado si cambian cuadros) sin llamadas a OpenAI.
- [x] Build `-Werror`, `ctest` completo y `git diff --check` pasan.

## Verificacion

1. `ctest` completo con las pruebas nuevas de migracion, identidad y textos.
2. `plutil -p` y `codesign -d --entitlements` sobre la build firmada.
3. Prueba manual del usuario con sus datos reales (migracion, e.firma, ZIP, PDF, notificacion, inicio automatico), antes respaldando `~/SAT-CFDI-Downloader`.
4. `git grep` del nombre y la ruta viejos.
5. Regrabacion del video y `video_tutorial_verificar`.

## Resultado

- Nombre visible "Archivo Fiscal" (`CFBundleName`, `CFBundleDisplayName`, `applicationDisplayName()`) con bundle id, grupo de Keychain, `applicationName` y AppDataLocation conservados (D1, D2).
- Migracion automatica de `~/SAT-CFDI-Downloader/paquetes` a `~/Archivo-Fiscal/paquetes` por renombre exclusivo, sin copiar ni borrar (`MigracionRaizPaquetes`, D4), con aviso no descartable en conflicto o error.
- Verificador `tools/verificar-nombre` con lista versionada de coincidencias permitidas; documentos vivos, capturas 10, 11 y 15 y video actualizados (D16 OK, sin OpenAI).
- Verificacion: `ctest` 23/23 (migracion 57/57), build `-Werror`, `git diff --check`; build firmada con `plutil`/`codesign` correctos; comprobacion manual del usuario con sus datos reales: funciona.

## Riesgos

- Que alguien cambie `applicationName` y la app arranque con una base vacia: lo evitan el comentario y la prueba (D2).
- Volver a una build anterior tras migrar crea una raiz vacia y los ZIP aparecen como faltantes; se revierte renombrando la carpeta a mano (documentarlo).
- El nombre viejo puede quedar en cache en Notificaciones o en Items de inicio hasta que Launch Services re-registre la app (`lsregister -f`).
- El perfil de aprovisionamiento vencido bloquea la verificacion firmada.

## Referencias

- `docs/meetings/T019-refinamiento/` (contexto-base, ronda 01, bitacora).
- `docs/tasks/T017-video-tutorial.md` (D18), `docs/tasks/T011-empaquetado-firma-local.md`, `docs/tasks/T020-logo.md`, `docs/tasks/T021-manual-capturas-al-dia.md`.
- ADR 0004, 0009 y 0010.
