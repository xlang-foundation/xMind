# Native Responses reasoning continuation

The native parser now retains the complete reasoning item from
`response.output_item.done` for subsequent model requests. A reasoning item's
nonempty `encrypted_content` string may differ in the terminal snapshot when
every other member is JSON-value equal and the member set is unchanged.

All other item, metadata, identity, status, phase, summary, argument, lifecycle
and sequencing checks remain strict. Missing, null, empty or non-string opaque
values are rejected. Message and function-call items do not receive the
reasoning-only allowance. The ordered completed-item receipt remains bounded
to 4 MiB before validated usage, finish or done events can be emitted. The
original completed opaque string is preserved; the parser does not decrypt or
normalize it.

OpenAI specifies the completed reasoning item as the continuation source.
[Official Responses streaming events](https://developers.openai.com/api/reference/resources/responses/streaming-events).
Other fields use the existing JSON-value comparison; this is not a byte-for-byte
equality claim for all JSON encodings.

## Verification scope

Three native source/test files received independent review and the HTTP peer
passed Node syntax checking. All 229 compiled/gate/vendor source hashes and the
unchanged native xlang3 SDK were frozen. The actual local launcher exited 3 at
its benchmark guard before configure, build or tests. The sibling benchmark
was left running. The exact hosted gate is pending for the pushed source
checkpoint. [Frozen source and deferred-gate record](evidence/native-responses-reasoning-source-provenance.json).

The unit fixture independently authors completed and terminal objects, checks
exact opaque string preservation through request serialization, and exercises
invalid fields, unknown members and the aggregate receipt bound. The HTTP
contract source exercises six real native model transports: two read-only
leaves each read their own fixture file, then the parent joins their observed
results. It checks separate response metrics, owned attempts, child histories
and xlang3 SQLite reopen without replay, plus 15 rejected terminal shapes.
Provider replies, opaque values, credentials and task objectives are explicitly
synthetic. These are reviewed test cases; execution results are still pending.

The installed browser uses the separately verified 7fe diagnostic backend. Its
actual public OpenAI probe found encrypted-content-only drift and failed before
tools or children; no live successful join is claimed.
[Actual diagnostic probe](evidence/native-responses-diagnostic-live-7fe7.json),
[installed/upgrade scope](native-responses-diagnostics.md),
[actual rendered failure](evidence/native-responses-diagnostic-live-7fe7.jpg).

This compatibility source has not been installed. Successful live delegation,
installed VS Code acceptance, mutable planning, broader coding/provider support
and complete MCP/A2A parity remain required.
