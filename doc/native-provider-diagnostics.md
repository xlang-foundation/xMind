# Bounded native provider HTTP diagnostics

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
