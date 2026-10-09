# Preparing a stopped legacy owner

The native backend supports an operator ticket for a **stopped** schema 12 or
13 owner that cannot issue the modern retirement receipt. Preparation verifies
actual PID/creation-time exit, acquires the same canonical database lease,
requires idle execution/maintenance records and snapshots the original tables.
It leaves the schema unchanged. This is a post-exit administration primitive;
it does not stop a running backend or itself authenticate its pre-exit listener,
executable or database command line. The separate
[native preflight](native-legacy-preflight.md) now verifies those bindings
without stopping the process. Source-image metadata for preparation is supplied by the
operator. The installed VS Code migration flow remains unfinished.

The administration interface is:

```text
xmind_admin inspect-owner-process PID
xmind_admin --db FILE --modules DIR --stdlib DIR prepare-legacy-owner TARGET_ROOT MANIFEST_SHA256 WORKSPACE PID PROCESS_BIRTH SOURCE_SERVER_SHA256 approved|read-only
```

Preparation requires the existing owner token in `XMIND_AUTH_TOKEN`, a verified
target package and the target's bundled module/library directories. It publishes
`legacy_ticket_id`, rather than a fabricated quiescence receipt. A live matching
source process, unavailable lease, busy data or existing pending ticket rejects
preparation. Obtaining the existing token and confirming the intended source
remain operator responsibilities; possession of this CLI is not proof of an
authenticated pre-exit inspection.

Qualified server startup accepts `--legacy-owner-ticket ID` with the same
database, target digest, workspace, token and edit policy. Schema migration and
prepared-owner publication share one transaction. Ordinary startup refuses a
pending legacy ticket. Prepared restart retains closed admission and obtains a
fresh generation. Existing tables must match their captured contents before
migration and again before activation. The authenticated activation command
checks the exact prepared receipt and target/workspace authority and consumes
the operator ticket in its transaction. Saved credentials remain encrypted;
provider YAML is not reimported during replacement startup.

All **98 native contracts passed in 193.73 seconds**, with 629 mapped inputs
unchanged. The production CLI/server fixture rejected preparation while its
owned legacy process was live, then prepared the same database after the test
explicitly killed that idle fixture. It verified retained sessions, closed
admission and exact activation. That test kill is not a product shutdown flow.
The separate native contract verifies encrypted credentials, history fidelity,
changed-record refusal, transaction rollback and ticket consumption using
synthetic process/target metadata and provider replies. No provider requests,
installed migration or file-writing acceptance occurred. The 39 view inputs are
unchanged from the previous **197 extension / 39 browser** passing checkpoint;
those suites were not rerun for this native increment.
[Exact source map and scope](evidence/native-legacy-owner-local.json),
[complete CTest output](evidence/native-legacy-owner-local-ctest.log).

The retained hosted run at `8e07ef0` passed 95 of 96 contracts; its handoff
fixture failed because the minimal package's `sqlite3` import lacked `connect`.
Persistence now imports xlang3's existing native `_sqlite3` export directly.
The local complete gate includes that repair. A new hosted success is not yet
claimed. [Original failed CTest](evidence/native-owner-replacement-hosted-failure-ctest.log).

The later [native stop/adoption increment](native-legacy-stop.md) connects
authenticated preflight to explicit writer-fenced termination and extension
recovery. Independently verified installation and real rendered create/edit
approvals are still required before the installed TestProj preview can write
files. Its running backend and profile were left unchanged.
