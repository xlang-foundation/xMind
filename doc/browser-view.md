# Browser view adapter

xMind has a local HTML view using the same sidebar renderer and native API client as the VS Code extension. The desktop layout places the agent sidebar on the right, Settings at its top, and the discovered-model selector beside the composer at the bottom. Narrow windows use the available width.

The C++ server owns execution, tools, graphs, permissions, providers and history. SQLite I/O remains in embedded xlang3. The Node HTTP adapter only serves immutable view assets and forwards a finite allowlist of API requests to a configured loopback native server. Closing the view does not cancel execution. This is a local HTTP transport; authenticated remote/team deployment, WebRTC signaling/data transport and the full standalone IDE remain incomplete.

## Start

From the repository, with the native build and extension dependencies available:

```powershell
node views/browser/build.mjs
node Tools/start-browser.mjs --port 60405
```

The launcher creates a dedicated `.agentflow/browser-ui` profile and runtime snapshot, preserving the existing VS Code previews. Its private `auth.token` authenticates the browser to that native server. Enter that token in Connect; provider API keys belong in the top-right Settings dialog afterward. The server token stays in browser memory, so reloading requires connecting again. Only selected session/run/model/workflow IDs are saved in sessionStorage; keys and conversation content are not stored there.

Settings supports provider enrollment and model discovery through the native API. This browser profile starts without a provider key. The existing VS Code preview's saved key is not copied. Live model inference in this browser profile has not been verified.

## Verification

The real browser completed the trusted `read.repository.file` graph with human input `{"path":"README.md"}`. Reload and reconnect restored completed run `8a164a082bb81d5ecd39853c5f979b0d`, its history and selected workflow. No model response or token usage was fabricated.

- Browser controller tests: 4 passed, zero failures/skips.
- Shared extension tests: 60 passed, zero failures/skips.
- Native browser contract: real native graph/file execution, human pause/input, authentication/origin/route boundaries, disconnect without cancellation and reconnect without replay passed.
- Native runtime used the previously validated build from checkpoint `67de5b16a8732477606957cb52736df06f078f16`; no native source changed in this browser checkpoint.

Desktop and Settings screenshots: [desktop](evidence/browser-native-desktop.png), [Settings](evidence/browser-native-settings.png). Test output is preserved in `doc/evidence/browser-*.log`. The Windows CI workflow runs browser validation after building the native server.

The current workspace pane supports operation comparison, not a complete file explorer/editor. Browser acceptance for live provider output and its actual usage metrics is still outstanding.
