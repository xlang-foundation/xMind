# Trusted HTTPS OAuth acceptance

Successful HTTPS login must be tested through the actual native service, not by
injecting tokens or setting a connected state. The new hosted-only contract runs
the compiled C++ service, its real callback receiver, code exchange, encrypted
xlang3 SQLite repository and MCP client factory against an independent synthetic
OAuth/MCP authority. It is prepared and compiled; a hosted successful result is
still required before advertising that acceptance.

The independent authority validates the real public client/resource/redirect,
state, scope and S256 challenge. Its token endpoint checks the native verifier
against that challenge and consumes a one-use code. Six actual native attempts
exercise success, denial, wrong callback state, wrong issuer, rejected code and
cancellation. Only success may create a grant; there must be exactly one success
token request and one rejected-code token request. No other case may exchange
tokens or store credentials.

The C++ fixture then checks the complete encrypted grant, actual revision and
expiry, refresh-token fidelity and scope. The production factory uses its bearer
for real MCP discovery/listing. After closing/reopening the real database, it
loads the same grant and repeats that authenticated discovery. Public setup
responses and raw database/WAL files must not contain either token. Synthetic
authority/tokens are explicitly test data, with no real provider account or
browser-launch/rendered acceptance implied.

## TLS and execution boundary

Both native WinHTTP and the Node browser driver keep certificate verification
enabled. The hosted fixture creates a short-lived CA and signed loopback leaf.
The certificate helper installs only that owned CA in the isolated runner's
CurrentUser Root store, checks its fingerprint and confirms removal afterward.
It requires real GitHub-hosted Windows runner variables, an exact owned path
under `RUNNER_TEMP`, a generated subject and matching certificate fingerprint.
It rejects development PCs and self-hosted runners. Never impersonate those
environment variables or disable verification to run it locally.

This uses the [documented certificate import store](https://learn.microsoft.com/en-us/powershell/module/pki/import-certificate?view=windowsserver2025-ps)
and [GitHub runner environment indicators](https://docs.github.com/en/actions/reference/workflows-and-actions/variables).
An import/verification failure rejects the test; it cannot become a skipped or
successful OAuth result. The PowerShell helper also rolls back a failed import
after it has confirmed no preexisting matching certificate.

`AGENTFLOW_HOSTED_OAUTH_TRUST_CONTRACT` defaults to OFF and refuses configuration
on a development PC when enabled. `Tools/ci-native.ps1` enables it on the hosted
runner and requires its exact additional contract in the complete inventory.
The normal local gate includes the independent peer contract; the hosted gate
includes both that contract and the actual native trusted-login contract.
Their verification scopes must remain separate.

Local checks verify the independent peer over real Node HTTPS with an explicit
CA and rejection under default untrusted TLS. The native trusted fixture compiles.
The complete local gate passes **115/115** in **386.74 seconds**, with **2459**
unchanged mapped inputs and full **257 extension / 55 browser** suites.
The certificate helper and CMake hosted option both reject on this development
PC; its CurrentUser Root thumbprints remain unchanged. These checks do not prove
the hosted native trusted-login flow. See the separately scoped
[local evidence](evidence/native-oauth-trusted-local.json).

Remaining delivery includes that hosted acceptance, real MCP authority login,
client-registration selection/CIMD/DCR, refresh/revocation, running-owner
coordination, complete graphical connector configuration and fresh installed
UI acceptance. [Native service](native-mcp-login.md) and
[console commands](native-mcp-console.md) retain their existing scoped results.
