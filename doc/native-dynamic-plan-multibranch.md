# Multi-branch human waits in dynamic plans

Status: **partially implemented**. Schema v29 stores a normalized wait set;
`RootExecutionBudget` rendezvous waits for every active descendant to finish or
register a human-only frontier; and one SQLite transaction pauses or resumes
all validated owner paths. The `native_dynamic_plan_repository_contract`
exercises two concurrent wait registrations, restart recovery, and resuming all
owners after one answer with xlang3-backed SQLite. The
`native_dynamic_plan_engine_contract` now also drives two real nested Agent
runners through one shared root pause, answers one branch, verifies the other
remains pending after the partial resume, and completes both original calls
exactly once. HTTP ownership lookup, root/nested question DTO validation, and
nested input/resume handling in the browser and VS Code views have focused
contracts. Full completion still requires exercising the nested wait set
through authenticated HTTP, CLI and the actual clients, plus reverse-order and
concurrent answers, cancellation races, expiry/uncertainty cases, and measured
budget/context checks across repeated partial resumes.

## Required behavior

Sibling dynamic-agent branches may each accept one or more human questions
while using the same ordinary root run, provider/workspace authority and
`RootExecutionBudget`. A question in one branch must not retire, abandon or
pause a sibling that still has active model work, a live child, a physical
transport, or an unresolved effect. Once every branch is either terminal or
waiting on an accepted question, xMind may pause the root and the participating
owner chains together. The same rule applies when the root graph itself owns
multiple questions.

An authenticated answer is bound to its exact root, plan owner, accepted plan
call, human request, plan revision and state sequence. Answering any one of
several questions resumes the complete durable root wait set and its one
budget segment. The scheduler then runs only dependency-ready work. Unanswered
questions remain waiting; if no other work is ready, the run closes a new
active-time segment and pauses again. Input ordering may differ from branch
creation order. Duplicate, stale, cross-root and post-expiry answers do not
create a continuation.

## Durable ownership model

Replace the current one-row `dynamic_owner_pauses` relation with a root pause
header plus child wait records. The header binds the root, the single closed
budget segment, pause event, context pin and lifecycle. Each wait record binds
one exact `(root_run_id, plan_id, plan_call_id, request_id)` tuple and the
complete immutable owner path from the ordinary root to the nested plan owner.
Multiple requests in one plan share its call and owner path; distinct sibling
plans retain distinct accepted calls and paths. Stable request IDs remain the
input idempotency keys.

The pause transaction must re-derive every owner path from plan ownership and
claimed parent-node receipts. It accepts the pause only if every nonterminal
root child belongs to a registered wait path and is physically drained, every
listed plan/call/request still has the expected accepted/waiting state, no
model reservation or MCP/A2A/process/file effect remains live or unresolved,
and the root budget has no active leaves. It then closes exactly one budget
segment, pauses the root and all participating owner runs, and emits one
committed pause event containing the bounded set of stable wait identities.
An unregistered live sibling prevents the transaction; it is never silently
cancelled.

The initial migration preserves every existing single-chain pause as a
one-entry wait set, including its context pin, segment, event and owner path.
Foreign keys, retained-history triggers and integrity checks must cover both
the header and each wait record. Old and new paused databases must reopen
without synthesizing an accepted plan call or changing event identity.

## Answer and resume lifecycle

Input acceptance records the actor and exact request/input event once. When
the root is paused, the first valid answer atomically changes the root wait set
to resuming, reopens one budget segment and marks every owner path running.
Accepted calls are reserved for continuation under their original identities;
no plan is reaccepted and no child or external effect is replayed. Concurrent
answers serialize through the root pause revision and each accepted request
continues at most once. A later branch may complete while another request
remains unanswered; after all currently ready work drains, the scheduler
re-evaluates the full wait set before it pauses again.

If restart occurs while paused, recovery validates all owner paths, accepted
calls, claimed child nodes, human requests and input receipts, effect journals,
context pins, budget segment and root/owner run states before exposing the
resume operation. If any member is stale, uncertain or incomplete, retain the
root for inspection/cancellation and do not resume any sibling from that
wait set. Recovery must not partially wake only the branches that happened to
validate.

## Client behavior

The native HTTP observation and shared browser/VS Code plan view expose all
waiting requests with their owner path, plan revision/state sequence and
expiry. Each answer submits the exact request identity and observed revision.
After one answer, clients keep unanswered cards visible while the root runs,
then refresh from committed plan/run events if another pause is needed. The
CLI's plan inspection must show the same wait set; it must not infer that one
answer completed sibling requests.

## Acceptance evidence

Use actual xlang3-backed SQLite and concurrent native child executions, with a
synthetic provider peer clearly labelled as such. Required contracts cover:

1. Two sibling nested agents accept human questions concurrently; an ordinary
   sibling agent continues and commits its result before the root pauses.
2. Both questions appear under their distinct owner paths. Answer them in
   reverse order and verify each exact accepted call continues once.
3. Reopen SQLite while the root is paused with both questions pending; validate
   all owners, answer one, verify the other remains waiting, and finish without
   replaying model calls, child effects or questions.
4. Race simultaneous answers, duplicate IDs, stale revisions, expiry,
   cancellation and one uncertain sibling effect. No branch may be partially
   resumed or have its uncertainty/quarantine cleared by another answer.
5. Prove budget and context accounting across multiple pause/resume segments,
   including exhaustion before acceptance and no time refund after restart.
6. Verify the same wait set through authenticated HTTP, console CLI, browser
   and VS Code renderers; client observations never submit answers or approve
   effects on their own.

The current native engine contract proves concurrent nested owners can pause
under one root and that a partial resume keeps the unanswered branch durable.
The feature is complete only when the remaining acceptance layers above pass.
A source change that merely allows another nested human node is not sufficient.

The Windows integration checkpoint also ran the full 142-contract Native gate
against xlang3's UTF-8 import-root branch, including the actual local-profile,
local-view and package-handoff fixtures that fail on the older path conversion.
See [the recorded runtime integration evidence](evidence/native-runtime-utf8-xmind-20261014.md).
