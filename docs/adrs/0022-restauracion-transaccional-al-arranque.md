# ADR 0022: Restauracion transaccional al arranque

## Estado

Accepted

Complementa ADR 0021 (respaldo cifrado), ADR 0006 y ADR 0010 (secretos; D22) y ADR 0004 (fronteras de persistencia), sin cambiarlos. Precisa el orden de arranque de ADR 0016 y T019 (ADR 0004, nota T019).

## Contexto

T025 (D5, D13, D15) pide restaurar un respaldo AFB1 en la misma u otra Mac. La restauracion sustituye la base, los paquetes ZIP y las preferencias por lista blanca; no fusiona datos ni restaura la e.firma (D3).

Hay cuatro dificultades:

- La base vive en la carpeta de datos (`AppDataLocation` o `--data-dir`) y los paquetes en `~/Archivo-Fiscal/paquetes` o en la raiz legada de T019. Pueden estar en volumenes distintos, asi que la sustitucion no es un solo `rename`.
- Con la app en marcha, el ejecutor, el worker, el indice y el respaldo escriben en la base y en la raiz.
- El `.afbak` puede estar en una carpeta de File Provider sin descargar. Leerlo puede tardar mucho o bloquear.
- Un corte de luz o un cierre forzado a mitad no debe dejar una mezcla de datos viejos y restaurados.

## Decision

### Dos momentos

1. **Preparar y solicitar, con la app en marcha** (`RestauracionService`, en `application`, sin E/S propia). La preparacion corre en un hilo de trabajo y nunca toca los datos actuales.
2. **Sustituir en el siguiente arranque** (`RestauracionArranque`, en `app_core`), dentro de `prepararProceso`. Corre despues de adquirir la instancia unica y antes de `MigracionRaizPaquetes` (T019), del bootstrap de SQLite y del worker (D13).

### Preparacion

- **Stagings** privados (0700, del euid, vacios), en el mismo volumen que cada destino:
  - `<padre de la raiz de paquetes>/.restauracion-<id>.noindex`: la extraccion completa. El sufijo evita que Spotlight la indexe. La raiz se toma canonica (sin enlaces). Una raiz que es punto de montaje se rechaza.
  - `<datos>/restauracion/staging-<id>`: la base y las preferencias extraidas. Llegan con `rename` si comparten volumen o con copia y `fsync` si no.
  - Ambos se excluyen de Time Machine. Se usa el xattr `com.apple.metadata:com_apple_backup_excludeItem` con el bplist `com.apple.backupd`, que es lo que escribe `NSURLIsExcludedFromBackupKey` (comprobado byte a byte contra `tmutil addexclusion`). Asi `app_core` no necesita Objective-C ni Foundation. Si el volumen no admite xattr, solo se registra un aviso.
- **Espacio**: antes de crear nada se exige en el volumen de la raiz el tamano del `.afbak` mas un margen (10 %, minimo 64 MiB). Si los datos estan en otro volumen (`st_dev` distinto), se exige ahi el margen minimo antes de crear nada, y el tamano de la base mas el margen antes de copiarla. Si falta, `EspacioInsuficiente`.
- **Marcador**: el marcador `preparando` se escribe antes de crear los stagings. Asi todo staging queda referenciado y el arranque puede borrarlo.
- **Credencial**: se prueba la contrasena y, si falla y el texto es una clave de recuperacion valida, la clave (D16). El archivo se reabre en cada intento. El error de clave es siempre generico. El texto tecleado se limpia en cuanto se tiene la KEK (o se agotan los intentos).
- **Verificacion** (D5): `ContenedorRespaldo::extraer` autentica todo el contenedor. El staging solo se usa si devuelve exito. Despues se comprueba:
  - los metadatos (formato 1);
  - un esquema mas nuevo que el conocido, que se rechaza antes de tocar nada (por los metadatos o por `schema_migrations`);
  - que solo haya `base/satcfdi.sqlite3`, `preferencias/preferencias.ini` y `paquetes/<ruta valida>`.
- **Base** (`SqliteBaseRestauracion`, conexion propia en el hilo de trabajo):
  - `integrity_check` antes de migrar;
  - `sqlite_master` (type, name, tbl_name y sql normalizado, sin `sqlite_*`) igual al que producen las migraciones conocidas para esa version, construido en una base en memoria. Un trigger, vista, tabla o indice extra o distinto se rechaza (`EsquemaInvalido`). Se compara otra vez despues de migrar;
  - migracion de un esquema anterior con `SqliteMigrationRunner`;
  - `DELETE` de `aviso_vencimiento_efirma` y `credencial_sat` (D3), con lo que el perfil queda `SinCredencial` y el ejecutor no llama a SAT por el (ADR 0018);
  - despues del COMMIT, 0 filas en `credencial_sat` y `aviso_vencimiento_efirma`;
  - `integrity_check` y `foreign_key_check` otra vez;
  - `journal_mode=DELETE`, para que quede un solo archivo.
- **mtime**: cada ZIP recibe con `futimens` el `modificado_en_ms` de la huella del indice (T015.2) si el SHA-256 y el tamano coinciden. Si no, la fecha de descarga. Asi el indice no reindexa.
- **Cierre de la preparacion**: `fsync` (`F_FULLFSYNC`) de cada archivo y carpeta, recorriendo por descriptor (`openat` con `O_NOFOLLOW`), y huella de los stagings. La huella exige que cada staging solo contenga lo que se promueve. Combina el SHA-256 y el tamano de la base, el arbol de `paquetes/` (ruta, tamano, inodo, mtime y ctime de cada ZIP, independiente de donde este la carpeta) y el SHA-256 de las preferencias. Como no depende de la ubicacion, se puede recalcular a mitad de la promocion. Despues se comprueba que el `.afbak` no cambio durante la lectura y se escribe el marcador `preparado`.
- **Resumen para la UI** (T025.6): fecha e id autenticados del respaldo, instalacion de origen, esquema antes y despues, conteos, credenciales retiradas y mtimes conservados.

### Lectura lenta o bloqueante

- El hilo de trabajo es independiente (no se une) y solo usa estado compartido.
- `cancelar()` resuelve el future de inmediato con `Cancelado`, aunque una lectura siga bloqueada.
- Cuando la lectura vuelve, el hilo ve la cancelacion y descarta su staging.
- Mientras ese hilo viva, una preparacion nueva devuelve `EnCurso`.
- El contenedor observa la cancelacion por bloque.
- Al salir (R2), main destruye el composition root, con lo que se cierran el ejecutor, el dispatcher y la base. Si `RestauracionService::hilosDesacopladosVivos() > 0` (`debeTerminarSinDestructores()`), por ejemplo con una lectura aun bloqueada en `fileproviderd`, termina con `_exit()` en lugar de volver de main. Asi no espera al hilo ni destruye objetos estaticos que este aun use.

### Solicitud

1. Antes de detener nada, se comprueba que ni el `.afbak` (stat) ni la huella del staging cambiaron. Si cambiaron, se descarta la preparacion y hay que verificar otra vez.
2. `detenerApp`:
   - el worker deja de programar;
   - se cierran el diario, el gestor y el servicio de respaldo;
   - se detienen el indice y la vista previa;
   - el ejecutor rechaza operaciones nuevas, pide la cancelacion cooperativa de la actual con plazo 0 y termina su hilo. Un resultado posterior no se aplica (ADR 0018).
3. Marcador `solicitado`, que vuelve a comprobar la huella, escrito con `fsync`.
4. `OSIntegration::solicitarRelanzamiento()` y salida ordenada. Si el relanzamiento no queda programado, el marcador se aplica en el siguiente arranque manual.

Si el marcador `solicitado` no se pudo escribir con la app ya detenida, se sale igual y no se lanza nada desacoplado (R1). El marcador sigue en `preparado` y el siguiente arranque descarta el staging.

### Diario de fases (marcador)

`<datos>/restauracion/marcador.json` (0600). Cada fase se escribe de forma atomica (temporal, `fsync`, `rename`, `fsync` de la carpeta) **antes** del paso siguiente:

| Fase | Al arrancar |
| --- | --- |
| `preparando`, `preparado` | No hubo confirmacion: se borran los stagings y el marcador (Descartada). |
| `solicitado` | Comprueba que la raiz del marcador sea una de esta sesion (nueva o legada del home, o `<data-dir>/paquetes`) y que la huella del staging no cambio. Si algo no cuadra, descarta sin tocar nada. Elige `pre-restauracion-<fechaUTC>`; si existe en cualquiera de los dos lados usa `-2`, `-3`... y nunca sobrescribe. |
| `pre_elegida` | Crea las carpetas `pre-restauracion` (0700, `mkdir` exclusivo): en la carpeta de datos y junto a la raiz (una sola si coinciden). Solo adopta una carpeta vacia, o con nuestra marca del mismo id (creada en un intento previo). Una que exista con otro contenido da Error sin tocar nada: nunca se marca ni se usa. Les escribe la marca de propiedad y las excluye de Time Machine. Su nombre no cambia. |
| `moviendo_actuales` | Con `renamex_np(RENAME_EXCL)`: base, `-wal`, `-shm` y `credentials/` a la de datos; la raiz a `<pre>/paquetes`; copia de `preferencias.ini`. Destino presente = paso hecho. |
| `actuales_movidos` | Si se retoma aqui, reverifica la huella del staging; si cambio, revierte. Despues escribe `promoviendo`. |
| `promoviendo` | Si se retoma aqui, reverifica cada parte donde este (en el staging o ya promovida); si cambio, revierte. Promueve la base y `paquetes/` del staging con renames en el mismo volumen. Anota el id en `<datos>/restauracion/aplicadas.json`. Aplica de las preferencias restauradas solo la lista blanca (`facturas/ultimoPerfil`; si falta se retira la del equipo). |
| `promovido` | Borra stagings y marcador: Aplicada. |
| `revirtiendo` | Devuelve lo restaurado al staging (solo mientras lo actual siga en `pre-restauracion`) y despues lo actual a su lugar. Restaura las preferencias, retira las carpetas `pre-restauracion` vacias, stagings y marcador: Revertida. |

- **Validacion en todas las fases** (seguridad 1): el marcador es entrada no confiable.
  - `raizPaquetes` debe ser una de las raices de esta sesion. Si no lo es en `preparando`, `preparado` o `solicitado`, solo se borran el marcador y el staging de datos (rutas fijas bajo `<datos>/restauracion`): Descartada. En fases posteriores, Error sin tocar nada.
  - `preDatos` debe ser exactamente `<datos>/pre-restauracion-<yyyyMMddTHHmmssZ>[-N]`, con la misma fecha que `fechaPre`, y `prePaquetes` la ruta con ese nombre junto a la raiz. Deben estar vacias antes de `pre_elegida`. Si no cumple, Error sin tocar nada.
  - Desde `moviendo_actuales`, cada carpeta pre debe llevar nuestra marca con el mismo id (se escribe antes de mover nada). Si no la lleva, Error sin tocar nada. Al revertir tambien se acepta una carpeta ya devuelta (vacia o retirada), y solo se devuelve contenido desde carpetas con nuestra marca.
  - Los stagings tienen nombres fijos derivados del id. Al preparar de nuevo, una preparacion previa cuya raiz no es la de la sesion solo se descarta bajo `<datos>/restauracion`.
- **Reverificacion**: en cada arranque se vuelve a comprobar la huella antes de cada paso que use el staging (calidad 1). Desde `solicitado` hasta `actuales_movidos`, sobre el staging; en `promoviendo`, parte por parte.
- **Durabilidad entre volumenes** (R3): si la carpeta de datos y la raiz estan en volumenes distintos, un `fsync` de carpeta fallido despues de mover o promover es un fallo y se revierte.
- **Fallos**: un fallo hacia adelante pasa a `revirtiendo`. Si tampoco se puede revertir, o el marcador es invalido, `prepararProceso` devuelve Error y la app no arranca. Nunca trabaja con datos mezclados. El siguiente arranque reintenta.

### Lo que es del equipo

No se sustituye ni entra al respaldo:

- la seccion `respaldo` de `preferencias.ini`, incluido `idInstalacion` (D14);
- la carpeta `respaldo/` (`publicados.json` y parciales, D20);
- el Keychain.

`credentials/` actual va a `pre-restauracion` y deja de estar activa: los perfiles restaurados requieren registrar la e.firma (D3).

### Datos previos y e.firma (T025 D22)

- **Marca de propiedad**: `RestauracionArranque` escribe en cada carpeta `pre-restauracion-*` (de forma atomica y antes de mover nada) la marca `.restauracion.json`, con el id de la restauracion y las dos rutas. La de la carpeta de datos es el registro y apunta a la hermana junto a la raiz de paquetes, que puede estar en otro volumen o junto a la raiz legada.
- **Que cuenta como datos previos**: solo un directorio real (no enlace) del usuario con ese nombre y una marca valida que coincida. Ademas el id de la marca debe estar en el registro local `<datos>/restauracion/aplicadas.json`, que se escribe al promover (seguridad 4). Una marca copiada o fabricada no cuenta, y tampoco una carpeta con ese nombre sin marca o un enlace: no cuentan ni se tocan. Al descartar o revertir, el id sale del registro.
- **Reconciliacion bloqueada**: mientras exista alguna carpeta con nuestra marca valida, registrada o no, o el registro `aplicadas.json` exista pero no se pueda leer (enlace, no regular, mayor de 1 MiB o corrupto), la reconciliacion de e.firma del arranque (`CredencialesSatServicePersistido::reconciliarSalvo`, sobre `SecretStore::reconciliar`) no se ejecuta. La comprobacion corre dentro de la tarea del dispatcher, como su primera tarea. Se registra una linea saneada (`credencial.reconciliar: omitida ...`). Asi volver a mano a `pre-restauracion` conserva la e.firma: sus contenedores y sus items de Keychain siguen siendo legibles.
- **Descartar**: `AppCompositionRoot::descartarDatosPrevios()` (para T025.6):
  - en un hilo del pool, borra recursivamente y sin seguir enlaces solo las carpetas propias, primero la hermana y al final la de datos (la del registro);
  - si ya no queda ninguna, encola la reconciliacion y devuelve `ResultadoDescarteDatosPrevios`: `Descartados`, `SinDatosPrevios`, `Incompleto` (no reconcilia) o `ErrorReconciliacion`, con carpetas borradas, ajenas y generaciones eliminadas.
- **Consulta para la UI**: `hayDatosPrevios()` dice si la reconciliacion sigue bloqueada (datos previos o registro ilegible). Descartar solo borra las carpetas registradas. Con un registro perdido o danado, las carpetas propias siguen bloqueando, y el usuario las retira a mano (T025.6 lo explica).
- **Reversion manual** (R8/N2): las carpetas `pre-restauracion` estan excluidas de Time Machine y la exclusion viaja con la carpeta. Si el usuario renombra una de ellas para usarla como datos actuales, debe quitar la exclusion (`tmutil removeexclusion <ruta>`) o mover solo su contenido a la carpeta original.
- **Reversion**: al revertir se retiran el id del registro y las marcas antes de borrar las carpetas vacias.
- **Borrado** de stagings y datos previos por descriptor: `openat(O_DIRECTORY|O_NOFOLLOW)`, `fdopendir` y `unlinkat` sobre el padre.

### Interaccion con T019

- La sustitucion usa la raiz efectiva de la sesion que preparo y corre antes de la migracion de T019. Al arrancar, T019 ve el resultado final, nunca un hueco.
- Con la raiz nueva, la migracion queda en `ya_nueva`.
- Con la legada (por error previo), `pre-restauracion` queda junto a la legada y T019 mueve solo `paquetes` (los restaurados) a la nueva.
- Con conflicto, T019 sigue usando la legada, que es la que se restauro.

## Consecuencias

- La restauracion necesita espacio libre para una copia completa en cada volumen (se valida antes de preparar), y los datos anteriores ocupan espacio en `pre-restauracion` hasta que el usuario los descarte.
- Una raiz de paquetes que es punto de montaje no se puede restaurar con renames. Se rechaza con un error de destino.
- El `.afbak` solo se vuelve a autenticar en la preparacion. Al arrancar no hay clave, asi que se reverifica el staging ya autenticado (huella con ctime y SHA-256 de la base).
- Si el usuario abre la app con otro `--data-dir` u otro HOME, una restauracion pendiente de la otra carpeta no se aplica. Con el mismo directorio pero otra raiz, se descarta sin tocar nada fuera de la carpeta de datos; la extraccion junto a la otra raiz queda como resto privado.
- Un marcador manipulado en fases avanzadas impide arrancar (Error) hasta que el usuario lo retire. Es preferible a mover o borrar carpetas ajenas.
- El hilo de preparacion es desacoplado: un hilo bloqueado en la lectura puede sobrevivir al servicio hasta que el proveedor responda o el proceso termine. Su staging lo limpia el siguiente arranque.
- Mientras existan datos previos sin descartar, el almacen de secretos conserva generaciones de e.firma que la base vigente no usa (D22). Ocupan poco espacio y desaparecen al descartar.

## Verificacion

`satcfdi_respaldo_restauracion_tests` (labels `respaldo;restauracion`) cubre:

- ida y vuelta con 3 ZIP y con base vacia en otro HOME/`--data-dir` (SHA-256, mtime, `integrity_check`);
- clave de recuperacion tecleada y contrasena incorrecta;
- esquema mas nuevo rechazado (por metadatos y por base) y esquema anterior migrado;
- `SinCredencial` sin llamadas SAT;
- operacion SAT activa al solicitar;
- falta de relanzamiento;
- lector bloqueante y lento cancelables sin congelar la UI;
- cambios del `.afbak` y del staging;
- corte simulado en cada paso del arranque y durante la reversion;
- `pre-restauracion` existente;
- preparacion sin confirmar;
- matriz T019 (nueva, legada, conflicto);
- raiz distinta;
- orden en `prepararProceso`;
- marcador invalido;
- la e.firma del almacen no se borra con datos previos sin descartar y si se reconcilia al descartarlos, sin tocar `pre-restauracion-*` ajenas ni enlazadas (D22);
- marcador manipulado en cada fase (raiz y pre-restauracion ajenas);
- trigger, vista, tabla o indice ajenos en la base;
- staging alterado al retomar `actuales_movidos` y `promoviendo`;
- colision de `pre-restauracion` en ambos lados con la raiz en otro directorio;
- falta de espacio;
- marca sin registro;
- pre-restauracion preexistente con contenido y nombre valido (no se adopta);
- registro `aplicadas.json` borrado, corrupto o demasiado grande (la reconciliacion sigue bloqueada);
- preparacion previa con raiz ajena;
- un respaldo posterior sin `pre-restauracion` ni `credentials/`;
- exclusion de Time Machine y `.noindex`;
- indicador de salida con `_exit()`.
