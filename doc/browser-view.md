# Browser view adapter

The current browser uses [durable native access sessions](browser-session-persistence.md).
Its opaque HttpOnly cookie survives refresh and access-adapter/backend restarts
at the same origins within the eight-hour expiry, without replaying execution.
The earlier `51724ab` backend-only restart contract is retained as
[historical evidence](evidence/browser-native-restart-contract.log); that older
adapter still used an in-memory login map. Current live two-child browser
acceptance has its own [source and scope](native-responses-reasoning.md).
Current acceptance and exact source/package scope are tracked in
[validation status](VALIDATION_STATUS.md). Earlier 326-test and 264/63-test
checkpoints and rendered-view evidence are historical; they do not accept
current browser paint, layout, input or installed editor activation.

xMind has a local HTML view using the same conversation renderer and native API client as the VS Code extension. The webpage places workspaces and their sessions on the left, the conversation in the middle, and optional Files, Changes and Activity views on the right. The global toolbar identifies the current workspace and session and toggles either sidebar. Both dividers support dragging and keyboard resizing; their widths are remembered as non-secret appearance preferences. Provider, model and permission controls sit beside the composer. The terminal opens at the bottom of the right sidebar.

On Windows, the embedded web server adds an xMind icon to the notification
area (possibly inside the tray overflow). Clicking or right-clicking the icon
opens its menu: **Open web UI**, **Open workspace**, and **Quit this xMind
server...**. Opening the browser requires choosing **Open web UI**. Quitting asks for confirmation
and stops that server through its existing shutdown path. Each web owner has
one icon; CLI clients and workers do not add more icons. The icon is removed
when its owner exits and is restored after Explorer restarts. Startup output
reports `windows_tray: true` when Windows accepts the notification icon.
Tray support is compiled only on Windows; other OS build paths are unchanged.
[Validation scope](evidence/windows-tray-20261029.json).

Response tables scroll horizontally inside their message rather than squeezing
headers or breaking inline code identifiers. Focus a table and use the arrow
keys, or scroll it with the pointer/touch controls. This shared-renderer change
has live webpage validation at desktop and mobile widths and is included in
installed VS Code package 0.1.41. Actual editor pane activation remains separate.
[Current scope and captures](evidence/table-readability-20261019.json),
[installed package evidence](evidence/coding-package-0.1.39-20261019.json).

The current development page also replaces the previous run's status as soon
as the newly selected run is presented. This prevents a completed label from
lingering while a new run starts. The shared renderer fix passes 429 extension
and 147 browser tests locally and on hosted CI. The test webpage and installed
0.1.41 extension now share the fix. Actual browser-agent diagnostics and conversation
reload are verified separately from renderer fixtures.
[Current browser and installed runtime evidence](evidence/coding-package-0.1.40-20261019.json).
[Installed shared renderer and preservation](evidence/coding-package-0.1.41-20261019.json).

The local page connects automatically through the owning xMind process; it has no Connect, Disconnect, Refresh or server-token controls. FastAPI runs inside that process's embedded xlang3 runtime and calls the bound native request router directly. Other workspace owners use standard xlang3 SDK IPC. Native admission enrolls and renews the durable HttpOnly browser session without exposing a launch credential to JavaScript. A failed reconnect retains the conversation. Team and remote authentication belong to private Nexus.

When the local view is launched with the trusted `.config/providers.yaml`, it
imports configured provider entries into the selected or added workspace
profile, merging new entries while preserving existing saved settings and
encrypted credentials. Import performs no provider request, and key values
never pass through the browser.

A profile with a blank `model` is key-only: the backend can discover models
with its encrypted saved key, but a model must be selected before the agent
can run. The sidebar receives the public profile/model list and uses the
selected profile on subsequent requests; it never needs or receives the raw
provider key.

The C++ server owns execution, tools, graphs, permissions, providers and history. SQLite I/O remains in embedded xlang3. FastAPI runs in the main xMind process, serves validated view-asset snapshots and calls bound C++ handlers. Other local xMind owners are reached through standard xlang3 IPC. The SDK adapter submits each request once and observes its captured native job; long verification does not block an SDK call. Closing an event response releases its backend admission slot. Managed retirement drains outstanding replies before shutting down. A page reload loads a complete new snapshot from its configured asset directory without restarting the adapter or losing its authenticated sessions. Boundary, route, size and CSP restrictions still apply; a rejected bundle does not revoke backend access. Closing the view does not cancel execution. This is a local HTTP transport. Team deployment, WebRTC and the standalone Electron IDE belong to private Nexus.

The top bar identifies the current workspace. Its name or gear opens Workspace
Settings, where the user switches workspace, creates one from an existing first
folder, or adds another folder to the current workspace. The left side shows
the selected workspace's files and previews. Runs start in that
selected profile and execute in the xMind backend process. The file list and preview are
read-only native workspace-handle reads; directory links and Git metadata are
omitted, and backend-private `.config` and `.agentflow` paths remain denied.
Git status plus working-tree, branch and committed diffs use native Git data
scoped to that selected profile; patch text is displayed as text in the
workspace review area. Conversation history and approvals stay with the
middle chat, while live tool and file activity appears in the right sidebar;
provider and discovered-model selection stay below the composer. Agent edits still appear as approval
proposals, and full editor, multi-file diff and recovery workflows remain open
xMind implementation work.

## Workspace selection

### Multi-folder workspace target

A workspace is a native-owned group of folders, not one folder profile. The
user creates a workspace by choosing its first existing folder; **Add folder**
then attaches another folder to that same workspace. The workspace selector
switches between these groups, while the workspace canvas shows and lets the
user manage their member folders. It must not create a new workspace whenever
an additional folder is attached.

Every member folder is canonicalized and opened by the native host. The user
explicitly grants each folder by adding it. Tool paths identify a member folder
and a relative path, so equal relative paths in different folders stay
unambiguous. A run captures the workspace membership revision at admission;
adding or removing a folder cannot retarget work that is already running.
Process working directories and language-server roots are selected from that
same captured membership. File reads, edits, Git operations, skills,
instructions, and process execution must enforce the corresponding opened
folder handle and approval boundary.

The local browser now implements membership creation and read-only browsing:
**New workspace** chooses the primary folder, **Add folder** attaches another
folder to the same workspace, and each member is shown in its own file tree.
Folder reads use the member folder ID and native handle identity. Native agent
reads, edits, patches and processes use the admitted member-folder snapshot;
Git and language-server requests can select a member by folder ID. An omitted
folder ID defaults to the primary folder. See the [multi-folder contract](native-multi-folder-workspaces.md)
for supported forms, authority checks and remaining rendered acceptance. VS Code can add
another folder from its opened workspace through **xMind: Add Opened Folder to
Workspace**; this does not create a `.code-workspace` file or change the primary
root. The CLI uses one primary `--workspace` root and can initialize its
workspace, inspect its members and attach additional folders through the shared
native membership registry.

### Current multi-folder integration and remaining gaps

The browser shows its active local profile, offers a selector for other local
profiles and provides **New workspace** for an existing folder. The selected
folder becomes that workspace's primary folder; **Add folder** adds more
folders while keeping the selected workspace and session. The browser canvas
can preview files from each attached folder. In the profile-aware local backend,
single-agent file reads, listings, searches and globs can select an attached
folder by ID; ordinary root runs capture the membership revision and child
agents reuse that snapshot. Direct graph read nodes and graph-agent children
use the membership captured when the graph run is admitted. Editing, creation,
patches, processes, Git and language-server requests can target an admitted
member folder; omitting its ID defaults to the primary folder. Actual browser
coding has edited an attached Vue project and completed its production build.
See the [membership contract](native-multi-folder-workspaces.md) and
[installed attached-folder coding evidence](evidence/rendered-coding-0.1.35-20261019.json)
for the distinct native and rendered scopes. The CLI can initialize a workspace
from one folder and attach more folders to it. Unlike VS Code,
a browser tab has no opened-folder context. On the profile-aware `xmind view`
host, the selector is populated from the native protected profile
catalogue and displays the workspace name and root; **New workspace** is
enabled when that host supports creation. When VS Code opens the browser
for its single-folder `serve` backend, the adapter reads the authenticated
workspace and exposes it as a fixed one-item selector. That keeps the opened
folder attached to the browser without pretending that this backend can add or
switch local profiles.

Workspace selection must be owned by the native host. The browser sends an
opaque workspace selection for switching and a user-entered folder path for
workspace creation. The host resolves and canonicalizes both on the native side,
checks them against its protected profile-storage boundary, and binds the view
to a workspace-specific backend and profile before exposing sessions or
admitting work. A workspace change must refresh the view's session,
run, model and workflow observations against the new authority; it must not
retarget an already admitted run. A browser directory picker cannot grant the
server access to the browser user's local files. For a Nexus connection, choices
must come from server-authorized workspaces, not the client machine. `xmind
workspaces` and the local view API enumerate valid profiles under the protected
profile root. The native host validates each DPAPI record, workspace identity
and storage boundary; missing or damaged profiles are omitted. **New workspace**
accepts one absolute local or UNC folder path over the same-origin browser
session. The native host opens and canonicalizes the existing directory,
rejects inaccessible roots and overlap with private profile storage, creates or
reuses its protected local profile, then rotates the browser's HttpOnly
credential onto that workspace. Workspace creation and profile switching are only
available through the view adapter, which keeps the rotated credential out of
page JavaScript. The local web UI cannot browse the client's filesystem; the
submitted path refers to the machine running the native host. Selecting a
profile attaches its own backend and reloads that profile's sessions, runs,
models and graphs.
The VS Code view remains bound to its original workspace, and runs already
admitted on either backend stay there. Native two-profile switching and stale
credential rejection pass the local-view contract; browser selection,
workspace creation routing and cookie rotation pass the webpage contract.

## Start

The main `xmind.exe` backend hosts FastAPI through embedded xlang3. Python
calls the bound native request handlers directly. Requests for another local
workspace use standard xlang3 IPC and captured native SDK objects. The browser
uses one loopback origin. Node.js is used for frontend builds, the required
VS Code extension adapter and independent test peers.

The VS Code command **xMind: Open Browser View** launches `xmind web` for the
selected managed profile and opens its credential-free URL. The runtime must
bundle the web libraries; prepublish and browser startup verify their manifest.
Installing this updated extension and invoking the command in a rendered IDE
still requires separate package acceptance.

For a development build, keep libraries inside xMind and install them with its
embedded installer. The SDK supplies the compatible native Pydantic wheel:

```powershell
$xmindRuntime='D:\CantorAI2026\xMind\.agentflow\xmind-web-runtime'
$xmindPackages='D:\CantorAI2026\xMind\.agentflow\web-python\site-packages'
& "$xmindRuntime\xmind.exe" pip --isolated install --only-binary=:all: --no-compile --no-input --find-links "$xmindRuntime\wheels" --target $xmindPackages -r views/browser/requirements-web.txt
node views/browser/build.mjs .agentflow/xmind-web-assets
& "$xmindRuntime\xmind.exe" web --workspace D:\path\to\project --assets D:\CantorAI2026\xMind\.agentflow\xmind-web-assets --packages $xmindPackages
```

Release staging copies those verified source libraries into the native runtime's
`modules` directory with `--web-library-source DIR`; a bundled runtime needs no
`--packages` argument. The FastAPI adapter source is `views/browser/xmind_web.py`.
No separate Python interpreter or Node HTTP host is started.

`Tools/start-browser.ps1` provides the same native launch with `-Workspace`,
`-RuntimeDirectory`, `-Assets`, optional `-Packages`, `-ProfileRoot`,
`-ProviderConfig` and `-McpConfig` parameters. Configuration is explicit and
native-owned. Omitted configuration keeps the recorded profile settings.
Use `--upgrade-backend` (or `-UpgradeBackend`) to apply a changed runtime or web
configuration through native quiescence, retirement and replacement. Busy or
uncertain work is preserved. The public port remains stable across a completed
managed handoff. Closing the launch command or page leaves native work alive.

The page automatically enrolls or restores its HttpOnly, SameSite=Strict native
session. A stale cookie after an owned restart is renewed without requesting a
server token or provider key. Workspace changes rotate that cookie. Native
C++ remains responsible for identity, permissions, execution and persistence.
The ASGI layer handles bounded HTTP requests, route/query admission, static
assets, security headers and incremental event delivery. Disconnecting a reader
closes its observation without replaying or cancelling the admitted work.

The previous launcher and acceptance narrative is retained in the
[2026-10-28 checkpoint archive](development-checkpoints-20261028.md).

## Historical browser checkpoint

The real browser completed the trusted `read.repository.file` graph with human input `{"path":"README.md"}`. Reload and reconnect restored completed run `8a164a082bb81d5ecd39853c5f979b0d`, its history and selected workflow. No model response or token usage was fabricated.

- Browser controller and connection/resize tests: 10 passed, zero failures/skips, including network abort/immediate retry and restoring the view independently of slow provider discovery.
- Shared extension tests: 63 passed, zero failures/skips.
- Native browser contract: real native graph/file execution, human pause/input, authentication/origin/route boundaries, disconnect without cancellation, reconnect without replay and two view adapters observing the same history/model catalogue passed. Clipboard/navigation in this contract are explicit host fixtures.
- VSIX packaging and verification: required access/renderer/license assets and shared-backend command present, private launch state excluded. An initial package attempt failed because repository metadata was missing; the metadata was corrected before successful packaging.
- Native runtime used the previously validated build from checkpoint `67de5b16a8732477606957cb52736df06f078f16`; no native source changed in this browser checkpoint.

Desktop and Settings screenshots: [desktop](evidence/browser-native-desktop.png), [Settings](evidence/browser-native-settings.png). Test output is preserved in `doc/evidence/browser-*.log`. The Windows CI workflow runs browser validation after building the native server.

Actual browser acceptance covered Cancel with and without an existing connection, the SVG controls and pointer dragging from 420 to 572 pixels at a desktop viewport. Temporary viewport testing was reset afterward. Live response output and metrics were observed after the user's own provider enrollment. Private browser screenshots remain in the ignored preview directory rather than the public evidence set.

At that historical checkpoint, repeated development reconnect prompts came from restarting the access adapter to load UI changes. Validated asset reload then removed that cause for UI refreshes. The later durable-session checkpoint also preserves login across same-origin adapter/backend restarts. Expiry, authority rotation and explicit Disconnect still revoke access. Reconnect cancellation aborts in-flight requests and permits an immediate new attempt; stale cleanup cannot unlock or overwrite a newer attempt. Provider model discovery is asynchronous after native state restoration.

2026-10-16 live webpage coding acceptance: the browser UI reviewed and approved
a one-file `HomeView.vue` change in TestProj, then ran the Vite production build
through xMind's native process tool (37 modules, exit 0). The browser reused
the configured OpenAI profile without asking for a key or server token. During
the separate rendered-page check, the frontend served successfully but the
TestProj API on port 8000 was unavailable, so catalog stats and results did not
load. See the [sanitized record](evidence/xmind-webpage-live-coding-20261016.json).

The current workspace pane supports operation comparison, not a complete file
explorer/editor. Actual IDE invocation of the shared browser command and broad
frontier-provider coverage remain outstanding. Remote/team view policy belongs
to private Nexus.
