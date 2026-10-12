# VS Code sidebar Git menu checkpoint

The xMind secondary-sidebar toolbar now has an accessible SVG **Workspace
Git** button. Its host menu offers Git status, review diff and repository
initialization by invoking the existing guarded VS Code commands. Those
commands keep using the connected native workspace, registered folder IDs,
confirmation for repository creation and backend-produced Git data; the menu
does not add a separate Git implementation.

## Local verification

- VS Code extension, renderer and host suite: **318/318 passed**.
- Browser suite: **97/97 passed**.
- `git diff --check`: passed.

The renderer contract checks the visible SVG control and exact host message.
The extension-host contract selects Git status in the menu, then verifies the
existing command reaches the native VCS status route and reports an empty
repository safely. These are DOM/harness tests, not a live Extension
Development Host or installed VS Code acceptance run. The toolbar update still
needs packaging and a rendered user-session check.
