# T025: Respaldo cifrado en la nube del usuario

## Estado

Completada (2026-10-24). T025.1 a T025.6 completadas (2026-10-22); prueba manual del usuario aprobada (2026-10-24).

## Prioridad y tamano

- Prioridad: media.
- Tamano: grande (~2-3 semanas); dividida en seis subtareas verticales (D19).

## Objetivo

Que el usuario pueda copiar un respaldo cifrado de la informacion de la app a una carpeta sincronizada de su servicio de nube y restaurarlo en la misma u otra Mac.

## Contexto

Hoy toda la informacion vive solo en el equipo (ADR 0002, 0004). Un respaldo a una carpeta controlada por el usuario (iCloud Drive, Dropbox, Google Drive, OneDrive) no es un backend del producto: la app solo escribe un archivo cifrado y el cliente de escritorio del servicio lo sube. Saca de la Mac datos fiscales (RFC, nombres, montos, XML), por lo que el cifrado es obligatorio y la e.firma no sale.

## Decisiones

| ID | Decision | Estado | Fuente y motivo |
| --- | --- | --- | --- |
| D1 | Mecanismo: carpeta sincronizada elegida por el usuario con el dialogo nativo; sin CloudKit ni APIs/OAuth de proveedores. La UI dice "copiado a la carpeta", nunca "subido a la nube". | Aprobada por usuario | Ronda 01. CloudKit exige programa de pago; OAuth exige registro, tokens y revision por proveedor. |
| D2 | Cifrado obligatorio con contrasena del usuario. Un respaldo se abre con la contrasena o con su clave de recuperacion asociada (D6); si se pierden ambas, es irrecuperable. | Aprobada por usuario | Ronda 01 seguridad. |
| D3 | La e.firma (`credentials/` y Keychain) nunca sale del equipo; al restaurar los perfiles requieren registrarla de nuevo. | Aprobada por usuario | Ronda 01 seguridad. Claves Keychain ThisDeviceOnly. |
| D4 | "Respaldar ahora" y respaldo diario opcional, evaluado al arrancar y al despertar. Para el diario se guarda en Keychain la clave derivada (no sincronizable, ThisDeviceOnly), nunca la contrasena. | Aprobada por usuario | Ronda 01. Un QTimer no basta con App Nap. |
| D5 | Restauracion incluida: verificacion completa en staging, reinicio de la app y datos previos conservados en `pre-restauracion-<fecha>`. | Aprobada por usuario | Ronda 01 macOS. |
| D6 | Se conservan las ultimas 5 copias, depuradas despues de publicar la nueva. Al crear la contrasena se ofrece una clave de recuperacion imprimible. | Aprobada por usuario | Usuario. |
| D7 | Contenido por lista blanca: snapshot de SQLite, ZIP de paquetes Descargado (sin XML extraidos) y preferencias sin rutas; nunca `credentials/`, Keychain, logs de aplicacion ni temporales. | Recomendacion tecnica | Rondas 01 y 02. |
| D8 | Un archivo por respaldo: `.partial` en la carpeta destino, `F_FULLFSYNC`, `rename` exclusivo a un nombre opaco con fecha (sin RFC); nunca se sobrescribe. Un `.partial` huerfano se borra. | Recomendacion tecnica | Rondas 01 y 02. |
| D9 | Formato propio en streaming (`AFB1`): cabecera versionada con parametros Argon2id (m=64 MiB, t=3, p=1, sal 16 B) y DEK aleatoria envuelta por la KEK de la contrasena y por la de la clave de recuperacion; datos en bloques de 1 MiB AES-256-GCM con AAD (cabecera, indice, bloque final); manifiesto cifrado con ruta, tamano y SHA-256. | Recomendacion tecnica | Rondas 01 y 02. Memoria constante con varios GB. |
| D10 | La captura se encola en `OperacionExecutor` (`VACUUM INTO` a staging privado y lista congelada de paquetes Descargado); el cifrado y la copia corren en un hilo propio del respaldo. | Recomendacion tecnica | Ronda 02. Ningun paquete queda a medias; pausa del worker breve. |
| D11 | Un ZIP ya faltante al congelar se anota en el manifiesto y el respaldo termina con advertencia; un ZIP que cambia o desaparece durante la lectura hace fallar el respaldo sin publicar nada. | Recomendacion tecnica | Conciliacion ronda 02. |
| D12 | OpenSSL minimo 3.2 (Argon2id) comprobado en CMake y al arrancar; sal fija por contrasena configurada, DEK aleatoria por respaldo. | Recomendacion tecnica | Ronda 02. Enlazado hoy 3.6.3. |
| D13 | Restaurar: se rechaza un esquema mas nuevo que la app; uno anterior se migra; se borran las credenciales en staging; el marcador se aplica tras la instancia unica y antes de la migracion T019, SQLite y el worker; se preserva el mtime de los ZIP; preferencias por lista blanca. | Recomendacion tecnica | Ronda 02. |
| D14 | La configuracion (carpeta y bookmark, diario, referencia Keychain, ultimo resultado) vive en `preferencias.ini`, seccion `respaldo`, que no se respalda. | Recomendacion tecnica | Ronda 02. Es propia del equipo. |
| D16 | Cambiar la contrasena conserva la clave de recuperacion: las copias anteriores se abren con la contrasena anterior o con esa clave; las nuevas, con la nueva o con la clave. Generar una clave nueva aplica solo a copias futuras. | Aprobada por usuario | Ronda 03 calidad; usuario 2026-10-21. |
| D19 | T025 es epica con T025.1 a T025.6, cada una con su ciclo de desarrollo. | Aprobada por usuario | Ronda 03; usuario 2026-10-21. |
| D17 | Retencion solo sobre archivos `AFB1` validos creados por esta instalacion (id de instalacion en la cabecera), ordenados por (fecha UTC, id aleatorio); archivos ajenos, enlaces y `.partial` no cuentan ni se borran. Un fallo al depurar no invalida el respaldo publicado. | Recomendacion tecnica | Ronda 03 calidad. |
| D18 | Un solo respaldo a la vez: un manual o diario solicitado durante otro en curso no inicia otro. | Recomendacion tecnica | Ronda 03 calidad. |
| D20 | Retencion con registro local de lo publicado (nombre, idRespaldo, fecha, tamano, identificador de archivo) fuera de SQLite y del respaldo; la app nunca descarga una copia para decidir: autentica la cabecera solo si el archivo esta en disco y, si esta solo en la nube, borra solo lo que coincide con su registro. | Aprobada por usuario | Revision macOS de T025.3, 2026-10-21. |
| D21 | El `.partial` se escribe en los datos de la app y se publica con rename exclusivo entre carpetas; si la carpeta destino esta en otro volumen, se escribe dentro de ella como antes. | Aprobada por usuario | Revision macOS de T025.3, 2026-10-21. |
| D22 | Mientras exista una carpeta `pre-restauracion-*` sin descartar, no se reconcilian (borran) del Keychain los secretos de e.firma que la base vigente no referencia; al descartarla el usuario (accion en T025.6), se reconcilia. Volver atras con `pre-restauracion` conserva la e.firma. | Aprobada por usuario | Desarrollo T025.5, 2026-10-22. |
| D23 | La contrasena del respaldo tiene al menos 12 caracteres al crearla o cambiarla (no se exige al restaurar ni al verificar la anterior). | Aprobada por usuario | Revision de seguridad de T025.6, 2026-10-22. |
| D15 | Dos ADR: "Respaldo cifrado a carpeta sincronizada del usuario" (enmienda acotada a ADR 0004) y "Restauracion transaccional al arranque"; README y requirements aclaran que un backend propio sigue fuera de alcance. | Recomendacion tecnica | Ronda 02. |

## Subtareas

| Tarea | Resultado | Depende de |
| --- | --- | --- |
| [T025.1](T025.1-formato-y-cifrado-respaldo.md) | Formato `AFB1`, KDF, envoltura de DEK, escritor y lector/verificador en streaming; ADR de respaldo | - |
| [T025.2](T025.2-captura-consistente.md) | Captura consistente: snapshot SQLite, lista congelada de paquetes, manifiesto | - |
| [T025.3](T025.3-respaldo-manual.md) | Respaldo manual a la carpeta elegida: validacion de destino, `.partial`, publicacion, retencion de 5, cancelacion | T025.1, T025.2 |
| [T025.4](T025.4-respaldo-diario.md) | Configuracion local, KEK en Keychain y respaldo diario al arrancar/despertar | T025.3 |
| [T025.5](T025.5-restauracion.md) | Restauracion: verificacion en staging, marcador, relanzamiento, matriz T019 y esquemas; ADR de restauracion | T025.1, T025.2 |
| [T025.6](T025.6-ui-y-documentacion-respaldo.md) | UI (pantalla o seccion de respaldo, progreso, estados y errores), textos y documentacion | T025.3, T025.4, T025.5 (puede avanzar con fakes) |

## Alcance

### Incluye

- Elegir la carpeta destino, crear la contrasena (con confirmacion y clave de recuperacion) y respaldar manual o diariamente.
- Restaurar desde un respaldo elegido en la misma u otra Mac.
- Estados visibles: ultimo respaldo, en curso con progreso y tamano, terminado con advertencias, y errores especificos (carpeta inexistente o sin escritura, volumen desmontado, espacio insuficiente, contrasena incorrecta, archivo alterado o incompleto, version mas nueva).

### No incluye

- Backend propio, CloudKit o integracion directa por API/OAuth con proveedores.
- Respaldo de la e.firma o de logs de aplicacion.
- Respaldo incremental o sin los ZIP.
- Sincronizacion entre equipos o fusion de datos al restaurar (la restauracion sustituye).

## Dependencias

- ADR 0002, 0004, 0006, 0009, 0010, 0014, 0016, 0018; T008 (reconciliacion de paquetes), T019 (migracion de carpeta).
- T011 debe empaquetar libcrypto >= 3.2.

## Criterios de aceptacion

Se reparten entre las subtareas (subtarea primaria entre corchetes). La epica cierra con T025.1 a T025.6 completadas y la prueba manual del usuario.

- [x] [T025.2/T025.5] Ida y vuelta: base sintetica (XAXX010101000) y 3 ZIP se respaldan y restauran con SHA-256 identicos e `integrity_check` = ok; una base sin perfiles ni paquetes tambien.
- [x] [T025.1] Ningun archivo publicado, `.partial`, staging ni log contiene en claro `XAXX010101000`, `/Users/usuario` ni `<cfdi:`; el respaldo no contiene archivos de `credentials/` ni la contrasena SAT.
- [x] [T025.1] Alterar 1 byte de cualquier bloque, truncar el ultimo o reordenar dos hace fallar la verificacion sin modificar los datos locales; contrasena o clave de recuperacion incorrecta falla con un mensaje generico; la clave de recuperacion abre el mismo respaldo; tras cambiar la contrasena se cumple D16.
- [x] [T025.1] Un manifiesto con `..`, rutas absolutas o enlaces se rechaza; una fuente ZIP enlazada nunca se sigue.
- [x] [T025.1] Durante el cifrado y la lectura de 3 GB, despues de completar Argon2id, la memoria adicional del pipeline es <= 32 MiB; el pico del KDF se mide y reporta aparte (prueba omitible en CI).
- [x] [T025.2] Con escrituras concurrentes durante la captura, el snapshot es consistente y el executor reanuda en menos de 1 s; un paquete Descargando no entra al manifiesto; un ZIP faltante produce "terminado con advertencias"; un ZIP que cambia tras congelarse hace fallar el respaldo sin publicar.
- [x] [T025.3] Con un adaptador de sistema de archivos que inyecta errores: carpeta inexistente, sin escritura, volumen desmontado y disco lleno muestran su estado especifico. Ante error manejado o cancelacion (en captura, cifrado o escritura) se elimina solo el `.partial` propio; tras terminacion forzada puede quedar solo ese `.partial`, que el siguiente arranque elimina sin seguir enlaces.
- [x] [T025.3] Tras publicar la sexta copia propia quedan exactamente las 5 propias mas recientes (D17); un `.afbak` ajeno, un enlace o un `.partial` ajeno permanecen; nunca se borra una copia antes de publicar la nueva; un nombre en conflicto no sobrescribe; un respaldo pedido durante otro no inicia otro (D18).
- [x] [T025.4] Con un respaldo vencido, `FakeReloj` y una senal inyectable de despertar programan una sola ejecucion; la clave en Keychain es no sincronizable y ThisDeviceOnly. Manual en el bundle firmado: tras despertar la Mac se ejecuta el respaldo vencido y durante una copia larga hay una actividad de sistema activa (`pmset -g assertions`).
- [x] [T025.5] Un esquema mas nuevo se rechaza antes de tocar nada; uno anterior se migra a la version actual; tras restaurar, las credenciales quedan vacias, los perfiles muestran "requiere registrar e.firma" y no se hacen llamadas SAT hasta registrarla.
- [x] [T025.5] Restaurar con una operacion SAT activa rechaza nuevas operaciones, solicita la cancelacion, espera el cierre, no aplica resultados posteriores y reinicia.
- [x] [T025.5] Restaurar con otro HOME resuelve los paquetes bajo la nueva raiz y conserva su mtime (sin reindexar); un corte en cualquier fase queda recuperable al siguiente arranque sin estado mixto; `pre-restauracion-<fecha>` queda intacta y no choca con una existente; la migracion T019 no elige la carpeta equivocada; el archivo se vuelve a verificar si cambia entre verificacion y sustitucion.
- [x] [T025.5] Leer un respaldo aun no descargado por el proveedor (simulado con un lector lento o bloqueante) no congela la UI y se puede cancelar.
- [x] [T025.6] Progreso, estados y errores visibles; la UI nunca afirma que el archivo se subio a la nube; mensajes y logs sin contrasena, claves, RFC, montos ni rutas personales.
- [x] [T025.1] CMake exige OpenSSL >= 3.2 y el flujo comprueba Argon2id antes de usarlo.
- [x] [T025.1/T025.5/T025.6] ADRs nuevos registrados; README y requirements actualizados.

## Verificacion

`ctest` con `QTemporaryDir` y datos sinteticos en cada subtarea; build `-Werror` y `git diff --check`. Manual del usuario con el bundle firmado: respaldo en iCloud Drive y en un proveedor File Provider (Dropbox, Google Drive u OneDrive), y restauracion en una carpeta de datos alterna (`--data-dir`) o en otra Mac. Solo el usuario maneja su e.firma y su cuenta de nube.

## Resultado

Respaldo cifrado (AFB1, Argon2id y AES-256-GCM) a la carpeta sincronizada que elige el usuario, manual o diario, con retencion de 5 copias, y restauracion transaccional al siguiente arranque (T025.1 a T025.6). Cada subtarea tiene su evidencia automatica. El usuario reporto el 2026-10-24 que la prueba manual con el bundle firmado y una carpeta de datos alterna funciono, sin usar e.firma.

Pendiente para decision posterior: R1 de T025.6 (la clave de recuperacion pendiente se pierde si la app termina antes de "Ya la guarde").

## Riesgos

- El cliente de sincronizacion puede estar pausado o sin sesion: la app no puede confirmar la subida.
- Respaldos de varios GB tardan en subir y ocupan cuota; cada copia es completa.
- Pico de 64 MiB de memoria por el KDF en cada respaldo manual (se mide aparte del pipeline).
- Olvidar contrasena y clave de recuperacion hace irrecuperables los respaldos.
- Interaccion de la restauracion con la migracion de carpeta de T019.

## Referencias

- `docs/meetings/T025-refinamiento/` (rondas 01 a 03, decisiones, bitacora).
- `docs/requirements.md`, `docs/architecture.md`, `docs/design/operational-rules.md`, ADR 0004, 0006, 0010, 0016.
