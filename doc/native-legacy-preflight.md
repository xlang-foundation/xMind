# Native legacy-owner preflight

`xmind_admin inspect-legacy-owner PORT IMAGE IMAGE_SHA256 DATABASE WORKSPACE WORKSPACE_ID AUTHORITY_ID`
performs a read-only check before an operator migration. The existing owner's
token is supplied through `XMIND_AUTH_TOKEN`; it is never a command-line argument
or an output field. This command neither stops the process nor publishes a
migration ticket.

C++ resolves the exact IPv4 loopback listener using the Windows owner-PID TCP
table, opens its actual process handle and captures creation time. Retained
image handles compare the running executable's file identity with the supplied
source image and verify its SHA-256. WMI supplies the process command line;
native Windows argument parsing checks the database and workspace against actual
retained file/directory identities. An explicit port must match the listener;
`--port 0` is allowed for a dynamically allocated listener. A qualified modern
owner is rejected here because it must use its retirement protocol.

The native HTTP transport authenticates `/v1/workspace` and checks the captured
workspace ID, authority and root identity. The process creation time, live
handle and listener PID are checked again afterward. Missing/inaccessible
metadata, invalid credentials or any mismatch fail. Output contains observed
PID/birth, image hash and canonical image/database paths, with explicit
`process_signalled: false` and `migration_ticket_created: false`.

The caller must obtain the expected image hash from its trusted source-package
inventory and the workspace/authority from its saved authenticated owner. A
successful observation is not a reusable shutdown authorization. The native
native stop operation must revalidate these bindings and protect idle database
ownership while stopping; it cannot authorize termination using stale JSON
returned by this command.

The actual production server/administration fixture checks successful preflight
and rejection of a wrong token, image, image hash, database, workspace, workspace
ID and authority. It confirms that sessions are unchanged and that the legacy
process is still running. Package metadata and VS Code state remain fixtures;
no installed migration, provider call or file-writing acceptance is claimed.
The complete gate passed **98 native contracts in 188.46 seconds**, with all
631 mapped inputs unchanged and the exact expected/registered/passed manifest
matched. The 39 view inputs are unchanged from the previous 197 extension / 39
browser passing checkpoint; those suites were not rerun for this native change.
[Exact source map and limits](evidence/native-legacy-preflight-local.json),
[complete CTest output](evidence/native-legacy-preflight-local-ctest.log).

This adds the native verification prerequisite to the
[stopped-owner preparation](native-legacy-owner.md). The subsequent
[native stop/adoption increment](native-legacy-stop.md) implements the operator
operation and extension recovery. Verified installation and rendered coding
approvals remain required before the installed read-only TestProj backend can
write files.

Windows API references: [owner-PID TCP rows](https://learn.microsoft.com/en-us/windows/win32/api/tcpmib/ns-tcpmib-mib_tcprow_owner_pid),
[process command-line metadata](https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/CIMWin32Prov/win32-process.md).
