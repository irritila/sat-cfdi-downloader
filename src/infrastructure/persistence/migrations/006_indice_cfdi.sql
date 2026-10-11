-- Migracion 006: indice local de CFDI por paquete (T015.2, T015 D2/D3/D10).
--
-- Convencion (DM2): UTF-8 sin BOM, comentarios solo en lineas completas,
-- cada sentencia termina con una linea que contiene unicamente punto y coma.
-- Sin BEGIN/COMMIT: el runner aplica el archivo en UNA transaccion
-- BEGIN IMMEDIATE (DM1); cualquier error revierte todo.
--
-- Datos DERIVADOS de los ZIP de paquete_solicitud: se pueden borrar y
-- regenerar sin perder informacion. Tablas nuevas: no tocan datos existentes
-- ni cambian paquete_solicitud. Contienen datos fiscales (RFC, nombres,
-- montos) en la base local (D3); nunca van a logs (D11).
--
-- ---------------------------------------------------------------------------
-- indice_cfdi_paquete: estado de indexacion de un paquete Descargado.
-- Huella del ZIP leido (tamano, mtime en ms UTC y SHA-256 hex minusculo),
-- version del parser y conteos (XML leidos, CFDI ilegibles y no soportados,
-- XML que no son CFDI y entradas omitidas por motivo, como objeto JSON).
-- Un paquete sin fila se considera Pendiente. Borrado en cascada con el
-- paquete; el borrado logico del paquete lo dispara el trigger de abajo.
-- ---------------------------------------------------------------------------
CREATE TABLE indice_cfdi_paquete (
    paquete_solicitud_id TEXT NOT NULL PRIMARY KEY
        REFERENCES paquete_solicitud (id) ON DELETE CASCADE,
    estado TEXT NOT NULL
        CHECK (estado IN ('Pendiente', 'Indexado', 'OrigenNoDisponible', 'Ilegible')),
    tamano_bytes INTEGER NULL
        CHECK (tamano_bytes IS NULL OR tamano_bytes >= 0),
    modificado_en_ms INTEGER NULL,
    sha256 TEXT NULL
        CHECK (sha256 IS NULL
               OR (length(sha256) = 64 AND sha256 NOT GLOB '*[^0-9a-f]*')),
    version_parser INTEGER NOT NULL
        CHECK (version_parser > 0),
    xml_leidos INTEGER NOT NULL DEFAULT 0
        CHECK (xml_leidos >= 0),
    cfdi_ilegibles INTEGER NOT NULL DEFAULT 0
        CHECK (cfdi_ilegibles >= 0),
    cfdi_no_soportados INTEGER NOT NULL DEFAULT 0
        CHECK (cfdi_no_soportados >= 0),
    xml_no_cfdi INTEGER NOT NULL DEFAULT 0
        CHECK (xml_no_cfdi >= 0),
    entradas_omitidas INTEGER NOT NULL DEFAULT 0
        CHECK (entradas_omitidas >= 0),
    omitidas_json TEXT NOT NULL DEFAULT '{}'
        CHECK (CASE WHEN json_valid(omitidas_json)
                    THEN json_type(omitidas_json) = 'object'
                    ELSE 0 END),
    causa TEXT NULL
        CHECK (causa IS NULL OR length(trim(causa)) BETWEEN 1 AND 64),
    actualizado_en TEXT NOT NULL,
--  Huella completa o ausente; Indexado e Ilegible la exigen, OrigenNoDisponible no.
    CONSTRAINT ck_indice_cfdi_huella CHECK (
        (tamano_bytes IS NULL) = (modificado_en_ms IS NULL)
        AND (tamano_bytes IS NULL) = (sha256 IS NULL)
        AND (estado NOT IN ('Indexado', 'Ilegible') OR sha256 IS NOT NULL)
        AND (estado <> 'OrigenNoDisponible' OR sha256 IS NULL)
    ),
--  La causa existe si y solo si el paquete no se pudo leer.
    CONSTRAINT ck_indice_cfdi_causa CHECK (
        (estado IN ('OrigenNoDisponible', 'Ilegible')) = (causa IS NOT NULL)
    )
)
;
-- ---------------------------------------------------------------------------
-- cfdi_indexado: resumen de cada CFDI de un paquete (T015.1). Unicidad por
-- (paquete, posicion de la entrada en el ZIP): un UUID puede repetirse entre
-- paquetes y solicitudes. solicitud_masiva_id se copia del paquete (misma
-- transaccion) para listar por solicitud sin JOIN. Importes como texto
-- exacto del XML (nunca REAL). texto_busqueda: RFC, nombres, UUID, serie y
-- folio en minusculas y sin acentos.
-- ---------------------------------------------------------------------------
CREATE TABLE cfdi_indexado (
    paquete_solicitud_id TEXT NOT NULL
        REFERENCES indice_cfdi_paquete (paquete_solicitud_id) ON DELETE CASCADE,
    posicion INTEGER NOT NULL
        CHECK (posicion >= 0),
    solicitud_masiva_id TEXT NOT NULL
        REFERENCES solicitud_masiva (id),
    estado_parseo TEXT NOT NULL
        CHECK (estado_parseo IN ('Completo', 'Parcial', 'VersionNoSoportada', 'Ilegible')),
    version TEXT NULL,
    tipo_comprobante TEXT NULL
        CHECK (tipo_comprobante IS NULL OR tipo_comprobante IN ('I', 'E', 'T', 'N', 'P')),
    fecha TEXT NULL,
    serie TEXT NULL,
    folio TEXT NULL,
    rfc_emisor TEXT NULL,
    nombre_emisor TEXT NULL,
    regimen_emisor TEXT NULL,
    rfc_receptor TEXT NULL,
    nombre_receptor TEXT NULL,
    regimen_receptor TEXT NULL,
    uso_cfdi TEXT NULL,
    domicilio_receptor TEXT NULL,
    lugar_expedicion TEXT NULL,
    subtotal TEXT NULL,
    descuento TEXT NULL,
    total TEXT NULL,
    moneda TEXT NULL,
    tipo_cambio TEXT NULL,
    metodo_pago TEXT NULL,
    forma_pago TEXT NULL,
    uuid TEXT NULL,
    complementos_json TEXT NULL
        CHECK (complementos_json IS NULL
               OR CASE WHEN json_valid(complementos_json)
                       THEN json_type(complementos_json) = 'array'
                       ELSE 0 END),
    campos_invalidos_json TEXT NULL
        CHECK (campos_invalidos_json IS NULL
               OR CASE WHEN json_valid(campos_invalidos_json)
                       THEN json_type(campos_invalidos_json) = 'array'
                       ELSE 0 END),
    texto_busqueda TEXT NOT NULL,
    PRIMARY KEY (paquete_solicitud_id, posicion)
)
;
-- Lista por solicitud ordenada por fecha (descendente) con desempate estable.
CREATE INDEX ix_cfdi_indexado_lista
    ON cfdi_indexado (solicitud_masiva_id, fecha DESC, paquete_solicitud_id, posicion)
;
-- Eliminacion logica de un paquete (eliminar solicitud, T007 D5): borra su
-- indice en la misma transaccion; cfdi_indexado cae en cascada.
CREATE TRIGGER tr_paquete_solicitud_eliminado_indice_cfdi
AFTER UPDATE OF eliminado_en ON paquete_solicitud
FOR EACH ROW WHEN NEW.eliminado_en IS NOT NULL
BEGIN
    DELETE FROM indice_cfdi_paquete WHERE paquete_solicitud_id = NEW.id;
END
;
