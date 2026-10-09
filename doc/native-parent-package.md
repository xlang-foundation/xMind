# xMind 0.1.2 package checkpoint

The local VSIX contains the accepted native file/folder-creation checkpoint
`1bf6a2d7d9d98514b4cc811be18899907ce64610`, including the preceding Responses
failure diagnostics. Its paired xlang3 SDK remains
`7b8b32ae3a0e6a99fac7babd97362736099448fb`, with the CPython bridge disabled.

Only extension version metadata changed from 0.1.1 to 0.1.2 after the native
gate. Both package JSON files were checked against the accepted source, with
no dependency or product-code changes. The native binaries match the recorded
real OpenAI CLI acceptance. Recompiling the C++ core for this metadata change
would not test additional behavior; the versioned client suites were rerun.

All **200 extension and 39 browser tests** passed with 41 view inputs and
11 browser assets unchanged. The existing native source gate remains
**98 contracts in 203.59 seconds**, with its exact manifest and source hashes.
The corrected parent-creation checkpoint's hosted view workflow
[run 37950004226](https://github.com/xlang-foundation/xMind/actions/runs/37950004226)
was separately observed as successful. Its artifact contents have not been
independently downloaded; no hosted native/package result is inferred from it.

The VSIX archive has **1,933 entries**. Independent ZIP verification checks
every runtime byte against its manifest, including **8 native programs/packages,
1,869 pure-source library files, 21 licenses and provenance**. Ten host/view
files match their source bytes. All 18 required shared-browser access, renderer
and license assets are present. The archive contains no private state,
CPython executables, CPython extensions or bytecode.

A separate model-free smoke starts the complete qualified packaged server in
an owned temporary workspace, opens SQLite through bundled xlang3, persists an
actual session and completes native retirement with process exit code zero.
It advertises approved file proposals but performs no inference or file-writing
operation. Earlier real CLI writing remains a separate scope, even though its
executed native binaries match the package.
[Package, hashes and validation](evidence/native-parent-package.json),
[prior real writing](evidence/live-native-cli-parent-creation.json).

Local artifact:
`D:\CantorAI2026\xMind\.agentflow\ci\parent-vsix-package\xmind-0.1.2.vsix`

VSIX SHA-256:
`2c586c3ca2d70ac421bcb8d860b6510481eec5c5381180265175eb00bd999e02`

Runtime manifest SHA-256:
`2501be8046cc4f600763a93665b464e2fee26d9afc2e0f7fe9c02311e6f918aa`

The old project staging directory was retained before replacement; no process
referenced it. The installed version **0.1.1**, saved profile, retained migration
target and live legacy source were left unchanged. This new artifact is ready
for distribution testing. It has not been installed into that pending migration
or used to claim rendered VS Code writing acceptance. Those checks, broader
real-provider coding and the remaining pinned OSS parity are still required.
