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

## Web request performance checkpoint before workspace session isolation

- Embedded web: FastAPI/xlang3 inside xmind.exe; bounded native jobs, cached SDK IPC. UI58613 main21648; peer35656 unchanged; old28948 exited. Node51253/57780 stopped; preserve native backends. NEVER replay roots in .agentflow/embedded-web-task.json and .agentflow/session-loading-task-20261028.json. Libraries .agentflow/web-python/site-packages. Validation/owners/fixture: doc/evidence/browser-navigation-speed-20261028.json; previous guidance archived verbatim in doc/development-checkpoints-20261028.md.


## Verbatim guidance preserved on 2026-11-03

- Auth fix installed0.1.80, native sourcef79c3f2327e1557f7352dfc844a0673bf8647c35, NativeSHAdaef39ff/VSIX6d8ad252/manifestb561039d: full185 Native/519 extension pass; actual Code web-host reload and fresh window reconnect without another sign-in, same Vue root from2folders and exact full rendered history. Installed2381 files match archive. Five user-state roots, old79 original2380files, required75/76 backups2442each and7 native owners unchanged. Native DPAPI+embedded-xlang3 SQLite caches browser-host client tokens; desktop SecretStorage retained. Code tabs10/11/server63300/view17628 are retained outputs; no chat turns or effects replayed. Exact ownership in .agentflow/auth-persistence-validation/code/live.json. Earlier-run picker still resets to latest; separate finding. See doc/evidence/vscode-auth-persistence-0.1.80-20261025.json.

- Current live UI projects: Python sessiond86051f1c0654a09b0083f689135a15c has3 completed turns5e2b3897162f4303b5e8b78c008a1809/c1ff055e7fa84ed4a7df16a35ecc9720/95f6ecf9c68544d6ba5b24a82a1032d3, actual xlang3 tests6 then9, sample1980cents/2rows, exact history/model/workspace reload pass. NEVER replay. C++ session915e28d27d6e41aeba40ad78a19e3e76 configure1ed2dd3bcb2f4474a4c0e64c684b733c and builda2e0891c38ee43ebacdc61f183e7e42e timed out with exit0/1; trees retired, build quarantine retained. Missing<string> is real; descendant cause unproven. Vue session0a57e2e25a694fc7b33746a2f7832e0d has3 completed turns f2c75f56aba747ffa1d9f9a68e91328c/b18029b9240849518f0e17e6749129af/d8624097c83d4249b38f74f4a7faed13: NEVER replay. Turn2 was guided; turn3 ordinary feature request. Final tests/build and app/reload checks pass; search layout overflows77.5px. C++ compile/timeouts and UI findings remain open; authentication is fixed in80. Cost full-rate conservative$1.465176 of$100. Current state: .agentflow/webui-project-tests-20261025/current-task.json.

## Preserved pre-host-manifest continuation — 2026-11-03

- 2026-11-03: Native 189/189 passes; 20,460 frozen inputs unchanged. Fixture TEMP/TMP belongs under build/skill-state-native/test-temp-20261103, NEVER under reserved .agentflow. Original setup failures retained. Live-model/client/package acceptance pending: doc/evidence/feature-acceptance-20261103.json.
- SDK 87512a5e in sparse .agentflow/xlang3-windows-exports leaves sibling main unchanged; see doc/evidence/sdk-integration-20261103.json for its build overlay. SDK 57 permitted tests / 11 performance cases pass. Initial accidental CPython pickle oracle was reported/retained. Keep the interpreter probe empty, bridge OFF and xlang3_cli_pickle_interop excluded/disabled; audit transitive wrappers. Python work and pip stay xlang3-only.
- CantorServe19445/v1: exact27B discovery/save/reload passes. Provider refresh reset Custom server to OpenAI; fixed with221/536 client passes. Dedicated test key revoked; replacement enrolled. See doc/evidence/provider-route-refresh-20261103.json. Preserve other keys, quota, external browser and Cantor identity. Never log credentials or replay the active expense-project root.

## Preserved pre-reimport continuation — 2026-11-03

- Native10db0fcb fixes host manifest and missing-parent approval guidance. First183/189 had five ASGI-env setup failures and one Rust90s failure; corrected7/7 passes. Full189 passes with23907 inputs/environment/tools unchanged. See doc/evidence/native-host-path-and-creation-20261103.json.
- Cantor19445/v1 setup/save/reload passes after221/536 routing fix. New test key revoked, replacement enrolled. NEVER replay expense roots9d2dfdde884d4fcbbcbc8adb0aabf266 or5d218554c80f4333b150d011cd226477: both failed, seven complete new model responses/eight missing-parent calls, no file effects. Preserve keys, quota and external browser.

## Superseded early November 3 checkpoint

- Native92c126d2 qualified189/23907 inputs;4aa98dfb fixes first-handoff explicit process reimport (red60.62s, green118.41s), full189 passes/23907 unchanged inputs. Live owner46516 at57048/57058 recovered by supported resume with exact25-message preservation; latest fix not deployed. See doc/evidence/explicit-process-reimport-20261103.json.
- SDK e1491db3 in .agentflow/xlang3-windows-exports:58 permitted/11 fixed perf pass; engine unchanged, sibling main clean. Earlier manifest was not embedded. Keep interpreter probe empty, bridge OFF, CPython pickle oracle disabled/excluded; audit wrappers. Initial incident retained in archive.
- Cantor19445/v1: setup/save/reload passes after221/536 routing fix; replacement key enrolled. NEVER replay roots9d2dfdde884d4fcbbcbc8adb0aabf266,5d218554c80f4333b150d011cd226477 or948c88ab48e24126808616a713b5311e. Third hit deadline after2approved package files and xlang3 pytest exit1 (no pytest); all3original files unchanged. No project pass. Preserve keys, quota, browser.
- User: keep existing run/staging/profile/workspace paths; no shortening workaround or duplicate test runtimes. Cleanup review blocked three directories (~726MiB); nothing deleted. Never retry them through another route.


## Older acceptance and owner references moved November 3

- Candidate74 editor updates clean buffer before reload; selected-context coding and history restore pass. NEVER replay session99b136d7bb544013a4d98bbc72705860/rootc3266a85e6704444b2c640e05babc442. See doc/evidence/candidate74-rendered-editor-coding-20261024.json.

- Candidate67 completed coding/handoff roots remain in doc/development-checkpoints-20261024.md verbatim; NEVER replay.

- Installed67 smoke55a89cad4783844c55b74fccd45ea580/root8866649c22b59f0793866858907107f4 completed read/history, zero models; NEVER replay. Owner exited. Installation66 guidance archived verbatim in doc/development-checkpoints-20261024.md.

- Browser first-send permission fix passes clients485/169 and actual UI without reload. Installed68 includes the fix. Both completed read/grant/revoke fixtures are in doc/evidence/browser-first-send-permission-refresh-20261024.json; NEVER replay.

- Qualification58 and completed browser58 guidance is preserved verbatim in doc/development-checkpoints-20261024.md; NEVER replay its tasks.

- Native55 CLI sessionf5eebd145890d132cfc59b0589491803/rootcc3d14ff5b4c570cebf41a92c85c6d52 completed2reads/1approvededit/1process, Node4/4,9messages. NEVER replay. Previous6sessions/150messages/11runs/configuration exact, owner56032 retained. See coding55-cli-live-coding evidence.

- Historical51/53/54 owner, history and NEVER-replay rules remain verbatim in doc/development-checkpoints-20261024.md. Preserve those completed tasks.

- Historical hosted view-count failures and corrected gates remain in doc/evidence/hosted-view-inventory-failure-20261019.json; those receipts are not new runtime acceptance.

- Historical e09259c4 qualification guidance is preserved verbatim in doc/development-checkpoints-20261024.md.

- Hosted39309088086/9c5bedbb is terminal failure154/155 in705.36s: graph PowerShell30051ms timeout, script_entered only, uncertain receipt/tree retired; SDK2/2. Official artifact12218800656/69559 bytes SHA1d68fe7a verifies. This is installed44 native source, not the new module policy; no hosted acceptance. See doc/evidence/hosted-native-9c5bedbb-process-timeout-20261019.json.

- Historical native and hosted timeout checkpoints (838e2eb, 9e4b118, 0c93640/a418 and f3e7718) remain in doc/development-checkpoints-20261018.md and doc/evidence. Failed cold commands are not qualified by later warm controls. Do not cancel/restart for observation delay, weaken deadlines or combine focused results into full acceptance.

- Keep VS Code closed and open no visible editor/browser windows unless asked. Earlier desktop control failed with black capture/GetCursorPos access denied; do not repeat unlock questions. The new Code pane observation is separate from still-unverified visible-browser acceptance. Headless sandboxed Edge development checks are separate evidence. Preserve all older user pages/owners, including ports 62084, 54662, 51118 and 52757. Exact older process identities, historical receipts and installation details remain in doc/development-checkpoints-20261018.md and doc/evidence. Verify PID, creation time and image before any intentional retirement; no mass cleanup of user owners, views or profiles.

- CLI59 owner68928/62378 session3b25f9b24e1fdc7e66aae877d42cf5f4/root0c35d911e7417e3c4982726fbb2fb201 completed wrapIndex/Node4/9msgs; NEVER replay. CLI60 owner48808/49919 (3123/5100, process-code) sessionfa20550e632f0522be7bfd4b0643c8ed/root41707328b40471d556818959016d73fb completed inclusiveRange via actual worker42868 retired: edit/Node4/13msgs/6charges/2 known argument denials. NEVER replay either task. Births/receipts: doc/VALIDATION_STATUS.md.

- Historical installed44/TestProj43/55921/u28 exact owner identities and NEVER-replay namespaces remain verbatim in [the owner archive](doc/development-checkpoints-20261025.md). Preserve all those owners, pages, profiles and completed tasks; global ownership/retirement rules below still apply.

<!-- End of verbatim November 3 checkpoint archive. -->

## 2026-11-03 pre-recovery checkpoint (superseded)

- Current: Native3caa54ef passes full189 in897.40s/23907 unchanged inputs. New Native bytecode-inheritance guard passes focused2 in13.32s/23911; its full189 is pending. SDK candidate CLI policy and two missed Unicode package-path conversions pass60; fixed11 performance passes with matched empty caches; original asymmetric-cache failure retained. Sibling main6186ffa6 is clean. Keep CPython bridge OFF/interpreter probe empty and pickle oracle excluded. See current-task.json under .agentflow/blockers-20261102 and forthcoming bytecode evidence.
- Cantor19445/v1 project6tests/pretty/help/version pass under xlang3, but worker deadline prevents complete-turn acceptance. NEVER replay roots9d2dfdde884d4fcbbcbc8adb0aabf266,5d218554c80f4333b150d011cd226477,948c88ab48e24126808616a713b5311e,753db94c51a54791b850ecbd74ba2d65 or4f0c5547d86c414db02b5af7c1069324. Original3files unchanged. Latest version-only edit succeeded; pytest absent, unittest approval cancelled at deadline. Preserve keys/quota/history.
- Currentowner40016 at57048/57058: upgrade rejected because project subprocesses added66unlisted stdlib caches; all2875manifested files unchanged. No cleanup/retirement yet. Preserve until qualified recovery. Source editor approved edit but clean buffer stayed stale; no forced revert/dirty-buffer overwrite. CodeServer57118 stays; its test tab closed to release SDK IPC bridge. Installed80/backups unchanged.
- Keep existing run/staging/profile/workspace paths. Cleanup review blocked three directories (~726MiB); nothing deleted. Never retry those targets through another route.

## 2026-11-03 superseded SDK checkpoint (verbatim)

- Candidate SDK499415d0 at .agentflow/xlang3-host-guard-sdk passes SDK5/default perf11 and complete Native172 at17be6368. Source: .agentflow/xlang3-ctypes-runtime-fix; build: .agentflow/sdk-host-guard-candidate/build/Release. Prior .agentflow/xlang3-sdk f809 and baseline .agentflow/xlang3-ctypes-baseline retained. Installed61 unchanged; fresh delivery pending. Original sibling SDK untouched. See doc/VALIDATION_STATUS.md.

## 2026-11-03 superseded serving, lifecycle and cleanup checkpoint (verbatim)

- Cantor19445/v1: six existing expense tests/pretty/help/version passed in an earlier terminal run, but no complete-turn acceptance. Latest rootd114abd36fd84c4f9c3ac3a2dda54ffb approved version0.1.1 edit, then second model attempt2387419832e0565872a07a67f86807d2 failed deadline ->dispatch_error with465288ms rootbudget left; no tests ran. Keep120s total/60s idle defaults. Trace .agentflow/blockers-20261102/expense-sixth-events.json.gz. Portal full server request ID/correlation unconfirmed. Do not submit more model turns until serving failure understood.
- Installed83 conversation lifecycle passes: CLI through-fork/export/import17messages; browser before-first fork0, rename/archive/reload/restore;4history pages5/5/5/2 and exact single lookup; stale rename/badarchive rejected. Code imported-copy reload restores without old run/failure. Three owned test copies removed viaCLI; exact original62messages/6roots/538events/13ops/6files and cold5/3/28 unchanged. No inference/source changes/runtime copies. See doc/evidence/conversation-lifecycle-20261103.json. Browser terminal earlier resize/red-green receipts remain in browser-terminal-exit evidence.
- Keep existing run/staging/profile/workspace paths. Automatic cleanup review now blocks10targets: prior3test dirs +6obsolete cold runtime copies +obsolete extracted isolated81. All remain untouched; never retry by another route. Archive81 remains verified/retained for the failed terminal reproduction. Latest deletion command rejected before execution as blocked by policy.

## 2026-11-03 superseded workspace/Git checkpoint (verbatim)

- Candidate83 installed in isolated Code57118 with bundled Native deed03f0/SDK92aa; manifest76ecdb2e, archive4648a7e3. All2922archive/2921installed files verify after activation. Complete223browser/540editor with23912source/23339runtime unchanged. Actual editor exit7/exit0/explicit-close and browser exit0retention/Close pass; no terminals/children remain. Same fixed staging/profile/workspace paths, production80/backups untouched. See doc/evidence/candidate83-installed-20261103.json. Full coding/remaining feature/stock-editor/final release gates pending; NOT accepted release.
- Cantor19445/v1 portal rechecked08:28PST: configured qwen3.8-27b-chat-32k maps to qwen38_27b_nvfp4_chat_32k_v2/GarnetQwen3827BChat32KRTX5090V2 onrtx5090; both providers Unavailable0/0. Other ready profiles/newer usage do not prove xMind repair. Hold new model turns until configured provider ready/serving failure understood; keep120s total/60s idle. Old rootd114/attempt2387419832e0565872a07a67f86807d2 remains uncorrelated;465288ms rootbudget left, no tests. Trace .agentflow/blockers-20261102/expense-sixth-events.json.gz. See doc/evidence/provider-readiness-and-file-context-20261103.json; actual preview/search/draft attach/remove pass, no send, exact original profile/cold unchanged.
- Keep existing run/staging/profile/workspace paths. Automatic cleanup review blocks11targets: prior3test dirs,6obsolete cold runtime copies,obsolete isolated81,and redundant full portal PNG(private state). All untouched; never retry by another route. Archive81 verified/retained. Latest PNG deletion command rejected before execution as blocked by policy; only focused provider panel published.
- Current primary owner38048 birth134381939851373843, runtime-3858e337; supported Code Apply Trusted Runtime Configuration retired exact old45908 identity and preserved API57048/browser57058. View44852 birth134381954430825584 loads installed83. Exact2sessions/62messages/6terminal roots/538events/13ops/6project files unchanged before/after handoff and terminal checks. Cold14932 unchanged. One required managed generation created by product handoff; no extra build-runtime directory. Preserve immediate rollback runtime-96cc8ef9. No direct SQLite edits.

## 2026-11-03 superseded provider hold (verbatim)

- Cantor19445/v1: qwen3.8-27b-chat-32k still maps to qwen38_27b_nvfp4_chat_32k_v2/GarnetQwen3827BChat32KRTX5090V2;08:44PST one rtx5090 provider Unavailable0/0(previously2). Hold model turns until configured provider ready/serving failure understood;120s/60s unchanged. Old d114/attempt2387419832e0565872a07a67f86807d2 uncorrelated; trace .agentflow/blockers-20261102/expense-sixth-events.json.gz. Recheck alias before assuming deployment fixed.


## Candidate84 handoff checkpoint before named-workspace follow-ups (2026-11-03)

## Current work and preserved installation

- Sources181eaec3/4041acf8 fix folder-denial503->403 and Git queries. Native-view1/1,224browser/540editor pass. Full189first188/189(897.68s): fixture mailbox-reader race; testfix4cc2ac56 passes1/1(57.20s), assertions/budgets unchanged. Full recheck running session71247/PID27900, label native-fixture-reader-qualified; freeze inputs until terminal. NOT staged/installed. See doc/evidence/workspace-git-and-folder-denial-20261103.json.
- Dedicated Git workspace owner17052/birth134381979678344628/API62058 in existing live-profiles; required generation10aa605d,0sessions/models. Threadmode/no provider: no agent turns. Browserstatus/CLI patches pass, browserdiff/Code Git pending. Fixture retained for reuse; paths in workspace-git evidence. Original expense4f and exact original/cold snapshots restored.

- Isolated83/Code57118 baseline Native deed03f0/SDK92aa, stage76ec/archive4648a7e3. Archive/installation/terminal/auth/history/conversation checks verify; production80/75/76 untouched. See candidate83-installed evidence. Refreshed package/live fixes/coding/stock-editor/remaining features/final release pending; NOT accepted release.

- Installed83 conversation lifecycle passes: CLI through-fork/export/import17payloads, paging5/5/5/2; browser before-first fork0, rename/archive/reload/restore; stale rename/badarchive reject. Code imported-copy reload has no inherited run/failure. Three owned copies removed viaCLI; exact original62messages/6roots/538events/13ops/6files and cold5/3/28 preserved. No inference/source/runtime copies. See doc/evidence/conversation-lifecycle-20261103.json.

- Cold client-profiles now works: current owner14932 birth134381868661814000 runtime-a1f39a6e, API62774/browser52887. This is a new Native process; the old Node probe with PID14932 is gone. Supported live handoff from45424 preserves exact5messages/3roots/28events and auto-auth/selected older read root. Basic read/human-answer/cancel graphs pass with0models; original project fixtures unchanged. NEVER replay sessione1cc694744574289bfdf0d9744da0a6d roots1fc7ffc280ad4ae2824446399f97b7ea,e9d4b158ec854a3083737a1504471364,74172bab6e8e49beb67bb66542086a6b. IABtab5/source task cold-client-task.json; see doc/evidence/cold-client-graphs-20261103.json. Six obsolete cold runtime copies also policy-blocked (684412569B), all retained; never retry deletion by another route.

- Native deed03f0 request-ID metadata now deployed in stage76ec and primary owner38048; only validated x-request-id/status, before body/failure, through standard xlang3 IPC/journal. Complete189/189 in902.02s with23912inputs/tools/env unchanged. SDK92aa retains60correctness/11fixed cold performance qualification; no SDK changes. Old timeout remains uncorrelated; do not invent server IDs or replay roots. Earlier build memory failure/focused controls in provider-request-correlation evidence. Sibling main6186 remains tracked clean.
- Primary38048 birth134381939851373843/runtime3858; supported handoff retired45908, retaining57048/57058. Codeview44852 birth134381954430825584 loads83. Old62messages/6roots/538events/13ops preserved; now66/7/719/14,6files exact; cold14932 unchanged. One required managed generation; preserve immediate rollback96cc8ef9. No direct SQLite edits. Handoff/terminal evidence candidate83-installed.
- Cantor19445/v1 configured27B providers Ready1/1 each09:18. Fresh model-backed test turn completes:2HTTP200s, approved xlang3-B unittest6/6, no file changes. See doc/evidence/cantorserve-expense-tests-20261103.json for real request IDs/trace. Keep120s/60s; oldd114 still uncorrelated. Full coding/new-package acceptance pending.
- NEVER replay roots9d2dfdde884d4fcbbcbc8adb0aabf266,5d218554c80f4333b150d011cd226477,948c88ab48e24126808616a713b5311e,753db94c51a54791b850ecbd74ba2d65,4f0c5547d86c414db02b5af7c1069324,d114abd36fd84c4f9c3ac3a2dda54ffb or a1c5735019644a9792c98ff5d341e7ec. Original3project files unchanged; only model-approved package edits. Preserve keys/quota/history.
- CodeServer57118 now bundled0.1.83; hidden IABtab4 restores auth/workspace/provider/model/run d114. Only isolated runtimeDirectory override removed; other6original Machine settings unchanged. Source-only82 briefly reported absent bundled runtime during settings transition; installed83 reload restored normally. Stock nested watcher remains independently unqualified (absolute Windows exclusion/parcel regex_error); no vendor patch, forced revert or dirty overwrite. Original80/75/76 untouched.
- Direct browser gates require XMIND_BROWSER_TEST_RUNTIME at verified fixed stage plus XMIND_BROWSER_TEST_HOST/MODULES/STDLIB/PACKAGES and valid TEMP/TMP; run-client-validation.ps1 sets these. Current full223/540 gate uses refreshed stage76ec. Keep CPython bridge OFF, interpreter probe empty and pickle oracle excluded.
- Keep existing run/staging/profile/workspace paths. Cleanup review rejected12targets including nested overlap: prior11 plus new failed own fixture xmind-local-view-Mtkpjm under already blocked test-temp parent(362921349B). No removal or alternate route; do not retry blocked targets. Archive81 retained. Full portal PNG private; focused panel public.

- Preserve Cantor NodeID `618367c4896d99c06ae8cf663909a519` and saved portal auth.

- Embedded web: FastAPI/xlang3 inside xmind.exe; native jobs/SDK IPC and workspace-scoped session lists. UI58613 main7628/peer16576; MCP task .agentflow/playwright-mcp/task.json. Node51253/57780 stopped; preserve native backends. NEVER replay roots in .agentflow/embedded-web-task.json, .agentflow/session-loading-task-20261028.json or doc/evidence/workspace-session-lists-20261028.json. Libraries .agentflow/web-python/site-packages. Earlier guidance archived verbatim: doc/development-checkpoints-20261028.md.

- Installed0.1.80 authentication baseline, backups75/76 and seven native owners remain preserved. Exact hashes, owners and acceptance scope are archived verbatim in doc/development-checkpoints-20261028.md. New source fixes do not qualify a new installation.

- Latest user test policy: use projects to find xMind bugs. Do not correct project prompts or supply diagnostic fixes/code. Use normal feature requests; observe its own recovery, approve scoped effects, preserve failures. Label earlier guided interventions separately.

- Preserve all earlier Python/C++/Vue projects and NEVER replay their completed or failed roots. Read doc/development-checkpoints-20261028.md before touching them; exact IDs, failures and the project-test spending policy are archived verbatim.

- Current user request: test the installed web UI first with multiple chat turns in C++, Python-only (xlang3), and Vue+Vite projects under D:/CantorAI2026/TestProj/webui-project-tests-20261025. User authorizes configured-model inference up to$100 TOTAL for these project exercises only; all prompts/follow-ups/approvals go through UI, never API chat scripts. Preserve old successful tasks. Then finish requested VS Code automatic authentication/reconnect and workspace/history restoration fix. User web UI origin63573 must stay available; exact owners/config in .agentflow/webui-project-tests-20261025.

- Installed79 qualification and preserved rollback checkpoint: see doc/development-checkpoints-20261026.md verbatim.

- Installed79 isolated smoke session10d52bf152ced65b437c1f8f31e30cd5/root0342d66ccb3b26a510d9f2ad1231f9dd completed one marker read and durable2-message history with0models; owner identity/held-handle exit verified. NEVER replay. Final installation preservation audit passes; keep both required75/76 rollback copies.

- Only delivery goal: deliver a working v1 with solid UI and reliable browser/VS Code/CLI coding, prioritizing usability and release blockers over feature breadth. The user removed the old broader goal; do not resume or pursue that backlog. Advanced breadth is outside this goal unless it blocks v1. Acceptance: [working v1 goal](doc/WORKING_V1_GOAL.md).

- Historical candidate74/67/68/58/55/51/53/54, CLI59/60 and hosted-timeout ownership/NEVER-replay checkpoints are preserved verbatim in doc/development-checkpoints-20261028.md and its linked earlier archives. Read them before touching those profiles. Keep visible VS Code/browser windows closed; preserve user ports62084/54662/51118/52757 and all old owners. Verify exact PID/birth/image before intentional retirement; never mass-clean owners or profiles.
