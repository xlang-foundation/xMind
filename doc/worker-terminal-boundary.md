# Worker failure and cancellation boundary

Current checkpoint: the unified `xmind.exe` private inherited-channel entry is
implemented and qualified with synthetic backend owners below. Service placement
and broader worker capabilities remain open. Earlier checkpoint limitations in
this document describe their respective source versions.

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

The [native model-gateway checkpoint](evidence/agent-worker-model-gateway-focused-20261020.json)
adds `make_agent_worker_model_gateway`. It retains the ordinary journal and
requires that journal's exact peer binding. It captures the public model ID and
wire from the scoped root budget, accepts only `worker://captured` local routing,
and reads the current attempt from acknowledged journal progress. Its typed RPC
sends only attempt/request values and streams events with cancellation. Private
provider configuration, credentials and original deadlines remain backend-owned.
A per-instance dispatch guard rejects re-entry. Count and compaction reject
explicitly until their own backend capabilities exist.

Actual plain/read AgentRunner fixtures now forward model calls to this native
gateway as well as journal calls to the native adapter. Factory probes reject a
null journal and a reported foreign binding. Model probes reject changed
endpoint/model/wire, pre-cancelled dispatch, re-entry and closed context methods.
Completion, provider failure, streaming cancellation and read-approval
cancellation pass with both adapters. The focused gate passes 53 contracts in
154.03 seconds with frozen inputs and all 133 binaries; the independent HTTP
peer still verifies exactly 27 requests. Worker-specific non-chat wires and
broader profile compatibility need further qualification. Production read/runtime
adapters, process entry and AgentService placement remain open.

The [native read-workspace checkpoint](evidence/agent-worker-read-workspace-focused-20261020.json)
adds `make_agent_worker_read_workspace`. It retains the ordinary journal,
requires its exact peer binding, rejects workspace-less roots and preserves the
captured workspace identity/revision. Request preparation forwards owned values
through the native model client; read hosts use the native read RPC client. No
native root, local guidance object, policy or grant is opened/created in the
child by this adapter. Backend request admission, permissions and durable read
claims remain authoritative; metadata alone grants no execution.

The actual read AgentRunner fixtures now use native workspace, journal and model
adapters together. Probes reject null/foreign bindings, workspace-less roots,
cancelled preparation and an unadmitted foreign read invocation. The three-window
canonical read loop and pending-read-approval cancellation both pass. The focused
gate passes 53 contracts in 154.88 seconds with frozen inputs and all 133 binaries;
the independent peer still verifies exactly 27 requests. Runtime/closed-context
adapters, production process entry, broader profiles and AgentService placement
remain open; this is not installed or complete product acceptance.

The [native closed-port checkpoint](evidence/agent-worker-closed-ports-focused-20261020.json)
adds `make_agent_worker_closed_ports` and removes fixture-only unsupported
context/catalogue/tool/runtime classes from the actual worker loop. The factory
returns native guard objects whose operations explicitly reject unadmitted
context maintenance, catalogue queries and child-local native effects/runtime.
They return neither empty facts nor successful write acknowledgements. Admitted
model/read operations continue through their separate native RPC adapters.

Actual plain/read AgentRunner fixtures use these native objects. Probes check
context compaction, a void maintenance write, agent/credential catalogue access,
child-local read construction and native A2A catalogue access. They require an
explicit not-admitted diagnostic, so an invalid future cannot masquerade as a
denial. Completion, provider failure, model cancellation and read-approval
cancellation pass with the guards. The focused gate passes 53 contracts in
153.56 seconds with frozen inputs and all 133 binaries; the independent peer
still verifies exactly 27 requests. These objects deny capabilities rather than
implement context maintenance, MCP/A2A, native effects or delegation in the
worker. Captured settings/bootstrap, production process entry, broad profiles
and AgentService placement remain open.

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

## Captured public model controls checkpoint

The worker model admission RPC now returns a strict version 1 public controls
record from the captured backend provider and policy. It checks the current
lease, original deadline and owner/caller cancellation without reserving or
dispatching a model, preparing context or admitting a read. The record excludes
provider endpoints, credentials and private deadlines. Unknown fields, duplicate
keys, unsupported enum values and inconsistent requested controls reject.

Actual plain/read child runners build their proxy model configuration from this
record and obtain turn/time limits from the scoped root budget. This is a step
toward captured bootstrap; instructions, complete settings, production process
entry and service placement remain open. The 53 focused contracts passed in
156.32 seconds with source, dependencies and 133 native binaries frozen. Evidence:
`evidence/agent-worker-model-admission-focused-20261020.json`. No installed,
rendered UI, live-provider or full native suite qualification is claimed.

## Native worker assembly checkpoint

`execute_agent_worker_read_root` now composes the production ordinary journal,
model gateway, read workspace and explicit closed ports around the shared native
`AgentRunner`. It requires backend-owned request windows and captured ordinary
root limits; it supplies no child-selected instructions, destination or local
execution owner. Exceptions and terminal handoff propagate without fabricated
run records. Its caller retains the peer and physical child ownership through
supervisor drain.

Fresh child cases call this production function for completion and cancellation
during native read approval. Parent records verify seven canonical history
messages, three charged models, two tool turns, six tool activity facts and one
fresh native read on completion. Cancellation retains the earlier canonical
turn and two charges, performs no read dispatch, and records terminal state only
after the exact child exits. Existing instrumented cases remain in place.
The independent peer verifies exactly 32 intentional requests. All 53 focused
contracts pass in 154.01 seconds, with 2,682 project files and 133 native binaries
frozen. Evidence: `evidence/agent-worker-native-assembly-focused-20261020.json`.

Production process entry, service placement, general settings bootstrap, broad
provider profiles and graph/context/effect placement remain open. This library
composition checkpoint is not installed-runtime or full-suite qualification.

## Unified product worker entry checkpoint

The unified executable now dispatches private `agent-channel-worker` launches
directly to the native worker entry. It accepts only its inherited handle,
requires the authenticated version 1 bootstrap, and exposes canonical empty
`worker.execute` and `worker.cancel` requests. Each child admits one execution;
concurrent and later duplicate execution requests reject. The entry uses the
production read-root composition and retains transport after completion or
terminal intent until parent shutdown. Fixed replies carry only version and
outcome; they neither fabricate a `Run` nor authorize backend retirement.

The child transport has a one-hour ceiling. Backend operations continue to
enforce the original deadline, exact current lease and owner cancellation.
Journal RPC cancellation and timeout are normalized at construction and future
boundaries to the shared runner's transport exceptions. This avoids treating a
typed cancellation during a journal history read as an ordinary agent error.

Fresh cases launch the actual bound `xmind.exe`, rather than the fixture image,
for canonical completion, read-approval cancellation and controlled provider
failure. Four further cases inject typed cancellation/timeout replies at journal
admission and history reads. They preserve the exact queued/running boundary,
existing history and zero model charges until observed child exit and backend
retirement. Invalid bootstrap exits without execution; malformed controls,
unknown methods and duplicate execute requests cannot add model calls.

The final focused gate passes 53 contracts in 155.70 seconds, with 2,684 project
files and 133 native binaries frozen. Its independent HTTP peer observes exactly
38 intentional requests. The earlier 53-contract entry gate (154.63 seconds) is
also retained with its different source patch. Evidence and exact product child
identities: `evidence/agent-worker-product-entry-cancel-focused-20261020.json`.

Normal AgentService/GraphService dispatch still requires integration. The owner
must derive terminal causes from captured backend evidence, retain physical
capacity through drain, and preserve a completed root when its reply is lost.
General settings, broad provider profiles, graph/context/effect placement and
installed/rendered/full-suite qualification remain open.

## Completed drain and physical capacity checkpoint

`drain_completed_agent_worker` supplies the completed-root counterpart to failed
and cancelled retirement. It checks the exact process identity, full captured
lease, retired/completed lease reason and ordinary completed root before drain,
then rereads the same completion after physical process/job exit. It performs no
terminal write. A lost reply or nonzero child exit cannot replace the canonical
completed run with failure. Reobservation returns the same facts without adding
events, history or model requests. Queued roots and altered retired leases reject
before shutdown. External call threads must already have joined.

Actual product read-worker and native context completion cases now use this
helper. A fresh controlled completed child holds its exit on a private release
event after transport closure. A one-slot `RootWorkExecutor` action observes an
expired drain deadline, retains the exact live child and occupied slot, and
leaves its successor queued. Only explicit release and the same child's actual
exit allow the successor to run. Canonical history/events and one charged model
call remain unchanged. The lost-completion-reply case also uses the helper and
preserves completion across the explicitly induced nonzero exit and repeated
observation.

All 53 focused contracts pass in 155.45 seconds, with 2,684 project files and
133 native binaries frozen. The independent HTTP peer verifies exactly 39
intentional requests. Evidence and held child identity:
`evidence/agent-worker-completed-drain-focused-20261020.json`.

This verifies the shared physical executor boundary needed for service placement;
it does not wire AgentService/GraphService to worker processes. Their captured
settings, failure-cause selection, retained ownership on observation timeout and
broader capability placement remain open. No installed or full-suite claim is
made by this focused checkpoint.

## Captured provider profile identity checkpoint

The native read-workspace port now retains the exact public provider identity
from its immutable backend root budget. The shared runner uses those complete
bytes for budget matching instead of reconstructing a two-field model/wire
identity. Local model and wire must still match; direct non-profile identities
retain their exact shape. This carries profile ID, revision, route and provider
without granting the worker destination, credential or profile-selection rights.
Worker-selected `ProviderExecutionIdentity` settings remain forbidden.

Two fresh actual `xmind.exe` children cover a six-field profiled read root's
completion and approval cancellation. Completion retains the exact budget and
prompt identity plus all three canonical assistant provider contexts. Cancellation
retains the prior canonical history, captured identity and two model charges with
no read dispatch. Foreign model budget selection and replacement profile settings
reject before dispatch. Existing direct-identity cases remain in the gate.

All 53 focused contracts pass in 157.58 seconds after the serial rebuild, with
2,684 project files and 133 native binaries frozen. The independent peer verifies
exactly 44 intentional requests. Evidence and exact profiled child identities:
`evidence/agent-worker-profile-identity-focused-20261020.json`.

This newly qualifies identity propagation under a controlled OpenAI-compatible
provider, not every provider wire or profile-management flow. Service placement,
general settings and graph/context/effect capabilities remain open. Installed
clients and their preserved user tasks are unchanged.

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
