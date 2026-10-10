# xMind for VS Code

The native shared-profile view adapter is a candidate whose native readiness
probe is still failing. The installed 0.1.5 package is unchanged. See
[acceptance boundary](../../doc/native-view-adapter.md) before building/installing
a package from this source.

xMind uses a dedicated right-hand secondary sidebar, with Explorer on the left
and the composer and model chooser at the bottom. C++ owns execution,
permissions and SQLite persistence; the extension observes its authenticated
command/event API. VS Code 1.140 or newer is required.

## Opened folders and backend ownership

Open a local folder and run **xMind: Open Workspace**. Managed mode uses the
actual VS Code `workspaceFolders` and extension-host environment. A single
folder is selected automatically; no `.code-workspace` file, separate server
launch or manual server-token prompt is required. Managed local execution
currently requires a local Windows x64 extension host. Remote and virtual
workspace folders are rejected rather than passed to a local backend.

VS Code must trust the opened folder before enabling xMind. In Restricted Mode,
VS Code disables this extension; trust only a folder whose contents you intend
to allow the agent to inspect and work with, then run **xMind: Open Workspace**.

A VS Code workspace can contain several folders. The extension retains that
folder set, but Native currently executes against **one active root**. For a
multi-root workspace, choose it explicitly when prompted or use **xMind:
Select Active Workspace Root**. It does not silently treat the first folder as
the whole workspace. The sidebar displays the actual Native backend root.

Managed source now launches a native view adapter that connects through the
shared local workspace profile controller. CLI and editor are intended to
observe one backend and SQLite profile. The editor holds only its view credential;
the native adapter retains backend authentication. Native owns startup and process
recovery. The editor verifies returned workspace authority before admission.

Closing the host closes its adapters and should leave backend work alive.
Selections are scoped to the native workspace/profile, so an adapter port change
does not reset conversation/model choices. The JavaScript owner registry and
upgrade commands are removed. Native owner-control contracts remain separate
from UI access; no legacy installation/profile conversion is required.

These are implementation contracts pending native/installed acceptance.

## Machine configuration and packaging

Configure these in VS Code's **User** settings. Executable, library and provider
paths are read from machine/global settings, never selected-project settings.

| Setting | Behavior |
| --- | --- |
| `agentflow.backendMode` | `managed` by default; `external` explicitly attaches to an existing loopback backend. |
| `agentflow.runtimeDirectory` | Optional absolute directory containing a verified `native-runtime-manifest.json`; otherwise use the extension's bundled runtime. |
| `agentflow.stdlibSource` | Optional trusted pure standard-library source directory for development; otherwise use bundled source. |
| `agentflow.providerConfigPath` | Optional absolute provider YAML path imported by Native. Development Host automatically uses this repository's `.config/providers.yaml`; the extension passes only its path and never searches the opened project. |
| `agentflow.workspaceEdits` | Default `true` for newly started managed backends: file creation/edit proposals require native approval before writing. Explicit `false` starts a read-only owner. Existing profiles retain their native startup policy; changed explicit launch options are rejected. |
| `agentflow.backendUrl` | External mode's explicitly configured loopback origin. |

An unset provider path starts Native unconfigured so Settings can enroll a
provider. With a configured path, every new managed backend imports that same
file through Native; the host does not copy keys into arguments, conversations
or workspace state. [Provider setup](https://github.com/xlang-foundation/xMind/blob/checkpoint/native-persistence-m1/doc/provider-setup.md).

External mode requires its actual backend root to match the selected folder.
Unknown workspace metadata, a different root or a changed generation blocks
submission. Configure that server's authentication with **xMind: Configure
Server Token**; external mode never silently falls back to managed mode.

A distribution includes verified native binaries/modules, pure standard-library
source and license notices. For development packaging, stage an already accepted
bundle using `scripts/package-native-runtime.mjs --stage` with explicit source
manifest/digest and standard-library/license inputs. `npm run package` builds
the browser assets and verifies the complete staged Native inventory before
creating a VSIX. There is no unverified executable/PATH discovery fallback.
Install it with **Extensions: Install from VSIX**. No Marketplace publication
is implied. [Build and bundle instructions](https://github.com/xlang-foundation/xMind/blob/checkpoint/native-persistence-m1/doc/DEVELOPMENT.md).

## Coding controls and validation scope

The footer offers backend-advertised model IDs and registered workflows.
**Ask About Selection** adds selected code to a draft for review before sending.
Sessions and run details come from the selected backend. History renders
sanitized Markdown/code and actual per-response provider usage/timing; absent
metrics remain unavailable. Human questions, context controls and approvals
retain their native ownership and revision checks. Comparing an edit does not
grant it, and uncertain effects offer no retry.

The footer displays the backend's actual file policy when Native advertises it:
**File changes require approval** or **Read only: file changes disabled**.
Changing the setting does not enable writing in an already-running owner.
Older backends do not advertise this field, so the view leaves the indicator
hidden rather than assuming their permissions.

The native patch checkpoint passed all 103 native contracts, and the shared
client passed 222 extension / 41 browser contracts. A real OpenAI packaged CLI
run completed an approved add/update/move/delete patch, preserved its first
creation after a later denial, and retained exact history across restart.
The installed 0.1.3 TestProj client separately completed approved file creation
and a fresh workspace read. Installed patch review for this 0.1.4 package remains
a separate acceptance step. [Patch implementation and scope](https://github.com/xlang-foundation/xMind/blob/checkpoint/native-persistence-m1/doc/native-file-patch.md),
[runtime handoff](https://github.com/xlang-foundation/xMind/blob/checkpoint/native-persistence-m1/doc/native-runtime-handoff.md).

**Open Browser View** opens a local browser adapter connected to the selected
workspace. The extension enrolls the page's browser session privately; no server
token needs to be copied into the page. Closing that access view does not stop
native execution. [Browser details](https://github.com/xlang-foundation/xMind/blob/checkpoint/native-persistence-m1/doc/browser-view.md).

Client tests use labelled synthetic host, process, HTTP and DOM fixtures. Live
and installed results are reported separately and do not establish complete
coding feature parity. The persistent window retains VS Code's folder-trust
requirement. [Opened-folder evidence](https://github.com/xlang-foundation/xMind/blob/checkpoint/native-persistence-m1/doc/vscode-opened-workspace.md).
Earlier preview and editor evidence belongs to its recorded runtime.
[Validation status](https://github.com/xlang-foundation/xMind/blob/checkpoint/native-persistence-m1/doc/VALIDATION_STATUS.md),
[graph/editor history](https://github.com/xlang-foundation/xMind/blob/checkpoint/native-persistence-m1/doc/vscode-graph-workflows.md),
[actual diff-host evidence](https://github.com/xlang-foundation/xMind/blob/checkpoint/native-persistence-m1/doc/evidence/vscode-native-diff.json).
