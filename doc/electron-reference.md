# Existing Electron runtime investigation

The user selected Electron for the standalone IDE and authorized copying or directly using the existing CantorAI/WorkSense Electron folder on this development PC. Keep the CLI, Electron IDE and VS Code plugin as clients of the same native backend; do not move platform logic into Electron.

## Inspected evidence

- `D:\CantroAI` is absent. `D:\CantorAI` exists.
- `D:\CantorAI\Manifold\apps\desktop-cantor-one\package.json` and `scripts/run-electron.mjs` reference a custom runtime at `D:\CantorAI\electron-build\.chromium\src\out\XLangRelease\electron.exe`. That directory/executable and its referenced distribution zip are currently absent.
- That application's metadata names a custom Electron distribution version `0.0.0-xlang.1253233.0`, a stock Electron development dependency `43.2.0`, and packaged runtime resources including `electron_xlang_bridge.dll` and `xlang3_runtime.dll`. These are source configuration values, not proof of the installed runtime's compatibility with the new backend.
- `D:\WorkSense\LiveCoach360` contains an installed application (`LiveCoach360.exe`), Chromium/Electron resources, `app.asar`, `default_app.asar`, xlang resources and license notices. Its `version` file reports `0.0.0-xlang.1253233.0`.

The installed application was not launched, modified or copied. Its executable is not yet established as a reusable development launcher. The existing package configuration also specifies embedded ASAR integrity validation and packaged-app loading; inspect actual runtime behavior before selecting an installed bundle as the IDE base. Do not copy application data, credentials, model downloads or Python runtime directories merely to obtain Electron.

## Next integration

Resolve the actual reusable Electron executable/build path with the user. Record runtime version, architecture, custom xlang bridge API and license notices. Prefer pointing the new IDE's development launcher at a suitable existing runtime using an explicit configuration path. Keep launch arguments and IDE data separate from WorkSense. Packaging can copy the required runtime/resources into an independent distribution after compatibility is validated.

Electron main/preload code exposes a narrow typed client interface. Renderer code consumes shared sessions, model selection, run events, context, approvals and diffs. File/process actions are backend/worker tools, not unrestricted renderer access. Local mode selects a local xMind Server; remote mode selects an authenticated server profile. Desktop startup, backend lifecycle, reconnect and actual editor interaction all remain unimplemented/unverified.
