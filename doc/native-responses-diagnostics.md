# Native Responses mismatch diagnostics

Historical implementation checkpoint `7fe7ec9bd37c34863efc9f74e6508f0aba643d61`
passed complete local and exact hosted gates, with terminal equality and
rejection unchanged. It adds evidence for a rejected Responses completion;
it does not fix live protocol compatibility or establish successful delegation.

This document records that diagnostic implementation and its earlier failed
live requests. The later
[reasoning continuation checkpoint](native-responses-reasoning.md) is revision
`ace246094f6c1fc8cf61c146cfe001c63f1bbc8f`, which passed the exact hosted
71/103/20 gates and is now installed with the retained repaired browser view.
On **2026-10-08**, a new actual browser request visibly completed its parent and
two read-only children with separate response metrics. A separate read-only
machine audit of that same completed request passed; its selected histories,
owned attempts and child settlements remained unchanged across repeated reads.
[Live owned-record audit](evidence/live-browser-responses-reasoning-delegation-ace24609.json),
[actual browser observation](evidence/live-browser-responses-reasoning-delegation-ace24609-browser.json).

The full guarded local gate passed **71/71 native contracts in 130.49 seconds**,
with the exact expected/registered/passed manifest, no failures/skips/exclusions,
and **229 source hashes** frozen before the build and unchanged afterward.
The Responses unit contract passed in **0.86 seconds** and actual native
HTTP/CLI contract in **2.59 seconds**. Fresh native/browser integration used
the rebuilt server and unchanged source-repaired browser assets.
[Source/runtime/manifest provenance](evidence/native-responses-diagnostic-local-provenance.json),
[raw CTest](evidence/native-responses-diagnostic-local-ctest.log),
[native/browser integration](evidence/native-responses-diagnostic-local-browser-native.log).

That implementation changes three native source/test files over base
`0c2624531011c9da70a8bb5c5bb2afee160ed199`; the provenance binds their exact
tested contents instead of claiming the base commit contains the patch. The
imported xlang3 SDK's two runtime binaries and 23 modules remained unchanged at
runtime revision `4aea7d8fb24da9ba86f9d7eeb92820794f213d29`, with its CPython
bridge disabled. Local standard-library source is allowed pure source, with no
Git pin claimed. Frontend sources retain the prior **103 extension / 20 browser**
results and 18-asset VSIX verification; those suites were not rerun in the local
native-only gate. The separate exact hosted gate did rerun them below.

Exact [hosted run 37773043445](https://github.com/xlang-foundation/xMind/actions/runs/37773043445),
job `113297055266`, passed all **71 native contracts in 188.04 seconds**,
**103 extension contracts in 2.9270174 seconds**, **20 browser contracts in
1.6413315 seconds**, native/browser integration and all **18 required VSIX
assets**, with zero failures/skips and the exact complete native manifest.
Its Responses unit/HTTP contracts took **0.03/2.79 seconds**. All 16 authentic
API job steps succeeded at numbers 1–11 and 19–23.
[Exact hosted provenance](evidence/native-responses-diagnostic-hosted-provenance.json),
[raw CTest](evidence/native-responses-diagnostic-hosted-ctest.log),
[raw job log](evidence/native-responses-diagnostic-hosted-ci-job.log).

Both artifact archive digests, exact source, runtime/stdlib/SQLite pins and
toolchain were verified. All **229 normalized compiled/gate/vendor hashes**
match committed local evidence; **247 exact Git-blob hashes** also cover 18
additional UI/build/package sources. Thirteen directly packaged UI source
copies and the emitted production HTML match the exact commit, while copied
renderer libraries/licenses match their packaged dependency bytes. The
**29 runtime / 12 view / 30 VSIX file maps** are verified. The original job
log's 172,607 bytes, UTF-8 BOM and emitted whitespace are preserved. Artifacts
were downloaded and safely extracted into fresh owned directories. That
collection performed no installation or live/private preview access; later
installation and live observations are recorded separately below.

## Event boundary

At the 7fe7 diagnostic checkpoint, after response identity/status/output-count
checks, the first cached
item versus terminal-output mismatch emits `model.protocol_diagnostic`, then
throws the same `responses_terminal_mismatch` protocol error. Completed-item
equality, stream sequencing, identity, lifecycle and exact argument-delta guards
remain intact. A failed stream cannot emit a second diagnostic or produce a
validated completion through a later `feed` or `finish` call.

The encoded event is bounded to **4,096 bytes** and contains only:

- The fixed code, output index **0–1023**, cached-item presence/completion flags,
  and fixed JSON-kind labels for the completed and terminal snapshots.
- Twelve ordered field records: `id`, `type`, `status`, `call_id`, `name`,
  `arguments`, `role`, `content`, `summary`, `encrypted_content`, `channel`,
  `phase`. Each reports presence, fixed JSON kinds and an `equal` boolean.
- `changed_fields`, a subset of those fixed labels, and one aggregate
  `unlisted_fields_equal` boolean. Unlisted members are compared in place.

No provider values, IDs, tool names, arguments, text, summaries, encrypted bytes,
value lengths, hashes or unlisted member names enter this event. JSON-kind labels
distinguish absent/null/object/array/string/boolean/integer/unsigned integer/number;
an unfinished or missing cached snapshot is labelled absent. Earlier tool-delta
events retain their established argument-prefix behavior; this diagnostic does
not claim to redact every event in the stream.

## Verified rejection and continuation limits

The 7fe7 unit fixtures independently construct completed-item and terminal
snapshots. Optional metadata, opaque bytes, execution fields, content/summary, unlisted
members, scalar/type changes, sparse indices and unfinished items still reject.
Multiple mismatched outputs emit only the first diagnostic; fragmentation and
repeat-feed/finish cases verify the failure latch and value-free bounds.

The actual native HTTP peer offers `delegate_tasks` and supplies explicitly
synthetic metadata/opaque/unlisted/argument mismatches. Each request persists
one structural diagnostic and one parent transport/model-budget attempt, then
fails before tool dispatch or child admission. User-only history, no effects,
no retries and no validated usage/finish/done callbacks persist across actual
xlang3 SQLite reopen. Existing valid read/tool, stateless continuation, graph
replay and cancellation cases continue to pass. Provider replies, keys,
objectives and encrypted data are fixtures; native transport, files and storage
are real.

OpenAI's streaming event reference specifies using the reasoning item from its
corresponding `response.output_item.done` for subsequent input; an added item's
encrypted content can be incomplete. [Official Responses streaming events](https://developers.openai.com/api/reference/resources/responses/streaming-events).
The 7fe7 guard required JSON-value equality against completed items;
the reasoning item's encrypted-content string must therefore match its completed
snapshot exactly at that checkpoint. The diagnostic patch left continuation
construction unchanged. The later reasoning checkpoint accepts only the
observed narrow opaque-string variation and retains completed-item snapshots;
it does not decrypt or normalize those strings.

## Historical installation and live scope

The verified **7fe7 diagnostic native bundle was installed**, retaining the
separately repaired browser view: 29 native files, 12 view files and 10 view
source bindings. At upgrade observation, saved native API records and typed
owned-attempt audit preserved **12 sessions, 16 roots, 38 history records,
3 operations, 2 graphs and 2 graph children**, with no delegated children or
active runs/claims. Configuration, journal observations, authentication,
cookie, origins and view were retained. This managed comparison used APIs and
audit events; it makes no direct live SQL-row or database-byte identity claim.

Separate disposable fixtures passed schema **v10→v10 upgrade and idle rollback**
using exact hosted xlang3 `query_only` queries on copied closed databases.
They compared all table rows, nonempty delegation ledgers and synthetic
encrypted credential purpose/ciphertext privately. Real native fixture work
included two read-only leaves, one joined batch, a failed predispatch root,
a graph read and an approved create; provider replies were synthetic.
An owner-loss fixture rejected rollback after newly accepted work and loss
of the replacement owner, preserving the accepted database. The initial
synthetic peer tool-order timeout remains recorded as a failed attempt;
the corrected fixture's pass does not overwrite it.
[Upgrade/rollback and installation provenance](evidence/native-responses-diagnostic-upgrade-provenance.json).
[Initial failed fixture log](evidence/native-responses-diagnostic-upgrade-initial-failure.log),
[corrected upgrade/rollback log](evidence/native-responses-diagnostic-upgrade-positive-attempt-2.log),
[owner-loss rollback log](evidence/native-responses-diagnostic-upgrade-owner-loss.log)
retain the authentic failed and passed attempts separately.

At that checkpoint, public live root `8dc406dca1efce9a8cb1dffc4454fa9a` on saved
OpenAI Responses `gpt-5.6-sol` failed with `responses_terminal_mismatch` after 150
delegation deltas. Its bounded diagnostic identifies **only `encrypted_content`**
as different, with both values strings and every other known/unlisted field
equal. It retained one finished model-budget attempt, user-only history,
zero accepted tool calls, children and operations; no validated usage exists.
Opaque values were withheld. [Actual live structural proof](evidence/native-responses-diagnostic-live-7fe7.json),
[rendered browser failure](evidence/native-responses-diagnostic-live-7fe7.jpg).

Historical root `20349b91c30e11b54f90902e137899da` has no structural diagnostic;
the later 7fe7 observation does not establish that earlier run's differing field.
[Earlier installed browser and live-failure record](native-delegation-acceptance.md).

## Later reasoning checkpoint

The narrow native compatibility patch in **ace24609** accepts reasoning-only
nonempty encrypted-content differences when every other member and the member
set remain equal. Continuation uses the original completed-item snapshots.
Its local guard exited **3** because a sibling benchmark was active; the
separate exact hosted gate subsequently passed **71 native, 103 extension and
20 browser contracts**. The managed preview was upgraded to the verified ace
native bundle while retaining the repaired e353 browser assets and schema v10.
Managed preservation used native APIs and typed audit observations, without a
direct live SQL-row or database-byte identity claim.
[Managed upgrade and disposable fixture scope](evidence/native-responses-reasoning-upgrade-provenance.json).

The new browser request observed on **2026-10-08** displayed a completed parent
and two completed read-only leaves. The separate audit of that same saved run
passed with **six finished owned model attempts, two read-only children and
zero operations**, including independent histories and supplied per-response
metrics, actual child settlements and parent continuation after the join.
Leaf responses may omit reasoning items; any present reasoning remains
strictly validated, and the parent's initiating delegation receipt carried
nonempty reasoning. The audit did not inspect live terminal opaque values or
repeat a model request. These results leave the earlier failed roots and their
documented causes unchanged.
[Live owned-record audit](evidence/live-browser-responses-reasoning-delegation-ace24609.json),
[actual browser completion](evidence/live-browser-responses-reasoning-delegation-ace24609-browser.json),
[Current reasoning implementation, hosted gate and acceptance scope](native-responses-reasoning.md).

Installed VS Code delegation, broader planning, skills, compaction, outbound
A2A and full coding/provider parity remain required.
