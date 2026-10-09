# Native patch implementation

## Packaged and installed checkpoint

The **0.1.4 VSIX** contains the locally tested native revision `622c9d1` and
the pinned xlang3 runtime. Independent verification checked its exact 1,938 ZIP
entries, all 1,901 runtime files and 34 client/asset byte bindings, with no
private state, CPython executable, native CPython extension or bytecode.
The versioned client gate passed **222/41** tests; only README documentation
changed afterward. The native compiler was not rerun for version/documentation
changes. [Package evidence and limits](evidence/native-patch-package.json).

Real OpenAI `gpt-6.1-sol` through the packaged interactive CLI completed one
four-file add/update/move/delete patch with actual per-file approvals. A second
patch preserved the first creation when the second was denied. Independent disk
hashes matched the native receipts, and actual restart preserved exact history
without replay. Two earlier invalid acceptance-driver attempts remain recorded;
they are not passing product results. [Live CLI evidence](evidence/native-patch-cli-live.json).

The existing TestProj VS Code profile now runs this package and native runtime.
Its upgrade preserved all 22 messages and seven runs exactly, without another
provider-key prompt. A fresh real-model two-file addition showed the patch
manifest, missing-folder disclosure and per-file approval. Comparison opened
before approval while the folder remained absent. Approving file one left file
two absent until its separate approval; both final receipts match actual bytes.
The completed view displayed actual provider usage.
[Installed receipts, migration and limits](evidence/vscode-patch-installed.json),
[actual comparison/sidebar frame](evidence/vscode-patch-installed.jpg).

Installed acceptance covers additions; mixed effects and partial denial were
verified separately through the CLI. The older independent browser preview is
unchanged. The composer keeps its expanded height after a long sent request,
and CLI input remains one request per line. These UI/CLI gaps, fuzzy patch
matching, broader provider/editor coverage and full coding parity remain open.

The local candidate now compiles with the pinned xlang3 SDK. Its first local
build exposed an incorrect fixture enum name (`PermissionDecision`); replacing
it with the repository's actual `OperationDecision` resolved compilation without
changing the approval assertions. The first focused
run passed three of seven checks and exposed the exact move failure at rename:
Win32 error 87. The Win32 wrapper was replaced with native
`NtSetInformationFile` / `FileRenameInformation`, keeping the already verified
destination directory handle, literal leaf and no-overwrite requirement. This
uses the same native file boundary as handle-relative creation; it adds no
xlang3 runtime changes or absolute-path fallback.
[Microsoft native rename structure](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information).

The corrected focused build passed all seven selected contracts in 20.12 seconds:
workspace tools, per-file patch execution, batch execution, patch model adapter,
agent/CLI HTTP patch lifecycle, MCP effects and graph runner. These use actual
files, approvals and embedded xlang3 SQLite with synthetic provider/controller
fixtures. Complete 103-contract and installed/live patch acceptance are separate.

The subsequent complete local build and exact registered manifest passed all
**103 contracts in 211.24 seconds**, with 2,390 mapped source inputs unchanged.
The initial complete run passed 102/103; the synthetic skill-catalogue routing
fixture omitted the required `patch-review.js` asset. Adding that fixture asset
resolved the failure without changing production checks or test assertions.
[Corrected complete output](evidence/native-patch-local-ctest.log),
[initial failure](evidence/native-patch-initial-ctest.log),
[candidate source/SDK hashes and limits](evidence/native-patch-local.json).
The existing SDK binaries were identity-checked and reused; its full suite and
external installed library bytes are outside this local source freeze.

## Earlier hosted gate failures

Run [37974972815](https://github.com/xlang-foundation/xMind/actions/runs/37974972815)
compiled revision `a7f484a809cc8b27b8b02d3f94dacfddf7def263` and passed
97 of 99 contracts. The workspace effect fixture returned an uncertain move
outcome, and the parallel delegation fixture exceeded its unchanged eight-second
wait. This is a failed gate, not patch acceptance. Move diagnostics now retain
the effect phase and the rename API's Win32 error number. The delegation fixture
prints actual durable parent/child states on timeout; its assertions and deadline
are unchanged. These diagnostic changes require a new native build.

The following [101-contract run](https://github.com/xlang-foundation/xMind/actions/runs/37978080333)
at `a1ed50d4fc314f93a1a57fb7115cc106d8003008` also compiled but failed:
97 contracts passed; MCP effect dispatch, per-file patch execution, patch batch
coordination and workspace effects failed. Delegation passed in this run. The
move failures still came from source predating the phase/Win32 diagnostics.
The batch test now observes an already-terminal executor future while waiting
for its next proposal, so its actual partial/uncertain report is printed instead
of an unrelated five-second proposal timeout. No assertion or timeout was relaxed.

General patch editing is being implemented in C++. OpenCode v2.0.16 is the
behavior reference (`packages/core/src/tool/patch.txt` and the patch tool's input
and operation declarations). No OpenCode implementation is embedded or copied.

The first source increment provides `agentflow_file_patch`: parsing the patch
envelope and add/update/delete sections, optional move destinations, context
anchors and end-of-file chunks. Preparation matches exact UTF-8 line context,
rejects ambiguous matches, preserves unchanged bytes outside each matched range
and retains context-line bytes. Insertions use the existing line-ending style;
updates preserve the original final newline choice. Added files use LF.

Parsing is bounded to 64 KiB, 128 files, 256 chunks per file and 1,024 total
chunks. Updating is bounded to 1 MiB before and after. Relative paths, duplicate
source/destination paths, malformed UTF-8 and invalid grammar are checked before
preparation. KMP matching avoids a repeated-context quadratic scan. Preparation
supports cancellation. These checks do not replace workspace handle authority,
private-path exclusions, snapshot preconditions or effect permissions.

The native contract source covers multiple operation kinds, CRLF/Unicode,
context ambiguity, anchors, EOF-only insertion, missing final newlines, multiple
chunks, invalid envelopes/paths, duplicate targets, file/input bounds, invalid
UTF-8, cancellation and a 100,000-line repeated-context fixture. Compilation and
execution of these increments are pending their isolated native workflow; the prior
98-contract search result does not validate it.

Workspace planning now prepares every section from actual bounded file snapshots
before any approval or effect. It retains verified parents/leaf handles while
reading each source and records its workspace, file, content and parent identity.
Moves also bind an absent destination and disclose any missing parent folders.
Private paths, reparse parents/leaves and hard links are rejected. All file paths
are compared with Windows Unicode case rules; duplicate or ancestor/descendant
file targets and repeated source identities are rejected before effects. Combined
before/after review text is bounded to 4 MiB. Patch edit plans carry their parent
identity into the existing edit executor and revalidate it under retained handles.

Expanded actual-filesystem contract source covers all four plan types, untouched
source/private files, destination absence, Unicode aliases, overlapping paths,
occupied move targets, aggregate bounds, cancellation and private/link boundaries.
These contracts have not yet executed for this source. Planning has no mutations;
model-driven delete/move execution is still unavailable.

Backend-only removal and move effect primitives are now implemented in source.
They reopen sources relative to retained parents, take exclusive file handles,
reject links/read-only targets, and recheck source identity and exact contents.
Removal marks the verified handle for deletion, explicitly closes it and checks
absence while the parent remains retained. Move validates source/destination
bindings before creating disclosed parents or updating source content. It then
renames the same handle to the absent destination, preserves file identity,
checks the final handle path/content and verifies source-name absence. Directory
handles needed for rename allow write sharing while excluding delete sharing.
There is no path-based delete, overwrite fallback or copy/delete move fallback.

The implementation uses Windows [SetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle)
with disposition/rename information. [FILE_RENAME_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info)
defines the directory-relative destination and the no-replacement flag. Rename
allocation, disposition/close, directory creation, source writes, and final
verification are covered by uncertainty handling after an effect attempt. No
cancellation check hides an effect already completed; failures never replay.

Expanded filesystem contract source exercises actual deletion, same-identity
rename/content update and disclosed directory creation in a disposable workspace.
It also tests changed bytes after planning, wrong file/parent/workspace/hash
bindings, occupied destinations, private/link targets, cancellation and read-only
removal refusal. Independent Node fixture reads check the actual resulting bytes,
hashes and absence of old names. These tests have not executed for this source;
they do not establish model admission, durable approval or journal recovery.

`PatchFileExecutor` now connects each prepared file to the existing native
permission waiter and operation journal. The executor owns a copy of the plan
while it is reviewed. Its immutable `patch_file` proposal contains the batch ID,
file index/count, exact before/after bytes, source/destination bindings and folder
effects. It waits for an explicit decision, acquires the durable claim, rechecks
guidance and invokes the corresponding workspace primitive. Known pre-effect
failures are recorded as failed; uncertain effects are quarantined. A failed
outcome write leaves the claim available to startup recovery and raises a fail-stop
error instead of continuing or replaying. Dynamic child authority maps this adapter
only to a sealed `apply_patch` capability; current presets do not expose it yet.

The new executor contract source uses actual disposable files and embedded xlang3
SQLite to exercise explicit allow/deny/cancellation, immutable review despite
caller changes, changed guidance, stale sources, all four effect kinds, exact
persisted receipts, rejected identity reuse, a deliberately injected journal-write
failure, restart quarantine and blocked further effects. The fault is a trigger
in a disposable test database, not a modification of a product database. This
contract has not executed for the candidate source. It does not establish a model
or client patch workflow.

The native multi-file coordinator is now implemented in source. It validates
every encoded proposal and guidance binding before requesting the first file,
discloses the requested batch manifest in each approval, rejects reused operation
identities and executes files sequentially. Its bounded report is derived from
actual durable operation states/results, with earlier successes, denied/failed
files and verified absence of requests for later files kept distinct. It stops on
denial, cancellation, stale preconditions or uncertainty. An unrecorded/unresolved
effect stops coordination rather than fabricating a summary or continuing.

Creation/move receipts now capture actual identities of created directories while
their handles remain retained. Later files may refresh only the already-created
prefix belonging to this batch, after verifying those identities and the original
ancestor. Remaining folder effects are disclosed in the new file's approval.
Externally replaced or independently created folders do not become an implicit
grant. This supports sibling files and moves sharing new folders, including
Windows Unicode/case aliases, while preserving each file's immutable review.

The batch contract source covers complete mixed-operation batches, shared folders,
validation before the first effect, identity reuse, partial denial, cancellation,
stale later files, externally replaced owned folders and exact per-file receipts
across an xlang3/SQLite restart. It independently reads actual files. These checks
have not executed for this candidate. Client live acceptance remains a separate
requirement. The shared client now
renders native per-file patch review and opens revalidated read-only comparisons;
its latest complete frozen adapter gate passed 222/41 contracts. It has not been
installed or exercised with a native model-generated patch.
[Client implementation and limits](patch-review.md).

## Native model and graph integration candidate

The source now advertises `apply_patch` for approval-enabled ordinary agents and
the sealed `workspace.coding` preset. Read-only agents/inspection children do not
receive it. Its strict arguments are `patch_text` and optional `create_parents`;
model-supplied authority fields and duplicate JSON fields are rejected. The
adapter discovers every source/destination guidance scope before planning an
approval. Undelivered guidance returns the existing
`repository_instructions_required` result without a proposal or requested effect.

Before each file, including files sharing newly created parent folders, the
adapter checks the currently applicable scopes against guidance already supplied
to the model. Moves bind both source and destination conditions into one durable
approval, rechecked after approval. A newly approved `AGENTS.md` that affects a
later file stops the batch with its actual earlier success preserved; that new
guidance must reach another model request before a fresh call and approval.
Partial/uncertain reports are recorded as failed tool outcomes, not full success.

Declared direct graph patch steps use the same native adapter and per-file
approvals, with guidance captured from all discovered scopes before proposal.
An incomplete patch fails its graph node with the actual partial report. A report
over the graph's 64 KiB output bound fails after effects and retains its patch id
and authoritative per-file journals; it cannot be mistaken for full completion.

The complete native manifest now contains **103 contracts**. New contract source
covers strict input and guidance delivery, approval-time changes, sibling-folder
refresh, own-guidance partial results, agent/CLI mixed effects and independent
file hashes, denial, read-only catalogue and exact SQLite history across restart.
The existing graph contract now also covers direct patch approvals and denial.
Provider replies/usage and controller fixture decisions are synthetic. These new
C++ paths compile and pass the complete local gate. The packaged CLI and
installed two-file addition acceptance are reported above; real-provider graph
patch execution and installed mixed-effect review remain separate requirements.

Broader delivery still requires hosted artifact verification and installed
browser/mixed-effect review. A multi-file patch must
report actual outcomes for each file; it must not claim an atomic filesystem
transaction or hide earlier effects when a later file fails. Fuzzy matching and
full upstream patch semantics remain explicit parity gaps.
