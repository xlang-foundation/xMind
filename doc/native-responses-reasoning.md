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
was left running. [Frozen source and deferred-gate record](evidence/native-responses-reasoning-source-provenance.json).

Exact revision `ace246094f6c1fc8cf61c146cfe001c63f1bbc8f` then passed
[hosted run 37780648430](https://github.com/xlang-foundation/xMind/actions/runs/37780648430):
**71 native contracts in 171.52 seconds**, **103 extension contracts in
3.1842023 seconds** and **20 browser contracts in 1.7960528 seconds**, plus
native/browser integration and all 18 required VSIX assets. All 16 authentic
job steps succeeded. Exact source/archive verification matched 229 normalized
compiled sources, 247 Git blobs, and runtime/view/VSIX file maps of 29/12/30.
The unit and HTTP contracts took **0.16/5.44 seconds**. The first local bundle
collection failed before extraction because the Windows PowerShell helper
needed the FileSystem compression assembly; its original discarded stderr is
not reconstructed. The corrected, independently reviewed continuation reused
the verified downloads without network requests and passed all artifact checks.
[Exact hosted and collection provenance](evidence/native-responses-reasoning-hosted-provenance.json),
[raw CTest](evidence/native-responses-reasoning-hosted-ctest.log),
[raw job log](evidence/native-responses-reasoning-hosted-ci-job.log).

The unit fixture independently authors completed and terminal objects, checks
exact opaque string preservation through request serialization, and exercises
invalid fields, unknown members and the aggregate receipt bound. The HTTP
contract source exercises six real native model transports: two read-only
leaves each read their own fixture file, then the parent joins their observed
results. It checks separate response metrics, owned attempts, child histories
and xlang3 SQLite reopen without replay, plus 15 rejected terminal shapes.
Provider replies, opaque values, credentials and task objectives are explicitly
synthetic. These cases passed in the exact hosted gate above; the local native
gate was deferred.

The earlier 7fe diagnostic backend's public OpenAI probe found
encrypted-content-only drift and failed before tools or children. Its historical
outcome remains recorded separately.
[Actual diagnostic probe](evidence/native-responses-diagnostic-live-7fe7.json),
[installed/upgrade scope](native-responses-diagnostics.md),
[actual rendered failure](evidence/native-responses-diagnostic-live-7fe7.jpg).

## Installed browser acceptance

The exact hosted ace native bundle is installed with schema v10, retaining the
separately repaired e353 browser view. The managed upgrade preserved the saved
API records and owned attempt audit, settings, authentication and access; its
observation covered 13 sessions, 17 roots and 39 root-history rows. It retained
a closed backup. This managed check does not assert direct live SQL-row or
database-byte identity and does not fence concurrent authorized admission.

Separate disposable fixtures passed the actual upgrade and idle rollback, plus
refusal to restore the old backup after accepted work and replacement-owner
loss. Closed copied SQLite rows, delegation ledgers and encrypted credentials
were checked through xlang3 in those fixtures. Their keys and provider replies
are synthetic; native execution and persistence are real. Upgrade and rollback
made no model calls. [Upgrade scopes and actual logs](evidence/native-responses-reasoning-upgrade-provenance.json).

A fresh public browser prompt on saved OpenAI Responses `gpt-5.6-sol` completed
two native read-only children: one read `README.md`, the other
`doc/native-delegation.md`. The parent joined both observed results and completed
its next response. The read-only acceptance audit matched the separate owned
histories, file receipts, six reserved/started/finished model attempts, per-response
usage and measured timings, durable settlements and committed tree order, with
zero effect operations. [Actual native acceptance](evidence/live-browser-responses-reasoning-delegation-ace24609.json).

The rendered page showed the joined answer, completed children and separate
response metrics. Immediate refresh retained the connection, public conversation,
completed run and selected model without credential entry.
[Actual browser observations](evidence/live-browser-responses-reasoning-delegation-ace24609-browser.json),
[rendered joined answer](evidence/live-browser-responses-reasoning-delegation-ace24609-joined.jpg).
The first evidence-recorder attempt rejected valid leaves without reasoning
items; the corrected check passed on the same completed run without another
provider request. Any present reasoning receipt still requires a nonempty opaque
string, and the parent delegation receipt is checked specifically. No live
terminal opaque values, provider-wire comparison or opaque fingerprints are
published or claimed inspected.
[Actual recorder failure and successful read-only retry](evidence/live-browser-responses-reasoning-delegation-ace24609-recorder.json).

Installed VS Code delegation acceptance, mutable planning, broader coding/provider
support and complete MCP/A2A parity remain required.
