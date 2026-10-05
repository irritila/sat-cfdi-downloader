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
~/SAT-CFDI-Downloader/paquetes/{rfc}/{yyyy-mm}/{solicitud_id}/
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

La raiz de produccion es `~/SAT-CFDI-Downloader/paquetes`. Con `--data-dir`,
la raiz es `<data-dir>/paquetes`; las pruebas inyectan una raiz temporal. La
ruta no se configura como preferencia del usuario.

## Consecuencias

- La base local no crece por contenido binario de paquetes.
- Los archivos descargados siguen siendo navegables fuera de la app.
- No existe tabla de CFDI/XML ni UUID de comprobante en el MVP.
- Cambiar la ruta en el futuro requerira una decision y migracion de configuracion.

## Referencias

- `docs/requirements.md`
- `docs/architecture.md`
