# Registered MCP OAuth callbacks

Backend-owned OAuth MCP configuration now accepts an optional `callback`
object with `path` and `port`. The native service binds that exact path/port
before publishing an authorization URL. A zero port selects an available
loopback port. An explicit occupied port fails authorization without switching
ports or issuing a token request. The listener remains IPv4 `127.0.0.1` only.

Example desired configuration for a pre-registered public client:

```json
{
  "servers": [{
    "id": "registered-tools",
    "transport": "http",
    "endpoint": "https://tools.example.test/mcp",
    "oauth": {
      "scope": "server",
      "id": "registered-tools-grant",
      "issuer": "https://identity.example.test/tenant",
      "client_id": "your-registered-public-client",
      "callback": { "path": "/oauth2redirect/registered-client", "port": 43210 }
    }
  }]
}
```

This example is configuration metadata, not a runnable authority or credentials.
The registration must accept `http://127.0.0.1:43210/oauth2redirect/registered-client`.
Omitting `callback` selects `/oauth/callback` and an available port. Explicit
default values normalize to the same configuration and credential purpose.
Paths use the existing bounded literal ASCII letters/digits, `/`, `_` and `-`
receiver contract. Unknown callback fields, a host override, invalid paths,
negative/out-of-range ports and noninteger ports are rejected before persistence.

The native credential purpose and admitted agent authority bind any nondefault
callback settings. Changing them changes the configuration revision and requires
a grant provisioned for that exact registration context; it cannot reuse a
different callback's encrypted grant. Configuration remains startup/admin-owned;
views send the configured server ID and observed revisions, not callback addresses.

The underlying [native-app OAuth standard](https://www.rfc-editor.org/rfc/rfc8252#section-7.3)
permits loopback HTTP, requires exact registered paths and requires authorization
servers to allow varying loopback ports. Fixed ports are optional client
configuration, not a requirement imposed on conforming authorities. xMind does
not yet provide IPv6 callbacks or arbitrary URI path syntax.

## Acceptance scope

The independent Node HTTPS authority checks both the default redirect and an
exact registered path/port, independent S256 verification and one-use code
exchange. Its local peer contract passes; this alone proves no native login.
The native grant contract adds rejected callback settings, default normalization,
configuration revision, encrypted purpose separation and actual SQLite reopen.
The real-socket receiver contract adds exact fixed-port callback/retirement.
Agent authority checks include callback changes. Focused Release compilation and
all **four** selected contracts pass, including **13** real-socket callback cases,
actual xlang3 SQLite reopen and the independent HTTPS peer. The complete rebuilt
**115/115** native gate subsequently passed in **388.83 seconds**, with **2460**
unchanged mapped inputs. Client sources are unchanged from the prior complete
**257 extension / 55 browser** acceptance; their suites were not rerun here.
[Exact source/binary and original output evidence](evidence/native-oauth-registered-local.json).

The hosted trusted-login contract additionally requires the actual C++ service
to complete default and registered sign-ins, reject a held occupied port before
publishing a link, persist exactly two grants and use both through the production
MCP factory before/after SQLite reopen. The strengthened native trusted fixture
compiles but is not executed on the development PC. Hosted trusted native login
remains unverified. [Trusted execution boundary](native-mcp-trusted-acceptance.md).

Client registration discovery/CIMD/DCR, confidential-client authentication,
refresh/revocation, running-owner coordination, complete graphical connector
configuration and fresh installed/rendered acceptance remain unfinished.
