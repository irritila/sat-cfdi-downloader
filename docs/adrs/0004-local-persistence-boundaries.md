# ADR 0004: Separar base local, almacenamiento de secretos y carpeta de paquetes ZIP

## Estado

Accepted

## Contexto

El MVP debe guardar perfiles SAT, solicitudes, paquetes reportados/descargados, metadata y logs. Tambien debe proteger e.firma y guardar paquetes ZIP localmente.

El alcance termina en descargar paquetes ZIP. No incluye extraer, parsear, indexar ni consultar XML internos.

## Decision

Separar la persistencia local en tres fronteras:

- Base de datos local: perfiles SAT, solicitudes, paquetes, configuracion y `LogSolicitud`.
- Almacenamiento seguro de secretos: material sensible de e.firma y referencias de credenciales. El token SAT no se persiste en el MVP.
- Carpeta local de paquetes: archivos ZIP descargados desde SAT.

Los ZIP se guardan por defecto en:

```text
~/Archivo-Fiscal/paquetes/{rfc}/{yyyy-mm}/{solicitud_id}/
```

La ruta queda fija para el MVP. No se guarda como preferencia configurable en `ConfiguracionApp`.

La eliminacion local es virtual en base de datos. No borra automaticamente ZIPs descargados.

## Nota de detalle (T008)

Sin cambiar esta decision ni su estado, la estructura concreta de cada ZIP es:

```text
<raiz>/<RFC>/<yyyy-mm de FechaInicial>/<UUID local>/<archivo>.zip
```

`yyyy-mm` usa el mes de `FechaInicial`, incluso si el rango de la solicitud
cruza meses. El directorio de solicitud usa el UUID local canonico de la app,
no el identificador SAT.

El nombre parte de `id_paquete_sat`. Si cumple `[A-Za-z0-9._-]{1,100}` y no
empieza por `.`, se usa `<id>.zip`. En otro caso, cada caracter no permitido se
sustituye por `_`, el prefijo se trunca a 100 y se agrega
`--<16 hex de SHA-256 del id original en UTF-8>`. Cada componente mide como
maximo 255 bytes. La ruta persistida es relativa a la raiz y no admite
componentes absolutos ni `..`.

La raiz de produccion es `~/Archivo-Fiscal/paquetes`. Con `--data-dir`,
la raiz es `<data-dir>/paquetes`; las pruebas inyectan una raiz temporal. La
ruta no se configura como preferencia del usuario.

## Nota de detalle (T019)

T019 renombro la app a "Archivo Fiscal" y la raiz de produccion paso de
`~/SAT-CFDI-Downloader/paquetes` a `~/Archivo-Fiscal/paquetes` (T019 D3, D4).
La base guarda rutas relativas a la raiz, asi que no se reescribe SQLite. La
carpeta de datos interna (`AppDataLocation`, `Adenium/SAT CFDI Downloader`) no
cambia (T019 D2).

Al arrancar, solo la instancia primaria y antes de que el worker escriba (no
con `--data-dir`), la app migra la carpeta vieja sin copiar ni borrar ZIP: la
renombra con `renamex_np(..., RENAME_EXCL)`. Si `~/SAT-CFDI-Downloader` solo
contiene `paquetes` (y `.DS_Store`) se renombra completa; si contiene otras
cosas, se crea `~/Archivo-Fiscal` y se mueve solo `paquetes`. Ante conflicto
(las dos carpetas con contenido) o error, no toca nada, sigue usando la raiz
vieja y avisa en la ventana. El detalle de casos esta en T019 D4 y el contrato
en T019 D8.

Revertir a mano (por ejemplo, para volver a una build anterior a T019, que
crearia una raiz vacia y mostraria los ZIP como faltantes): con la app cerrada,

```bash
mkdir -p ~/SAT-CFDI-Downloader
mv ~/Archivo-Fiscal/paquetes ~/SAT-CFDI-Downloader/paquetes
rmdir ~/Archivo-Fiscal   # solo si quedo vacia
```

Una build con T019 vuelve a migrar en su siguiente arranque.

## Consecuencias

- La base local no crece por contenido binario de paquetes.
- Los archivos descargados siguen siendo navegables fuera de la app.
- No existe tabla de CFDI/XML ni UUID de comprobante en el MVP.
- Cambiar la ruta en el futuro requerira una decision y migracion de configuracion.

## Referencias

- `docs/requirements.md`
- `docs/architecture.md`
