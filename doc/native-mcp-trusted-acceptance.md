# Trusted HTTPS OAuth acceptance

The first completed hosted gate at `6708269` passed **115 of 116** contracts in
**407.85 seconds**. The trusted contract failed before native login because
`Import-Certificate` into `CurrentUser\Root` reported that UI is not allowed.
The downloaded archive digest, exact inventory and original logs are verified.
This is a test provisioning failure, not successful trusted OAuth acceptance.
[Original hosted failure](evidence/native-oauth-registered-hosted-6708269.json).

Successful HTTPS login must be tested through the actual native service, not by
injecting tokens or setting a connected state. The new hosted-only contract runs
the compiled C++ service, its real callback receiver, code exchange, encrypted
xlang3 SQLite repository and MCP client factory against an independent synthetic
OAuth/MCP authority. The original `6cde3ba` fixture compiled and passed its local
peer/guard checks. The newer registered-callback candidate passes the complete
local native gate; a hosted successful result is still required before advertising
trusted native login acceptance.

The independent authority validates the real public client/resource/redirect,
state, scope and S256 challenge. Its token endpoint checks the native verifier
against that challenge and consumes a one-use code. The strengthened fixture
requires eight actual native attempts: default and exact registered-path/port
success, denial, wrong callback state, wrong issuer, rejected code, cancellation
and an occupied registered port. Only the two positive cases may create grants;
there must be one token request for each and one rejected-code token request.
No other case may exchange tokens or store credentials. An occupied port must
fail before publishing a sign-in link, with no alternate-port fallback.

The C++ fixture then checks the complete encrypted grant, actual revision and
expiry, refresh-token fidelity and scope. The production factory uses its bearer
for real MCP discovery/listing for both positive cases. After closing/reopening
the real database, it loads both grants and repeats authenticated discovery. Public setup
responses and raw database/WAL files must not contain either token. Synthetic
authority/tokens are explicitly test data, with no real provider account or
browser-launch/rendered acceptance implied.

## TLS and execution boundary

Both native WinHTTP and the Node browser driver keep certificate verification
enabled. The hosted fixture creates a short-lived CA and signed loopback leaf.
The certificate helper installs only that owned CA in the isolated runner's
LocalMachine Root store, checks its fingerprint and confirms removal afterward.
It requires an elevated identity in addition to the isolated-runner checks.
The machine store avoids the CurrentUser Root consent UI that rejected the first
hosted run; no certificate provisioning is permitted on the development PC.
[Repair boundary evidence](evidence/native-oauth-refresh-trust-ci-local.json)
records helper parsing, desktop Install/Remove rejection, unchanged user/machine
root stores and the independent HTTPS peer. Compiled native code and binaries
are unchanged from `6afaaf7`; the complete native gate was not rerun for this
test-only repair. These checks do not prove hosted import or native trusted login.
It requires real GitHub-hosted Windows runner variables, an exact owned path
under `RUNNER_TEMP`, a generated subject and matching certificate fingerprint.
It rejects development PCs and self-hosted runners. Never impersonate those
environment variables or disable verification to run it locally.

This uses the [documented certificate import store](https://learn.microsoft.com/en-us/powershell/module/pki/import-certificate?view=windowsserver2025-ps)
and [GitHub runner environment indicators](https://docs.github.com/en/actions/reference/workflows-and-actions/variables).
[Microsoft's noninteractive CI guidance](https://github.com/microsoft/PowerToys/blob/main/.github/skills/ui-tests-local-vm/references/shell-extensions-and-signing.md)
describes machine-root import and the user-root consent limitation.
An import/verification failure rejects the test; it cannot become a skipped or
successful OAuth result. The PowerShell helper also rolls back a failed import
after it has confirmed no preexisting matching certificate.

`AGENTFLOW_HOSTED_OAUTH_TRUST_CONTRACT` defaults to OFF and refuses configuration
on a development PC when enabled. `Tools/ci-native.ps1` enables it on the hosted
runner and requires its exact additional contract in the complete inventory.
The normal local gate includes the independent peer contract; the hosted gate
includes both that contract and the actual native trusted-login contract.
Their verification scopes must remain separate.

The later [single-use refresh primitive](native-mcp-refresh.md) adds one local
contract, bringing the complete local/hosted inventories to **116 / 117**.
While the owned test root is installed, the existing hosted trusted contract
also invokes the actual native refresh primitive for seven independently checked
response/failure modes. These separate synthetic protocol checks prove no durable
refresh publication, automatic renewal or client refresh workflow; hosted results
for this strengthened source remain unverified.

Local checks verify the independent peer over real Node HTTPS with an explicit
CA and rejection under default untrusted TLS. At original source `6cde3ba`, the
native trusted fixture compiled and the complete local gate passed **115/115**
in **386.74 seconds**, with **2459**
unchanged mapped inputs and full **257 extension / 55 browser** suites.
The certificate helper and CMake hosted option both reject on this development
PC; its CurrentUser Root thumbprints remain unchanged. These checks do not prove
the hosted native trusted-login flow or acceptance of the newer registered
callback changes. See the separately scoped
[original local evidence](evidence/native-oauth-trusted-local.json). The newer
registered-callback source separately passes **115/115** local native checks in
**388.83 seconds**, with **2460** unchanged inputs and unchanged client source.
Its eight-case hosted trusted-login fixture compiles but is not executed locally.
[Current registered-callback evidence](evidence/native-oauth-registered-local.json).

Remaining delivery includes that hosted acceptance, real MCP authority login,
client-registration selection/CIMD/DCR, refresh/revocation, running-owner
coordination, complete graphical connector configuration and fresh installed
UI acceptance. [Native service](native-mcp-login.md) and
[console commands](native-mcp-console.md) retain their existing scoped results.
