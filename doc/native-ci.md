# Isolated native verification

The latest published native gate and installed acceptance are recorded in
[validation status](VALIDATION_STATUS.md) and
[Responses continuation](native-responses-reasoning.md). Historical source
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

- xlang3 source `58a8aab9b40a464e38a741129b4804ac36e27a53`, checked clean.
  It includes the reviewed SQLite transaction/text changes and native Windows
  long-path support, native Unicode pure-library paths and the xMind runtime
  compatibility fixes. The historical SQLite patch is not applied again.
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

## Bounded owned-process observations

The graph and agent test peers optionally observe their exact spawned native
process on the isolated hosted runner. The collector validates the root image
and creation time, follows bounded descendant identities, and records only
actual PowerShell CPU time, thread states/wait reasons and loaded module names.
It does not collect command lines, environment values or process memory, and
never signals the native test or its descendants. These are diagnostic tools;
the native C++ process executor, permissions and deadlines are unchanged.

Each shell identity has at most three samples, separated by ten seconds. The
collector bounds families, shell identities, thread/module entries and output
size. Normal observation lasts at most 65 seconds; its own process has an
80-second execution limit. A stop marker retires observation after the native
process closes. The harness waits at most two seconds for the observer; an
unfinished observation is labelled incomplete and does not replace the actual
native execution result. Both peers preserve their failed fixture directory.

The three actual Windows collector checks run after the native gate, including
after a failure. They must not warm PowerShell before its native startup tests.
They verify descendant isolation, omission of private arguments/environment,
identity rejection without terminating the root, and preservation of an actual
fast exit-7 error. The two affected native harnesses pass in 18.80 seconds, and
all 2,602 project inputs, dependencies, SDK and 124 native binaries verify
unchanged during the final checks. The earlier overly strict retirement
assertion and its failed output remain recorded separately.

This adds evidence for the next hosted run. It does not fix or identify the
underlying older hosted PowerShell timeout, and does not accept the currently
running source or any rendered interface. [Exact local scope and retained
failure](evidence/owned-process-diagnostics-20261018.json).

The collector uses the documented [Node child-process handle](https://nodejs.org/docs/latest-v22.x/api/child_process.html#child_processexecfilefile-args-options-callback)
and [Windows thread wait reason](https://learn.microsoft.com/en-us/dotnet/api/system.diagnostics.processthread.waitreason)
observations. A wait reason is a snapshot, not proof of an underlying fault.

## Local verification and historical evidence

Only isolated GitHub runners may invoke `ci-native.ps1`. Local native builds
use `Tools/native-milestone.ps1` and its observed-benchmark guard. Exit 3 is
deferral, not a test pass. [Development instructions](DEVELOPMENT.md).

Earlier 19-, 26-, 28-, 30- and 31-contract gates and their initial failures remain
in the [milestone ledger](milestones.md) and `evidence/`. Their older runtime
pins, test counts and unconfigured preview descriptions are historical; they
are not the current workflow configuration. Source-matched artifact verification,
live model acceptance and runtime performance have separate evidence scopes.
