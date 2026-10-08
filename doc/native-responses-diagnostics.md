# Native Responses mismatch diagnostics

Status: implementation checkpoint `7fe7ec9bd37c34863efc9f74e6508f0aba643d61`
passed complete local and exact hosted gates, with terminal equality and
rejection unchanged. It adds evidence for a rejected Responses completion;
it does not fix live protocol compatibility or establish successful delegation.

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
were downloaded and safely extracted into fresh owned directories, with no
installation or live/private preview access.

## Event boundary

After existing response identity/status/output-count checks, the first cached
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

Unit fixtures independently construct completed-item and terminal snapshots.
Optional metadata, opaque bytes, execution fields, content/summary, unlisted
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
The unchanged guard requires JSON-value equality against completed items;
the reasoning item's encrypted-content string must therefore match its completed
snapshot exactly. This diagnostic leaves continuation construction unchanged.
No normalization is implemented or established by it; a future compatibility
change must respect the documented completed-item continuation source.

## Installed and live scope

The installed backend remains the verified e353 bundle with its separately
repaired browser view. Historical live root
`20349b91c30e11b54f90902e137899da` failed before children and has no new structural
diagnostic. Its differing field remains unconfirmed; the existing failure,
rendered screenshot and preserved-history evidence remain scoped to that run.
[Installed browser and live-failure record](native-delegation-acceptance.md).

The exact hosted/artifact gate is passed. Reviewed schema v10→v10
upgrade/rollback acceptance, installation and a new explicit public live probe
remain pending. Successful live children/join, installed
VS Code delegation, broader planning, skills, compaction, outbound A2A and full
coding/provider parity remain required. The local passing gate is not evidence
that the historical live failure was repaired or replayed.
