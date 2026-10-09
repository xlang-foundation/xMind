# Isolated native verification

The latest published native gate and installed acceptance are recorded in
[validation status](VALIDATION_STATUS.md) and
[Responses continuation](native-responses-reasoning.md). Exact source
`ace246094f6c1fc8cf61c146cfe001c63f1bbc8f` passed 71 native, 103 extension and
20 browser contracts, native/browser integration and VSIX verification.
[Exact hosted provenance](evidence/native-responses-reasoning-hosted-provenance.json).
These results do not validate uncommitted dynamic-plan work.

## Current isolated workflow

[Native Windows workflow](../.github/workflows/native-windows.yml) runs on a
separate GitHub Windows runner. It builds its own runtime and never rebuilds the
benchmark checkout on this development machine. Actions and source inputs are
pinned; workflow permissions are read-only.

The current inputs are:

- xlang3 source `ad8040ffb8aba6eeabeb09053a8e222df09a4e7a`, checked clean.
  It includes the reviewed SQLite transaction/text changes and native Windows
  long-path support. The historical SQLite patch is not applied again.
- Standard-library source `ebf955df7a89ed0c7968f79faec1de49f61ed7cb`
  (CPython 3.14.0 `Lib/` sources only).

The job builds native xlang3 with `XLANG3_BUILD_CPYTHON_BRIDGE=OFF` and
`XLANG3_PYTHON314_EXECUTABLE:FILEPATH=OFF`, including
its JSON and SQLite modules. It never executes CPython or installs CPython
native extension binaries.

`Tools/ci-native.ps1` records source/toolchain provenance, discovers the
runner's supported Visual Studio installation, verifies vendored schema sources,
installs pinned Node SDK fixtures with `--ignore-scripts`, builds all native
targets and runs CTest. The registered tests must exactly match its expected
manifest before execution; missing Node/OpenSSL-dependent contracts fail the
gate rather than silently reduce coverage.

The workflow then runs extension and browser contracts, actual browser/native
integration, and packages/verifies the VSIX. Test providers are labelled
synthetic; native filesystem, HTTP and xlang3 SQLite operations are real.
Headless contracts do not establish rendered IDE or live-provider acceptance.

Diagnostics are retained even after failure. The development bundle is uploaded
only after its required build/contract steps succeed. Original failed logs and
later passing results retain their respective source identities.

## Local verification and historical evidence

Only isolated GitHub runners may invoke `ci-native.ps1`. Local native builds
use `Tools/native-milestone.ps1` and its observed-benchmark guard. Exit 3 is
deferral, not a test pass. [Development instructions](DEVELOPMENT.md).

Earlier 19-, 26-, 28-, 30- and 31-contract gates and their initial failures remain
in the [milestone ledger](milestones.md) and `evidence/`. Their older runtime
pins, test counts and unconfigured preview descriptions are historical; they
are not the current workflow configuration. Source-matched artifact verification,
live model acceptance and runtime performance have separate evidence scopes.
