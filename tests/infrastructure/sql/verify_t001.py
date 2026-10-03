#!/usr/bin/env python3
"""Verificacion reproducible de T001 sin Qt ni dependencias externas.

Se ejecuta desde la raiz con:
    python3 tests/infrastructure/sql/verify_t001.py
"""

import re
import sqlite3
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
MIGRATION_PATH = ROOT / "src/infrastructure/persistence/migrations/001_initial_schema.sql"
MIGRATION_SQL = MIGRATION_PATH.read_text(encoding="utf-8")
VERSION = 1
NOW = "2026-10-02T12:00:00.000Z"
RFC = "AAA010101AAA"
EVENT_TYPES = {
    "solicitud_creada", "duplicado_confirmado", "envio_iniciado",
    "solicitud_enviada", "envio_fallido", "envio_incierto",
    "verificacion_realizada", "verificacion_fallida", "paquetes_registrados",
    "descarga_iniciada", "paquete_descargado", "descarga_fallida",
    "descarga_interrumpida", "paquete_reconciliado", "paquete_vencido",
    "archivo_huerfano", "accion_pendiente_registrada",
    "accion_pendiente_descartada",
}


class FutureVersionError(RuntimeError):
    pass


class MissingMigrationHistoryError(RuntimeError):
    pass


def split_dm2(sql):
    """Divide exactamente en las lineas cuyo strip() es ';'."""
    chunks, current = [], []
    for line in sql.splitlines():
        if line.strip() == ";":
            block = "\n".join(current).strip()
            if block and any(
                part.strip() and not part.lstrip().startswith("--")
                for part in current
            ):
                chunks.append(block)
            current = []
        else:
            current.append(line)
    if any(line.strip() for line in current):
        raise ValueError("DM2 exige terminar cada bloque con una linea ';'")
    return chunks


def user_tables(connection):
    return [
        row[0]
        for row in connection.execute(
            "SELECT name FROM sqlite_master "
            "WHERE type = 'table' AND name NOT LIKE 'sqlite_%' "
            "AND name <> 'schema_migrations'"
        )
    ]


def run_migrations(connection, migrations=((VERSION, MIGRATION_SQL),)):
    """Emulacion DM1/DM2: una transaccion IMMEDIATE por migracion."""
    connection.execute("PRAGMA foreign_keys=ON")
    has_history = connection.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name='schema_migrations'"
    ).fetchone() is not None
    if not has_history and user_tables(connection):
        raise MissingMigrationHistoryError("hay tablas pero no schema_migrations")

    latest_known = max(version for version, _ in migrations)
    current = 0
    if has_history:
        current = connection.execute(
            "SELECT COALESCE(MAX(version), 0) FROM schema_migrations"
        ).fetchone()[0]
    if current > latest_known:
        raise FutureVersionError("la base tiene una version futura")

    for version, source in sorted(migrations):
        if version <= current:
            continue
        connection.execute("BEGIN IMMEDIATE")
        try:
            connection.execute(
                "CREATE TABLE IF NOT EXISTS schema_migrations "
                "(version INTEGER PRIMARY KEY, aplicada_en TEXT NOT NULL)"
            )
            for block in split_dm2(source):
                connection.execute(block)
            connection.execute(
                "INSERT INTO schema_migrations(version, aplicada_en) VALUES (?, ?)",
                (version, NOW),
            )
            connection.execute("COMMIT")
        except Exception:
            connection.execute("ROLLBACK")
            raise


def connect(path):
    connection = sqlite3.connect(path, isolation_level=None)
    connection.execute("PRAGMA foreign_keys=ON")
    return connection


class T001SchemaTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.db_path = Path(self.tempdir.name) / "t001.sqlite3"
        self.connection = connect(self.db_path)
        run_migrations(self.connection)
        self.addCleanup(self.connection.close)
        self.addCleanup(self.tempdir.cleanup)
        self.sequence = 1
        self.profile_id = self.insert_profile()

    def uid(self):
        value = self.sequence
        self.sequence += 1
        return f"{value:08x}-0000-4000-8000-{value:012x}"

    def execute(self, sql, parameters=()):
        return self.connection.execute(sql, parameters)

    def rejected(self, sql, parameters=()):
        with self.assertRaises(sqlite3.IntegrityError):
            self.execute(sql, parameters)

    def insert_profile(self, **overrides):
        row = {
            "id": self.uid(), "rfc": RFC, "nombre": "Perfil de prueba",
            "activo": 1, "creado_en": NOW, "actualizado_en": NOW,
            "eliminado_en": None,
        }
        row.update(overrides)
        columns = ", ".join(row)
        placeholders = ", ".join("?" for _ in row)
        self.execute(
            f"INSERT INTO perfil_sat ({columns}) VALUES ({placeholders})",
            tuple(row.values()),
        )
        return row["id"]

    def insert_request(self, **overrides):
        row = {
            "id": self.uid(), "perfil_sat_id": self.profile_id,
            "tipo_cfdi": "emitidos",
            "operacion_sat": "SolicitaDescargaEmitidos",
            "rfc_solicitante": RFC, "rfc_emisor": RFC,
            "fecha_inicial_sat": "2026-01-01T00:00:00",
            "fecha_final_sat": "2026-01-31T23:59:59",
            "dedup_key": f"v1:{self.sequence:064x}",
            "estado_local": "Creada", "creada_en": NOW,
        }
        row.update(overrides)
        state = row["estado_local"]
        if state in ("Enviando", "EnvioIncierto", "Enviada"):
            row.setdefault("envio_iniciado_en", NOW)
        if state == "Enviada":
            row.setdefault("id_solicitud_sat", f"SAT-{self.sequence}")
            row.setdefault("cod_estatus_solicitud", "5000")
            row.setdefault("enviada_en", NOW)
        if row.get("estado_solicitud_sat") is not None:
            row.setdefault("ultima_verificacion_en", NOW)
        columns = ", ".join(row)
        placeholders = ", ".join("?" for _ in row)
        self.execute(
            f"INSERT INTO solicitud_masiva ({columns}) VALUES ({placeholders})",
            tuple(row.values()),
        )
        return row["id"]

    def insert_package(self, request_id, **overrides):
        row = {
            "id": self.uid(), "solicitud_masiva_id": request_id,
            "id_paquete_sat": f"PKG-{self.sequence}", "estado_descarga": "Disponible",
            "disponible_en": NOW,
        }
        row.update(overrides)
        state = row["estado_descarga"]
        if state == "Descargando":
            row.setdefault("descarga_iniciada_en", NOW)
        if state == "Descargado":
            row.setdefault("ruta_local", "/tmp/paquete.zip")
            row.setdefault("descargado_en", NOW)
        if state == "Vencido":
            row.setdefault("motivo_vencimiento", "paquete_expirado")
            row.setdefault("origen_vencimiento", "SAT")
            row.setdefault("vencido_en", NOW)
        columns = ", ".join(row)
        placeholders = ", ".join("?" for _ in row)
        self.execute(
            f"INSERT INTO paquete_solicitud ({columns}) VALUES ({placeholders})",
            tuple(row.values()),
        )
        return row["id"]

    def insert_log(self, request_id, **overrides):
        row = {
            "id": self.uid(), "solicitud_masiva_id": request_id,
            "tipo_evento": "solicitud_creada", "origen": "usuario",
            "creado_en": NOW,
        }
        row.update(overrides)
        columns = ", ".join(row)
        placeholders = ", ".join("?" for _ in row)
        self.execute(
            f"INSERT INTO log_solicitud ({columns}) VALUES ({placeholders})",
            tuple(row.values()),
        )
        return row["id"]

    def test_dm2_file_convention_and_event_catalog(self):
        raw = MIGRATION_PATH.read_bytes()
        self.assertFalse(raw.startswith(b"\xef\xbb\xbf"), "DM2: UTF-8 sin BOM")
        self.assertTrue(split_dm2(MIGRATION_SQL))
        for line in MIGRATION_SQL.splitlines():
            self.assertFalse(
                ";" in line and line.strip() != ";",
                f"DM2: punto y coma fuera de linea terminadora: {line!r}",
            )
        executable = "\n".join(
            line for line in MIGRATION_SQL.splitlines()
            if not line.lstrip().startswith("--")
        ).upper()
        self.assertFalse(any(
            line.lstrip().startswith(".")
            for line in executable.splitlines()
        ), "DM2: no admite comandos del cliente sqlite")
        for forbidden in ("BEGIN", "COMMIT", "ROLLBACK", "SAVEPOINT", "VACUUM", "ATTACH", "TRIGGER"):
            self.assertNotRegex(executable, rf"\b{forbidden}\b")
        self.assertNotIn("schema_migrations", executable.lower())
        catalog = re.search(
            r"tipo_evento IN \((.*?)\)\),\n    origen", MIGRATION_SQL, re.S
        )
        self.assertIsNotNone(catalog)
        actual_events = set(re.findall(r"'([^']+)'", catalog.group(1)))
        self.assertEqual(actual_events, EVENT_TYPES)
        self.assertEqual(len(actual_events), 18)

    def test_empty_database_schema_and_integrity(self):
        tables = {
            row[0] for row in self.execute(
                "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'"
            )
        }
        self.assertEqual(
            tables,
            {"schema_migrations", "perfil_sat", "credencial_sat", "solicitud_masiva",
             "paquete_solicitud", "log_solicitud", "configuracion_app"},
        )
        self.assertEqual(self.execute("SELECT version FROM schema_migrations").fetchall(), [(1,)])
        self.assertEqual(self.execute("SELECT id FROM configuracion_app").fetchall(), [(1,)])
        self.rejected(
            "INSERT INTO configuracion_app(id, actualizada_en) VALUES (2, ?)", (NOW,)
        )
        self.assertEqual(self.execute("PRAGMA integrity_check").fetchall(), [("ok",)])
        self.assertEqual(self.execute("PRAGMA foreign_key_check").fetchall(), [])

    def test_reapplication_does_not_change_schema_history_or_configuration(self):
        before_master = self.execute(
            "SELECT type, name, tbl_name, sql FROM sqlite_master "
            "WHERE name NOT LIKE 'sqlite_%' ORDER BY type, name"
        ).fetchall()
        before_history = self.execute(
            "SELECT version, aplicada_en FROM schema_migrations ORDER BY version"
        ).fetchall()
        before_config = self.execute("SELECT * FROM configuracion_app").fetchall()
        run_migrations(self.connection)
        self.assertEqual(before_master, self.execute(
            "SELECT type, name, tbl_name, sql FROM sqlite_master "
            "WHERE name NOT LIKE 'sqlite_%' ORDER BY type, name"
        ).fetchall())
        self.assertEqual(before_history, self.execute(
            "SELECT version, aplicada_en FROM schema_migrations ORDER BY version"
        ).fetchall())
        self.assertEqual(before_config, self.execute("SELECT * FROM configuracion_app").fetchall())

    def test_runner_rejects_future_version_and_legacy_tables_without_writing(self):
        self.connection.close()
        future = connect(self.db_path)
        future.execute("INSERT INTO schema_migrations(version, aplicada_en) VALUES (2, ?)", (NOW,))
        future.close()
        bytes_before = self.db_path.read_bytes()
        future = connect(self.db_path)
        with self.assertRaises(FutureVersionError):
            run_migrations(future)
        future.close()
        self.assertEqual(self.db_path.read_bytes(), bytes_before)

        legacy_path = Path(self.tempdir.name) / "legacy.sqlite3"
        legacy = connect(legacy_path)
        legacy.execute("CREATE TABLE legacy_data(id INTEGER PRIMARY KEY)")
        with self.assertRaises(MissingMigrationHistoryError):
            run_migrations(legacy)
        self.assertIsNone(legacy.execute(
            "SELECT 1 FROM sqlite_master WHERE name='schema_migrations'"
        ).fetchone())
        legacy.close()

    def test_runner_rolls_back_an_injected_broken_migration(self):
        path = Path(self.tempdir.name) / "broken.sqlite3"
        connection = connect(path)
        broken = "CREATE TABLE rollback_probe (id INTEGER)\n;\nNOT VALID SQL\n;"
        with self.assertRaises(sqlite3.OperationalError):
            run_migrations(connection, ((1, broken),))
        self.assertEqual(user_tables(connection), [])
        self.assertIsNone(connection.execute(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name='schema_migrations'"
        ).fetchone())
        connection.close()

    def test_profile_credential_foreign_key_and_unique_constraints(self):
        self.rejected("UPDATE perfil_sat SET activo=2 WHERE id=?", (self.profile_id,))
        self.rejected("UPDATE perfil_sat SET rfc='aaa010101aaa' WHERE id=?", (self.profile_id,))
        self.rejected("UPDATE perfil_sat SET nombre='  ' WHERE id=?", (self.profile_id,))
        inactive = self.insert_profile(rfc="BBB010101BBB", activo=0)
        self.rejected(
            "INSERT INTO perfil_sat(id,rfc,nombre,activo,creado_en,actualizado_en) VALUES (?,?,?,?,?,?)",
            (self.uid(), "BBB010101BBB", "Duplicado", 1, NOW, NOW),
        )
        self.execute("UPDATE perfil_sat SET eliminado_en=? WHERE id=?", (NOW, inactive))
        self.insert_profile(rfc="BBB010101BBB")
        self.rejected(
            "INSERT INTO credencial_sat VALUES (?,?,?,?,?,?,?)",
            (self.uid(), "missing", "cert", "key", "pass", NOW, NOW),
        )
        self.rejected(
            "INSERT INTO credencial_sat VALUES (?,?,?,?,?,?,?)",
            (self.uid(), self.profile_id, " ", "key", "pass", NOW, NOW),
        )
        self.execute(
            "INSERT INTO credencial_sat VALUES (?,?,?,?,?,?,?)",
            (self.uid(), self.profile_id, "cert", "key", "pass", NOW, NOW),
        )
        self.rejected(
            "INSERT INTO credencial_sat VALUES (?,?,?,?,?,?,?)",
            (self.uid(), self.profile_id, "cert2", "key2", "pass2", NOW, NOW),
        )
        no_credential = self.insert_profile(rfc="CCC010101CCC")
        self.assertIsNotNone(no_credential, "un perfil no requiere credencial")

    def test_solicitud_checks_uniques_and_state_separation(self):
        self.rejected("INSERT INTO solicitud_masiva(id,perfil_sat_id,tipo_cfdi,operacion_sat,rfc_solicitante,rfc_emisor,fecha_inicial_sat,fecha_final_sat,dedup_key,creada_en) VALUES (?,?,?,?,?,?,?,?,?,?)", (
            self.uid(), self.profile_id, "emitidos", "SolicitaDescargaRecibidos", RFC, RFC,
            "2026-01-01T00:00:00", "2026-01-02T00:00:00", "v1:bad", NOW))
        self.rejected("INSERT INTO solicitud_masiva(id,perfil_sat_id,tipo_cfdi,operacion_sat,rfc_solicitante,rfc_emisor,rfc_receptores_json,fecha_inicial_sat,fecha_final_sat,dedup_key,creada_en) VALUES (?,?,?,?,?,?,?,?,?,?,?)", (
            self.uid(), self.profile_id, "emitidos", "SolicitaDescargaEmitidos", RFC, RFC, "{}",
            "2026-01-01T00:00:00", "2026-01-02T00:00:00", "v1:json", NOW))
        self.rejected("UPDATE solicitud_masiva SET fecha_inicial_sat='2026-02-01T00:00:00', fecha_final_sat='2026-01-01T00:00:00' WHERE id=?", (self.insert_request(),))
        self.rejected("INSERT INTO solicitud_masiva(id,perfil_sat_id,tipo_cfdi,operacion_sat,rfc_solicitante,rfc_emisor,fecha_inicial_sat,fecha_final_sat,dedup_key,estado_local,estado_solicitud_sat,ultima_verificacion_en,creada_en) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)", (
            self.uid(), self.profile_id, "emitidos", "SolicitaDescargaEmitidos", RFC, RFC,
            "2026-01-01T00:00:00", "2026-01-02T00:00:00", "v1:no-id", "Creada", "Aceptada", NOW, NOW))
        self.rejected("UPDATE solicitud_masiva SET envio_iniciado_en=? WHERE id=?", (NOW, self.insert_request()))
        self.rejected("INSERT INTO solicitud_masiva(id,perfil_sat_id,tipo_cfdi,operacion_sat,rfc_solicitante,rfc_emisor,fecha_inicial_sat,fecha_final_sat,dedup_key,estado_local,creada_en) VALUES (?,?,?,?,?,?,?,?,?,?,?)", (
            self.uid(), self.profile_id, "emitidos", "SolicitaDescargaEmitidos", RFC, RFC,
            "2026-01-01T00:00:00", "2026-01-02T00:00:00", "v1:try", "Enviando", NOW))
        self.rejected("UPDATE solicitud_masiva SET cod_estatus_solicitud='5000' WHERE id=?", (self.insert_request(),))
        self.rejected("UPDATE solicitud_masiva SET verificacion_pendiente=1 WHERE id=?", (self.insert_request(),))
        sent = self.insert_request(estado_local="Enviada", dedup_key="v1:sent", id_solicitud_sat="SAT-UNICO")
        self.rejected("INSERT INTO solicitud_masiva(id,perfil_sat_id,tipo_cfdi,operacion_sat,rfc_solicitante,rfc_emisor,fecha_inicial_sat,fecha_final_sat,dedup_key,estado_local,envio_iniciado_en,enviada_en,id_solicitud_sat,cod_estatus_solicitud,creada_en) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", (
            self.uid(), self.profile_id, "emitidos", "SolicitaDescargaEmitidos", RFC, RFC,
            "2026-01-01T00:00:00", "2026-01-02T00:00:00", "v1:other", "Enviada", NOW, NOW, "SAT-UNICO", "5000", NOW))
        self.execute("UPDATE solicitud_masiva SET codigo_estado_solicitud=?, mensaje_verificacion_sat=?, ultima_verificacion_en=? WHERE id=?", ("AUTH", "Autenticacion fallida", NOW, sent))
        self.assertIsNone(self.execute("SELECT estado_solicitud_sat FROM solicitud_masiva WHERE id=?", (sent,)).fetchone()[0])

    def test_remaining_column_checks_reject_invalid_values(self):
        request_id = self.insert_request(dedup_key="v1:columns")
        for column, value in (
            ("tipo_cfdi", "otro"),
            ("rfc_solicitante", "aaa010101aaa"),
            ("fecha_inicial_sat", "2026/01/01"),
            ("tipo_solicitud_sat", "Metadata"),
            ("estado_comprobante_sat", "Cancelado"),
            ("tipo_comprobante", "X"),
            ("complemento", "   "),
            ("dedup_key", "sin-version"),
            ("estado_local", "EliminadaLocalmente"),
            ("estado_solicitud_sat", "NoExiste"),
            ("numero_cfdi", -1),
            ("verificaciones_sin_cambio", -1),
        ):
            self.rejected(f"UPDATE solicitud_masiva SET {column}=? WHERE id=?", (value, request_id))
        self.rejected(
            "UPDATE solicitud_masiva SET verificacion_pendiente=2, accion_pendiente_en=? WHERE id=?",
            (NOW, request_id),
        )
        self.rejected(
            "INSERT INTO solicitud_masiva(id,perfil_sat_id,tipo_cfdi,operacion_sat,rfc_solicitante,rfc_emisor,fecha_inicial_sat,fecha_final_sat,dedup_key,creada_en) VALUES (?,?,?,?,?,?,?,?,?,?)",
            (self.uid(), "missing", "emitidos", "SolicitaDescargaEmitidos", RFC, RFC,
             "2026-01-01T00:00:00", "2026-01-02T00:00:00", "v1:missing-profile", NOW),
        )

    def test_dedup_exceptions_are_not_blocked_by_partial_index(self):
        active = self.insert_request(dedup_key="v1:active")
        self.rejected("UPDATE solicitud_masiva SET dedup_key='v1:active' WHERE id=?", (self.insert_request(),))
        # D003: EnvioIncierto requiere confirmacion de T003, no rechazo SQL.
        self.insert_request(estado_local="EnvioIncierto", dedup_key="v1:d003")
        self.insert_request(dedup_key="v1:d003")
        # D008: Terminada sin paquetes requiere confirmacion de T003, no rechazo SQL.
        ended_without_packages = self.insert_request(estado_local="Enviada", estado_solicitud_sat="Terminada", dedup_key="v1:d008")
        self.insert_request(dedup_key="v1:d008")
        # D002: Vencido + Descargado requiere confirmacion de T003, no rechazo SQL.
        ended_expired = self.insert_request(estado_local="Enviada", estado_solicitud_sat="Terminada", dedup_key="v1:d002")
        self.insert_package(ended_expired, estado_descarga="Vencido")
        self.insert_request(dedup_key="v1:d002")
        # Terminada con todo Descargado tambien queda fuera del indice: T003 debe bloquearlo transaccionalmente.
        ended_downloaded = self.insert_request(estado_local="Enviada", estado_solicitud_sat="Terminada", dedup_key="v1:terminada")
        self.insert_package(ended_downloaded, estado_descarga="Descargado")
        self.insert_request(dedup_key="v1:terminada")
        self.assertIsNotNone(active)
        self.assertIsNotNone(ended_without_packages)

    def test_package_checks_unique_recovery_and_expiry_flows(self):
        request_id = self.insert_request(estado_local="Enviada", estado_solicitud_sat="Terminada", dedup_key="v1:packages")
        self.rejected("INSERT INTO paquete_solicitud(id,solicitud_masiva_id,id_paquete_sat,estado_descarga,disponible_en) VALUES (?,?,?,?,?)", (self.uid(), request_id, "bad-state", "EliminadoLocalmente", NOW))
        self.rejected("INSERT INTO paquete_solicitud(id,solicitud_masiva_id,id_paquete_sat,estado_descarga,disponible_en) VALUES (?,?,?,?,?)", (self.uid(), request_id, "downloading", "Descargando", NOW))
        self.rejected("INSERT INTO paquete_solicitud(id,solicitud_masiva_id,id_paquete_sat,estado_descarga,disponible_en) VALUES (?,?,?,?,?)", (self.uid(), request_id, "downloaded", "Descargado", NOW))
        self.rejected("INSERT INTO paquete_solicitud(id,solicitud_masiva_id,id_paquete_sat,estado_descarga,disponible_en) VALUES (?,?,?,?,?)", (self.uid(), request_id, "expired", "Vencido", NOW))
        recovering = self.insert_package(request_id, estado_descarga="Descargando")
        self.execute("UPDATE paquete_solicitud SET estado_descarga='Disponible', reconciliado_en=? WHERE id=?", (NOW, recovering))
        self.assertEqual(self.execute("SELECT estado_descarga FROM paquete_solicitud WHERE id=?", (recovering,)).fetchone()[0], "Disponible")
        self.execute("UPDATE paquete_solicitud SET estado_descarga='Vencido', motivo_vencimiento='paquete_expirado', origen_vencimiento='SAT', vencido_en=?, codigo_descarga_sat='5007' WHERE id=?", (NOW, recovering))
        self.assertEqual(self.execute("SELECT estado_solicitud_sat FROM solicitud_masiva WHERE id=?", (request_id,)).fetchone()[0], "Terminada")
        duplicate = self.insert_package(request_id, estado_descarga="Disponible", id_paquete_sat="PAQ-UNICO")
        self.rejected("INSERT INTO paquete_solicitud(id,solicitud_masiva_id,id_paquete_sat,estado_descarga,disponible_en) VALUES (?,?,?,?,?)", (self.uid(), request_id, "PAQ-UNICO", "Disponible", NOW))
        self.assertIsNotNone(duplicate)

    def test_remaining_package_and_log_checks_reject_invalid_values(self):
        request_id = self.insert_request(estado_local="Enviada", dedup_key="v1:remaining", estado_solicitud_sat="Terminada")
        package_id = self.insert_package(request_id)
        for column, value in (
            ("id_paquete_sat", " "),
            ("ruta_local", " "),
            ("motivo_vencimiento", "otra_causa"),
            ("origen_vencimiento", "otro_origen"),
        ):
            self.rejected(f"UPDATE paquete_solicitud SET {column}=? WHERE id=?", (value, package_id))
        self.rejected(
            "UPDATE paquete_solicitud SET motivo_vencimiento='paquete_expirado', origen_vencimiento='estimacion_local' WHERE id=?",
            (package_id,),
        )
        self.rejected(
            "UPDATE paquete_solicitud SET descargado_en=? WHERE id=?", (NOW, package_id)
        )
        self.rejected(
            "INSERT INTO paquete_solicitud(id,solicitud_masiva_id,id_paquete_sat,estado_descarga,disponible_en) VALUES (?,?,?,?,?)",
            (self.uid(), "missing", "orphan", "Disponible", NOW),
        )
        log_id = self.insert_log(request_id)
        self.rejected("UPDATE log_solicitud SET origen='otro' WHERE id=?", (log_id,))
        self.rejected("UPDATE log_solicitud SET tipo_evento='otro' WHERE id=?", (log_id,))
        self.rejected("UPDATE log_solicitud SET codigo_sat='5000' WHERE id=?", (log_id,))
        self.rejected("UPDATE log_solicitud SET mensaje_sat='mensaje' WHERE id=?", (log_id,))
        self.rejected("UPDATE configuracion_app SET monitoreo_pausado=2 WHERE id=1")

    def test_log_checks_catalog_and_out_of_scope_columns(self):
        request_id = self.insert_request(dedup_key="v1:logs")
        for event in EVENT_TYPES:
            self.insert_log(request_id, tipo_evento=event)
        self.rejected("INSERT INTO log_solicitud(id,solicitud_masiva_id,tipo_evento,origen,creado_en) VALUES (?,?,?,?,?)", (self.uid(), request_id, "solicitud_eliminada", "usuario", NOW))
        self.rejected("INSERT INTO log_solicitud(id,solicitud_masiva_id,tipo_evento,origen,origen_codigo_sat,creado_en) VALUES (?,?,?,?,?,?)", (self.uid(), request_id, "envio_fallido", "usuario", "creacion", NOW))
        self.rejected("INSERT INTO log_solicitud(id,solicitud_masiva_id,tipo_evento,origen,payload_resumen_json,creado_en) VALUES (?,?,?,?,?,?)", (self.uid(), request_id, "envio_fallido", "usuario", "[]", NOW))
        columns = " ".join(
            row[1].lower()
            for table in ("perfil_sat", "credencial_sat", "solicitud_masiva", "paquete_solicitud", "log_solicitud", "configuracion_app")
            for row in self.execute(f"PRAGMA table_info({table})")
        )
        for forbidden in ("xml", "cliente", "usuario", "rol", "permiso", "comprobante_uuid"):
            self.assertNotIn(forbidden, columns)

    def test_query_plans_use_named_indexes(self):
        request_id = self.insert_request(estado_local="Enviada", dedup_key="v1:plan", siguiente_verificacion_en=NOW)
        self.insert_package(request_id, estado_descarga="Disponible")
        self.insert_log(request_id)
        checks = (
            ("SELECT id FROM solicitud_masiva WHERE eliminado_en IS NULL AND estado_local='Enviada' AND estado_solicitud_sat IS NULL ORDER BY siguiente_verificacion_en", (), {"ix_solicitud_masiva_monitoreo"}),
            ("SELECT id FROM paquete_solicitud WHERE eliminado_en IS NULL AND estado_descarga IN ('Disponible','Error')", (), {"ix_paquete_solicitud_descarga"}),
            ("SELECT id FROM log_solicitud WHERE solicitud_masiva_id=? ORDER BY creado_en", (request_id,), {"ix_log_solicitud_detalle"}),
            ("SELECT id FROM solicitud_masiva WHERE dedup_key=?", ("v1:plan",), {"ix_solicitud_masiva_dedup", "ux_solicitud_masiva_dedup_bloqueante"}),
        )
        for query, parameters, names in checks:
            plan = " ".join(row[3] for row in self.execute("EXPLAIN QUERY PLAN " + query, parameters))
            self.assertTrue(any(name in plan for name in names), (query, plan, names))

    def test_uuid_requires_hyphens_only_at_uuid_separators(self):
        """Contrato documentado: UUID 8-4-4-4-12, no solo longitud y alfabeto."""
        malformed = "-1234567-89ab-cdef-0123-456789abcdef"
        self.rejected(
            "INSERT INTO perfil_sat(id,rfc,nombre,activo,creado_en,actualizado_en) VALUES (?,?,?,?,?,?)",
            (malformed, "DDD010101DDD", "UUID mal ubicado", 1, NOW, NOW),
        )


if __name__ == "__main__":
    result = unittest.main(verbosity=2, exit=False).result
    print("sqlite_version() usada:", sqlite3.sqlite_version)
    raise SystemExit(not result.wasSuccessful())
