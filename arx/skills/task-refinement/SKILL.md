---
name: task-refinement
description: Coordinate a repository task-refinement session with the user and selected specialist agents, recording decisions and producing an implementable task. Use for refinement meetings, not for implementing an already-refined task.
metadata:
  short-description: Coordina refinamientos de tareas
---

# Task Refinement

Use this skill to run a refinement session for one repository task. The
coordinator stays as the sole channel to the user, assigns the floor to
specialists and turns their input into explicit decisions, open questions and a
coherent task specification.

## Canonical policy and context

Read and apply `arx/agents/refinement-lead.md`. Also read `AGENTS.md`,
`README.md`, the task, task template and only the relevant requirements,
architecture, design rules and ADRs. Report `Contexto leido` before substantive
recommendations.

This skill refines scope and acceptance criteria. For implementation of a
refined task, use the `development-cycle` skill instead.

## Start or resume

1. Confirm the task ID and inspect `git status`; note unrelated existing changes.
2. Create a new refinement session with:

   ```sh
   python3 arx/skills/task-refinement/scripts/init_session.py \
     --repo <repository-root> --task <task-id>
   ```

   It creates `docs/meetings/<task-id>-refinamiento/` with `bitacora.md`,
   `sesiones.json` and `contexto-base.md`, without replacing existing files.
   For a resumed session, continue from its current artifacts and state.
3. Complete `contexto-base.md` with precise sources, task goal, decisions,
   constraints and actual questions. Keep role dossiers small and scoped.

## Participants and provider route

Select only specialists needed to resolve product, architecture, platform,
implementation-boundary or testability questions. The default provider route is:

| Roles | Provider and model |
| --- | --- |
| `qt-architecture-lead`, `qt-quality-engineer` | Codex GPT-5.6 |
| Other selected specialists | Claude Opus 5.5 |

Record selected roles, exact model/effort and transport in `sesiones.json` before
contacting them. Follow `references/bridge-recovery.md` when a provider bridge is
used or fails. Do not claim a specialist participated unless an actual response
was received. If transport is unavailable, continue only with independent work
and label coordinator analysis as such.

## Run the meeting

1. Establish the starting state and distinguish documented facts, assumptions,
   decisions and unresolved questions.
2. Give specialists bounded turns. Assign one role the floor at a time; use
   additional roles to review concrete proposals rather than repeat broad
   repository analysis.
3. For every turn, record timestamp, speaker, model/provider, prompt or concise
   instruction, response or sanitized failure, and coordinator synthesis in
   `bitacora.md`. Store substantial prompts and synthesis in numbered
   `ronda-NN-<tema>.md` files and link them from the log.
4. Present only decisions that affect product behavior, scope, shared contracts,
   security or feasibility to the user. State a recommendation and consequence;
   never interpret silence as approval. Continue independent topics while
   awaiting a material decision.
5. Keep an explicit decision register. Mark each item approved by user,
   technical recommendation, assumption or pending; record source and rationale.
6. Reconcile specialist disagreement against requirements, operational rules and
   ADRs. Ask for a focused review of the exact conflict when needed.

## Close refinement

Update the existing task using `docs/tasks/_template.md`, preserving its ID.
Update `docs/tasks/README.md` only if task inventory or status needs it. Align
objective, scope, dependencies, expected work, acceptance criteria and
verification. Criteria should have observable outcomes and relevant error and
boundary cases.

Before declaring the task ready, check that no blocking question is hidden, each
decision has a status and basis, and implementation details left open have clear
limits. A blocked task may still be delivered as a draft with its blockers.
Record the verdict and next step in the meeting log. Do not mark implementation
complete or tests passed during refinement.

Create a commit only if the user requests one; limit it to living task/design
documents explicitly in scope and exclude meeting artifacts or unrelated local
changes.
