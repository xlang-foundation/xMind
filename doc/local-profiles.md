# Native local workspace profiles

The native profile controller is the startup and discovery boundary for the
local `xmind` console. The backend owns execution and the SQLite database;
database operations continue through embedded xlang3. The profile controller
does not read or modify agent tables.

Without `--port`, the console selects its current directory, or the explicit
`--workspace DIR`, and resolves one profile under the user's Local AppData
`xMind/LocalProfiles` directory. `--profile-root DIR` selects an explicit private
storage location. Storage that overlaps the selected workspace is rejected.
The profile directory is keyed by the native workspace directory identity,
not a spelling of its path or a package revision.

```powershell
xmind --workspace D:\Projects\Example --config D:\Private\.config\providers.yaml
xmind --workspace D:\Projects\Example sessions
xmind --workspace D:\Projects\Example profile-info
```

`--config` passes the one provider YAML to the backend. `--graphs-config` can
pass a native graph configuration. These paths and the proposal policy are
retained with the profile; reconnecting without launch options preserves them.
Changing an explicit retained option requires an explicit backend update.
`--read-only` selects an initial owner without file-effect proposals. Otherwise
a new owner permits proposals that still require normal native approval.

Explicit `--port PORT` remains an operator-selected HTTP connection using
`XMIND_AUTH_TOKEN`. It cannot also change managed-profile launch options.
The native console workspace pinning checks apply to automatic connections.
Invalid console commands are rejected before profile creation.

## Startup and ownership

The controller serializes creation with an OS-held startup lock. A new profile
gets a protected rendezvous record before process startup. It retains an exact
verified runtime inventory outside the workspace, including the same `xmind.exe`
and xlang3 dependencies. It never fetches or substitutes another interpreter.

The backend starts suspended in a Windows Job Object assigned atomically by
`CreateProcess`. Until startup is published, a lost controller closes the job
and terminates its own unpublished process. PID and creation time are committed
before the controller resumes that process.

After binding an available loopback port, the backend validates the prepared
record against its actual process, runtime, workspace and authentication and
publishes readiness. The controller then releases startup-only job termination
and checks authenticated workspace/owner HTTP metadata before returning the
connection. The record is a rendezvous channel; model, tool and session commands
use the backend's existing HTTP protocol. Console pipes are not used for startup
coordination. Agent-worker shared-memory IPC remains a separate pending feature.

Clients validate PID, creation time, actual executable identity, runtime
inventory, workspace identity and backend authority. A live but unreachable
owner is an unresolved outcome and does not authorize a competing backend or
an empty database. An observed exited owner can restart against the same
retained database, authentication and launch configuration. Closing a console
detaches observation and leaves its backend running.

## Private state

The profile directory has a user-owned Windows ACL. Directory aliases and
hard-linked control files are rejected. The rendezvous payload, including its
bootstrap access token, is encrypted with user DPAPI and bound to the actual
profile directory identity. It is process-control state, not a second store
for agents or provider credentials. Provider credentials and agent information
remain in the backend's SQLite repositories.

`profile-info` returns public connection metadata without the token. Backend
startup output goes to the protected profile's `native.log`. The token is not
passed on the command line or imported into model context.

This controller is not an OS sandbox. Trusted code running as the same Windows
user retains that user's operating-system permissions.

## Verification boundary

The actual unified executable passed managed startup, four concurrent reconnects,
an authenticated native owner check, console detach, protected-record damage
refusal, a real file-reading graph and exact session/history preservation after
an observed owner exit and restart. The complete **109 native contracts** and
**10 focused checks** passed with unchanged source inputs.
[Source hashes, binaries and original logs](evidence/native-local-profile-local.json).

The test stages the actual native/xlang3 runtime and full pure-source inventory;
its provenance and license metadata are scoped integrity fixtures. It performs
no provider requests and does not establish release-package or installed-editor
acceptance. The initial path-length and directory-lock failures and the test
reporting failure are recorded in the evidence.

Existing installed VS Code 0.1.5 still uses its separately accepted runtime and
managed launcher; it has not adopted this controller. Client adoption, shared
UI SSE and agent-worker IPC remain separate delivery work. There is one current
format, with no migration of existing installations or profiles.
