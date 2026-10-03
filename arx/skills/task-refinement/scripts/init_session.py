#!/usr/bin/env python3
"""Create refinement-session artifacts without overwriting an existing session."""

from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path


TASK_ID_PATTERN = re.compile(r"^T\d+(?:\.\d+)?$")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--task", required=True)
    args = parser.parse_args()

    task_id = args.task.strip()
    if not TASK_ID_PATTERN.fullmatch(task_id):
        parser.error("--task debe tener formato TNNN o TNNN.N")
    repo = args.repo.expanduser().resolve()
    tasks = repo / "docs" / "tasks"
    if not (repo / "AGENTS.md").is_file() or not tasks.is_dir():
        parser.error("--repo no contiene AGENTS.md y docs/tasks")
    matches = sorted(tasks.glob(task_id + "-*.md"))
    if len(matches) != 1:
        parser.error("Se esperaba una sola tarea para %s; se encontraron %d" % (task_id, len(matches)))

    task_path = matches[0]
    text = task_path.read_text(encoding="utf-8")
    first = text.splitlines()[0] if text.splitlines() else task_path.name
    title = (first[2:] if first.startswith("# ") else first).strip()
    objective_match = re.search(r"^## Objetivo\s*\n(.*?)(?=^## |\Z)", text, re.M | re.S)
    objective = objective_match.group(1).strip() if objective_match else "Completar manualmente desde la tarea."
    meeting = repo / "docs" / "meetings" / (task_id + "-refinamiento")
    meeting.mkdir(parents=True, exist_ok=True)
    now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    artifacts = {
        "bitacora.md": "# Bitacora de refinamiento: %s\n\n## %s - Coordinador\n\nSesion iniciada. Estado: `Preparacion`.\n" % (title, now),
        "contexto-base.md": "# Contexto de refinamiento: %s\n\n- Tarea: `%s`\n- Estado: `Preparacion`\n\n## Objetivo actual\n\n%s\n\n## Fuentes y decisiones\n\nCompletar con referencias puntuales antes de asignar turnos.\n\n## Preguntas por resolver\n\nCompletar después de leer tarea, criterios y dependencias.\n" % (title, task_path.relative_to(repo).as_posix(), objective),
        "sesiones.json": json.dumps({
            "task": task_id,
            "coordinator": "codex-or-claude",
            "provider_defaults": {
                "qt-architecture-lead": {"provider": "codex", "model": "gpt-5.6"},
                "qt-quality-engineer": {"provider": "codex", "model": "gpt-5.6"},
                "other_specialists": {"provider": "claude", "model": "claude-opus-5-5"}
            },
            "participants": [],
            "log": "docs/meetings/%s-refinamiento/bitacora.md" % task_id
        }, indent=2, ensure_ascii=False) + "\n",
    }

    for name, content in artifacts.items():
        path = meeting / name
        if path.exists():
            print("existente %s" % name)
        else:
            path.write_text(content, encoding="utf-8")
            print("creado %s" % name)
    print("sesion %s" % meeting.relative_to(repo).as_posix())
    return 0


if __name__ == "__main__":
    sys.exit(main())
