# Browser view adapter

The current browser uses [durable native access sessions](browser-session-persistence.md).
Its opaque HttpOnly cookie survives refresh and access-adapter/backend restarts
at the same origins within the eight-hour expiry, without replaying execution.
The earlier `51724ab` backend-only restart contract is retained as
[historical evidence](evidence/browser-native-restart-contract.log); that older
adapter still used an in-memory login map. Current live two-child browser
acceptance has its own [source and scope](native-responses-reasoning.md).
The combined browser and VS Code contract suite passed **326/326** in its
[captured run](evidence/browser-web-contract-20261010.log). The latest isolated
source-integrity workflow passed **264 VS Code and 63 browser contracts**;
all 49 source/vendor files and 12 generated browser assets remained unchanged.
See the exact [run and artifact evidence](evidence/view-contracts-c7fbc194.json).
Live local auto-connection was also verified against the running native backend
and in the rendered browser view.

xMind has a local HTML view using the same sidebar renderer and native API client as the VS Code extension. The desktop layout places the agent sidebar on the right, Settings at its top, and the discovered-model selector beside the composer at the bottom. Drag the divider to resize it; arrow keys, Shift+arrows, Home and End also resize a focused divider. Its width is remembered as a non-secret appearance preference in localStorage. Narrow windows use the available width.

Connect and Disconnect are accessible SVG icon buttons. The OSS page connects directly to the xMind runtime on the same computer; it has no server-token field. The local launcher enrolls the browser session with its private credential, while the durable session stays in an HttpOnly cookie. If a native restart invalidates that cookie, the local adapter renews it automatically from its private launch credential. If the local runtime cannot enroll the page, the dialog explains how to reopen it from xMind or VS Code. It never asks for a server token. Cancel, close and Escape leave an established session intact; a failed reconnect does not clear the current conversation. Team and remote authentication belong to private Nexus.

When the local view is launched with the trusted `.config/providers.yaml`, it
imports that file into another selected or added workspace profile only when
that profile has no saved provider profiles. Each profile stores its own
encrypted xlang3-backed credentials; existing non-empty provider settings are
preserved. Import performs no provider request, and key values never pass
through the browser.

A profile with a blank `model` is key-only: the backend can discover models
with its encrypted saved key, but a model must be selected before the agent
can run. The sidebar receives the public profile/model list and uses the
selected profile on subsequent requests; it never needs or receives the raw
provider key.

The C++ server owns execution, tools, graphs, permissions, providers and history. SQLite I/O remains in embedded xlang3. The Node HTTP adapter serves validated view-asset snapshots and forwards a finite allowlist of API requests to a configured loopback native server. A page reload loads a complete new snapshot from its configured asset directory without restarting the adapter or losing its authenticated sessions. Boundary, route, size and CSP restrictions still apply; a rejected bundle does not revoke backend access. Closing the view does not cancel execution. This is a local HTTP transport. Team deployment, WebRTC and the standalone Electron IDE belong to private Nexus.

Today, the left side is the workspace canvas: its workspace selector switches
between single-folder local profiles, and **New workspace** registers a chosen
existing folder as another profile on the same xMind node. Runs start in that
selected profile and execute in the xMind backend process. The file list and preview are
read-only native workspace-handle reads; directory links and Git metadata are
omitted, and backend-private `.config` and `.agentflow` paths remain denied.
Git status plus working-tree, branch and committed diffs use native Git data
scoped to that selected profile; patch text is displayed as text in the
workspace review area. The right sidebar holds conversation history, the
active chat, run activity and approvals; provider and discovered-model
selection stay below the composer. Agent edits still appear as approval
proposals, and full editor, multi-file diff and recovery workflows remain open
coding-parity work.

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
Folder reads use the member folder ID and native handle identity. Agent file
tools can read from the captured member-folder set; edits and processes, plus
Git and language-server routing, still use the primary folder. VS Code can add
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
agents reuse that snapshot. Editing and creation proposals, processes, Git,
language-server roots and graph-node runs still use the primary folder. Unlike VS Code,
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

In the updated VS Code extension, run **xMind: Open Browser View**. It validates the selected backend, starts a loopback view adapter for that same origin, privately enrolls a browser session in memory and opens a credential-free URL. Models and history remain on the same server as the editor. This command does not create a second database or copy provider credentials. The launcher reuses its view for the same backend and closes the old access adapter when the configured backend changes. Actual IDE invocation of this newly added command remains pending; host contracts, native sharing and packaged assets are verified.

From the repository, with the native build and extension dependencies available:

```powershell
node views/browser/build.mjs
node Tools/start-browser.mjs --runtime D:\path\to\verified\native-runtime --port 60405
```

The runtime must be verified and contain the same `xmind.exe` as the current native build; the launcher rejects stale packages. It starts `xmind view` with the shared local profile root and a protected native ready file, then attaches the HTML adapter to that profile-aware host. Do not point `--backend` at generic `xmind serve` when workspace selection is expected. To attach the adapter to a view host already running, pass that host's loopback origin with `--backend`.

For local language-server support, put trusted profiles in
`.config/language-servers.yaml`. The profile-aware browser launcher passes that
file to the native view automatically; use `--language-server-config FILE` to
select a different trusted file. `Tools/start-ui.ps1` accepts the same switch
and otherwise uses the repository-local file when present. Both launch paths
keep the executable and its arguments in owner configuration, never in page or
model input.

The profile-aware local launcher keeps its generated access credential in the private launch-state directory and enrolls the page server-side. The VS Code launcher passes its already authenticated local credential directly to the loopback adapter in memory. The page never receives the server credential in JavaScript, a URL, clipboard or browser storage, and users do not enter one. The adapter returns the durable native view credential as an HttpOnly, SameSite=Strict cookie. Reload reconnects with that cookie, which lasts up to eight hours across browser close/reopen and same-origin adapter/backend restarts. Expiry, authority rotation or explicit Disconnect revokes access; Disconnect does not cancel native execution. Only selected session/run/model/workflow IDs are saved in sessionStorage; keys and conversation content are not stored there.

The standalone page attempts cookie-session restoration when it loads. This
uses the same-origin session endpoint without an Authorization header. When
launched by the local xMind or VS Code host, the adapter enrolls a fresh browser
session using its private in-memory local credential. If the browser has no
valid local enrollment, the page reports that the local runtime needs to be
reopened from xMind or VS Code; the user is never asked for a server token.

Live browser smoke verification used the actual native profile-aware host and rendered both `xMind` and `TestProj` in the selector. Switching to `TestProj` changed the selected backend and cleared its session list; switching back restored the xMind workspace. No model request or file effect was issued. The smoke test also caught and fixed same-origin GET handling: browsers omit `Origin` on same-origin catalogue reads, so the adapter allows that read while workspace-changing POST still requires the exact view origin. The in-app browser's fetch rejected one abort-signal wrapper; the view retries only that specific pre-request compatibility error without the signal, while normal browsers retain cancellation and timeouts. Workspace timeout and cancellation signals use `AbortController` directly so implementations whose `AbortSignal` static helpers throw `Illegal invocation` still allow profile switches; a browser regression contract exercises that failure mode.

On 2026-10-10, rendered webpage acceptance submitted the existing
The first rendered acceptance used a profile-aware host whose binary differed
from the current build; its exact scope remains in the
[initial live evidence](evidence/browser-workspace-live-20261010.json). A later
run used the current `xmind.exe` hash and the real standalone webpage at a
648-pixel viewport. After a page refresh, its HttpOnly session restored without
entering any server token. **Add workspace** opened the workspace dialog,
registered `D:\CantorAI2026\xMind` alongside `TestProj`, and selected it. The
selector then switched back to `TestProj`; the picker re-enabled and the page
showed the TestProj native workspace. The Add and Connect controls remained
separate at the narrow viewport. No provider request or workspace file change
was issued. This proves the browser needs its own **Add workspace** flow
because it has no VS Code opened-folder context. See the
[current-build rendered acceptance](evidence/browser-workspace-live-current-build-20261010.json).

Cookie authentication requires browser same-origin fetch metadata, while session enrollment and revocation also require the exact view Origin. Cross-origin requests remain rejected. Session identifiers are not accepted in URLs. There is a 32-session limit and bounded session-request bodies. This cookie is for the loopback HTTP development transport; remote access still requires a separately designed HTTPS/team authentication adapter.

Settings supports provider enrollment and model discovery through the native API. A separate development profile initially starts without a provider key; the existing VS Code preview's saved key is not copied. The user has now configured this webpage profile and completed a live OpenAI Chat Completions response. Its actual rendered and native-persisted usage was observed: 3,679 input, 789 output and 4,468 total tokens, model `chat-latest`, first token 1,425 ms and elapsed 7,200 ms. [Metadata-only evidence](evidence/browser-live-response-metadata.json) excludes conversation content and credentials. This proves one live response and its usage rendering, not full coding/provider parity.

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

The current workspace pane supports operation comparison, not a complete file explorer/editor. Full coding acceptance, actual IDE invocation of the shared browser command and broad frontier-provider coverage remain outstanding. Remote/team view policy belongs to private Nexus.
