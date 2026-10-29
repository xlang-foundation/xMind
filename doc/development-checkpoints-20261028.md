- Preview57780: current-file/change card with active-only motion and paused approval preview; edit_file/replace_file alias fixed. Terminal bottom-right, toggle in right toolbar.336 tests pass. Real UI session0d86307ecb1a48d69966dff5c1d146e6 completed2turns/8tools; report66.50/Mug-only low stock verified; NEVER replay roots in doc/evidence/webui-current-work-real-followup-20261027.json. Gemini pricing unavailable; prior$1.520440 excludes these2turns/manual; cap$100. Owners .agentflow/composer-layout-preview/live.json unchanged; backend22920/installed80/original63573 retained. Browser uploads NOT IMPLEMENTED. Earlier names/archives/NEVER-replay: doc/development-checkpoints-20261027.md.

- Preview57780: compact32px filename/action rows replace large activity card; concurrent ops visible, scan only while working, details on demand. MCP aliases resolve tool/server names; approval/input stop motion.337 browser/shared checks pass. Actual UI/history proof doc/evidence/webui-compact-activity-20261028.json; no new inference. Terminal remains bottom-right/right-toolbar. Owners .agentflow/composer-layout-preview/live.json unchanged; backend22920/installed80/original63573 retained. NEVER replay session0d86307ecb1a48d69966dff5c1d146e6 roots in prior proof. Gemini2turns unpriced; prior$1.520440 excludes them/manual; cap$100. Browser uploads NOT IMPLEMENTED. Older constraints archived20261027/28.

- Search51253: Exa web_search_exa configured in isolated profile; actual UI sessionf9f1321179144314b7b603121a8ea703/root97c7b11f740b46d7b6d5bed70eb0569a completed2 approved searches; NEVER replay. Activity Review/animated Calling/Called and exact reload restore verified. Proof doc/evidence/webui-web-search-mcp-20261028.json. Owners .agentflow/web-search-preview/live.json (backend36816/view45972/Node26144) retained. Old57780/backend22920 retained: trusted MCP handoff refused unresolved operation; pending upgrade intent not cleared. Priced subtotal$1.561452 excludes2unpriced Gemini turns/manual; cap$100. Old constraints archived20261027/28; browser uploads NOT IMPLEMENTED.

- Search51253: Inventory + search now includes ui-layout-check folder. Chat Inventory web search/session179a9c1c67ba432ba42cebfe0c074f44/root235c5d57e2fd4a4b920392e54fb200ae read CSV and called Exa4times; NEVER replay. Notebook rate-limited; other generic listings are NOT verified inventory price matches. All3 project hashes unchanged. Proof doc/evidence/inventory-web-search-20261028.json. Old57780 chat lacks Exa and remains preserved; no history migration. Owners .agentflow/web-search-preview/live.json retained. Priced subtotal$1.631670 excludes2unpriced Gemini turns/manual; cap$100. Earlier setup/owners/NEVER-replay archived20261028; browser uploads NOT IMPLEMENTED.

## Superseded Node browser launch documentation (verbatim)
## Start

In the updated VS Code extension, run **xMind: Open Browser View**. It validates the selected backend, starts a loopback view adapter for that same origin, privately enrolls a durable browser access session and opens a credential-free URL. Models and history remain on the same server as the editor. This command does not create a second agent-history database or copy provider credentials. The launcher reuses its view for the same backend and closes the old access adapter when the configured backend changes. Actual IDE invocation of this command remains pending; host contracts, native sharing and packaged assets are verified.

From the repository, with the native build and extension dependencies available:

```powershell
node views/browser/build.mjs
node Tools/start-browser.mjs --runtime D:\path\to\verified\native-runtime --port 60405
```

The runtime must be verified and contain the same `xmind.exe` as the current native build; the launcher rejects stale packages. It starts `xmind view` with the shared local profile root and a protected native ready file, then attaches the HTML adapter to that profile-aware host. Do not point `--backend` at generic `xmind serve` when workspace selection is expected. To attach the adapter to a view host already running, pass that host's loopback origin with `--backend`.

To reopen an existing profile using its recorded configuration, add
`--config-mode saved`. This skips automatic discovery of workspace provider,
language-server, process and MCP configuration files. Explicit replacement
configuration paths are refused in this mode. The default `workspace` mode
retains the existing discovery behavior.

To explicitly update the managed backend runtime while retaining that profile,
add `--upgrade-backend true`. This uses the native owner's existing upgrade and
busy-owner protections. Merely attaching with `--backend` cannot request an
upgrade. Normal launches default to `--upgrade-backend false`.

Launching again with the same state and matching asset directory rebuilds the
generated UI files, then reuses the existing browser host and URL. Reload that
page to load its new validated snapshot. This does not restart the native
backend, reimport provider settings or resubmit work. A different backend or
asset directory is rejected before writing those files. A timeout or unavailable
HTTP observation preserves the existing host and reports that it could not be
observed; a refused connection permits a new attachment. Do not infer backend
death from a slow page probe. These UI refresh rules do not apply a new native
runtime or backend configuration.

Launching again with the same state directory reuses its live browser URL and
does not start another view. If the existing adapter is attached to a different
requested backend, startup stops with an explicit message; use a separate state
directory for a second backend.

The launcher owns the native view it starts. Press Ctrl+C in its console to
close the browser adapter and retire that native child; Ctrl+Break is also
handled on Windows. When `--backend` attaches to an existing server, shutdown
closes only the browser adapter and leaves the shared backend running.

For local language-server support, put trusted profiles in
`.config/language-servers.yaml`. The profile-aware browser launcher passes that
file to the native view automatically; use `--language-server-config FILE` to
select a different trusted file. `Tools/start-ui.ps1` accepts the same switch
and otherwise uses the repository-local file when present. Both launch paths
keep the executable and its arguments in owner configuration, never in page or
model input.

For MCP servers, the profile-aware launcher accepts `--mcp-config FILE` and
otherwise uses the repository-owned `.config/mcp.json` when present.
`Tools/start-ui.ps1` accepts `-McpConfig FILE` for its development backend.
These are trusted host settings, never paths chosen by a page or model.
Managed profiles keep the imported list when the source file is absent.
Apply changes through the console's `--mcp-config FILE --upgrade-backend` or
VS Code's **xMind: Apply Trusted Runtime Configuration**; ordinary reconnection
does not silently replace the configured server commands.

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

## Embedded web IPC checkpoint before asynchronous request jobs

- Embedded web: xmind.exe hosts FastAPI/xlang3, direct C++/cached SDK IPC; bundled libraries .agentflow/web-python/site-packages. UI58613 owner28948; peer35656 unchanged. Node51253/57780 stopped; preserve their native backends. Completed roots in .agentflow/embedded-web-task.json and .agentflow/session-loading-task-20261028.json: NEVER replay. Native base: doc/evidence/embedded-web-native-20261028.json. IPC/restore: doc/evidence/browser-session-load-ipc-20261028.json.
