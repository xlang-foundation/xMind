# Native editor and browser access adapter

Candidate implementation; **native integration acceptance is pending**. Do not
package or advertise this candidate as a working editor connection yet.

`xmind view --workspace DIR --ready-file FILE` connects through the native local
profile controller and binds a separate loopback HTTP access endpoint. The
caller supplies a random `XMIND_VIEW_TOKEN` in the child environment. That is a
view access credential, not the backend access token. Native retains the backend
token, obtains a scoped view session and forwards public commands using it.
Owner-control operations do not use the view credential.

The private rendezvous file contains only public connection/workspace/process
metadata. Startup failures have a separate private `error.json`, attributed to
the adapter PID. Neither file contains backend or provider credentials. The
product host uses no stdin/stdout pipe protocol. A test driver can capture stderr
to observe a native failure; that is diagnostic output, not a product transport.

The VS Code host selects the actual trusted opened folder, verifies the package,
launches `xmind view` and authenticates its returned workspace. Native owns backend
startup, storage, process ownership and recovery. The editor no longer allocates
database paths/ports or stores native-owner records. Disposing the host closes
only its view adapters; backend runs should survive. CLI and editor use the same
native workspace profile rather than independent editor databases.

Saved model, session, run and graph choices use the native workspace ID and
profile directory. The adapter's ephemeral HTTP port is not their persistence
scope. External connections continue to use their explicitly selected origin.
No existing-profile or installed-format migration is implemented.

The existing browser adapter can enroll a cookie access session through this
native endpoint. Native-issued sessions can be bound to a PID and creation time.
The backend checks process exit/PID reuse before accepting those credentials and
cleans expired/dead credentials when issuing another session. Adapter-bound
browser grants are intended to become invalid after adapter exit.

This adapter currently forwards bounded HTTP requests. It does not implement the
shared SSE feed or agent-worker IPC, and it provides no OS sandbox. Those remain
separate delivery requirements.

## Evidence and outstanding work

All **217 extension fixture checks** and **41 browser fixture checks** pass.
These prove access-adapter/controller behavior at the fixture boundary, not an
installed IDE, rendered sidebar, native execution or live model response.

The first native probe timed out during fresh backend preparation. Reusing held
verification handles removes duplicate full-inventory hashing while preserving
source/destination checks. The second probe completed backend startup but failed
before view readiness. Its native error still needs to be observed. Private
startup-error reporting and stderr observation are prepared for the next probe.
The latest C++ source, including that reporting, has not been rebuilt yet.

A separate live xlang3 97-case performance run caused subsequent native phases
to be deferred. The complete **110-contract native gate has not passed** for
this candidate. Original failures are retained in
[candidate evidence](evidence/native-view-adapter-candidate.json).

Required next acceptance: rebuild current source, resolve the actual readiness
failure, pass the real console/editor-host/browser-cookie integration test and
the complete native suite with frozen source, then build a new verified VSIX and
validate its right sidebar in the actual IDE. The existing installed 0.1.5
package is unchanged and still contains its earlier accepted runtime/launcher.
