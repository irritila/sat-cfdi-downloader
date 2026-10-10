-- Migracion 005: dedupe de avisos de vencimiento de e.firma (T014.3 D2, D3).
--
-- Convencion (DM2): UTF-8 sin BOM, comentarios solo en lineas completas,
-- cada sentencia termina con una linea que contiene unicamente punto y coma.
-- Sin BEGIN/COMMIT: el runner aplica el archivo en UNA transaccion
-- BEGIN IMMEDIATE (DM1); cualquier error revierte todo.
--
-- aviso_vencimiento_efirma: un aviso ya enviado por (perfil, vigencia,
-- umbral). La clave incluye vigente_hasta (notAfter de la credencial, UTC):
-- reemplazar la e.firma cambia la vigencia y reinicia el dedupe sin borrar
-- filas. No guarda RFC, certificado ni material. Tabla nueva: no toca datos
-- existentes.
-- ---------------------------------------------------------------------------
CREATE TABLE aviso_vencimiento_efirma (
    perfil_sat_id TEXT NOT NULL
        REFERENCES perfil_sat (id),
    vigente_hasta TEXT NOT NULL
        CHECK (length(trim(vigente_hasta)) > 0),
    umbral_dias INTEGER NOT NULL
        CHECK (umbral_dias IN (30, 7)),
    avisado_en TEXT NOT NULL
        CHECK (length(trim(avisado_en)) > 0),
    PRIMARY KEY (perfil_sat_id, vigente_hasta, umbral_dias)
)
;
