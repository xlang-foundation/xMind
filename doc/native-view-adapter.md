# Native editor and browser access adapter

Source `689404e` passed real native integration and the full **110-contract**
hosted gate, followed by **224 extension / 41 browser** checks and packaging.
The production editor adapter was exercised with a fixture VS Code API;
**fresh installed/rendered editor acceptance remains pending**.
[Original gate and independent package verification](evidence/native-view-hosted-689404e-package.json).
The exact VSIX is installed into a fresh isolated profile and all 1895 installed
extension files match the archive, apart from installer-added manifest metadata.
It has not been launched for local native or rendered acceptance yet.
[File-level installation evidence](evidence/native-view-hosted-689404e-fresh-install.json).

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

The host chooses a fresh unused leaf under its qualified private root. Native
creates that leaf with an explicit current-user owner and protected DACL; the
host does not create it with Node's default ownership. Existing names are refused,
and the published directory is requalified before authentication is stored.
On the hosted Windows runner, the default owner differs from the user: the prior
host-created leaf caused native's strict owner check to fail. Those checks remain
unchanged. The corrected host passes **224 extension / 41 browser fixtures**;
its real native rerun reached view readiness and failed on the first session
write. The adapter forwarded POST requests in cpp-httplib's pre-routing handler,
before the library had read their bodies. Forwarding now uses regular GET/POST
handlers; the pre-routing hook admits or rejects access before body reading.
The native contract additionally checks exact Unicode titles, chunked UTF-8
writes, malformed JSON and the adapter's payload limit. The corrected source
passed both the early real view test and that test within the complete gate.
[Observed failure, original provenance and candidate scope](evidence/native-rendezvous-owner-candidate.json).
[Actual write failure and forwarding correction](evidence/native-view-body-forwarding-candidate.json).

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

The initial candidate passed **217 extension fixture checks** and **41 browser
fixture checks**. A later startup-selection fix passed **219 extension checks**:
[scoped race evidence](evidence/view-startup-selection-race-local.json).
These prove access-adapter/controller behavior at the fixture boundary, not an
installed IDE, rendered sidebar, native execution or live model response.

The first native probe timed out during fresh backend preparation. Reusing held
verification handles removes duplicate full-inventory hashing while preserving
source/destination checks. The second probe completed backend startup but failed
before view readiness. Its native error still needs to be observed. Private
startup-error reporting and stderr observation are prepared for the next probe.
Hosted source `48aa772` compiled, but its local profile/view tests stopped during
package staging because they requested `LICENSE.txt` instead of the pinned
source checkout's `LICENSE`. The full gate passed 107/110: the third failure
invoked the removed editor upgrade API. These failures and the candidate fixes
are recorded in [hosted evidence](evidence/native-ci-current-format-candidate.json).
The readiness failure still has no accepted native rerun; later rendezvous
preflight changes also require the complete gate.

A separate live xlang3 97-case performance run caused subsequent native phases
to be deferred. The complete **110-contract native gate has not passed** for
this candidate. Original failures are retained in
[candidate evidence](evidence/native-view-adapter-candidate.json).

Required next acceptance: rebuild current source, resolve the actual readiness
failure, pass the real console/editor-host/browser-cookie integration test and
the complete native suite with frozen source, then build a new verified VSIX and
validate its right sidebar in the actual IDE. The existing installed 0.1.5
package is unchanged and still contains its earlier accepted runtime/launcher.

## Pending preflight change

The latest source validates the private ready-file location before backend
discovery/start and provider configuration import. Existing rendezvous files
are refused without overwriting their contents. A native contract is prepared
to reject a ready file inside the workspace before creating profile storage.
This change is **uncompiled and unaccepted** while native validation is deferred.
[Source-bound candidate record](evidence/native-view-preflight-candidate.json).
