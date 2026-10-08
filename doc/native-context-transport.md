# Native JSON POST transport

The native provider transport now supports bounded, non-streaming JSON POST
alongside JSON GET and streaming POST. The complete local **78-contract gate
passed in 193.91 seconds**, with all **427 frozen inputs** and the accepted
xlang3 runtime files verified unchanged. Its transport contract passed in
**8.80 seconds**. [Source and scope evidence](evidence/native-context-transport-local-provenance.json).

This supplies the HTTP primitive needed by future token-counting and compaction
adapters. Those adapters, durable compacted contexts and client controls are
still unimplemented. See the [full native compaction design](native-context-compaction-design.md).

## Native API

`post_json(request, credential, max_response_bytes, cancel)` is declared in
`Native/include/agentflow/http_stream_transport.hpp` and implemented through
the existing native WinHTTP transport. The caller supplies serialized request
bytes, a backend-selected endpoint and an explicit response cap from **1 byte
through 8 MiB**. Empty bodies, request bodies above 8 MiB and invalid response
caps reject before dispatch.

Successful replies require the JSON media type and return their original
bytes, including UTF-8 and opaque provider fields. The native provider adapter
must validate the resulting protocol JSON. This transport does not manufacture
model messages, usage, checkpoints or tool calls from a JSON reply.

The existing credential placement, TLS certificate verification, loopback HTTP
restriction, cancellation, deadline and idle timeout rules apply. Redirects
are not followed, ambient credentials are not sent, and HTTP errors retain
only bounded allowlisted diagnostics. There is no automatic retry. Existing
JSON GET remains capped at 1 MiB; streaming POST retains its 64 MiB cap.

## Verification limits

The independent synthetic HTTP/TLS peer asserts exactly **15 native JSON POST
requests**. It checks the three credential placements, the selected Claude
version header, exact numeric/escaped-key/UTF-8 request bytes, split UTF-8 reply
bytes, exact response limits, replies larger than the discovery cap, sanitized
errors, redirects, cancellation, deadlines, idle timeout and reuse after failure.
Invalid configuration and untrusted TLS cases do not reach its HTTP handler.

That request count is a source-bound assertion reached by the passing contract;
it is not a separately retained raw request audit. The full per-test transcript
was preserved before CTest discovery. Fixture counts and opaque strings are
synthetic protocol data, not live provider usage or compaction acceptance.

The earlier [hosted MCP checkpoint](evidence/native-dynamic-mcp-hosted-2cd-provenance.json)
passed 78 native, 119 extension and 26 browser tests at exact source `2cd392f`.
It precedes this transport change. The installed preview, user history and
credentials were not upgraded or accessed by this transport validation.
