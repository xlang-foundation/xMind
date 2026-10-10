# Durable native MCP refresh storage

The storage checkpoint at `57da085` adds typed C++ repository operations for refresh claims,
dispatch and atomic complete-grant publication through embedded-xlang3 SQLite.
The complete rebuilt **117/117** local native gate passes in **389.30 seconds**,
with all **2464** frozen inputs unchanged and three focused contracts passing.
It is not automatic refresh or a working network coordinator. Client source is
unchanged from the earlier complete **257 extension / 55 browser** acceptance;
those suites were not rerun for this candidate.
[Exact local evidence](evidence/native-oauth-refresh-storage-local.json).

The subsequent [native manual renewal coordinator](native-mcp-renewal.md)
integrates these operations with network discovery/exchange, authenticated
commands and console/view controls. Its acceptance is recorded separately;
the storage checkpoint alone does not prove that integration.

## Ownership and receipts

The journal uses the existing SQLite information repository with a reserved
`native-mcp-oauth-refresh` category. Generic information writes and compare/write
operations cannot mutate it. Each bounded request identity retains its exact
server configuration, credential purpose/revision, token endpoint and backend
generation. The journal contains metadata, not access or refresh token bytes.
An existing request observes its receipt without obtaining another private grant
snapshot. Changing its binding rejects the request.

Only a new claim returns the complete private snapshot. Competing claims for that
credential are rejected. Dispatch commits before the caller can make its exchange.
Dispatch is single use. A preparation abandoned before dispatch becomes cancelled;
an abandoned dispatched exchange becomes uncertain. A new actual backend generation
retires old preparations as cancelled and old dispatches as uncertain in the same
transaction that publishes its ownership. It does not replay either exchange.

Generic credential replacement/deletion participates in the same exclusion.
Prepared, dispatched and uncertain claims fence the claimed credential. Backend
quiescence refuses active preparation/dispatch. An uncertain revision requires
explicit recovery or fresh authorization with a new credential identity; automatic
recovery into the same identity is not implemented.

## Publication

The connector validates the complete response, previous scopes, token endpoint,
expiry and refresh-token retention. The repository checks the exact current
configuration entry, grant revision/purpose, dispatch and backend generation inside
one transaction. It protects the complete replacement for the next credential
revision, updates the ciphertext and commits the published receipt atomically.
An independent receipt-update fault must roll back the credential update too.
Publication does not authorize another network exchange or a tool-effect replay.

Receipts are bounded to 4096 per local repository. History retention/archive and
same-identity reauthorization remain product work. No release capacity or complete
recovery claim is made for this candidate.

## Required verification and integration

The new contract uses actual C++ persistence and xlang3 SQLite. It exercises eight
concurrent claims, duplicate identity without another private snapshot, stale
revisions/generations, credential and quiescence fences, exact configuration checks,
receipt-write fault rollback, encrypted rotation/retention, scope restriction,
corrupted receipts and restart retirement without replay. Grants are synthetic and
no network exchange is executed by this storage contract.

The complete required inventory becomes **117 local / 118 hosted** contracts.
The local inventory passes as recorded above; current hosted acceptance remains
pending. The final product still needs a coordinator
that owns claim → dispatch → single-use exchange → publication, observes uncertain
commit outcomes, integrates new revisions with MCP admission, and provides actual
CLI/view renewal and fresh-login recovery. Existing tool effects must keep their
admitted authority. [Exchange primitive](native-mcp-refresh.md),
[trusted acceptance](native-mcp-trusted-acceptance.md).

The original two focused failures are retained: the first found an incorrectly
counted purpose prefix in the validator, and the second found whitespace in the
fixture's bearer access-token strings. The validator and fixture were corrected;
neither failure is relabelled as passing.
[First failure](evidence/native-oauth-refresh-storage-first-failed.json),
[second failure](evidence/native-oauth-refresh-storage-second-failed.json).
