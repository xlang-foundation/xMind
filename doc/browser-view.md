# Browser view adapter

The current browser uses [durable native access sessions](browser-session-persistence.md).
Its opaque HttpOnly cookie survives refresh and access-adapter/backend restarts
at the same origins within the eight-hour expiry, without replaying execution.
The earlier `51724ab` backend-only restart contract is retained as
[historical evidence](evidence/browser-native-restart-contract.log); that older
adapter still used an in-memory login map. Current live two-child browser
acceptance has its own [source and scope](native-responses-reasoning.md).

xMind has a local HTML view using the same sidebar renderer and native API client as the VS Code extension. The desktop layout places the agent sidebar on the right, Settings at its top, and the discovered-model selector beside the composer at the bottom. Drag the divider to resize it; arrow keys, Shift+arrows, Home and End also resize a focused divider. Its width is remembered as a non-secret appearance preference in localStorage. Narrow windows use the available width.

Connect and Disconnect are accessible SVG icon buttons. The connection popup supports Cancel, its close button and Escape. Cancelling keeps an established connection and its session; with no connection it closes the popup and leaves the view disconnected. A rejected replacement token or a late response after Cancel cannot clear the existing conversation.

The C++ server owns execution, tools, graphs, permissions, providers and history. SQLite I/O remains in embedded xlang3. The Node HTTP adapter serves validated view-asset snapshots and forwards a finite allowlist of API requests to a configured loopback native server. A page reload loads a complete new snapshot from its configured asset directory without restarting the adapter or losing its authenticated sessions. Boundary, route, size and CSP restrictions still apply; a rejected bundle does not revoke backend access. Closing the view does not cancel execution. This is a local HTTP transport. Team deployment, WebRTC and the standalone Electron IDE belong to private Nexus.

## Start

In the updated VS Code extension, run **xMind: Open Browser View (Copy Connection Token)**. It validates the selected backend, starts a loopback view adapter for that same origin, copies its connection token to the clipboard and opens a credential-free browser URL. Paste the token into Connect once. Models and history remain on the same server as the editor. This command does not create a second database or copy provider credentials. The launcher reuses its view for the same backend and closes the old access adapter when the configured backend changes. Actual IDE invocation of this newly added command remains pending; host contracts, native sharing and packaged assets are verified.

From the repository, with the native build and extension dependencies available:

```powershell
node views/browser/build.mjs
node Tools/start-browser.mjs --port 60405
```

The launcher creates a dedicated `.agentflow/browser-ui` profile and runtime snapshot, preserving the existing VS Code previews. Its private `auth.token` authenticates the browser to that native server. Enter that token once in Connect; provider API keys belong in the top-right Settings dialog afterward. After validating the token, the access adapter requests a durable native view credential and returns it as a HttpOnly, SameSite=Strict cookie. JavaScript does not retain the master token or view credential. Reload automatically reconnects with that cookie. The browser remembers it for eight hours, including browser close/reopen and same-origin adapter/backend restarts. Expiry, authority rotation or explicit Disconnect revokes access; Disconnect does not cancel native execution. Only selected session/run/model/workflow IDs are saved in sessionStorage; keys and conversation content are not stored there.

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
