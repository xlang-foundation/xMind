# Native model/tool agent loop

`AgentRunner` is the shared C++ model/tool loop for backend execution workers and future graph nodes. It invokes the configured provider through the native transport, forwards streamed model events to durable storage, executes offered workspace tools and sends their actual results in the next model request. There is no built-in response generator or substitute provider. This checkpoint implements the loop library; HTTP/CLI agent scheduling is still pending, so no product execution route is enabled yet.

The settings select an endpoint/model, optional read workspace, optional encrypted credential reference, instructions, turn bound and run deadline. A workspace requires declared model function-call capability. Without a workspace, the same loop handles text-only single-agent requests. Credentials are resolved through the persistence worker into backend-owned secret bytes for each provider request, separate from model messages. `AgentRunner` and its persistence service must outlive their execution calls.

## Durable lifecycle

Starting commits the queued run and user prompt together. Execution claims queued-to-running atomically; a duplicate worker losing that claim does not fail another worker's run. The current read tools validate arguments and workspace boundaries before file access, and persist started/completed/failed events with distinct activity IDs. An assistant tool-call message and every matching tool result commit in one batch. The final answer, completed state and corresponding events also commit together. User messages submitted separately are rejected while a root run is active, preventing concurrent prompt changes during context construction.

Tool denial and input/file errors are genuine error results returned to the provider. Unknown/unoffered tools never execute. Provider failures, incomplete/model-protocol errors and exhausted turns produce failed states; cancellation produces cancelled state. Provider HTTP error events preserve status only, without copying private provider error bodies. Run timeout is distinct from user cancellation and produces `agent_timeout`.

The run deadline is cooperative: it cancels pending native HTTPS operations and is checked between tool operations. Filesystem I/O proceeds at the OS's pace; this is not a hard real-time deadline. Deadline-thread initialization failure is handled as an agent failure. Pauses, external-effect reconciliation and write/shell permissions are not implemented by this loop. Only the three actual read tools are offered.

## Validation scope

The first full Release run passed twelve native contracts. The loop contract was then rerun after adding the run deadline; the final request/loop contracts passed after preserving refusal provenance and handling deadline-thread creation failures. Evidence: [native-agent-loop-final.log](evidence/native-agent-loop-final.log). A subsequent active-owner claim test also passed: [native-agent-claim-final.log](evidence/native-agent-claim-final.log).

The loop test uses an explicitly synthetic inference wire peer plus real filesystem tools and embedded-xlang3 persistence. It verifies model/tool continuation, actual file content in the persisted tool result, encrypted credential resolution without placing the credential in model messages, workspace denial, HTTP failure, cancellation during a pending request, deadline failure, turn bound, rejection of a competing worker while the actual owner is streaming, atomic rollback under injected storage faults and reopening the saved conversation/state. No live model was called and no coding-task success is claimed by the synthetic reply.

Live provider validation requires the selected endpoint/model and privately configured credential. The product still needs asynchronous HTTP/CLI admission/cancellation, VS Code integration, complete tool permissions/mutations/processes, context compaction, provider families/routing, MCP/A2A and graphs. These remain part of the full project goal.
