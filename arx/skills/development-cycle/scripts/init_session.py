#!/usr/bin/env python3
"""Create development-cycle session artifacts without overwriting a session."""

from __future__ import annotations

import argparse
import re
import sys
from datetime import datetime, timezone
from pathlib import Path


TASK_ID_PATTERN = re.compile(r"^T\d+(?:\.\d+)?$")


def task_file(repo: Path, task_id: str) -> Path:
    matches = sorted((repo / "docs" / "tasks").glob(f"{task_id}-*.md"))
    if len(matches) != 1:
        found = ", ".join(str(path.relative_to(repo)) for path in matches) or "ninguno"
        raise ValueError(
            f"Se esperaba una sola tarea para {task_id}; encontrados: {found}."
        )
    return matches[0]


def section(text: str, heading: str) -> str:
    pattern = re.compile(
        rf"^## {re.escape(heading)}\s*$\n(.*?)(?=^## |\Z)", re.MULTILINE | re.DOTALL
    )
    match = pattern.search(text)
    return match.group(1).strip() if match else "No extraido; completar desde la tarea."


def write_if_missing(path: Path, content: str) -> str:
    if path.exists():
        return f"existente {path.name}"
    path.write_text(content, encoding="utf-8")
    return f"creado {path.name}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, type=Path, help="Raiz del repositorio")
    parser.add_argument("--task", required=True, help="Identificador, por ejemplo T005.1")
    args = parser.parse_args()

    task_id = args.task.strip()
    if not TASK_ID_PATTERN.fullmatch(task_id):
        parser.error("--task debe tener formato TNNN o TNNN.N")

    repo = args.repo.expanduser().resolve()
    if not (repo / "AGENTS.md").is_file() or not (repo / "docs" / "tasks").is_dir():
        parser.error("--repo no parece contener AGENTS.md y docs/tasks")

    try:
        task = task_file(repo, task_id)
    except ValueError as error:
        parser.error(str(error))

    task_text = task.read_text(encoding="utf-8")
    first_line = task_text.splitlines()[0] if task_text.splitlines() else ""
    title = (first_line[2:] if first_line.startswith("# ") else first_line).strip()
    title = title or task.name
    objective = section(task_text, "Objetivo")
    session = repo / "docs" / "meetings" / f"{task_id}-desarrollo"
    session.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    task_ref = task.relative_to(repo).as_posix()

    artifacts = {
        "bitacora.md": f"# Bitacora de desarrollo: {title}\n\n"
        f"## {timestamp} - Coordinador\n\n"
        f"Sesion creada para `{task_id}`. Estado inicial: `Preparacion`.\n",
        "contexto.md": f"# Contexto de desarrollo: {title}\n\n"
        f"- Tarea: `{task_ref}`\n"
        "- Estado de sesion: `Preparacion`\n\n"
        "## Objetivo\n\n"
        f"{objective}\n\n"
        "## Decisiones cerradas\n\n"
        "- Completar desde la tarea refinada y ADRs aplicables.\n\n"
        "## Dependencias y contratos\n\n"
        "- Completar con archivos y secciones concretas antes de delegar.\n\n"
        "## Archivos previstos\n\n"
        "- Completar despues de inspeccionar el arbol y el plan.\n",
        "plan-implementacion.md": f"# Plan de implementacion: {title}\n\n"
        "Estado: `Preparacion`\n\n"
        "| Corte | Resultado integrable | Responsable | Archivos | Verificacion | Estado |\n"
        "| --- | --- | --- | --- | --- | --- |\n"
        "| 1 | Por definir | Por asignar | Por definir | Por definir | Pendiente |\n",
        "propiedad.md": f"# Propiedad de cambios: {title}\n\n"
        "| Area o archivo | Responsable de escritura | Revisores | Corte | Estado |\n"
        "| --- | --- | --- | --- | --- |\n"
        "| Por definir | Por asignar | Por asignar | Por definir | Pendiente |\n",
        "evidencia.md": f"# Evidencia: {title}\n\n"
        "| Criterio de aceptacion | Comprobacion | Evidencia o comando | Resultado | Limitaciones |\n"
        "| --- | --- | --- | --- | --- |\n"
        "| Por mapear | Por definir | Por definir | Pendiente | Ninguna registrada |\n",
        "revision-final.md": f"# Revision final: {title}\n\n"
        "Estado: `Pendiente`. Completar al terminar integracion y verificacion.\n\n"
        "## Diff y archivos vivos\n\n- Pendiente.\n\n"
        "## Revisiones y evidencia\n\n- Pendiente.\n\n"
        "## Riesgos o pendientes\n\n- Pendiente.\n",
    }

    for name, content in artifacts.items():
        print(write_if_missing(session / name, content))
    print(f"sesion {session.relative_to(repo)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
