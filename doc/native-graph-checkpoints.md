# Durable native graph checkpoints

Interactive CLI graph control passed its native graph-service contract at
`51d731d` in a gate whose overall result failed two separate request-count
assertions. The exact [provenance](evidence/native-cli-graph-control-initial-gate-provenance.json)
and [unaltered test excerpts](evidence/native-cli-graph-control-initial-gate.log)
record that limited result; no runtime bundle was published.
`chat` now accepts `/graph-watch ROOT_ID`, using the same recorded root and
validated children rather than admitting a new workflow. It exposes human
prompts with checkpoint revisions and accepts explicit bounded JSON input,
child-operation approval/denial and root cancellation. Stale input is rejected
without automatic replay, and detaching leaves backend execution owned by the
server. The extended native graph service contract covers model-free human/tool
flows, cross-client stale input, reconnection, cancellation and a real child file
creation. These cases ran against actual compiled native components and xlang3
persistence; inference and human answers are labelled protocol fixtures. Later
console graph launch, run navigation, rejection recovery and cancellation receipt
changes still await the corrected full gate at `37e153b`. This does not establish
live console acceptance, dynamic graph planning or full coding parity.
See [interactive CLI scope](native-interactive-cli.md).

Source `b9e96fceb8a8cc25db597f51b2a50583294b6009` passed **40 native and 41 extension contracts** in [isolated CI](https://github.com/xlang-foundation/xMind/actions/runs/37653997518): [hosted CTest](evidence/native-graph-checkpoint-hosted-ctest.log), [original job/TAP output](evidence/native-graph-checkpoint-passing-ci-job.log), [hosted provenance](evidence/native-graph-checkpoint-hosted-provenance.json). Earlier local evidence is preserved: [complete build/CTest output](evidence/native-graph-checkpoint-local-build-ctest.log), [source/binary provenance](evidence/native-graph-checkpoint-local-provenance.json). Completion/controller values in the ledger fixture are synthetic; transactions and SQLite failure/reopen checks are actual. The subsequent [native graph executor](native-graph-runner.md) adds actual component execution; public graph scheduling, authenticated graph HTTP/CLI/view controls and live-provider acceptance remain pending.

Repository schema version 6 adds an immutable-plan-bound coordinator snapshot and positive checkpoint revision to each graph root. Admission advances the coordinator and creates the child, private prompt and root/child events in one transaction. A revision precondition rejects stale scheduling decisions. Failed admission events roll checkpoint changes back along with the child. Node readiness/conditions are checked again from the stored checkpoint, so a declared but unready agent cannot be admitted.

Settlement takes a child identity, not caller-supplied success/output. It reads the observed terminal child row and stored final assistant message, then advances the coordinator and commits a settlement event. Repeated settlement is idempotent. Active children cannot settle; stale revisions fail. Failed/uncertain outcomes halt further admission. Oversized/unusable completed-child output fails coordinator progress while the full child history remains retained; result artifacts remain required. Root completion requires both retired/completed children and a finished coordinator, including conditionally skipped nodes.

Human-node admission persists its wait and, when no work remains running/ready, the root's paused state. Input consumes a waiting node, commits its typed data/controller event and returns a paused root to running in the same transaction. Conditional and propagated skips also update the snapshot/event atomically. These methods are internal C++ controller operations. The future access service must derive the actor and authorize root/session controller access; no public actor/state-mutation endpoint was added. Human input is graph data, not a grant for native effects.

Normal repository operations restore the coordinator in a live-owner mode, preserving admitted work. Startup owner recovery uses interrupted mode: previously running nodes become uncertain, with a persisted recovery snapshot/event. Completed data and human waits are preserved. Version-5 ledger migration reconstructs known agent completions from actual private messages, treats other recorded work as uncertain and validates dependency/condition order. Unsupported legacy ledgers fail migration rather than invent completion. Loop attempts, richer result artifacts and reconciliation remain pending.

The new actual repository fixture covers dependency admission, checkpoint revisions, settlement-event and human-input-event trigger failures, pause/reopen/resume, stale input rejection, conditional skips and unchanged completed first-child history. Existing concurrent native child loops now settle their actual outputs before root completion. Older schema migration contracts continue verifying credential/conversation/resource preservation. The scheduler, tool-node dispatcher, queue ownership and public/client integration must still be implemented before claiming runnable graph workflows.
