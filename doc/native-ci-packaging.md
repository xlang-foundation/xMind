# Native CI runtime staging

The Windows workflow builds the native C++/xlang3 runtime, validates the complete
registered CTest set, runs the extension/browser suites and native browser
integration, then stages that tested runtime before packaging the VSIX.

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
that file-only scope. The package is verified but has not been installed or
launched locally. The live preview still uses its previously accepted runtime.
Local execution and installed acceptance remain deferred while the separate
xlang3 benchmark is running. Later skill changes have separate pending gates.
