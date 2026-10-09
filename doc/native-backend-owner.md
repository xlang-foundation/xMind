# Native database-owner admission barrier

The C++ persistence service now has a durable database-owner barrier. This is
the persistence part of the [runtime handoff](native-runtime-handoff.md), not
an installed upgrade protocol. It does not enable writing in the existing
read-only `TestProj` backend.

`PersistenceService` acquires the existing exclusive database lease, binds a
fresh owner generation and performs normal interrupted-work recovery. Its
typed owner record is stored through embedded xlang3 in the reserved
`native-backend-owner` information category. Generic information writes cannot
replace that record. Malformed records fail explicitly. An active generation
can reopen with a new generation/revision; an updated runtime's ordinary
startup rejects a saved quiescence receipt before interrupted-work recovery.
The newer [retirement request](native-owner-retirement.md) checks closed or
malformed owner state under the worker's lease before schema migrations.
Qualified schema upgrade and target-runtime admission remain pending.

Every queued mutation checks the durable owner generation and phase on the
persistence worker immediately before execution. This includes tasks already
queued behind a quiescence request. A cached memory flag cannot reopen
admission after an uncertain commit response. Forty-five explicitly classified
observations remain available, including saved sessions, runs, transcript,
events, public credential metadata and native credential resolution. Context
snapshot/projection/payload lookup and closed-pause validation can update
legacy indexing, so they remain fenced mutations.

The backend-only C++ methods are:

| Method | Native preconditions and result |
| --- | --- |
| `backend_owner()` | Read the current generation, revision, phase and receipt ID. |
| `quiesce_backend(expected)` | Require the exact active generation/revision and idle persisted execution/effect/maintenance ownership; commit a new revision and receipt before returning. |
| `resume_backend(receipt)` | Require the same live generation and exact quiesced revision/receipt; commit the next active revision and discard the receipt. |
| `request_backend_retirement(receipt)` | Consume the exact live quiescence receipt, retain closed admission and permanently reject same-generation resume. This is not a process-exit acknowledgement. |

All owner mutations verify the actual database lease. A stale or repeated command
fails; a lost response must be observed through status rather than replayed.
Publication and deferred commit failures preserve the prior durable phase.
Quiescence never cancels a run or settles an effect to manufacture idleness.

The idle check includes queued/running/paused runs, unresolved effects, model
reservations, delegation, dynamic plans/calls/claims/human pauses, active budget
segments, idle context owners, pending manual context requests, compaction,
counting and inference ownership. These persisted checks are necessary, but
the final transport must also check the actual runtime's health and idle state,
verify workspace authority, and prevent concurrent transport admissions.

The local contract uses actual C++ and embedded-xlang3 SQLite transactions in
disposable databases. It tests queued and parallel admission races, active run
states, actual manual/idle context ownership, an unresolved synthetic effect
ledger beside a terminal root, stale/repeated receipts, encrypted credential
retention, publication/commit failure rollback, malformed-state rejection and
real database reopen. Provider inputs and effect receipts are fixtures; no
provider calls or file effects are claimed. The complete native gate and its
retained earlier attempts are recorded in the accompanying evidence.
[Complete 93-contract gate and retained attempts](evidence/native-backend-owner-local.json).

The [optional HTTP controller](native-owner-control.md) separately supplies
authenticated observation, quiescence and same-generation resume. Public
shutdown commands, CLI lifecycle controls, target-package binding,
receipt-controlled generation startup and installed/rendered continuity remain
pending. No retirement route or UI button is advertised. Older runtimes do not
implement this barrier; they require the
explicit legacy operator path and cannot be treated as receiving a native
quiescence receipt. Views never open or copy the profile database.
