# VS Code opened-folder checkpoint

The extension defaults to a managed local backend for the folder supplied by the
actual VS Code workspace API. Opening one local folder is sufficient; a
`.code-workspace` file and a manually entered server token are unnecessary.
Multi-root workspaces select one active execution root. The backend publishes its
actual physical workspace identity and a generation identifier. Runs carry both
identifiers, and a mismatch is rejected before admission.

The packaged runtime uses the native C++ backend and embedded xlang3 with SQLite.
Provider credentials are imported by Native from one explicitly configured YAML
path. The extension passes that path without reading the credentials. Native
owners and their sessions survive view closure and folder switching.

Validation on 2026-10-08:

- Fresh Windows Release configure/build and all 88 registered native contracts
  passed (177.60 seconds). The provider runtime contract includes two actual
  filesystem roots, cross-root/stale-generation rejection, junction retargeting,
  and the default Claude output limit. Model transports in this contract are
  synthetic; these results do not establish live provider inference.
- All 171 extension and 32 browser tests passed with no skipped tests. These are
  host/controller/renderer and file-integrity tests, not a real IDE acceptance.
- All 560 native input hashes remained unchanged during the native gate. All 39
  frontend input hashes remained unchanged during the Node gate; the VS Code
  README received a documentation-only update afterward.
- xlang3 is pinned to `ad8040ffb8aba6eeabeb09053a8e222df09a4e7a` on
  `xmind/windows-native-file-longpaths`. The CPython bridge and Python executable
  were disabled. Only pure Python standard-library source is reused.

The actual packaged VS Code acceptance on `D:\CantorAI2026\TestProj` is pending
at this source checkpoint. Installation, rendered UI, and live model behavior
must be recorded separately after execution.
