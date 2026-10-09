# Native CI runtime staging

The Windows workflow builds the native C++/xlang3 runtime, validates the complete
registered CTest set, runs the extension/browser suites and native browser
integration, then stages that tested runtime before packaging the VSIX.

The later skills source `d767d5069f52e3e6b899ae7a3e8313ebcc0ecbaf` passed
[the complete workflow](https://github.com/xlang-foundation/xMind/actions/runs/37883217775)
with all 90 native contracts and successful frontend, native/browser, staging
and VSIX checks. Independent verification checked both advertised archive
digests, every registered native result, all 30 accepted source-bundle files,
all 1,861 packaged runtime inventory files and the pinned host/view source
copies. [Skills package evidence](evidence/native-skills-hosted-package.json)
records this file-only verification. The skills VSIX subsequently replaced the
persistent profile's earlier package, with complete installed file verification
and unchanged settings. A real isolated VS Code opened-folder host passed;
four real providers passed native skill activation and companion reads, with
the initial Claude formatting failure and successful retry retained. See the
[installed/live receipt](evidence/native-skills-installed-live.json).
The browser preview later replaced its runtime/assets with this accepted package,
preserving exact saved API records and connection cookies. Rendered reload and
skill metadata were observed; the approval interaction exposed a card-rebuild
bug and was cancelled before an effect. The DOM-retention fix subsequently
passed its complete isolated 177/35 view gate, was verified and published as the
preview's `chat.js`. An actual browser retry passed guidance retention, approval,
native file creation and completed-response reload. Neither preview process
restarted for this script update. The persistent VS Code profile still needs
the fixed script/package and ordinary-window rendered acceptance.
[Browser upgrade evidence](evidence/native-skills-browser-upgrade.json),
[isolated view evidence](evidence/native-approval-stability-views-hosted.json) and
[live retry evidence](evidence/native-approval-stability-live.json) retain these
distinct scopes; the earlier checks below remain historical evidence.

`Tools/ci-native.ps1` writes a successful gate receipt only after the exact
registered contract set has passed. `Tools/ci-stage-runtime.mjs` binds that
receipt to the source revision, registered names and SHA-256 hashes of the test
manifest and complete CTest log. It checks the pinned xlang3 and standard-library
source revisions and bundle provenance before generating an accepted file
manifest. The existing runtime packager verifies and copies those exact native
files, notices and pure standard-library sources. The normal VSIX prepublish
verifier remains mandatory.

The staging adapter executes no native program or Python interpreter and accesses
no provider configuration, credentials or SQLite database. It requires an
isolated GitHub runner and a fresh runtime output directory. It rejects missing
native files, unsupported paths, aliases, oversized inventories and incomplete
or changed test evidence.

The preceding full workflow at source
`293f66af8621121785123d75a7e544a5db4dc3ee` passed all 89 native contracts,
175 extension tests, 35 browser tests and native browser integration, then failed
VSIX packaging because the runtime had not been staged. That workflow is a
failed packaging gate, not a successful release. See the
[failed hosted run](https://github.com/xlang-foundation/xMind/actions/runs/37874177687).

Exact source `516d91842f34a2009c26f7521f9fea66b5ae736b` subsequently passed
[the complete hosted workflow](https://github.com/xlang-foundation/xMind/actions/runs/37876565394),
including its 89-contract native gate, extension/browser checks, native browser
integration, staging, VSIX verification and runtime artifact publication.
Independent inspection verified both downloaded archive digests, all accepted
native bundle files and the complete VSIX runtime inventory: eight native
artifacts, 1,830 pinned pure-library source files and 1,861 inventory files in
total. Seven host files and five copied view sources were bound to the exact
commit; two vendor copies matched their dependencies inside the same VSIX.

[Independent package evidence](evidence/native-ci-staging-package.json) records
that file-only scope. Once a fresh process guard confirmed the separate benchmark
was no longer running, the actual Code CLI installed this exact VSIX into the
existing owned VS Code profile. All 1,861 runtime inventory files, 19 other
package files and 12 browser-runtime files were verified after installation.
Existing settings bytes were unchanged, and no workspace-trust bypass was
requested. [Installation evidence](evidence/native-ci-staging-installed.json)
records this scope. Window reload, rendered acceptance and native execution were
not performed by this installation; the live browser preview retains its
previously accepted runtime. Later skill changes have the successful hosted
gate and separate installed/live acceptance above; rendered checks are pending.

A subsequent fresh VS Code test host loaded this verified package, opened
`D:\CantorAI2026\TestProj` directly and passed its real extension-host workspace
API and authenticated native root checks. It required no `.code-workspace` and
passed provider configuration by path only. The isolated test profile used the
standard workspace-trust test flag; no normal-profile trust override was
requested. [Opened-folder evidence](evidence/native-ci-staging-vscode-opened-folder.json)
binds the successful host exit and accepted runtime manifest. This verifies
native startup and folder attachment; model inference, screenshot verification
and reload of the ordinary persistent window remain separate acceptance work.

The installed `516d918` runtime also passed fresh real OpenAI, Claude, Gemini
and DeepSeek catalogue/inference checks through its production controller.
Each provider read an owned random-marker file in `TestProj` using native
`read_file` and returned the exact text. All four runs completed with actual
usage counters and measured response timings, and no effect-operation events.
The marker fixture was removed after verification. Native imported the single
provider configuration path; the adapter read no provider keys.
[Live installed-package evidence](evidence/native-ci-staging-live-providers.json)
binds the runtime and SDK revisions and the private run-record hashes. This is
native/controller acceptance, with no rendered browser or IDE claim.
