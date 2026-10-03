# Bridge and provider recovery for refinement

Use this when the coordinator cannot get a response from a selected Codex,
Claude or other provider during a refinement session.

## Health check and diagnosis

1. Before assigning technical work, confirm the configured transport and model
   with a minimal request such as `pong`. This checks basic connectivity only.
2. On failure, record the provider, role, model, transport, sanitized error and
   time in `bitacora.md`. Never log API keys, cookies, tokens or credentials.
3. Classify the failure: inactive bridge/process, authentication/account/quota,
   unsupported model, transient timeout/connection, malformed payload, or empty
   response.
4. Consult repository instructions for starting or restarting the bridge. Do
   not invent commands or change account credentials/settings. Restart once
   only when use of that configured bridge is already authorized, then repeat
   the health check.
5. For a transient failure, retry the same role/model once with a narrower
   prompt. Treat empty or truncated output as no response; ask for continuation
   once if the same session supports it.

Do not retry the same work repeatedly without new evidence. If the cause recurs,
mark that specialist turn blocked and continue independent refinement work.

## Preserve decision quality

- Never attribute a recommendation to an agent that did not return it.
- A coordinator recommendation remains labeled as the coordinator's own.
- Do not replace the provider or model route silently. Ask the user to choose a
  replacement if the assigned provider/model cannot be reached.
- Do not treat provider failure as user approval. Keep affected questions open;
  continue only where project sources determine the answer.
- Log concise, sanitized evidence and the recovery action/result in the meeting
  bitacora.
