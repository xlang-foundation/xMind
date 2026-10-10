# VS Code opened-folder checkpoint

## Current live development host — 2026-10-10

The live preview launchers no longer disable VS Code workspace trust. A fresh
normal development host opened `D:\CantorAI2026\TestProj`; the actual extension
completed `open()` and wrote its `secondarySidebar` readiness record after
attaching to the native backend launched for the same root. The exact native
executable SHA-256 was
`6C64FAD7909F755A8610D1B34F2AA6158088D456CD10D62F98DB71EB995BE858`.
Agent execution was unconfigured and no provider request was made. The shared
VS Code suite passed **262/262**. A stale Windows UI window handle prevented
screenshot inspection, so this does not claim visual composition or a
model-backed coding action. [Exact live evidence](evidence/vscode-live-opened-workspace-20261010.json).

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

The current package accepts one `xmind.exe` entry point. Old isolated development
copies from 0.1.0–0.1.4 still contain separate server/CLI executables and are
rejected; there is no profile/package migration path. The verifier now identifies
that retired layout and directs the user to install the current unified package.
The current source runtime inventory verifies **1,896 files**.

Validation on 2026-10-08:

- Windows Release configure/build and all 88 registered native contracts
  passed after the final lease fix (176.44 seconds). The provider runtime contract includes two actual
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

The final native ownership fix also addresses a real VS Code startup failure:
its private database path was 254 characters, and the `.backend-lock` suffix made
the lock path 267 characters. The backend now uses extended Windows spelling
only for opening that lock. Its public database identity and exclusive ownership
remain unchanged. The existing repository contract verifies this exact boundary,
ordinary/extended alias contention, lease release/reopen, and retained SQLite
history and information.

The packaged extension was installed and passed an actual VS Code extension-host
acceptance on `D:\CantorAI2026\TestProj`. The actual workspace API reported one
opened folder and no `.code-workspace` file; authenticated Native metadata
confirmed that exact physical execution root. The accepted runtime contains eight
native artifacts, 1,869 pure standard-library source files and 13 license notices.
All 1,890 inventory hashes and the six checked extension source files matched the
actual VSIX archive. Its native source revision is
`1e3d4ae84044fd934882ced3114526b44aa445db`.

This acceptance used a fresh isolated test profile with the standard
`--disable-workspace-trust` test flag. The normal persistent VS Code profile was
also installed and opened on TestProj with its trust settings unchanged. It
reported Restricted Mode and awaits the user's folder-trust choice. No rendered
screenshot or live inference is claimed for this checkpoint.

The later accepted `516d918` package passed another actual VS Code host check
on `D:\CantorAI2026\TestProj`, with one opened folder, no `.code-workspace` and
authenticated native metadata bound to that exact physical root. Its runtime
manifest and complete inventory were verified before launch. The host exited
successfully. The fresh isolated test profile used the standard workspace-trust
test flag; model inference and rendered screenshot acceptance were not performed.
[Current package host evidence](evidence/native-ci-staging-vscode-opened-folder.json).
