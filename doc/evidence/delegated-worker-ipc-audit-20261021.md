# Delegated worker IPC integration audit — 2026-10-21

This is a source audit, not runtime acceptance. Baseline: 0ffa0aaa. The latest
65-contract regression qualifies ordinary workers only; selected delegation and
graph tests still exercise their existing execution path.

## Confirmed execution boundaries

- `Native/src/delegation_executor.cpp`: NativeChildExecutor reserves bounded
  scheduling capacity before accept_delegation_batch and dispatches afterward.
  Each action constructs AgentRunner in-process and shares RootExecutionBudget.
  Preserve that admission-before-dispatch order and physical drain on join.
- `Native/src/agent_runner.cpp`: read_workspace explicitly rejects children;
  the following child branch requires child_admission and a supplied shared
  budget. Delegated leaves require workspace.inspect revision 1, at most four
  turns, and no edits, process profiles, MCP, A2A, planning or delegation.
- `Native/src/agent_worker_run_journal.cpp`: OrdinaryJournal requires root=owner,
  zero child/parallel allowances, and native.direct-agent/native.context. Its
  root_budget and funding methods also apply owner-only identity checks;
  child_admission is closed. Merely removing the first constructor check fails.
- `Native/src/agent_worker_journal_rpc.cpp`: reservation decoding requires role
  parent and constructs a parent reservation. A leaf reply needs exact derived
  role validation here, independently of backend funding correctness.
- `Native/src/persistence_service.cpp`: reserve_agent_worker_model ALREADY derives
  parent/leaf from root_run_id versus owner_run_id. No new caller-supplied role
  is necessary. This corrects the earlier working assumption that it was fixed
  to parent.
- `Native/src/repository.cpp`: worker_running_scope recognizes admitted agent
  children, but queued startup is root-only. It compares every worker provider
  to the root budget provider; named child models therefore need owner-specific
  captured provider validation. Workspace membership is read from the owner.
  reserve_model_call separately restricts worker funding to direct roots.
  transition startup, workspace read authority, assistant receipts, canonical
  turns and terminal retirement retain ordinary-root restrictions.
- `Native/src/agent_worker_runner.cpp`: assembly reads the shared root budget
  but turns its total model allowance into local max_turns and passes no shared
  budget to execute. A child needs its own admitted turn cap and the remaining
  original deadline, not a fresh full-duration clock.

## Implementation constraints established by this audit

Use an exact already-admitted child record captured by the backend. A scoped
read of that record may satisfy AgentRunner; it must not introduce arbitrary
child creation, catalogue editing or admission RPC. Keep root-budget identity
and owner-run identity distinct throughout the journal and reply codecs.

Keep NativeChildExecutor as the bounded scheduler and shared leaf-slot owner;
replace its inline execution action with the unified xmind process executor.
The parent retains its RootExecutionBudget and original cancellation/deadline.
Repository transactions continue owning counters, child provenance, canonical
model/tool receipts and terminal state. Child processes receive no credentials.

Extend delegated read-only admission coherently across startup, funding,
read/guidance tools, canonical turns and retirement. Do not widen file/process,
remote or callable authority merely because those ordinary-root checks look
similar. Named subagents must bind their actual frozen model, instructions,
tool intersection and permission layers, including different configured models.

## Required runtime evidence

Actual xmind child PIDs and xlang3 SDK C++ remote calls; two concurrent children;
shared root call exhaustion and original deadline; per-child four-turn cap;
wrong root/owner/generation/preset/workspace/model rejection; no child edit,
process, MCP/A2A or nested-delegation authority; cancellation and physical drain;
canonical child results and parent join without replay; named child model and
permission intersection. Then integrate planning and graph placement, run the
full suite, and perform updated installed client acceptance.
