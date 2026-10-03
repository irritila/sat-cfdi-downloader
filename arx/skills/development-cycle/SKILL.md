---
name: development-cycle
description: Coordinate the implementation of a refined repository task through scoped specialist work, session artifacts, integration, and evidence. Use when starting or resuming a task-level development cycle, not for task refinement.
---

# Development Cycle

Use this skill to run a task-level development session. The coordinator owns the
conversation with the user, assigns each specialist a turn, records meaningful
interactions, and keeps the implementation within the refined task.

## Prerequisite

This skill requires the repository role
`arx/agents/development-cycle-lead.md`. Read and apply it before creating a
session. If it is absent, explain that the repository has not installed the
coordination policy and do not substitute a generic workflow.

Read the repository's `AGENTS.md`, `README.md`, active task, applicable
requirements, architecture, design rules and ADRs before proposing work. Report
`Contexto leido` at the beginning, as required by the role.

Use this skill after a task is refined. If its scope, dependencies or acceptance
criteria remain unresolved, use the repository's refinement process first.

## Start or resume a session

1. Locate the repository root and confirm the task identifier, such as `T005.1`.
2. Inspect Git status. Record pre-existing changes separately; do not attach them
   to the task.
3. For a new session, run:

   ```sh
   python3 arx/skills/development-cycle/scripts/init_session.py \
     --repo <repository-root> --task <task-id>
   ```

   The helper creates `docs/meetings/<task-id>-desarrollo/` without replacing
   existing files. For a resumed session, read its existing artifacts instead.
4. Fill `contexto.md` with the bounded dossier: objective, accepted decisions,
   dependencies, contracts, affected files and precise references. Do not ask
   specialists to reread unrelated files.
5. Create the implementation plan and ownership map before parallel work. Each
   file or coherent area has only one writer per vertical slice.

## Bridge and provider recovery

When a session uses a Claude, Codex or other provider bridge, read
[the recovery guide](references/bridge-recovery.md) before starting jobs and
whenever the coordinator cannot obtain a response. The bridge is optional: use
this procedure only when the repository has one configured or the user has
asked to use it.

## Coordinate the cycle

- Unless the user changes it for a session, use this provider route:

  | Roles | Provider and model |
  | --- | --- |
  | `qt-architecture-lead`, `qt-quality-engineer` | Codex with GPT-5.6 |
  | Every other selected specialist | Claude with Opus 5.5 |

  Record the selected route in `bitacora.md` before launching jobs. A provider
  outage or unavailable model does not silently change this route; follow the
  bridge recovery guide and obtain a replacement choice when one is needed.
- Select specialists because the task needs them. Typical roles are
  `qt-architecture-lead`, `qt-core-engineer`, `qt-interface-engineer`,
  `qt-platform-engineer` and `qt-quality-engineer`; include security or macOS
  specialists only when the task warrants it.
- Give each intervention a bounded question, dossier, expected output, scope of
  writing and reviewer. Start with read-only proposals where contracts are still
  uncertain.
- Record each user instruction, specialist response, decision, owner, command
  result and change of session state in `bitacora.md`.
- Escalate only a decision that changes scope, visible behavior, a shared
  contract or feasibility. Advance independent work while a decision is pending.
- Implement in integrated vertical slices. Inspect each diff and run the checks
  affected by that slice before moving on.
- In review, send the relevant diff and evidence rather than a broad repository
  reading request. Fix actionable findings inside the task's scope.

## Evidence and closing

Map every applicable acceptance criterion to a test, manual check or explicit
blocker in `evidencia.md`. Record commands and observed outcomes; do not call an
unexecuted check successful.

Complete `revision-final.md` with the final diff, specialist reviews, remaining
risks and delivery status. Update task or design documents only when justified
by the implementation. Prepare a commit limited to live files, excluding
meeting artifacts and unrelated local changes; create it only when the user has
asked or already authorized it.
