# xMind for VS Code

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

A VS Code workspace can contain several folders. The extension retains that
folder set, but Native currently executes against **one active root**. For a
multi-root workspace, choose it explicitly when prompted or use **xMind:
Select Active Workspace Root**. It does not silently treat the first folder as
the whole workspace. The sidebar displays the actual Native backend root.

Each selected root gets an independent Native backend, private SQLite state
outside all opened roots, a loopback port and fresh authentication held in
SecretStorage. The host verifies the canonical root and Native workspace and
generation identities before allowing mutations. Run and graph admission
also carry those identities for an atomic backend check.

Closing the view, deactivating the extension or switching folders leaves ready
backends and their running or paused work alive. Returning to a retained
authenticated generation reconnects observation. A changed machine-level
launch configuration creates a separate generation without stopping the old
owner. Only a newly spawned backend that fails startup before authenticated
readiness is eligible for startup cleanup.

## Machine configuration and packaging

Configure these in VS Code's **User** settings. Executable, library and provider
paths are read from machine/global settings, never selected-project settings.

| Setting | Behavior |
| --- | --- |
| `agentflow.backendMode` | `managed` by default; `external` explicitly attaches to an existing loopback backend. |
| `agentflow.runtimeDirectory` | Optional absolute directory containing a verified `native-runtime-manifest.json`; otherwise use the extension's bundled runtime. |
| `agentflow.stdlibSource` | Optional trusted pure standard-library source directory for development; otherwise use bundled source. |
| `agentflow.providerConfigPath` | The **one** explicit absolute provider YAML path imported by Native. The extension does not read its contents or search opened projects for configuration. |
| `agentflow.workspaceEdits` | Enables native edit proposals in newly started managed backends; each effect retains its separate native approval. Default `false`. |
| `agentflow.backendUrl` | External mode's explicitly configured loopback origin. |

An unset provider path starts Native unconfigured so Settings can enroll a
provider. With a configured path, every new managed backend imports that same
file through Native; the host does not copy keys into arguments, conversations
or workspace state. [Provider setup](../../doc/provider-setup.md).

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
is implied. [Build and bundle instructions](../../doc/DEVELOPMENT.md).

## Coding controls and validation scope

The footer offers backend-advertised model IDs and registered workflows.
**Ask About Selection** adds selected code to a draft for review before sending.
Sessions and run details come from the selected backend. History renders
sanitized Markdown/code and actual per-response provider usage/timing; absent
metrics remain unavailable. Human questions, context controls and approvals
retain their native ownership and revision checks. Comparing an edit does not
grant it, and uncertain effects offer no retry.

**Open Browser View (Copy Connection Token)** opens the selected backend's
browser access adapter and copies its connection token for Connect. Closing
that access view does not stop Native execution. [Browser details](../../doc/browser-view.md).

The workspace-binding source passed **171 extension and 32 browser Node
contracts**, with no failures or skips. These use synthetic host, process,
filesystem, HTTP and DOM fixtures; they do not establish installation or an
actual TestProj managed-backend/IDE result. The subsequent source-matched Native
gate passed all 88 contracts, and the packaged extension passed the actual
opened-folder VS Code test on TestProj. The normal persistent window retains
VS Code's folder-trust requirement. [Opened-folder evidence](../../doc/vscode-opened-workspace.md).
Earlier actual
preview and editor evidence belongs to its recorded runtime and remains
separate. [Validation status](../../doc/VALIDATION_STATUS.md),
[graph/editor history](../../doc/vscode-graph-workflows.md),
[actual diff-host evidence](../../doc/evidence/vscode-native-diff.json).
