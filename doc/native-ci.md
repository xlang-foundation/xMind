# Isolated native verification

`.github/workflows/native-windows.yml` builds the native runtime and xMind contracts on a separate Windows runner. It does not rebuild the measured runtime on this development machine. The workflow is prepared source until an actual run passes; CI presence is not completion evidence.

Inputs are pinned: xlang3 `914783909835116969aad7c66b210a5ac9a27661`, the previously reviewed SQLite prerequisite patch in `runtime-prerequisites`, and CPython 3.14.0 commit `ebf955df7a89ed0c7968f79faec1de49f61ed7cb` for **standard-library source only**. The job never executes CPython, builds its bridge, or installs its native extension binaries. It builds the stock native xlang3 executable/shared runtime and JSON/SQLite packages in an isolated temporary branch. No benchmark checkout or local xlang3 branch is modified.

The [official Windows 2025 image](https://github.com/actions/runner-images/blob/main/images/windows/Windows2025-Readme.md) provides Visual Studio 2022/CMake and native tools. `Tools/ci-native.ps1` uses that C++20-compatible toolchain, verifies input revisions and patch hash, records provenance, builds the native targets, and verifies **all nineteen named contracts** are registered before running CTest. Missing conditional Node/OpenSSL tests fail the gate instead of silently reducing coverage. Inference peers remain labeled synthetic; no live model credential is supplied. Runtime performance and live-provider validation are separate requirements, not established by these contracts.

Extension contracts use `npm ci --ignore-scripts` and Node.js; dependency install hooks cannot launch an interpreter. The separate actual VS Code diff/interactive persistence tests remain recorded local evidence, not hosted headless UI claims. Action implementations are pinned by full commit IDs. Workflow permissions are read-only; build/test diagnostics are retained as artifacts even on failure.

Only the isolated GitHub job may invoke `ci-native.ps1`. Local builds continue using `native-milestone.ps1` and its observed-benchmark guard. A remote build failure must be investigated; never copy an old local pass log or remove a required test to mark CI green.
