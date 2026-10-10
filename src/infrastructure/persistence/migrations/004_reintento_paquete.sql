-- Migracion 004: intencion de reintento por paquete (T014.2 D2, ADR 0007
-- enmendado).
--
-- Convencion (DM2): UTF-8 sin BOM, comentarios solo en lineas completas,
-- cada sentencia termina con una linea que contiene unicamente punto y coma.
-- Sin BEGIN/COMMIT: el runner aplica el archivo en UNA transaccion
-- BEGIN IMMEDIATE (DM1); cualquier error revierte todo.
--
-- paquete_solicitud.reintento_pendiente_en: instante (UTC, mismo formato que
-- el resto de timestamps) en que el usuario pidio reintentar ESE paquete con
-- el monitoreo pausado; NULL sin intencion. ADD COLUMN no reconstruye la
-- tabla ni toca sus CHECK; las filas existentes quedan en NULL.
-- ---------------------------------------------------------------------------
ALTER TABLE paquete_solicitud ADD COLUMN reintento_pendiente_en TEXT NULL
    CHECK (reintento_pendiente_en IS NULL OR length(trim(reintento_pendiente_en)) > 0)
;
-- Intenciones por paquete pendientes, en orden de registro.
CREATE INDEX ix_paquete_solicitud_reintento_pendiente
    ON paquete_solicitud (reintento_pendiente_en)
    WHERE reintento_pendiente_en IS NOT NULL
      AND eliminado_en IS NULL
;
