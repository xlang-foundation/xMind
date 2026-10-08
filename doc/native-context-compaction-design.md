# Native context compaction design

**Complete local contract gate passed; release and installed/live compaction
acceptance remain pending.** The
design baseline is grounded in the native source at checkpoint
`2cd392f83f642ae5278e7d77fa55822258a8a126` and the complete local 78-contract MCP
checkpoint. Its exact source
capture, commit identity and gate evidence remain separate from this proposal.
The installed preview is still the separately accepted ace/schema-v10 backend.
The current schema-v12 source has not been installed; preview upgrade remains
blocked by the separately recorded
Windows file-open compatibility finding; compaction does not work around it or
require a CPython fallback.

The first complete local build configured all 86 expected native contracts and
passed compilation. Its full test run passed 81 and failed five: the repository
recovery fixture, context HTTP controller and three graph HTTP observations.
The second attempt configured and compiled all **88 contracts**, preserving all
78 original contracts, then passed **85 and failed three in 162.98 seconds**.
The remaining failures were the context repository, context HTTP controller and
provider CLI route fixture. After their repairs, the third full gate passed
**88/88 in 161.48 seconds**, with zero failures/skips, 560 frozen inputs unchanged
and original test output captured. Both failed attempts remain separate and no
failing contract was excluded. A later isolated Responses legacy-history
reproduction found a serializer defect. Its role-aware repair subsequently
passed the fourth complete gate: **88/88 in 171.89 seconds**, with all 560 inputs
unchanged and no failures/skips.
[Exact local source/log evidence](evidence/native-context-provider-local-provenance.json)
also retains the scope of the earlier attempts. This local result does not complete installed/live
compaction or all provider strategies.

The two synthetic DeepSeek native contracts passed individually in **0.54 and
0.71 seconds** during the second attempt. The shared-view suites retain their
**131 extension / 29 browser** source-test scope after the DeepSeek renderer
change. The final saved-provider footer subsequently passed **141 extension /
32 browser** tests, with all 37 frozen inputs unchanged.
[Frontend source evidence](evidence/native-provider-footer-ui-provenance.json)
binds that separate gate. Model-free native/browser integration subsequently
passed in **3.30 seconds** and the VSIX passed **18 asset checks**, with all 12
tested view files matching.
[Integration/package evidence](evidence/native-provider-footer-integration-provenance.json)
preserves the first guard-host failure. These results do not establish installed IDE or live compaction
acceptance. The installed preview still has only one configured OpenAI Responses
profile and has not imported the four-key configuration. Its earlier HTTP 400
`invalid_value` remains recorded; a direct reproduction later identified legacy
assistant `input_text` where `output_text` is required. A fresh native OpenAI
browser prompt returned `OK` separately, without exercising compaction or that
legacy-history path.
[Provider and preview scopes](provider-setup.md).

The product target is bounded context throughout real coding conversations:
across user prompts, inside model/tool loops, in independent child agents and
across a clean human pause. It includes automatic compaction and authenticated
manual controls. A first-root-only summary, byte truncation, a caller-supplied
summary or a manually advanced checkpoint does not satisfy this target.

The behavior baseline remains OpenCode `v2.0.16`, exact commit
`3a103fe0aff726a4edc7492f03f7b88195d9e4c9`. Its
[compaction requirements](https://github.com/anomalyco/opencode/blob/3a103fe0aff726a4edc7492f03f7b88195d9e4c9/services/www/src/docs/content/compaction.mdx)
and [session contracts](https://github.com/anomalyco/opencode/blob/3a103fe0aff726a4edc7492f03f7b88195d9e4c9/specs/v2/session.md)
cover automatic/manual admission, rolling context, logical steps versus physical
attempts and bounded overflow handling. They are behavior references; all xMind
implementation belongs in native C++ and embedded-xlang3 persistence.

## Source boundaries at the 2cd checkpoint

| Source | Existing behavior and integration consequence |
| --- | --- |
| `Native/src/agent_runner.cpp`, `message()` and `reload_history()` | Ordinary roots reload all ordinary session history; children load their own history. A provider projection must replace this input selection without replacing the history API. |
| `Native/src/repository.cpp`, `run_history()` | Root history spans the session. Child records are isolated by `execution_run_id`. A checkpoint cannot be keyed only to the current root. |
| `Repository::record_tool_turn()` | An assistant and all its tool results commit atomically. Compaction boundaries must keep that complete group together. |
| `Repository::commit_dynamic_tool_turn()` | A signed planning response and its exact result commit once from the accepted-call ledger. A summary must never replace this ledger or grant a planning revision. |
| `RepositoryInstructionContext` and the runner's instruction refresh | Current configured and scoped repository instructions are rebuilt before inference. They are trusted input, outside the compressible historical transcript. |
| `Native/src/model_request.cpp` | Chat validation rejects dangling or repeated call identities, foreign receipts and oversized requests. It has count/byte limits, not a tokenizer or a model context-window registry. |
| `Native/src/responses_request.cpp` and `responses_stream.cpp` | Ordinary Responses receipts contain validated message/function/reasoning items; completed reasoning items are the replay authority. A canonical compaction window requires a separate representation. |
| `anthropic_history.cpp`, `anthropic_request.cpp` | Ordered blocks, thinking signatures and redacted blocks remain in exact `content_json` strings. Visible text and raw ordered calls must agree with the receipt. |
| `gemini_history.cpp`, `gemini_request.cpp` | Exact parts, thought signatures, metadata and native/provider call bindings are replayed from `parts_json`. Function responses are correlated with the original calls. |
| `RootExecutionBudget` and schema-v11 planning tables | Actual attempts, held parent continuations and measured active-time segments are durable. A compaction call cannot take a held continuation or obtain a new deadline. |
| `provider_catalogue.hpp` and `ChatProviderConfig` | Discovery returns model IDs. Compaction support, input/context capacities and token-counting support are currently absent. Listing a model does not prove any of them. |
| `http_stream_transport.hpp` | Native transport has bounded JSON GET and streaming POST. Standalone provider compaction and token counting need bounded JSON POST with the same destination/authentication/deadline policy. |

The accepted complete gate proves these existing contracts, not the new context
implementation.
No missing xlang3 API has been established for compaction: C++ can own selection,
transport, validation and orchestration while database I/O stays embedded xlang3.

Checkpoint `859aca7743e561e46fd574c82dff65df333a7bf4` adds
`post_json(request, credential, max_response_bytes, cancel)` as the stage-0
transport foundation. The complete local 78-contract
gate passed in 193.91 seconds with 427 frozen inputs verified. An earlier fresh
guard deferred before any native action while an SDK benchmark was live; the
later attempt began with clear guards. This transport checkpoint establishes the
single bounded POST primitive; the later token-counting/compaction adapters have
their own source and gate scope. Its
[local evidence](evidence/native-context-transport-local-provenance.json) remains
separate from the context implementation described below.

## Current implementation status

The working source contains native Responses count/compact adapters, an exact
private canonical-window representation, metadata-based complete-group
selection, typed context records and schema-v12 repository
transactions through embedded xlang3. The native context manager is connected
in source to the real agent pre-inference path, including shared physical-call
budgets, original signed planning continuations, durable compaction receipts,
once-only pre-output overflow recovery and clean dynamic-pause context pins.
These changes compiled and were exercised in the two failed attempts and the
third complete 88/88 gate above. They have not been installed; the later
serializer repair still needs a fourth full gate. Complete local contract
acceptance is distinct from installed/live acceptance and broader strategy
completion.

Source guards cover receipt compatibility and concurrent budget admission within
the third local gate's scope. Opaque receipts require proved producing ownership
and an exact compatible private binding before any count, compaction or inference
request. Refreshing a raced budget revision may retry only an undispatched
transaction with identical scope, source, authority, payload and member IDs;
accepted provider requests are never replayed.

Backend policy registration, authenticated manual commands, preclaimed idle-owner
preparation and shared registered-graph execution now have native source paths.
Manual requests admitted during a root retain their exact bound scheduler
ticket: the actual root can consume the request at a safe boundary, or the
service can claim real idle maintenance after that root retires. A racing root
using another model does not absorb or silently discard the request. Views
observe public status and supplied metrics; private provider windows, receipt
proofs and authority identities are excluded from those DTOs.

Client controls and independent controller tests have source implementations;
the Node suites passed at the separate adapter/DOM scope above. The repaired
native controller and repository paths passed the third complete gate; the
later serializer change must preserve that result in a fresh full gate.
Native acceptance must exercise
rolling context across roots, mid-run/repeated compaction, original held
continuations, concurrent leaves, cancellation, recovery and real populated-v11
migration before acceptance. Live-provider tests and installed browser/VS Code
acceptance remain separate gates. The installed preview does not expose this
source work as a working product capability.

## Requirements that apply to every strategy

1. Keep the full original user-visible transcript, exact private provider
   receipts, operation journals, accepted plans and child histories. A model
   context checkpoint is a separate, append-only projection of that history.
2. Keep current backend instructions, current repository guidance and offered
   tool definitions outside model-authored summaries. Their actual serialized
   bytes still count toward the request limit. Refreshing guidance may itself
   make a previously fitting context too large.
3. Compact only a validated, settled prefix. Retain complete assistant/tool
   groups in their original order, raw argument strings, refusals and opaque
   state wherever the registered provider strategy requires them. Never splice
   half a group, fabricate a thinking signature or convert opaque content into
   visible text.
4. Protect the latest user request, the chosen recent complete groups, any
   still-required provider state and the exact signed planning turn awaiting its
   held continuation. No compaction runs while an owner has an unanswered tool,
   active child, effect in flight, uncertain effect or uncommitted accepted
   planning result. A protected tail that cannot fit fails explicitly.
5. Bind each checkpoint to its conversation scope, exact provider route/profile
   revision, wire and selected model, workspace identity, compaction policy,
   instruction policy and backend-private authority identity. No cross-provider
   fallback, credential exposure or conversion of foreign signed history into
   apparently native receipts is allowed.
6. Produce summaries or canonical windows through an actual provider request.
   Record its actual outcome, supplied usage and measured time separately from
   the subsequent inference. Token-count measurements are not response usage;
   estimates remain labelled estimates. Missing counts stay missing.
7. Perform admission, reservations, result publication and head replacement in
   typed transactions. An acknowledged response that cannot be journalled
   fails the generation; it must not be retried or represented as a fabricated
   successful checkpoint.
8. Continue checking every final request. Repeated compaction must remain
   bounded, and provider context overflow needs an explicit bounded correction
   policy. Failed requests remain counted. No blind transport retry, unlimited
   rebuild loop or silent pruning is permitted.

## Conversation scopes and safe boundaries

An ordinary session has a context head for each immutable compatible provider
binding. Successive roots reuse that head plus the original messages committed
after its covered watermark. One active root per session remains enforced.
Changing profiles does not reinterpret a head or hide incompatible original
history; the backend either finds a matching compatible head or rejects with a
specific compatibility result.

Each delegated, dynamic or registered-graph agent child has its own run context
scope. A child never consumes its parent's context head, provider receipts or
summary. Its objective and backend-derived dependency observations stay
protected. Parent joins contain only their actual recorded observations.
Registered graph roots themselves do not become model-context owners.

Automatic compaction is evaluated before the first inference and before every
subsequent inference, after instruction refresh and current tool selection.
Within a running root, it follows a complete ordinary tool-turn transaction or
a typed signed-plan result commit. It may compact older groups from that same
root as well as earlier roots. It does not require ending and restarting the
Agent to reclaim context.

A clean human pause pins the context head and covered watermark in its durable
owner record. Closed wait performs no counting, compaction or inference.
Authenticated resume validates the original authority, reopens measured active
time, executes the owned pending plan and commits its signed result once. Only
then may a new safe-boundary compaction run. The planning origin and result
remain the protected tail for that continuation. The original held allowance
and call identity remain authoritative.

Session heads should be selected from indexed group metadata before loading
payloads. Reading the entire unbounded history into memory and then trimming it
would leave the original scalability failure in place. New groups are indexed
when the actual conversation transaction commits. Existing histories require
a bounded native validation/backfill pass; unknown or malformed legacy groups
are not silently guessed into a compactable prefix.

## Provider strategies

`ContextCompactionCapabilities` is a backend-registered capability snapshot for
the exact route/wire/model. It includes a strategy revision, counting semantics,
bounded request/response limits and compatibility rules. `unknown` or
`unsupported` never enables automatic compaction. Model arguments and clients
cannot choose the endpoint, strategy, capacity or compatibility exceptions.

| Wire | Required strategy boundary |
| --- | --- |
| Chat Completions | A genuine no-tools model-generated historical summary can be used with a complete protected tail. The summary is clearly labelled historical data in a backend-built message, not a fabricated earlier assistant or a system instruction. Foreign provider receipts still reject. |
| Responses | Prefer an explicitly registered standalone canonical-window strategy. Store the full returned window, including opaque compaction state and retained items. Replay it through a separate typed request representation; never insert it into an ordinary assistant receipt or prune its output. |
| Claude Messages | Use a registered model-compatible native/provider compaction strategy. Preserved-thinking models may reject plain client summaries combined with retained thinking. A generic summary plus a signed tail is not assumed valid. No stripping or fabrication of thinking is an alternative. |
| Gemini GenerateContent | Preserve exact parts, thought signatures and provider/native call bindings. Register and verify a provider-compatible summary/checkpoint construction before enabling it. A text summary is not claimed to preserve hidden state merely because the outgoing JSON is syntactically valid. |

The distinction between durable preservation and provider continuation is
explicit. Original opaque receipts are always retained privately. A shorter
future request must carry state in the form the provider actually supports;
an archive alone does not prove unchanged continuation semantics.

OpenAI documents standalone `/responses/compact` as returning a canonical
window that must be passed onward without pruning. The input must still fit the
model's context. Server-side compaction has a different output/pruning contract;
it is a later separately registered strategy, not an interchangeable shortcut.
[Official OpenAI compaction guide](https://developers.openai.com/api/docs/guides/compaction).
Normal stateless Responses continuation also preserves returned reasoning
items; the existing completed-item source requirement remains in force.
[Official Responses migration guidance](https://developers.openai.com/api/docs/guides/migrate-to-responses?api-mode=responses).

Claude's on-demand, threshold and client-side approaches need separate
compatibility handling, particularly for preserved thinking.
[Official Claude compaction guide](https://platform.claude.com/docs/en/build-with-claude/compaction).
The generic Gemini representation must continue respecting the existing
signature and correlation contracts; no new provider capability is inferred
from the current native model catalogue.

## Capacity and measurement

Introduce explicit backend policy for the soft compaction threshold, hard
serialized-byte limit, protected-tail limit, maximum maintenance calls and
provider input/context capacity where verified. Count the final wire payload,
including role formatting, tools and refreshed instructions. Do not use
`characters / 4`, response usage from an earlier request, or a missing catalogue
field as an exact input count.

A native counting adapter records `count`, its semantics (`provider_exact`,
`provider_estimate` or `serialized_bytes`), exact payload binding, model and
measurement time. It cannot publish a capacity percentage without a compatible
verified capacity and a matching measurement. It must be repeated after a
projection or instruction/tool change invalidates that payload binding.

Keep verified input, output and total context ceilings as distinct model-policy
fields, with their source and revision. Bind the actual requested output
allowance to each measured request; if omitted, use only a verified
provider/model default. An unknown output default is not a guessed reserve.
For a compatible token measurement, the automatic trigger is the lower of the
applicable input ceiling minus buffer and total context ceiling minus the
larger of that output allowance and buffer. Output allowance must itself fit
the verified output ceiling. A separately documented input-only ceiling may
provide an input-only trigger, but it cannot prove total context fit. Missing
applicable capacities stay unknown; wire-byte bounds remain independent.

OpenAI's Responses input-token endpoint accepts the same input shape and tool
definitions and provides request input counts. Its implementation can reuse the
new bounded native JSON POST transport, without an SDK.
[Official OpenAI token-counting guide](https://developers.openai.com/api/docs/guides/token-counting).
Claude token counting is a provider estimate, not actual billed response usage;
unsupported server-tool/MCP-connector shapes require a typed failure. Native
local MCP function definitions are a distinct request shape.
[Official Claude token-counting guide](https://platform.claude.com/docs/en/build-with-claude/token-counting).
Gemini's count-tokens adapter likewise needs a native, bound request and
validated response.
[Official Gemini token API](https://ai.google.dev/api/tokens).

Serialized-byte guards remain mandatory even when tokens are measured. If a
single protected request or required canonical window exceeds either applicable
bound, retain the last committed head and fail with `context_capacity_exceeded`.
A visible byte counter is useful evidence, not proof of a token-window fit.

Context overflow has a separate, once-only rebuild rule for the current logical
inference step. Only an explicitly mapped provider/wire context-limit error may
trigger it, and only while automatic compaction is enabled and that physical
attempt has produced no durable assistant/text/refusal/tool delta or dispatch.
Record the failed attempt, perform separately budgeted maintenance and rebuild
the same pending step once; do not re-admit its user prompt or replay completed
tools. A second overflow, a late overflow after such evidence, or an unrelated
authentication/rate-limit/timeout/cancellation/HTTP-400 failure is terminal for
this rule. With automatic compaction disabled, an error cannot initiate
compaction; an independently admitted manual request remains allowed. Exact
classification and the attempt-scoped output/dispatch watermark require their
own native contracts before enabling a provider's overflow path.

## Proposed native interfaces

These names are proposed interfaces, not currently callable APIs.

```cpp
struct ContextScope { ContextScopeKind kind; std::string id; };
struct ContextBinding;       // private route/model/workspace/authority policy
struct ContextPolicy;        // backend bounds and registered strategy
struct ContextSnapshot;      // head, watermark, exact complete group metadata
struct ContextSelection;     // eligible prefix and protected original tail
struct ContextProjection;    // visible-summary or private canonical-window
struct ContextCompactionResult; // actual provider result, usage and timing
enum class ContextAdmissionKind {
    active_inference, planning_continuation, idle_maintenance
};
struct ContextStepReservation;  // kind-specific variant; idle has no inference

ContextSnapshot Repository::context_snapshot(
    const ContextScope&, const ContextBinding&, ContextReadBound);
ContextStepReservation Repository::begin_context_compaction(
    const ContextCompactionSpec&); // CAS + atomic call capacity
ContextProjection Repository::commit_context_compaction(
    const ContextCompactionCommit&); // actual response + source/head checks
void Repository::retire_context_compaction(
    const ContextCompactionFailure&); // fixed reason and actual attempt

ContextSelection select_context(const ContextSnapshot&, const ContextPolicy&);
PreparedModelContext ContextManager::prepare(
    const ContextOwner&, const ModelRequest& trusted_current,
    std::shared_ptr<RootExecutionBudget>, std::stop_token);
ProviderInputMeasure measure_model_context(
    const ProviderContextConfig&, const PreparedModelContext&, ...);
ContextCompactionResult compact_model_context(
    const ProviderContextConfig&, const ContextCompactionRequest&, ...);
```

The stage-0 transport signature uses an explicit `std::size_t` response byte
bound in `post_json`, as declared in `http_stream_transport.hpp`. Its adapter
selects that bound and validates the returned protocol JSON; the transport
preserves raw bytes without interpreting a provider checkpoint.

`PreparedModelContext` contains a variant for original model messages and
provider-specific canonical windows. A Responses canonical-window validator
checks its own bounded item schema, ordering, identities, correlation and
authority placement. It does not relax `message()` or the ordinary assistant
receipt validator. Unexpected tool/effect requests in a maintenance response
are rejected and never executed. The selected provider and credential remain
backend owned; credentials are resolved privately only for the physical request.

The active runner calls `ContextManager::prepare()` at its current pre-inference
boundary and uses its returned exact next inference reservation. An idle owner
settles only its maintenance result. The manager
does not append model-generated context to ordinary history, mutate tool
results, perform effects or re-admit children. Its dedicated events and public
observations distinguish maintenance from the user's answer.

## Proposed schema-v12 ledger

| Relation | Required fields and checks |
| --- | --- |
| `conversation_groups` | Scope, original sequence interval, execution owner, group kind, committed event, exact payload byte count and private manifest binding. Immutable; an assistant/tool group has exactly its actual results. |
| `context_heads` | Scope and private compatible binding, head revision and checkpoint ID. Typed compare-and-swap only; never rewritten by a client. |
| `context_compactions` | ID, owning root/run, scope, admission kind, policy/binding revisions, previous head, source watermark and prefix boundary, protected-tail binding, maintenance attempt, kind-specific next inference/plan-call binding, state and audit event IDs. Idle maintenance has no inference/plan binding. Unique per accepted maintenance request. |
| `context_checkpoints` | Immutable compaction ID, source manifest, covered boundary, strategy, private exact provider result/window, optional actual visible summary, supplied usage, measured timing and committed event. No credential fields. |
| `context_requests` | Authenticated manual request ID, scope, expected head, requested mode, backend actor, pending/claimed/settled state and actual owner. Exact duplicate input is idempotent; changed reuse rejects. |
| `context_measures` | Exact payload binding, model, count semantics/value, byte count and actual measurement outcome. Measurements are not model responses or billed usage. |
| `inference_steps` and physical-attempt membership | Immutable logical step, execution owner, original inference reservation, optional original signed planning call/held reservation, per-member approved input projection binding, physical members, overflow/rebuild count and designated successful member. The original held pointer is never replaced; a rebuild member consumes extra physical capacity without acquiring another logical allowance. |

Add an immutable `purpose` to actual model-call reservations, such as
`inference` or `context_compaction`. Existing rows migrate as inference without
inventing measurements. Preserve parent/leaf ownership. Track logical inference
allowances separately from bounded maintenance attempts; a maintenance purpose
is not a new unbounded role. Dedicated result storage binds
the actual maintenance response to its started attempt, without treating it as
an assistant turn eligible to originate tools or planning changes.

All relations use embedded xlang3 SQLite. Admission checks running owners,
private binding, safe group boundary, exact source/head/budget revisions and
absence of conflicting work. Head publication, checkpoint insertion, actual
attempt retirement and their events commit atomically. Foreign event IDs,
missing groups, changed source manifests, duplicate results and arbitrary
caller-provided checkpoints reject with no partial head change.

Existing transcript, operation and planning tables are not replaced. Migration
and rollback must preserve their rows and encrypted credential records, and
must not pretend old conversations already had compacted contexts.

## Atomic budget and planning continuation rules

Compaction consumes a real physical model attempt under the same root's total
calls and the same active deadline, plus a separately bounded maintenance
allowance. It does not manufacture a new user prompt or consume an extra logical
inference step. The parent's sixteen and child's four logical inference limits
remain in force; their inference counters count distinct logical
`inference_steps`, not all physical members with an inference purpose.
The global physical-call counter includes summaries and corrections, so a
compaction can still exhaust the existing thirty-two-call root allowance. JSON
counting requests have bounded
request-count/deadline accounting and their own observations; they do not
fabricate model-token usage or consume an inference result slot.

For an ordinary next turn, `begin_context_compaction()` reserves maintenance
and the exact follow-up inference allowance atomically. No provider call begins
when the remaining allowance cannot cover both. Other leaves cannot take the
reserved follow-up. For a planning continuation, the transaction reserves only
the additional maintenance capacity while retaining or materializing the
already-held exact continuation for that same accepted call. It must not use
the generic parent-reservation path to bypass an outstanding signed call, nor
mint a replacement continuation attempt after the original starts.

Admission has three typed variants. Active inference requires its exact funded
follow-up; planning continuation retains its already-held signed call; idle
manual maintenance reserves only its own bounded maintenance capacity under
the exclusive idle owner. Idle admission cannot create a follow-up inference,
user prompt, planning authority or child allowance. If new user work is pending,
it waits for that idle owner to settle and receives its own admission afterward.
Each variant has enforced ledger checks for required and forbidden bindings.

The once-only overflow rebuild uses a typed physical-attempt group even for a
held planning continuation. The original continuation reservation and signed
call stay immutable. A transaction requires its first attempt's recorded,
recognized context-overflow outcome and zero attempt-scoped durable output or
dispatch; it then reserves separately charged maintenance and one new physical
member under that same logical step. It does not replace or restart the original
attempt, use generic parent admission, grant another logical turn or re-commit
the planning result. Completion identifies the group's one successful member
and its actual response. Stale identities, a second rebuild, absent capacity,
projection drift from that member's approved checkpoint, changed authority or
any output evidence reject. A legitimate rebuilt member has its own binding to
the newly committed compaction projection; the original member retains its
original binding. Recovery never
replays a started group member. This typed transition is required before
enabling overflow recovery for held continuations.

The transaction checks root-total physical calls, per-owner maintenance limits
and the reserved follow-up's durable parent/child inference limits together.
Existing public attempt counters retain their physical meaning; new logical
inference and maintenance counters expose the separate limits. Once reserved,
attempts are retired once even on failed transport or
cancellation; no refund or hidden second request occurs. A cancelled reserved
follow-up is retired without dispatch. Reusing a request ID cannot allocate new
capacity. A bounded correction, if configured, is a separately reserved and
recorded attempt. Active/planning admission must leave capacity for its exact
follow-up; idle correction remains within its maintenance-only allowance.

Maintenance duration includes counting, transport, validation, journalling and
any required peer retirement. It uses the existing steady owner clock. A
closed segment cannot reserve or dispatch maintenance. Clean pause requires no
reserved/started maintenance or inference attempt. A paused owner cannot be
made to expire later by creating a fresh clock during compaction.

Recovery marks unfinished maintenance/attempts interrupted, keeps the previous
committed head, retains any actual recorded provider result and never retries a
request of unknown outcome. A committed checkpoint remains usable only under
its compatible binding. A clean paused root adopts its pinned checkpoint and
signed call without automatic compaction or provider replay. Journal failure
after a response is fail-stop, not a generic tool error.

## Manual controls and observations

Backend policy supports automatic enable/disable and authenticated manual
request at a safe boundary. Proposed commands are `context_status(owner)`,
`request_context_compaction(owner, request_id, expected_head_revision)` and
`set_context_policy(expected_revision, ...)`. Clients cannot submit summary
text, source boundaries, provider items, capacity grants or checkpoint states.

For an active root, manual compaction is a durable pending request fulfilled by
that same execution owner at its next safe boundary. During a human pause it
stays pending without opening the segment; it executes only after authenticated
resume and the actual plan-result commit. An idle session needs a typed real
maintenance execution owner, session exclusivity and its own admitted bounded
call budget. It does not add a fictional user prompt or complete itself through
manual state updates. New user work cannot race the accepted idle owner.

CLI, browser and the right VS Code sidebar observe the same context head,
covered history range, request state and supplied maintenance metrics. A visible
summary is labelled model-generated historical data and remains separate from
the original messages. An opaque provider checkpoint shows only a fixed public
description and actual counters. No provider signatures, encrypted content,
private manifest hashes or credentials appear in public DTOs. Automatic and
manual actions use backend identities/CAS; reconnect does not replay a mutation.

## Staged delivery without reducing the target

0. **Native transport foundation.** Add bounded JSON POST and extend its actual
   independent HTTP transport contract. This primitive is required for counting
   and standalone compaction endpoints, but is not a compaction, counting,
   checkpoint or user-control implementation by itself.
1. **Shared production foundation and Responses vertical slice.** Implement
   bounded group selection, schema-v12 heads/attempts, typed provider windows,
   JSON POST, exact actual usage and reservations. Exercise new-root and mid-run
   automatic compaction, manual active/idle requests, repeated compaction and
   the exact signed planning continuation. Use the currently selected configured
   model, with explicitly registered support; do not substitute another model.
   Unknown route/model capabilities remain visibly unavailable.
2. **All current provider wires.** Add the registered Chat summary, Claude and
   Gemini strategy/counting adapters and their compatibility contracts. Preserve
   original receipts and supported hidden-state continuity. Unsupported opaque
   combinations remain explicit until a correct native strategy is verified;
   silently stripping signatures is not partial implementation.
3. **All execution scopes and recovery acceptance.** Complete delegated,
   dynamic and registered-graph child scopes, concurrent budgets, clean human
   pause/reopen, manual idempotency, bounded overflow correction and thin-view
   observation. Where included in stage 1, retain those tests rather than
   redefining the scope as only the first prompt of a new root.
4. **Provider and rendered acceptance.** Run the complete native and client
   gates, disposable migration/rollback and separate installed/live coding
   acceptance. Synthetic socket fixtures prove native contracts, not provider
   feature availability, summary quality or an installed editor.

The first concrete fixture should cross several safe boundaries in one real
Agent session: actual file reads create large history, a provider-generated
checkpoint is committed, a later request uses that checkpoint plus exact recent
tool groups, an approved edit still checks current guidance, and a dependent
read observes the actual bytes. A subsequent user root and a repeated manual
compaction prove session reuse rather than first-root-only behavior. A separate
human-plan fixture proves original signed call/result/held continuation remain
once-only across compaction and reopen.

## Acceptance matrix

| Boundary | Required independently observable result |
| --- | --- |
| Root/session/mid-run | More than one user root and multiple tool loops use committed checkpoints; full original histories remain byte-preserved. |
| Complete groups | Multi-tool assistant groups retain all results, IDs and exact raw decimal/escaped arguments; partial, duplicate or dangling groups reject before provider dispatch. |
| Actual maintenance | Independent protocol peers supply labelled synthetic canonical/summary outputs, usage with zeros/missing fields and nontrivial receipts; later requests contain the exact accepted result. Separate live acceptance proves actual provider generation and continuation. |
| Responses | Canonical output is replayed unpruned; compaction and completed reasoning opaque strings remain exact/private. Ordinary receipt guards still reject malformed unrelated items. |
| Claude/Gemini | Verified supported strategy respects signatures/parts/bindings. Preserved-thinking incompatibility rejects; no client-summary stripping workaround passes. |
| Instructions/effects | Refreshed root/scoped guidance survives; an exact approval occurs under the actual child/root, and a changed guidance snapshot still prevents dispatch. Summary text cannot authorize an effect. |
| Planning | Compaction after signed result commit keeps the accepted call and original held continuation. Final-human-only wake makes exactly the required continuation; no provider runs in closed human wait. |
| Held-continuation overflow | A recognized preoutput overflow admits at most one extra charged physical member in the same logical group; the original held pointer and signed result remain unchanged. Late/second overflow or insufficient capacity cannot remint or replay work. |
| Shared allowance race | Compaction plus follow-up admission is atomic at the last capacity while two leaves compete; no extra physical request and no stolen/reminted continuation. |
| Manual controls | Exact duplicate requests are idempotent; changed ID reuse and stale head reject. Automatic off remains off; active/idle/paused requests have truthful native ownership and state. |
| Output/capacity limits | Large opaque output, escaped JSON expansion, oversized protected tail and repeated non-shrinking results fail explicitly; no truncation, fake tokens or unlimited correction. |
| Fault/recovery | Inject response/event/head failures at each transaction boundary, cancel during actual maintenance, crash/reopen before/after publication and reject unknown-outcome replay. Previous head and original journals remain intact. |
| Profile/isolation | Route/model/workspace/instruction/credential-version drift rejects; foreign signed history cannot be laundered by compaction. Parallel children retain separate heads/history. |
| Measures/metrics | Count semantics and exact measured payload binding are shown; supplied per-response usage/timing remains separate from count estimates and from the following answer. |
| Thin clients | Actual head/request observations survive cursor reconnect; stale actions clear. Opaque/secret material never reaches renderer payloads; original conversation metrics remain available. |

## Decisions still required before file ownership freezes

- Register verified compaction/counting capabilities and input capacities for
  the exact selected provider models. Current discovery does not provide them.
- Choose the bounded default thresholds and maximum maintenance/correction
  attempts, with enough real allowance for the next inference. These are policy
  decisions, not inferred model capacities.
- Define each provider's supported hidden-state continuation strategy. Durable
  archival preservation alone is not a substitute for that compatibility proof.
- Extend existing budget admission for ordinary non-planning/text-only and idle
  maintenance owners, and for registered graph agents, without granting child
  or planning authorities they did not have. Current root-budget policies are
  limited to native delegation/planning.
- Freeze schema-v12 records and the typed canonical-window representation
  before splitting repository, transport/provider, runner and view ownership.

These are native product contracts to implement and verify. They do not require
an xlang3 SDK change or justify claiming compaction complete before its actual
provider, transaction, lifecycle and user-interface acceptance.
