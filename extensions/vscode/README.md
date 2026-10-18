# xMind for VS Code

**0.1.20** is a local Windows x64 development package. Its native runtime
preserves checked conversation history across authority, model and supported
provider changes. DeepSeek receives verified foreign tool turns as labelled
context data while retaining its own native reasoning and tool continuation.
Reconnecting with unchanged provider YAML preserves later UI/console provider
selection, including while a graph owns execution. Deliberate file-default
changes retain idle and revision checks. Human console output shows final
supplied usage once per response, including available reasoning/cache counts.
Foreground Windows processes receive the OS-derived SystemDrive without
forwarding provider secrets. If another client changes the active provider
during selection, both clients refresh capabilities and saved-key discovery
after the conflict without retrying the selection or submitting a task.
Process directory leases allow child-directory publication while retaining
leased-path rename/deletion protection. The host observes the same live native
startup beyond a single observation interval and reports elapsed progress in
the existing sidebar document.
Unchanged transcript snapshots preserve open history details. Streamed Markdown
is parsed once per animation frame during bursts, with a synchronous final
flush; completed graph children retain their saved responses without replayed
deltas creating another live answer.
An event stream cut halfway through a frame now resumes from its last accepted
cursor, including when the cut splits a UTF-8 character. Only read-only event
requests retry, under the existing retry limit; prompts and effects are not
resubmitted. Complete malformed frames and invalid UTF-8 still stop observation.

The extension suite passes **414/414**; the browser suite passes **138/138**.
All **148** local native Release contracts pass. A real Claude run validated a
source-verified ModelZoo copy: xlang3 dependency installation, **9 passing**
FastAPI tests, and a Vue production build. After correcting native directory
sharing, a real xlang3 pytest run passed all nine tests and published its cache;
only the dependency's Starlette/AnyIO deprecation warning remains. A native
runtime handoff retained the original 12-message conversation and selected
provider. Event observation resumed through completion without replaying the task.
The earlier history
checkpoint passed ten focused native contracts. A real saved-key coding conversation completed
OpenAI → Claude → Gemini → DeepSeek → OpenAI, including approved file creation
and editing, two xlang3 assertions and retained history through a managed runtime
handoff. These checks cover the selected models and production HTTP clients;
they do not establish rendered IDE or release acceptance. Exact source, package, live and installed
scope is recorded in [validation status](https://github.com/xlang-foundation/xMind/blob/main/doc/VALIDATION_STATUS.md).

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
does not reset conversation/model choices. Backend ownership remains native.
**xMind: Apply Trusted Runtime Configuration** is the explicit,
confirmation-gated command for a profile-preserving backend upgrade after
trusted settings change; no legacy profile conversion is required.

Native view and shared host/controller contracts cover these ownership rules;
rendered installed-client acceptance is tracked separately.

## Machine configuration and packaging

Configure these in VS Code's **User** settings. Executable, library and provider
paths are read from machine/global settings, never selected-project settings.

| Setting | Behavior |
| --- | --- |
| `agentflow.backendMode` | `managed` by default; `external` explicitly attaches to an existing loopback backend. |
| `agentflow.runtimeDirectory` | Optional absolute directory containing a verified `native-runtime-manifest.json`; otherwise use the extension's bundled runtime. |
| `agentflow.stdlibSource` | Optional trusted pure standard-library source directory for development; otherwise use bundled source. |
| `agentflow.providerConfigPath` | Optional absolute provider YAML path imported by Native. Development Host automatically uses this repository's `.config/providers.yaml`; the extension passes only its path and never searches the opened project. |
| `agentflow.languageServerConfigPath` | Optional absolute trusted native LSP profile YAML path. Development Host automatically uses this repository's `.config/language-servers.yaml` when present; the extension never searches the opened project. |
| `agentflow.processConfigPath` | Optional absolute trusted native process profile YAML path. Development Host automatically uses this repository's `.config/processes.yaml` when present; the extension never searches the opened project. |
| `agentflow.workspaceEdits` | Default `true` for newly started managed backends: file creation/edit proposals require native approval before writing. Explicit `false` starts a read-only owner. Existing profiles keep their stored policy until the user applies the setting through **xMind: Apply Trusted Runtime Configuration**, which restarts the same profile and preserves sessions/history. |
| `agentflow.profileRoot` | Optional machine-level profile directory for sharing the same local workspace sessions with the webpage or console. Defaults to `%USERPROFILE%\.xMind\p`; keep custom paths outside every opened workspace. |
| `agentflow.openOnStartup` | Optional machine-level setting that opens the xMind sidebar and connects the active workspace after VS Code starts. Defaults to `false`. |
| `agentflow.backendUrl` | External mode's explicitly configured loopback origin. |

An unset provider path starts Native unconfigured so Settings can enroll a
provider. With a configured path, every new managed backend imports that same
file through Native; the host does not copy keys into arguments, conversations
or workspace state. [Provider setup](https://github.com/xlang-foundation/xMind/blob/main/doc/provider-setup.md).
After changing a trusted runtime, language-server, or process profile path,
run **xMind: Apply Trusted Runtime Configuration**. It requests a protected
native owner handoff after confirmation; sessions and history persist and
already dispatched work is never replayed.

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
is implied. [Build and bundle instructions](https://github.com/xlang-foundation/xMind/blob/main/doc/DEVELOPMENT.md).

## Coding controls and validation scope

The footer offers backend-advertised model IDs and registered workflows.
**Ask About Selection** adds selected code to a draft for review before sending.
Sessions and run details come from the selected backend. History renders
sanitized Markdown/code and actual per-response provider usage/timing; absent
metrics remain unavailable. Human questions, context controls and approvals
retain their native ownership and revision checks. Comparing an edit does not
grant it, and uncertain effects offer no retry.

The command palette also provides **xMind: Show Git Status**, **xMind: Review
Git Diff**, and **xMind: Initialize Git Repository** for the connected VS Code
workspace. The sidebar toolbar's **Workspace Git** menu exposes the same three
guarded commands beside the conversation and terminal controls. Review Git Diff
reads patches from Native, prompts for a base only when the backend cannot
infer one, and opens the selected patch in VS Code's diff language mode. The
menu is covered by source renderer/host tests; packaging and actual installed
VS Code display for this toolbar change remain pending.

The footer displays the backend's actual file policy when Native advertises it:
**File changes require approval** or **Read only: file changes disabled**.
Changing the setting does not enable writing in an already-running owner.

Native file creation, edits, patches and restore use approved operations and
recorded effect receipts. Comparing a proposal leaves the filesystem unchanged.
See [file effects and validation](https://github.com/xlang-foundation/xMind/blob/main/doc/native-file-patch.md)
and [runtime handoff](https://github.com/xlang-foundation/xMind/blob/main/doc/native-runtime-handoff.md).

**Open Browser View** opens a local browser adapter connected to the selected
workspace. The extension enrolls the page's browser session privately; no server
token needs to be copied into the page. Closing that access view does not stop
native execution. [Browser details](https://github.com/xlang-foundation/xMind/blob/main/doc/browser-view.md).

Client tests use labelled synthetic host, process, HTTP and DOM fixtures. Live
and installed results are reported separately from xMind's own coding acceptance.
The persistent window retains VS Code's folder-trust
requirement. [Opened-folder evidence](https://github.com/xlang-foundation/xMind/blob/main/doc/vscode-opened-workspace.md).
Earlier preview and editor evidence belongs to its recorded runtime.
[Validation status](https://github.com/xlang-foundation/xMind/blob/main/doc/VALIDATION_STATUS.md),
[graph/editor history](https://github.com/xlang-foundation/xMind/blob/main/doc/vscode-graph-workflows.md),
[actual diff-host evidence](https://github.com/xlang-foundation/xMind/blob/main/doc/evidence/vscode-native-diff.json).
