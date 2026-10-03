-- Migracion 001: esquema inicial SQLite del MVP.
-- Diseno: docs/design/sqlite-physical-model.md
--
-- Convencion (DM2): UTF-8 sin BOM, comentarios solo en lineas completas,
-- cada sentencia termina con una linea que contiene unicamente punto y coma.
-- Sin BEGIN/COMMIT: el runner aplica todo el archivo en una transaccion
-- BEGIN IMMEDIATE y es propietario de schema_migrations (DM1).
-- Sintaxis limitada a SQLite >= 3.9.0 con JSON1 (DM4).
--
-- ---------------------------------------------------------------------------
-- perfil_sat: RFC/contribuyente operado por el usuario local.
-- ---------------------------------------------------------------------------
CREATE TABLE perfil_sat (
    id TEXT NOT NULL PRIMARY KEY
        CHECK (length(id) = 36
               AND id GLOB '[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'),
    rfc TEXT NOT NULL
        CHECK (length(rfc) BETWEEN 12 AND 13
               AND rfc NOT GLOB '*[^A-Z0-9&Ñ]*'),
    nombre TEXT NOT NULL
        CHECK (length(trim(nombre)) > 0),
    activo INTEGER NOT NULL DEFAULT 1
        CHECK (activo IN (0, 1)),
    creado_en TEXT NOT NULL,
    actualizado_en TEXT NOT NULL,
    eliminado_en TEXT NULL
)
;
-- RFC reservado mientras el perfil no este eliminado, aun con activo=0.
CREATE UNIQUE INDEX ux_perfil_sat_rfc_vigente
    ON perfil_sat (rfc)
    WHERE eliminado_en IS NULL
;
-- ---------------------------------------------------------------------------
-- credencial_sat: referencias no secretas a la e.firma vigente (1 a 0..1).
-- Sin eliminado_en: se borra fisicamente al eliminar la credencial vigente.
-- ---------------------------------------------------------------------------
CREATE TABLE credencial_sat (
    id TEXT NOT NULL PRIMARY KEY
        CHECK (length(id) = 36
               AND id GLOB '[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'),
    perfil_sat_id TEXT NOT NULL
        REFERENCES perfil_sat (id),
    certificado_ref TEXT NOT NULL
        CHECK (length(trim(certificado_ref)) > 0),
    llave_privada_ref TEXT NOT NULL
        CHECK (length(trim(llave_privada_ref)) > 0),
    contrasena_ref TEXT NOT NULL
        CHECK (length(trim(contrasena_ref)) > 0),
    registrada_en TEXT NOT NULL,
    actualizada_en TEXT NOT NULL
)
;
CREATE UNIQUE INDEX ux_credencial_sat_perfil
    ON credencial_sat (perfil_sat_id)
;
-- ---------------------------------------------------------------------------
-- solicitud_masiva: entidad central. Filtros SAT en columnas explicitas (DC1).
-- ---------------------------------------------------------------------------
CREATE TABLE solicitud_masiva (
    id TEXT NOT NULL PRIMARY KEY
        CHECK (length(id) = 36
               AND id GLOB '[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'),
    perfil_sat_id TEXT NOT NULL
        REFERENCES perfil_sat (id),
    id_solicitud_sat TEXT NULL
        CHECK (id_solicitud_sat IS NULL OR length(trim(id_solicitud_sat)) > 0),
    tipo_cfdi TEXT NOT NULL
        CHECK (tipo_cfdi IN ('emitidos', 'recibidos')),
    operacion_sat TEXT NOT NULL
        CHECK (operacion_sat IN ('SolicitaDescargaEmitidos', 'SolicitaDescargaRecibidos')),
    rfc_solicitante TEXT NOT NULL
        CHECK (length(rfc_solicitante) BETWEEN 12 AND 13
               AND rfc_solicitante NOT GLOB '*[^A-Z0-9&Ñ]*'),
    rfc_emisor TEXT NULL
        CHECK (rfc_emisor IS NULL
               OR (length(rfc_emisor) BETWEEN 12 AND 13
                   AND rfc_emisor NOT GLOB '*[^A-Z0-9&Ñ]*')),
    rfc_receptor TEXT NULL
        CHECK (rfc_receptor IS NULL
               OR (length(rfc_receptor) BETWEEN 12 AND 13
                   AND rfc_receptor NOT GLOB '*[^A-Z0-9&Ñ]*')),
    rfc_receptores_json TEXT NULL
        CHECK (rfc_receptores_json IS NULL
               OR CASE WHEN json_valid(rfc_receptores_json)
                       THEN json_type(rfc_receptores_json) = 'array'
                            AND json_array_length(rfc_receptores_json) > 0
                       ELSE 0 END),
    tipo_solicitud_sat TEXT NOT NULL DEFAULT 'CFDI'
        CHECK (tipo_solicitud_sat = 'CFDI'),
    estado_comprobante_sat TEXT NOT NULL DEFAULT 'Vigente'
        CHECK (estado_comprobante_sat = 'Vigente'),
    fecha_inicial_sat TEXT NOT NULL
        CHECK (length(fecha_inicial_sat) = 19
               AND fecha_inicial_sat GLOB '[0-9][0-9][0-9][0-9]-[0-1][0-9]-[0-3][0-9]T[0-2][0-9]:[0-5][0-9]:[0-5][0-9]'),
    fecha_final_sat TEXT NOT NULL
        CHECK (length(fecha_final_sat) = 19
               AND fecha_final_sat GLOB '[0-9][0-9][0-9][0-9]-[0-1][0-9]-[0-3][0-9]T[0-2][0-9]:[0-5][0-9]:[0-5][0-9]'),
    tipo_comprobante TEXT NULL
        CHECK (tipo_comprobante IS NULL OR tipo_comprobante IN ('I', 'E', 'T', 'N', 'P')),
    complemento TEXT NULL
        CHECK (complemento IS NULL OR length(trim(complemento)) > 0),
    dedup_key TEXT NOT NULL
        CHECK (dedup_key GLOB 'v[1-9]*:?*'),
    estado_local TEXT NOT NULL DEFAULT 'Creada'
        CHECK (estado_local IN ('Creada', 'Enviando', 'Enviada', 'EnvioFallido', 'EnvioIncierto')),
    cod_estatus_solicitud TEXT NULL,
    mensaje_solicitud_sat TEXT NULL,
    estado_solicitud_sat TEXT NULL
        CHECK (estado_solicitud_sat IS NULL
               OR estado_solicitud_sat IN ('Aceptada', 'EnProceso', 'Terminada', 'Error', 'Rechazada', 'Vencida')),
    codigo_estado_solicitud TEXT NULL,
    mensaje_verificacion_sat TEXT NULL,
    numero_cfdi INTEGER NULL
        CHECK (numero_cfdi IS NULL OR numero_cfdi >= 0),
    creada_en TEXT NOT NULL,
    envio_iniciado_en TEXT NULL,
    enviada_en TEXT NULL,
    ultima_verificacion_en TEXT NULL,
    siguiente_verificacion_en TEXT NULL,
    verificaciones_sin_cambio INTEGER NOT NULL DEFAULT 0
        CHECK (verificaciones_sin_cambio >= 0),
    ultimo_error TEXT NULL,
    verificacion_pendiente INTEGER NOT NULL DEFAULT 0
        CHECK (verificacion_pendiente IN (0, 1)),
    descarga_pendiente INTEGER NOT NULL DEFAULT 0
        CHECK (descarga_pendiente IN (0, 1)),
    accion_pendiente_en TEXT NULL,
    eliminado_en TEXT NULL,
--  Coherencia tipo_cfdi / operacion_sat (ADR 0013).
    CONSTRAINT ck_solicitud_operacion CHECK (
        (tipo_cfdi = 'emitidos' AND operacion_sat = 'SolicitaDescargaEmitidos')
        OR (tipo_cfdi = 'recibidos' AND operacion_sat = 'SolicitaDescargaRecibidos')
    ),
--  Emitidos: RfcEmisor = solicitante, contraparte en rfc_receptores_json.
--  Recibidos: RfcReceptor = solicitante, contraparte en rfc_emisor (DC2).
    CONSTRAINT ck_solicitud_rfcs CHECK (
        (operacion_sat = 'SolicitaDescargaEmitidos'
         AND rfc_emisor IS NOT NULL
         AND rfc_emisor = rfc_solicitante
         AND rfc_receptor IS NULL)
        OR (operacion_sat = 'SolicitaDescargaRecibidos'
            AND rfc_receptor IS NOT NULL
            AND rfc_receptor = rfc_solicitante
            AND rfc_receptores_json IS NULL)
    ),
    CONSTRAINT ck_solicitud_rango_fechas CHECK (
        fecha_inicial_sat <= fecha_final_sat
    ),
--  id_solicitud_sat existe si y solo si estado_local = 'Enviada'.
    CONSTRAINT ck_solicitud_id_sat_enviada CHECK (
        (estado_local = 'Enviada') = (id_solicitud_sat IS NOT NULL)
    ),
--  enviada_en existe si y solo si estado_local = 'Enviada'.
    CONSTRAINT ck_solicitud_enviada_en CHECK (
        (estado_local = 'Enviada') = (enviada_en IS NOT NULL)
    ),
--  Enviada exige codigo de creacion, Creada/Enviando no tienen respuesta de creacion.
    CONSTRAINT ck_solicitud_codigo_creacion CHECK (
        (estado_local <> 'Enviada' OR cod_estatus_solicitud IS NOT NULL)
        AND (estado_local NOT IN ('Creada', 'Enviando')
             OR (cod_estatus_solicitud IS NULL AND mensaje_solicitud_sat IS NULL))
    ),
--  Creada no tiene intento, Enviando, Enviada y EnvioIncierto si lo tienen.
    CONSTRAINT ck_solicitud_intento_envio CHECK (
        (estado_local <> 'Creada' OR envio_iniciado_en IS NULL)
        AND (estado_local NOT IN ('Enviando', 'Enviada', 'EnvioIncierto')
             OR envio_iniciado_en IS NOT NULL)
    ),
--  Datos de verificacion solo con IdSolicitud, estado SAT implica verificacion registrada.
    CONSTRAINT ck_solicitud_verificacion CHECK (
        (id_solicitud_sat IS NOT NULL
         OR (estado_solicitud_sat IS NULL
             AND codigo_estado_solicitud IS NULL
             AND mensaje_verificacion_sat IS NULL
             AND numero_cfdi IS NULL
             AND ultima_verificacion_en IS NULL
             AND siguiente_verificacion_en IS NULL))
        AND (estado_solicitud_sat IS NULL OR ultima_verificacion_en IS NOT NULL)
    ),
--  accion_pendiente_en no nulo si y solo si alguna intencion esta activa (DC5).
    CONSTRAINT ck_solicitud_accion_pendiente CHECK (
        (verificacion_pendiente = 1 OR descarga_pendiente = 1) = (accion_pendiente_en IS NOT NULL)
    )
)
;
CREATE UNIQUE INDEX ux_solicitud_masiva_id_solicitud_sat
    ON solicitud_masiva (id_solicitud_sat)
    WHERE id_solicitud_sat IS NOT NULL
;
-- Duplicados inequivocamente bloqueantes (DM6). Terminada y las excepciones
-- D002/D003/D008 se resuelven con evaluarDuplicado bajo BEGIN IMMEDIATE.
CREATE UNIQUE INDEX ux_solicitud_masiva_dedup_bloqueante
    ON solicitud_masiva (dedup_key)
    WHERE eliminado_en IS NULL
      AND (estado_local IN ('Creada', 'Enviando')
           OR (estado_local = 'Enviada'
               AND (estado_solicitud_sat IS NULL
                    OR estado_solicitud_sat IN ('Aceptada', 'EnProceso'))))
;
-- Busqueda de todas las coincidencias por dedup_key, incluidas eliminadas.
CREATE INDEX ix_solicitud_masiva_dedup
    ON solicitud_masiva (dedup_key)
;
-- Monitoreo del worker y recuperacion al arrancar.
CREATE INDEX ix_solicitud_masiva_monitoreo
    ON solicitud_masiva (estado_local, estado_solicitud_sat, siguiente_verificacion_en)
    WHERE eliminado_en IS NULL
;
-- Intenciones manuales pendientes al reanudar monitoreo.
CREATE INDEX ix_solicitud_masiva_accion_pendiente
    ON solicitud_masiva (accion_pendiente_en)
    WHERE eliminado_en IS NULL AND accion_pendiente_en IS NOT NULL
;
-- Solicitudes por perfil (FK y validacion de eliminacion de perfil).
CREATE INDEX ix_solicitud_masiva_perfil
    ON solicitud_masiva (perfil_sat_id, creada_en)
;
-- Lista principal ordenada por fecha de creacion.
CREATE INDEX ix_solicitud_masiva_lista
    ON solicitud_masiva (creada_en)
    WHERE eliminado_en IS NULL
;
-- ---------------------------------------------------------------------------
-- paquete_solicitud: ZIP reportado por SAT. El archivo vive fuera de SQLite.
-- ---------------------------------------------------------------------------
CREATE TABLE paquete_solicitud (
    id TEXT NOT NULL PRIMARY KEY
        CHECK (length(id) = 36
               AND id GLOB '[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f]-[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'),
    solicitud_masiva_id TEXT NOT NULL
        REFERENCES solicitud_masiva (id),
    id_paquete_sat TEXT NOT NULL
        CHECK (length(trim(id_paquete_sat)) > 0),
    estado_descarga TEXT NOT NULL DEFAULT 'Disponible'
        CHECK (estado_descarga IN ('Disponible', 'Descargando', 'Descargado', 'Error', 'Vencido')),
    ruta_local TEXT NULL
        CHECK (ruta_local IS NULL OR length(trim(ruta_local)) > 0),
    disponible_en TEXT NOT NULL,
    descarga_iniciada_en TEXT NULL,
    descargado_en TEXT NULL,
    vencimiento_estimado_en TEXT NULL,
    vencido_en TEXT NULL,
    motivo_vencimiento TEXT NULL
        CHECK (motivo_vencimiento IS NULL
               OR motivo_vencimiento IN ('solicitud_expirada', 'paquete_expirado', 'vencimiento_estimado')),
    origen_vencimiento TEXT NULL
        CHECK (origen_vencimiento IS NULL
               OR origen_vencimiento IN ('SAT', 'estimacion_local')),
    reconciliado_en TEXT NULL,
    codigo_descarga_sat TEXT NULL,
    mensaje_descarga_sat TEXT NULL,
    ultimo_error TEXT NULL,
    eliminado_en TEXT NULL,
--  Descargado si y solo si descargado_en, Descargado implica ruta_local.
    CONSTRAINT ck_paquete_descargado CHECK (
        (estado_descarga = 'Descargado') = (descargado_en IS NOT NULL)
        AND (estado_descarga <> 'Descargado' OR ruta_local IS NOT NULL)
    ),
--  Descargando implica marca de inicio de descarga.
    CONSTRAINT ck_paquete_descargando CHECK (
        estado_descarga <> 'Descargando' OR descarga_iniciada_en IS NOT NULL
    ),
--  Vencido si y solo si motivo, origen y vencido_en.
    CONSTRAINT ck_paquete_vencido CHECK (
        (estado_descarga = 'Vencido') = (motivo_vencimiento IS NOT NULL)
        AND (estado_descarga = 'Vencido') = (origen_vencimiento IS NOT NULL)
        AND (estado_descarga = 'Vencido') = (vencido_en IS NOT NULL)
    ),
--  Coherencia motivo/origen de vencimiento.
    CONSTRAINT ck_paquete_motivo_origen CHECK (
        (motivo_vencimiento IS NULL AND origen_vencimiento IS NULL)
        OR (motivo_vencimiento IS NOT NULL
            AND origen_vencimiento IS NOT NULL
            AND ((motivo_vencimiento IN ('solicitud_expirada', 'paquete_expirado')
                  AND origen_vencimiento = 'SAT')
                 OR (motivo_vencimiento = 'vencimiento_estimado'
                     AND origen_vencimiento = 'estimacion_local')))
    )
)
;
-- Unicidad por solicitud e IdPaquete, tambien sirve FK, detalle y evaluarDuplicado.
CREATE UNIQUE INDEX ux_paquete_solicitud_id_paquete_sat
    ON paquete_solicitud (solicitud_masiva_id, id_paquete_sat)
;
-- Paquetes pendientes de descarga y recuperacion de Descargando.
CREATE INDEX ix_paquete_solicitud_descarga
    ON paquete_solicitud (estado_descarga, solicitud_masiva_id)
    WHERE eliminado_en IS NULL
;
-- Vencimiento estimado de paquetes aun descargables.
CREATE INDEX ix_paquete_solicitud_vencimiento_estimado
    ON paquete_solicitud (vencimiento_estimado_en)
    WHERE eliminado_en IS NULL
      AND vencimiento_estimado_en IS NOT NULL
      AND estado_descarga IN ('Disponible', 'Descargando', 'Error')
;
-- ---------------------------------------------------------------------------
-- log_solicitud: eventos sanitizados por solicitud. Sin payload crudo.
-- ---------------------------------------------------------------------------
CREATE TABLE log_solicitud (
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
            'accion_pendiente_descartada'
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
-- Logs del detalle en orden cronologico, tambien sirve la FK.
CREATE INDEX ix_log_solicitud_detalle
    ON log_solicitud (solicitud_masiva_id, creado_en)
;
-- ---------------------------------------------------------------------------
-- configuracion_app: registro unico local.
-- ---------------------------------------------------------------------------
CREATE TABLE configuracion_app (
    id INTEGER NOT NULL PRIMARY KEY
        CHECK (id = 1),
    inicio_automatico_habilitado INTEGER NOT NULL DEFAULT 0
        CHECK (inicio_automatico_habilitado IN (0, 1)),
    monitoreo_pausado INTEGER NOT NULL DEFAULT 0
        CHECK (monitoreo_pausado IN (0, 1)),
    ultimo_cierre_en TEXT NULL,
    actualizada_en TEXT NOT NULL
)
;
INSERT OR IGNORE INTO configuracion_app (
    id,
    inicio_automatico_habilitado,
    monitoreo_pausado,
    ultimo_cierre_en,
    actualizada_en
) VALUES (
    1,
    0,
    0,
    NULL,
    strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
)
;
