-- Migracion 002: metadata no secreta de credencial_sat (T005, DA4).
-- Diseno: docs/design/sqlite-physical-model.md
--
-- Convencion (DM2): UTF-8 sin BOM, comentarios solo en lineas completas,
-- cada sentencia termina con una linea que contiene unicamente punto y coma.
-- Sin BEGIN/COMMIT: el runner aplica el archivo en una transaccion.
--
-- Columnas anulables solo por compatibilidad con filas previas a 002; toda
-- escritura nueva (CredencialSatRepository) exige las tres. No guardan RFC,
-- estado derivado ni material: numero de serie y vigencia (UTC ISO-8601 con
-- milisegundos y sufijo Z) del certificado.
ALTER TABLE credencial_sat ADD COLUMN numero_serie TEXT NULL
    CHECK (numero_serie IS NULL OR length(trim(numero_serie)) BETWEEN 1 AND 64)
;
-- Formato UTC `yyyy-MM-ddTHH:mm:ss.zzzZ` (GLOB de longitud fija, 24).
ALTER TABLE credencial_sat ADD COLUMN vigente_desde TEXT NULL
    CHECK (vigente_desde IS NULL
           OR vigente_desde GLOB '[0-9][0-9][0-9][0-9]-[01][0-9]-[0-3][0-9]T[0-2][0-9]:[0-5][0-9]:[0-5][0-9].[0-9][0-9][0-9]Z')
;
-- Vigencia completa o ausente, y orden desde < hasta.
ALTER TABLE credencial_sat ADD COLUMN vigente_hasta TEXT NULL
    CHECK ((vigente_hasta IS NULL AND vigente_desde IS NULL)
           OR (vigente_hasta IS NOT NULL AND vigente_desde IS NOT NULL
               AND vigente_hasta GLOB '[0-9][0-9][0-9][0-9]-[01][0-9]-[0-3][0-9]T[0-2][0-9]:[0-5][0-9]:[0-5][0-9].[0-9][0-9][0-9]Z'
               AND vigente_desde < vigente_hasta))
;
