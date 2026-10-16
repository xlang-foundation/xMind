# Native terminal sessions

xMind now hosts Windows ConPTY shells in the native server process. A terminal
is bound to the server's opened workspace and to one persisted conversation.
Clients never supply a shell executable, command line, environment or working
directory; the server starts its fixed system `cmd.exe` in the qualified
workspace. The shell is owned by a Windows Job Object and is stopped when its
terminal is closed or the backend exits.

Authenticated local clients use these HTTP operations:

| Operation | Route | Request/response |
| --- | --- | --- |
| Create | `POST /v1/sessions/{session}/terminals` | Optional `title`, `rows`, `columns`; returns session metadata and generated terminal ID |
| List | `GET /v1/sessions/{session}/terminals` | Returns only terminals attached to that conversation |
| Inspect | `GET /v1/terminals/{id}` | Returns current dimensions, running state, and output cursor |
| Read output | `GET /v1/terminals/{id}/output?after={sequence}` | Returns a replayable byte range as lowercase hex so partial UTF-8 and VT sequences remain intact |
| Send input | `POST /v1/terminals/{id}/input` | UTF-8 `data`, at most 64 KiB per request |
| Resize | `POST /v1/terminals/{id}/resize` | `rows` 5–300 and `columns` 20–500 |
| Close | `POST /v1/terminals/{id}/close` | Stops the process tree and retires the terminal |

The console client exposes the same lifecycle as `terminals SESSION`,
`terminal-create SESSION [TITLE]`, `terminal-output ID [AFTER]`,
`terminal-input ID UTF8`, `terminal-resize ID ROWS COLUMNS` and
`terminal-close ID`. `terminal-attach SESSION [TITLE] [--console]` opens a
native console shell, relays backend output, forwards key presses immediately,
and follows console window resizes. `--console` explicitly opens the attached
Windows console when a launcher redirected standard input; ordinary pipe input
stays line-based for scripted use. The VT mapping covers printable Unicode,
control keys, arrows, navigation and function keys. VS Code opens the same backend-owned shell in its native terminal
panel through a pseudoterminal bridge. The bridge pins one workspace,
conversation and server generation, then relays terminal bytes without
interpreting or estimating output. The browser view uses xterm.js and the same
session-authenticated xMind terminal routes; it keeps the project file tree
visible while switching the preview pane to the terminal. Both editor clients
pin one workspace and conversation, relay byte-cursor output, send input and
resize updates, and close their terminal when the view disconnects or its
conversation/workspace changes. The browser does not start a local shell.
Full-screen application behavior still requires acceptance in a real browser,
console and editor terminal.

The in-memory output ring retains at most 1 MiB per terminal. Each response
returns the next sequence, whether older bytes were truncated, and actual
process state. Clients reconnect by requesting bytes after their last sequence.
The server caps the number of live/retained terminals globally and per
conversation. Terminal buffers are process-local and do not survive backend
restart; the SQLite conversation remains intact.

The key-mapping contract checks VT sequences, Unicode/control input, navigation,
function keys and backend dimension bounds. The actual-server contract also
launches the unified CLI inside a nested Windows ConPTY, types through
`CONIN$`, resizes the console and checks the shell-reported dimensions and
command output. The native contract launches the real system command shell in
an isolated fixture workspace, waits for its initial prompt, sends a synthetic echo command,
checks exact output replay, resizes the pseudoconsole, observes shell exit and
closes the session. Browser terminal unit and page-contract tests verify
session-bound creation, UTF-8 output, input, resize, close and the same-origin
route allowlist. The HTTP integration contract runs the same lifecycle
through the actual native server. It also attaches the CLI to a real fixture
shell, sends commands and confirms output and cleanup. VS Code bridge tests
cover UTF-8 chunk boundaries, input and resize routing, and workspace
revocation. Neither native contract runs a command in the user's workspace.
