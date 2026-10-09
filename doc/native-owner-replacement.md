# Native replacement startup

The replacement implementation binds retirement to the target runtime's actual
package root/file identity, inventory digest, native/SDK/source revisions and
server hash. It also records the captured workspace root/identity, a digest of
the native owner token, the requested edit policy and the lease's canonical
database path. These fields are checked before schema writes.

The original native quiescence receipt is retained as `bootstrap_receipt` when
target-bound retirement commits. It is a source precondition, not an exit
acknowledgement. The caller must observe the retiring process's actual exit and
then let native startup verify the committed source state under the database
lease. It must not fabricate a receipt or replay a retirement command after a
lost response. Owner status reports actual Windows PID and process creation
time; the latter is a decimal string to avoid JavaScript integer rounding.

`POST /v1/backend/owner/retire` validates the same native authority/health/idle
conditions as quiescence, verifies the requested target package, commits the
target-bound retirement record and stops the HTTP listener. This route requires
native owner bearer authentication; scoped view credentials cannot invoke it.

The production server accepts `--runtime-manifest-sha256 DIGEST` to qualify its
actual loaded image. It requires the package's own module and pure-library
directories and a captured execution workspace. For replacement startup, add
`--owner-receipt GENERATION:REVISION:RECEIPT_ID`, using the issued source receipt,
and the bound `--workspace-edits approved` policy when requested. Configuration
overrides, including provider YAML import, are rejected in replacement mode;
saved native configuration is loaded instead.

Authorized migrations and publication of the prepared owner use one native
SQLite transaction through embedded xlang3. A failed commit rolls back both.
Prepared startup keeps admission closed while sessions, history and public
provider metadata remain observable. A process that exits before activation
can be prepared again against the same qualified target/source receipt, with
a fresh native generation and closed admission. Ordinary startup cannot adopt
that state. Same-generation resume cannot activate a prepared replacement.

After comparing the saved records, the owner calls
`POST /v1/backend/owner/activate` with the new prepared generation/revision/
receipt and workspace authority. Native revalidates the loaded package, actual
runtime health/idleness and immutable edit policy before opening admission.
An already-consumed source receipt cannot restart an activated profile.

The complete local gate passed **96 native contracts in 198.23 seconds**, with
all 623 mapped inputs unchanged. Registered and passed names match the complete
manifest. The production-server fixture uses the actual C++ entry point,
embedded-xlang3 SQLite and two verified package directories. Unused package
members and deployment metadata are synthetic. No provider calls, installed
editor or file effects were tested. This is not yet a release or installed
VS Code upgrade.
[Exact validation and retained attempts](evidence/native-owner-replacement-local.json).

The first focused gate passed both native owner contracts, including atomic
schema 12 migration/publication rollback and target-field checks. The new
production-server handoff fixture retired its first process but failed the
replacement's Unicode native-module lookup. Its failure is retained in
`.agentflow/ci/owner-handoff-first`; no complete 96-contract success is claimed.

The failure was traced to xlang3's import-root C API converting UTF-8 input with
Windows ANSI path conversion. The SDK fix is isolated in
`D:\CantorAI2026\.worktrees\xlang3-xmind-import-utf8` on branch
`xmind/windows-import-root-utf8`, based on `ad8040ff`. It also keeps source and
native-module path metadata in UTF-8. The fixed accepted Release baseline was
preserved before SDK changes. The SDK fix was committed and pushed as
`5e86144cce2730b06e0de11c155097cf71b84a58`. Its Release build and focused checks
passed. The allowed SDK set ran 53 checks: 51 passed; both failures reproduced
with the unchanged baseline. Five CPython peer checks were excluded by the
user's requirement. All 11 default performance cases passed with 21 paired
samples, five warmups and the unchanged threshold.

The first xMind rebuild with that source still copied its cached older DLL/import
library. This failure is retained separately. Build configuration now rejects
artifacts that differ from the selected runtime directory, and the corrected
launcher explicitly selects the new files. The production-server handoff fixture
then passed, including Unicode package/workspace paths and actual old-process
exit, prepared startup, activation and new-session admission. The following
whole build exposed a missing workspace-library link dependency. After that
dependency was fixed, the fresh complete 96-contract gate passed. Every earlier
attempt remains retained; none establishes installed or live-provider continuity.

The earlier `416c8d3` hosted workflow completed successfully, including 95 native
contracts, views, integration and packaging. Its artifacts have not yet been
independently verified or installed. The installed legacy `TestProj` process
still lacks the owner protocol and remains read-only. Explicit legacy migration,
thin-client adoption, real provider/history continuity and rendered create/edit
approval acceptance remain required. No OpenCode equivalent-operation or Nexus
completion is established by this increment.
