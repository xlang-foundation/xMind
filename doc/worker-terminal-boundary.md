# Worker failure and cancellation boundary

Source audit against `ff463d0c` (2026-10-20). This is an implementation
requirement, not evidence that service placement or failure retirement works.

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
