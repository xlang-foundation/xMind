# Bounded native provider HTTP diagnostics

Later checkpoint `d3a395d89a1ecf93947728338a65822c441464bd` passed
[hosted validation](https://github.com/xlang-foundation/xMind/actions/runs/37702741919),
including the added native discovery and agent-event diagnostic propagation
assertions. [Exact-source scope](evidence/native-cli-discovery-setup-hosted-provenance.json)
and [results](evidence/native-cli-discovery-setup-hosted-summary.log) distinguish
that validation from newer reasoning/Responses routing work still in progress.

Checkpoint `d0a70fef888c3724c2490bcb2cf8ef38d60f08c0` now passed its
[hosted gate](https://github.com/xlang-foundation/xMind/actions/runs/37701625318):
52 native and 66 extension contracts, including bounded native transport
diagnostic cases. [Results](evidence/native-cli-approval-diagnostics-hosted-summary.log),
[scope/provenance](evidence/native-cli-approval-diagnostics-hosted-provenance.json).
Later discovery-HTTP propagation and additional agent-event assertions remain
newer source pending their own gate. The original source-prepared notes below
record the earlier local build deferral; they do not override this exact hosted
result. The live HTTP 400 cause is still unresolved and previews are unchanged.

The Windows transport preserves an unsuccessful HTTP status and, for JSON error
responses with status 400 or higher, attempts to read a diagnostic body. The read
is limited to 32 KiB and the lesser of the request deadline or two seconds.
Redirect responses remain rejected without following the redirect or reading
diagnostics. Errors never reach the model stream consumer.

Only exact allowlisted values of `error.type`, `error.code`, and `error.param`
are retained. Raw messages, body content, arbitrary parameter paths, credentials,
and unknown identifiers are omitted. AgentRunner persists available identifiers
as `provider_error_type`, `provider_error_code`, and `provider_error_param` on its
real `run.failed` event. The exception message contains only the HTTP status.
Diagnostic read/parse failures preserve that status; cancellation remains a
cancelled transport operation. There is no automatic retry or model switch.

The native socket contract was extended with explicitly synthetic JSON errors,
private fields, malformed/oversized bodies, stalled reads, and cancellation. Node
syntax validation passed. The guarded local native build deferred because the
sibling xlang3 benchmark was still running; these C++ changes are therefore
source-prepared, pending native compilation and execution in CI. The running
browser backend has not been replaced and does not yet produce these fields.

The user's observed HTTP 400 remains undiagnosed. These diagnostics provide
evidence for a subsequent fix; they are not evidence that model compatibility is
fixed or that a specific parameter caused the failure.

Provider discovery HTTP errors now retain the same safe identifiers, together
with `provider_status`, in the authenticated server response. The ordinary
`detail` includes only these known identifiers so existing Settings clients can
display them without another model/key input or raw provider body. The server
still returns HTTP 502 for an upstream HTTP failure. Discovery and agent HTTP
contracts now check end-to-end propagation of synthetic authentication/rate-limit
identifiers and omission of private parameter/message values. Their JavaScript
syntax checks pass; current native compilation/execution remains pending while
the guarded local build defers for active sibling benchmarks.
