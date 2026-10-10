# Native MCP HTTP transport

HTTP and OAuth are unfinished. The current product accepts native stdio MCP
configuration only. Do not advertise the new interface as a working HTTP client.

The implementation now separates `McpToolClient` from `McpStdioClient`.
The existing production `McpToolRegistry` receives the transport-neutral owner;
the stdio binding implements it. Discovery remains public metadata access,
while `tools/call` remains private to the journal-backed registry. Dispatch
failure still carries whether the request may have been sent, its request ID
and any response bytes needed for effect reconciliation. The interface supplies
no approval authority, outcome setter or automatic request replay.

The existing actual MCP effect contract now passes its real stdio peer through
that interface. Compile-time assertions require an abstract base, a shared
deadline type, noncopyable stdio ownership and no public tool dispatch on either
type. Whitespace/source checks pass; native compilation and execution of these
changes are pending. The accepted native `977e945` package predates this change.

## Protocol and ownership

The native wire codec already pins modern `2026-07-28`, with explicit older
negotiation. HTTP must implement those distinct protocol eras too; this is
peer interoperability, not product-format migration. The
[modern HTTP specification](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/streamable-http)
uses request metadata/mirrored headers, POST JSON or request-scoped SSE, and
POST notification subscriptions. It has no session IDs, standalone GET or
Last-Event-ID resumption. Closing a response stream cancels its MCP request.
The [older HTTP specification](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports)
uses initialize/session headers, GET streams, optional DELETE and resumable
SSE. Those mechanisms cannot be silently mixed into a modern request.

`mcp_http_metadata.cpp` now projects standard headers from actual native request
bytes and validates tool parameter header declarations. Values preserve exact
property paths, Unicode/control/whitespace encoding, boolean spelling and safe
integer precision without a floating-point round trip. Null/absent values omit
headers. Invalid declarations throw before projection; the future HTTP client
must catch declaration rejection per tool and omit that tool from discovery.
Existing stdio discovery remains unchanged. Byte/header-count limits are xMind
implementation limits. This component is linked into the actual MCP wire target
and has additions to its existing native contract, but has not been compiled
or executed yet. Network dispatch, discovery filtering and HTTP ownership are
still unfinished.

The following are xMind implementation decisions, not claims of delivered
protocol support:

- The native backend selects a trusted endpoint and immutable configuration
  revision. Views/models cannot supply an endpoint, raw header or token.
- The HTTP binding reuses native negotiation/correlation and the common
  approval registry. Protocol IDs/cursors belong to the MCP owner; UI SQLite
  event sequences remain separate.
- TLS uses OS verification. Redirects cannot forward credentials to another
  destination. Parsing, response sizes, session IDs and deadlines are bounded.
- Transport loss records uncertain effects when dispatch may have occurred.
  Older resumable observation cannot repeat a journalled tool POST. Modern
  stream closure cannot be treated as automatic resumable observation. UI
  disconnection detaches the viewer; it must not close runtime-owned MCP work.
- The factory selects and connects the actual transport before registry
  discovery. Factory/configuration, HTTP binding and response-header access
  still need implementation; existing provider helpers alone are insufficient.

## Authorization

The [modern MCP authorization specification](https://modelcontextprotocol.io/specification/2026-07-28/basic/authorization)
requires resource and authorization-server discovery, PKCE capability checks
and resource parameters in authorization/token requests. HTTP session identity
does not substitute for authorization.

xMind will own authorization state and encrypted access/refresh credentials
behind embedded-xlang3 SQLite. Reference purposes must bind the server/config,
resource, issuer and account. Browser and VS Code views initiate the user flow
through native commands and receive public status; they do not retain refresh
tokens or replace effect approval. OAuth discovery, callback/state/PKCE,
registration, expiry/refresh/revocation and user-facing connection controls are
all unfinished.

## Acceptance still required

- Compile the shared interface and run the complete existing native gate,
  including actual stdio effect and official SDK interoperability contracts.
- Test actual native HTTP with independent official SDK peers: initialize,
  JSON/SSE response variants, notifications, tool discovery, schema validation,
  explicit approvals and independently observed peer effects/receipts.
- Verify modern metadata/header agreement, safe parameter projection and
  per-tool rejection, concurrent request isolation and cancellation/deadlines.
  Verify older session headers, resumable GET, expiry and DELETE separately.
- Lose a reply around a peer effect; require recorded uncertainty and no
  duplicate tool dispatch after reconnect or backend restart.
- Exercise TLS/authentication, OAuth discovery/PKCE/issuer/resource binding,
  token rotation and client view lifecycle without exposing credentials.

None of these checks is completed by UI SSE, a source-only abstract interface,
a provider HTTP helper or a successful stdio peer.
