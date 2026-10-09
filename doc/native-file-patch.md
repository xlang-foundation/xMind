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
delete/move execution is still unavailable.

This increment does not expose a model tool or filesystem endpoint and performs
no filesystem effects. Delivery still requires delete/move effect primitives,
durable per-file effect ownership and
approval, disclosed missing parents, changed-guidance/file checks, recorded
partial or uncertain outcomes, recovery without replay, model/graph integration,
CLI/browser/VS Code review and real-provider acceptance. A multi-file patch must
report actual outcomes for each file; it must not claim an atomic filesystem
transaction or hide earlier effects when a later file fails. Fuzzy matching and
full upstream patch semantics remain explicit parity gaps.
