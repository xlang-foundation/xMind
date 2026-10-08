# Native provider transport

Revision `5cf3c79` passed the complete hosted gate (52 native, 73 extension and
14 browser contracts): [provenance](evidence/native-cli-recovery-provider-headers-hosted-provenance.json),
[test/TAP summary](evidence/native-cli-recovery-provider-headers-hosted-summary.log).
Its exact CI bundle is installed in the browser preview. Credential-header
fixtures use independent synthetic peers; this gate does not establish live
Claude/Gemini provider support. The later Claude request component remains
separately scoped in [native provider development](native-chat-provider.md).

Backend-selected credential placement now has source for bearer, `x-api-key`
and `x-goog-api-key` headers. The request contract uses an enum rather than
client-supplied header names; unsupported values and missing API-key credentials
are rejected. Credentials remain separate from request bodies and URLs, require
bounded printable bytes and are copied into wiped native header storage. The
existing certificate verification, redirect rejection, deadlines and cancellation
policy apply to all three placements.

The independent transport peer now requires each API-key placement on actual
native POST/SSE and GET/JSON requests, absence of the other credential headers,
no injected header and no followed redirect. Missing credentials, CRLF injection
and unsupported enum values must fail before sending. All 52 native contracts
passed locally, including these actual socket checks and the accumulated CLI
graph/recovery changes: [CTest log](evidence/native-provider-credential-headers-local-ctest.log),
[source/runtime provenance](evidence/native-provider-credential-headers-local-provenance.json).
The local xlang3 compatibility-branch changes and binary hashes are recorded;
this is separate from exact pinned hosted validation. Existing
model adapters continue selecting bearer by default; this infrastructure does
not implement or claim Claude/Gemini request, stream or live-model support.

The official [Claude authentication documentation](https://platform.claude.com/docs/en/manage-claude/authentication)
allows bearer authentication and the legacy `x-api-key` header; version/workspace
headers and native Claude wire support remain separate work. The
[Gemini API reference](https://ai.google.dev/api) documents `x-goog-api-key` for
API-key authentication. These sources were checked on 2026-10-07.

The Windows provider transport posts actual HTTP/HTTPS requests and delivers incremental SSE bytes to the native model decoder. It uses WinHTTP directly, outside the C++ core. No Python, OpenCode or LiteLLM implementation is invoked.

The request supplies an explicitly configured endpoint/body, whole-request deadline and idle timeout. Provider credentials are supplied as backend-owned `SecretBytes`; a temporary header buffer reserves before copying and wipes its owned credential bytes on release. WinHTTP's own internal buffers remain OS-managed. Error messages contain status/error codes and do not echo provider error bodies, URLs, request bodies or credential values.

Remote endpoints require HTTPS with the OS's normal certificate/hostname verification. HTTP is permitted only for configured localhost/loopback endpoints. URL credentials, fragments and control characters are rejected. Redirects are not followed, and the transport does not retry or automatically supply ambient Windows credentials. Responses must have SSE content type. Request bodies are bounded at 8 MiB and responses at 64 MiB; timeout configuration is positive and limited to ten minutes.

## Asynchronous ownership and cancellation

One caller owns all WinHTTP API calls for a request. Operations run asynchronously; callbacks report completion/error state through a mutex and condition variable. Cancellation/deadline wakes the caller, which closes the pending asynchronous request after the initiating API call has returned. The request context, buffers and credential header remain alive until the final handle-close callback. Notifications occur under the context mutex so teardown cannot release the condition variable while a callback still uses it. Consumer exceptions also unwind through this cleanup.

This follows Microsoft's [WinHTTP concurrency contract](https://learn.microsoft.com/en-us/windows/win32/winhttp/concurrency-in-winhttp), [callback lifecycle](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpsetstatuscallback) and [redirect policy options](https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags).

## Verified scope

The Release build and eight native contracts passed. The independent Node protocol peer tests real native sockets for fragmented SSE delivery into the decoder, bearer header/request body transport, HTTP 429 redaction, redirect rejection without a request reaching the redirect target, incorrect media type, self-signed TLS rejection, cancellation before/during a request, whole-request deadline, idle timeout, consumer failure and a fresh request after cleanup. Ephemeral TLS test keys/certificates are generated outside the repository, never installed in a trust store and removed after the test.

These are explicitly synthetic protocol-peer tests, not model execution evidence. They verify negative certificate validation; successful production TLS and a real model response remain to be validated with the chosen endpoint. Evidence: [native-provider-transport-ctest.log](evidence/native-provider-transport-ctest.log), with subsequent transport-only verification in [native-provider-transport-final.log](evidence/native-provider-transport-final.log).

The transport is Windows-only at present. The [native Chat provider](native-chat-provider.md) adds request serialization and explicit capability gates. Other OS implementations, live provider coverage, provider-specific authentication, retry/routing policy and the real model/tool loop remain required. No product execution route is enabled by this transport component.
