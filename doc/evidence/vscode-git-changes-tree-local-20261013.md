# VS Code Git Changes tree

The extension now contributes an **xMind Git Changes** tree to Explorer. It
reads file status through the selected xMind backend, groups results by the
registered workspace folders, and shows each file's native status and line
counts. The view refreshes from its title action, after saved editor documents,
and after terminal single-agent, owned-child, and graph runs. Workspace disconnect
clears the previous snapshot.
This is read-only; Git initialization and patch review keep their existing
workspace-bound commands.

Local verification on 2026-10-13:

- `node --test tests/*.test.js` in `extensions/vscode`: **324/324 passed**.
- `node --test views/browser/tests/*.test.cjs` from the xMind repository root:
  **103/103 passed**.
- The focused Extension Host contract exercises the connected native API,
  primary and attached-folder status requests, grouped file rows, and the
  status tree's validated empty/error states plus refresh after an editor save.
- VSIX packaging did not complete: browser asset generation succeeded, then
  `package-native-runtime.mjs --verify` rejected the checked-in runtime because
  its native/pure-standard-library inventory is incomplete for the current
  verifier. No VSIX was produced or installed.

The tests use a synthetic VS Code host and backend. They do not establish a
rendered Extension Development Host or installed VSIX acceptance.
