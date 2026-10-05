-- Migracion 003: worker y ejecutor serial (T007, D7 y D8).
-- Diseno: docs/design/sqlite-physical-model.md, ADR 0017, ADR 0018.
--
-- Convencion (DM2): UTF-8 sin BOM, comentarios solo en lineas completas,
-- cada sentencia termina con una linea que contiene unicamente punto y coma.
-- Sin BEGIN/COMMIT: el runner aplica el archivo en UNA transaccion
-- BEGIN IMMEDIATE (DM1); cualquier error revierte todo.
--
-- 1. log_solicitud.tipo_evento: agrega 'envio_no_iniciado' (D7, ADR 0017) y
--    'verificacion_suspendida' (D8). SQLite no permite alterar un CHECK: se
--    reconstruye la tabla (procedimiento de "ALTER TABLE" de SQLite):
--    a) crear log_solicitud_v3 con la definicion NUEVA completa;
--    b) copiar todas las filas columna a columna (mismos valores, incluidos
--       los eliminados logicamente);
--    c) DROP TABLE log_solicitud (elimina tambien su indice);
--    d) RENAME log_solicitud_v3 -> log_solicitud;
--    e) recrear ix_log_solicitud_detalle con la misma definicion de 001.
--    Llaves foraneas: PRAGMA foreign_keys no puede cambiarse dentro de la
--    transaccion del runner (es un no-op), asi que la verificacion de FK sigue
--    ACTIVA durante la migracion. Es seguro porque log_solicitud solo es
--    tabla HIJA (FK -> solicitud_masiva): ninguna tabla la referencia, el DROP
--    no dispara acciones y la copia conserva solicitud_masiva_id validos; la
--    FK se declara igual en la tabla nueva. El RENAME (SQLite >= 3.26, sin
--    legacy_alter_table) no reescribe referencias porque no las hay. Las
--    pruebas ejecutan PRAGMA foreign_key_check e integrity_check tras migrar.
-- 2. solicitud_masiva: racha de fallas de verificacion (D8), sin reconstruir
--    (ADD COLUMN no toca CHECKs existentes):
--    - ultima_clave_falla_verificacion: "<fase>[:<codigo>]" o NULL;
--    - fallas_verificacion_iguales: contador de fallas consecutivas con esa
--      clave; 0 si y solo si la clave es NULL.
-- ---------------------------------------------------------------------------
CREATE TABLE log_solicitud_v3 (
    id TEXT NOT NULL PRIMARY KEY
        CHECK (length(id) = 36
               AND id GLOB '[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'),
    solicitud_masiva_id TEXT NOT NULL
        REFERENCES solicitud_masiva (id),
    tipo_evento TEXT NOT NULL
        CHECK (tipo_evento IN (
            'solicitud_creada',
            'duplicado_confirmado',
            'envio_iniciado',
            'solicitud_enviada',
            'envio_fallido',
            'envio_incierto',
            'verificacion_realizada',
            'verificacion_fallida',
            'paquetes_registrados',
            'descarga_iniciada',
            'paquete_descargado',
            'descarga_fallida',
            'descarga_interrumpida',
            'paquete_reconciliado',
            'paquete_vencido',
            'archivo_huerfano',
            'accion_pendiente_registrada',
            'accion_pendiente_descartada',
            'envio_no_iniciado',
            'verificacion_suspendida'
        )),
    origen TEXT NOT NULL
        CHECK (origen IN ('worker', 'usuario', 'recuperacion')),
    origen_codigo_sat TEXT NULL
        CHECK (origen_codigo_sat IS NULL
               OR origen_codigo_sat IN ('creacion', 'verificacion', 'descarga')),
    codigo_sat TEXT NULL,
    mensaje_sat TEXT NULL,
    payload_resumen_json TEXT NULL
        CHECK (payload_resumen_json IS NULL
               OR CASE WHEN json_valid(payload_resumen_json)
                       THEN json_type(payload_resumen_json) = 'object'
                       ELSE 0 END),
    creado_en TEXT NOT NULL,
    eliminado_en TEXT NULL,
--  Un codigo SAT siempre declara su origen, el mensaje SAT requiere codigo.
    CONSTRAINT ck_log_codigo_sat CHECK (
        (origen_codigo_sat IS NULL) = (codigo_sat IS NULL)
        AND (mensaje_sat IS NULL OR codigo_sat IS NOT NULL)
    )
)
;
INSERT INTO log_solicitud_v3 (
    id, solicitud_masiva_id, tipo_evento, origen, origen_codigo_sat, codigo_sat,
    mensaje_sat, payload_resumen_json, creado_en, eliminado_en
)
SELECT
    id, solicitud_masiva_id, tipo_evento, origen, origen_codigo_sat, codigo_sat,
    mensaje_sat, payload_resumen_json, creado_en, eliminado_en
FROM log_solicitud
;
DROP TABLE log_solicitud
;
ALTER TABLE log_solicitud_v3 RENAME TO log_solicitud
;
-- Logs del detalle en orden cronologico, tambien sirve la FK (igual que 001).
CREATE INDEX ix_log_solicitud_detalle
    ON log_solicitud (solicitud_masiva_id, creado_en)
;
-- ---------------------------------------------------------------------------
-- solicitud_masiva: racha de fallas de verificacion (D8).
-- ---------------------------------------------------------------------------
ALTER TABLE solicitud_masiva ADD COLUMN ultima_clave_falla_verificacion TEXT NULL
    CHECK (ultima_clave_falla_verificacion IS NULL
           OR length(trim(ultima_clave_falla_verificacion)) BETWEEN 1 AND 64)
;
ALTER TABLE solicitud_masiva ADD COLUMN fallas_verificacion_iguales INTEGER NOT NULL DEFAULT 0
    CHECK (fallas_verificacion_iguales >= 0
           AND (fallas_verificacion_iguales = 0) = (ultima_clave_falla_verificacion IS NULL))
;
