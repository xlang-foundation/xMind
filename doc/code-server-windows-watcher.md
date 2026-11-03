# Windows Code Server watcher workaround

Verified with official VS Code Server 1.143.0, commit `2d0b7506a36c1d6db9a466440131a8e01eab2289`, and xMind candidate 0.1.85. Regular desktop VS Code is a separate acceptance scope.

The server adds its extension directory to recursive watcher exclusions using a Windows path. In this installation, the unchanged Parcel watcher rejects the resulting glob with `regex_error(error_escape)`. See the [pinned Microsoft implementation](https://raw.githubusercontent.com/microsoft/vscode/2d0b7506a36c1d6db9a466440131a8e01eab2289/src/vs/server/node/remoteFileSystemProviderServer.ts).

For the existing acceptance host, changing the spelling of the `--server-data-dir` argument resolves the startup error:

| Argument | Value |
| --- | --- |
| Previous | `D:/CantorAI2026/xMind/.agentflow/rendered-vscode-server/server-data` |
| Working | `D:/CantorAI2026/XMind/.agentflow/rendered-vscode-server/server-data` |

Both resolve to exactly the same physical directory on this Windows machine. No directory is renamed, moved or copied. Keep the existing host, port, workspace, token-file, pinned commit and settings arguments. This is a verified installation workaround; it does not change the upstream watcher implementation or establish behavior on other releases.

After restarting the owned Code Server, its existing browser page requested **Reload Window**. Reload restored authentication, provider, model, workspace and selected completed run. Loading older messages restored the exact full conversation. An external write to a nested text file then appeared in its open editor without another reload or a buffer edit. The owned probe was removed afterward.

The native backend stayed on its original PID and run/profile paths. All existing records and project files remained exact, with no inference, effect replay or new managed runtime directory. [Detailed evidence and screenshots](evidence/code-server-watcher-case-20261103.json).
