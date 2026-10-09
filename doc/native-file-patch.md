# Native patch implementation

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
have not executed for this candidate. Batch-wide model guidance delivery and
client review remain separate requirements.

These increments do not expose a model tool or filesystem endpoint and perform
no model-invokable filesystem effects. Delivery still requires model-call input and guidance
integration, recorded
partial or uncertain outcomes, recovery without replay, model/graph integration,
CLI/browser/VS Code review and real-provider acceptance. A multi-file patch must
report actual outcomes for each file; it must not claim an atomic filesystem
transaction or hide earlier effects when a later file fails. Fuzzy matching and
full upstream patch semantics remain explicit parity gaps.
