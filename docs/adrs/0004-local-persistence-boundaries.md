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

## Consecuencias

- La base local no crece por contenido binario de paquetes.
- Los archivos descargados siguen siendo navegables fuera de la app.
- No existe tabla de CFDI/XML ni UUID de comprobante en el MVP.
- Cambiar la ruta en el futuro requerira una decision y migracion de configuracion.

## Referencias

- `docs/requirements.md`
- `docs/architecture.md`
