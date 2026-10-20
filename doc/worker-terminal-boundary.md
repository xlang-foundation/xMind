# Worker failure and cancellation boundary

Source audit against `ff463d0c` (2026-10-20). This is an implementation
requirement, not evidence that service placement or failure retirement works.

An initial backend persistence operation is now implemented as
`Repository::terminate_agent_worker`, queued through `PersistenceService`. It
accepts failed outcomes with fixed `deadline`, `provider_failure`, `worker_exit`
or `dispatch_error` reasons, or cancellation with `cancelled`. It requires the
exact active current lease and an ordinary queued/running direct root, rejects
dynamic roots and executing operations, and preserves the native root boundary.
Run termination, waiting-operation cancellation, unfinished reservation
interruption and generation retirement share one transaction. Failed
observations map to the existing lease `worker_exit`/`dispatch_error` reasons;
the terminal run event retains the specific fixed reason. Duplicate calls reject
without adding events. No terminal RPC, child drain or service placement is
implemented by this persistence API.

The [focused gate](evidence/agent-worker-terminal-focused-20261020.json) passed
52 contracts in 182.36 seconds. Its new synthetic ledger case verifies exact
process-identity rejection, invalid outcome/reason rejection, full rollback at
an injected lease-retirement failure, interruption of reserved/started calls
while retaining finished reservations and charges, queued cancellation without
model startup, and rejection of duplicate termination/late continuation. Source
and dependency closures and all 133 native binaries were frozen across tests.
This does not qualify actual child failure/cancellation, pending-approval drain,
effect retirement or product placement. The remaining qualification below stays
open; the source observations describe the pre-operation baseline.

`drain_and_terminate_agent_worker` now provides an owner-thread component around
that transaction. Before shutdown it checks the captured PID, creation time,
executable identity, scope and current durable lease. `AgentWorkerRpcPeer::drain`
also requires the exact process channel object, joins the RPC reader/handlers
through stop, closes the channel and observes process/job exit. Only then does
the helper call the terminal transaction. It never terminates or restarts a
process on an observation timeout; the caller retains the same objects and
authority for further observation. External RPC call threads must already be
joined, and this operation must never run inside a handler or sink.

The [drain component gate](evidence/agent-worker-drain-focused-20261020.json)
passed 52 focused contracts in 159.92 seconds with frozen inputs and all 133
native binaries. The actual controlled child RPC cases now use this component
for streaming cancellation, two provider errors and streaming child exit (code
73). The fixture checks wrong-PID rejection without stopping the worker, and
rejects a different channel even when its scope matches. The independent peer
still observes exactly 23 model requests. These are model-proxy child cases;
the actual child AgentRunner failure/cancellation journal and AgentService
integration remain open. Dedicated timeout and post-drain storage-failure
injections also remain open.

Those two drain fault boundaries are now covered by the
[fault-injection checkpoint](evidence/agent-worker-drain-faults-observed-20261020.json).
An actual child acknowledges that it is blocked on an explicit owned release
event. An expired observation deadline closes RPC but leaves that same PID and
creation time alive, the run running, the lease active, events unchanged and the
reservation charged. Explicit owner release then permits observation and
termination of the same process without restart or model dispatch. A separate
actual model-proxy case injects a lease-retirement SQL abort after the child has
exited. Run state, lease/revision, charged uncertain model, history and event
counts remain unchanged; removing the fixture fault permits recording against
the same exited process without a second model request. Both cases passed in
both recorded gates; the independent peer still observes exactly 23 requests.

The first fault gate was 51/52 because the existing Responses HTTP contract
received empty CLI stdout with exit status zero. The original logs are retained.
Its synchronous command wrapper now reports command name, status, signal and
output byte counts and explicitly rejects empty/invalid JSON; deadlines and
inference assertions are unchanged and no retry was added. A fresh diagnostic
gate passed 52/52 in 178.22 seconds, with source/dependency closures and all 133
native binaries frozen. This passing observation does not establish the cause
or a production fix for the intermittent empty CLI output. That issue remains
open alongside actual AgentRunner failure/cancellation and service placement.

A [CLI output follow-up](evidence/cli-output-focused-20261020.json) establishes
and fixes one concrete output failure: an ordinary JSON command returned status
zero when its inherited stdout handle was read-only. The unchanged new contract
fails against the baseline CLI, then passes after ordinary JSON results and
session-export acknowledgements explicitly flush and check stdout before
returning success. A write failure now returns status one with a fixed diagnostic
to inspect the result before retrying; the CLI does not resubmit the command.
The focused gate passes 53 contracts in 154.16 seconds, and a separate existing
HTTP/CLI integration gate passes, including successful session export/import and
persisted history, on identical source and all 133 binary hashes. The baseline
failure and both passing gates are retained. This reproduces the unwritable
handle false-success defect, not the earlier unexplained empty output on a
normal pipe; the latter remains unproven as a root-cause match or resolved issue.

The [terminal-handoff checkpoint](evidence/agent-worker-terminal-handoff-focused-20261020.json)
now qualifies one actual plain, no-tool child AgentRunner provider failure.
`AgentTerminalHandoff` carries only failed/cancelled intent and is preserved by
budget retirement and the runner failure boundary. A worker journal can unwind
without returning a fabricated terminal `Run` or reservation. The controlled
child fixture uses this path when a failed model attempt still requires owner
drain. Its backend independently captures the real provider HTTP failure before
accepting intent; it checks that the child is still alive, root running, lease
active, history user-only and model claim uncertain. Exact child drain then
precedes atomic failure, one interrupted reservation and one terminal event,
with the single charged model retained and no assistant invented.

The focused gate passes 53 contracts in 155.65 seconds with frozen inputs and
all 133 binaries; the independent HTTP peer verifies exactly 24 requests,
including the new actual runner failure. This is a native unwind mechanism and
an actual child fixture, not a production worker journal or AgentService
placement. Actual child AgentRunner cancellation, read-worker failure, broader
terminal causes and graph/effect placement still need their own qualification.

The [actual runner cancellation checkpoint](evidence/agent-worker-runner-cancel-focused-20261020.json)
now covers controller cancellation of that plain, no-tool child runner during a
real controlled model stream. The backend waits for streaming, records its own
explicit stop request and sends an authenticated fixture stop to the captured
child. The child links handler cancellation to the same stop source, passes its
token into the actual `AgentRunner::execute`, and preserves cancelled intent
when unfinished model accounting hands off. Before drain the test requires a
live child, running root, active lease, user-only history and an uncertain charged
model. After exact exit it requires one cancelled event, one interrupted
reservation and the original charge. The independent HTTP peer verifies stream
abort and exactly 25 requests, including the separate provider-failure case.
All 53 focused contracts pass in 154.52 seconds with frozen inputs and binaries.

The stop command and journal adapters here remain fixture-only. This qualifies
the existing native handoff/drain components with an actual child AgentRunner;
public controller-to-worker integration in AgentService, read-worker approval
cancellation, production adapters and graph/effect placement remain open.

The [read approval-cancellation checkpoint](evidence/agent-worker-read-approval-cancel-focused-20261020.json)
now covers the actual read-capable child AgentRunner while its native read
approval is pending. Two actual backend model windows supply root/nested
guidance and the real read catalogue. The first model's two guidance-blocked
reads form a committed canonical turn; the second model's read reaches an
awaiting-approval operation. The controller observes that operation and stops
the captured child. The real runner hands off cancellation, the operation
becomes cancelled, and the backend retains a returned failed read claim with
`dispatched=false`. Before drain the test requires live process ownership,
running root, active lease and the four prior canonical messages. Exact child
exit then precedes one terminal cancellation, preserving both model charges and
all prior message bytes. No target-read dispatch or interruption of finished
model reservations is recorded.

The focused gate passes 53 contracts in 153.67 seconds with frozen inputs and
all 133 native binaries. The independent peer verifies exactly 27 requests,
including two windows for this new cancellation case and all earlier cases.
This remains qualification through fixture adapters. Production worker entry,
public AgentService placement, broader profiles and graph/effect placement are
still required; no installed or complete product acceptance follows from it.

The [native run-journal checkpoint](evidence/agent-worker-run-journal-corrected-20261020.json)
adds `make_agent_worker_ordinary_journal` to the production worker journal
library. It admits a captured ordinary, non-delegating root and exposes scoped
reads and typed backend startup, funding, model receipt, activity, read-turn and
completion operations. Graph, context, effect and skill mutation capabilities
throw explicit errors; they do not return empty state or acknowledge writes.
Stopped funding rejects locally, while backend RPC continues to enforce its
captured lease and original authority boundaries. Terminal requests retain the
existing typed handoff for post-drain retirement.

The adapter derives tool-call bindings from the backend canonical assistant
receipt through the native history projection. Model finish sends only its
attempt identity and replaces the local assistant with the backend canonical
receipt. Read-turn and completion bytes are comparisons, never authority for
backend writes. Read-turn receipt/history
comparisons happen after the backend commits its actual facts; a comparison
failure does not roll those facts back or authorize replay. Progress snapshots
contain acknowledged facts and counters, not live execution authority.

Actual child AgentRunner fixtures now forward journal operations through this
production adapter, retaining adversarial replay probes and independent model
proxies. Plain/read completion, provider failure, model cancellation and pending
read-approval cancellation pass with it. Direct adapter probes reject foreign
scope, forged completion, stopped funding and closed capabilities. The focused
corrected gate passes 53 contracts in 155.39 seconds with frozen source/dependencies and
all 133 binaries, and the independent peer still verifies exactly 27 requests.
The initial 53-contract pass is retained too. Review then corrected journal
authority cancellation/expiry to the shared loop's transport exception types,
and the stopped-funding probe now requires `TransportCancelled`. Parent success
records follow the actual child checks; child stdout is discarded by the owned
launch and is not used as evidence.
This supplies the native journal component; production process entry, remaining
runtime/model adapters, broad profiles and AgentService placement remain open.

## Observed ownership gap

`Repository::transition` permits a captured worker only for the exact ordinary
root queued-to-running transition. Its general terminal path checks root and
operation boundaries, cancels waiting operations and appends the terminal event.
It does not call `Impl::retire_worker`.

`Repository::retire_agent_worker` separately commits `Impl::retire_worker`.
That operation marks unfinished read claims and dispatching model claims
uncertain and retires the generation. It does not terminalize the run or update
`agent_model_call_reservations`. Returned read facts and responded model facts
are retained. Combining the two public calls in a controller would leave a
failure window between transactions.

The successful completion path already combines run completion and lease
retirement in one transaction. Startup recovery has a broader transaction under
the database owner lease; it is not an online worker failure API.

`AgentService::execute_job` currently executes the local runner. Its exception
paths quarantine a job as faulted rather than inventing terminal results. The
child runner fixture explicitly rejects terminal transitions other than its
implemented successful completion path. Positive read-loop qualification
therefore does not qualify service placement or failed/cancelled child loops.

Sources: `Native/src/repository.cpp` (`transition`, `complete_run`,
`Impl::retire_worker`, `recover_interrupted`),
`Native/src/agent_service.cpp` (`execute_job`), and
`Native/tests/agent_worker_runner_fixture.hpp` (`ChildJournal::transition`).

## Required online operation

Add a backend-owned typed terminal operation for an exact captured lease and
current generation. Initially restrict it to ordinary direct roots, matching the
actual read worker's supported scope. Accept only failed/cancelled outcomes and
a fixed reason vocabulary; never accept arbitrary child lifecycle JSON.

In one xlang3/SQLite transaction, validate ownership and the existing native root
and operation boundaries, terminalize the run, cancel pending approvals,
interrupt unfinished model reservations, and retire the generation with its
unfinished claims marked uncertain. Preserve consumed allowance, actual model
responses, actual read outputs and already committed conversation turns. An
unresolved dispatched effect must retain its uncertainty and ownership rules.

Cancellation or deadline expiry must stop new authority without preventing the
backend from recording an observed terminal fact. Do not reuse the ordinary
RPC mutation guard unchanged: it rejects stopped/expired requests before any
such fact can be recorded. Conversely, terminal observation must not grant new
model/read execution, extend the deadline or revive a retired generation.

The supervisor must stop and drain owned handlers, gateways and native work,
and observe the exact owned child process terminal before releasing physical
ownership. A terminal ledger row alone is not proof of physical drain. An
observation timeout does not authorize restarting the child or replaying a task.

## Qualification required before service placement

- Ledger contracts: exact lease rejection (including process identity), stale
  generation, illegal outcome/reason, active effect boundary, queued and running
  cancellation, and no duplicated terminal events on an explicitly supported
  identical acknowledgement replay.
- Transaction rollback: inject a lease-retirement write failure and verify the
  run, reservations, claims, approval state and event history all roll back.
- Fact retention: preserve finished model reservations, charges, supplied model
  responses, returned reads and canonical tool turns; interrupt only unfinished
  reservations. Retain late actual backend results without redispatch.
- Actual child runner: controlled provider failure, caller cancellation during
  model work, cancellation during approval, and child exit after dispatch. Pass
  the real handler cancellation token into `AgentRunner::execute`; no fabricated
  failed `Run` return or no-op unsupported journal methods.
- Lifecycle: prove physical drain before service slot release, exact process
  identity observation, rejection of late continuation, and absence of retries.

These gates remain open. Keep graph/context/effect placement and broad provider
profiles outside this initial ordinary read-worker qualification, then qualify
their own ownership boundaries before claiming the full product requirement.
