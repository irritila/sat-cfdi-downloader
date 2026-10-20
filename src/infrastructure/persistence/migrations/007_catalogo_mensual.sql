-- Migracion 007: catalogo mensual guiado (T023.2, ADR 0019).
--
-- Convencion (DM2): UTF-8 sin BOM, comentarios solo en lineas completas,
-- cada sentencia termina con una linea que contiene unicamente punto y coma.
-- Sin BEGIN/COMMIT: el runner aplica el archivo en UNA transaccion
-- BEGIN IMMEDIATE (DM1); cualquier error revierte todo.
--
-- Solo agrega: una columna NULL en solicitud_masiva (filas existentes quedan
-- NULL y no acreditan cobertura) y dos tablas nuevas. FK sin ON DELETE
-- (DC10): la eliminacion es logica y el vinculo de lote se conserva.
--
-- ---------------------------------------------------------------------------
-- conjunto_paquetes_vacio_confirmado_en: instante de la verificacion SAT
-- Terminada que persistio un conjunto VACIO de IdPaquete, escrito en la misma
-- transaccion que la verificacion. No se infiere de numero_cfdi = 0.
-- ---------------------------------------------------------------------------
ALTER TABLE solicitud_masiva
    ADD COLUMN conjunto_paquetes_vacio_confirmado_en TEXT NULL
;
-- ---------------------------------------------------------------------------
-- lote_descarga_guiada: una accion del flujo guiado sobre un mes de un
-- perfil. El perfil se identifica por perfil_sat_id (no por RFC): un perfil
-- eliminado y recreado con el mismo RFC no hereda sus lotes. mes: YYYY-MM.
-- ---------------------------------------------------------------------------
CREATE TABLE lote_descarga_guiada (
    id TEXT NOT NULL PRIMARY KEY
        CHECK (length(id) = 36
               AND id GLOB '[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'),
    perfil_sat_id TEXT NOT NULL
        REFERENCES perfil_sat (id),
    mes TEXT NOT NULL
        CHECK (length(mes) = 7
               AND mes GLOB '[0-9][0-9][0-9][0-9]-[0-1][0-9]'
               AND substr(mes, 6, 2) BETWEEN '01' AND '12'),
    creado_en TEXT NOT NULL
)
;
-- Historial por perfil y mes, mas reciente primero.
CREATE INDEX ix_lote_guiado_perfil_mes
    ON lote_descarga_guiada (perfil_sat_id, mes, creado_en DESC)
;
-- ---------------------------------------------------------------------------
-- lote_descarga_guiada_solicitud: tramo SAT exacto (inclusivo, por segundos,
-- hora Centro sin offset) de cada solicitud del lote. Por ADR 0020 un lote
-- puede tener varias solicitudes por direccion (una por hueco); sus tramos no
-- se traslapan (lo valida LoteDescargaGuiadaRepository::insertar bajo BEGIN
-- IMMEDIATE: DM2 no admite triggers). Una solicitud pertenece a lo sumo a un
-- lote.
-- ---------------------------------------------------------------------------
CREATE TABLE lote_descarga_guiada_solicitud (
    lote_id TEXT NOT NULL
        REFERENCES lote_descarga_guiada (id),
    solicitud_masiva_id TEXT NOT NULL
        REFERENCES solicitud_masiva (id),
    tipo_cfdi TEXT NOT NULL
        CHECK (tipo_cfdi IN ('emitidos', 'recibidos')),
    fecha_inicial_sat TEXT NOT NULL
        CHECK (length(fecha_inicial_sat) = 19
               AND fecha_inicial_sat GLOB '[0-9][0-9][0-9][0-9]-[0-1][0-9]-[0-3][0-9]T[0-2][0-9]:[0-5][0-9]:[0-5][0-9]'),
    fecha_final_sat TEXT NOT NULL
        CHECK (length(fecha_final_sat) = 19
               AND fecha_final_sat GLOB '[0-9][0-9][0-9][0-9]-[0-1][0-9]-[0-3][0-9]T[0-2][0-9]:[0-5][0-9]:[0-5][0-9]'),
    PRIMARY KEY (lote_id, solicitud_masiva_id),
    CONSTRAINT ck_lote_guiado_tramo CHECK (fecha_inicial_sat <= fecha_final_sat)
)
;
-- Una solicitud pertenece a un solo lote; sirve tambien la FK y la consulta
-- "la solicitud es guiada" del catalogo mensual.
CREATE UNIQUE INDEX ux_lote_guiado_solicitud
    ON lote_descarga_guiada_solicitud (solicitud_masiva_id)
;
