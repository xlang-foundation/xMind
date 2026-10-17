# Keeping approvals visible

The shared browser/VS Code renderer shows the number of unexpired operations
awaiting approval above the scrollable conversation. **Review approvals** moves
to the first proposal; Allow/Deny remain on that proposal and use the existing
backend decision protocol. The notice never approves, changes permissions or
dispatches tools.

While a proposal awaits review, streamed events, status updates and unchanged
operation polls preserve the conversation scroll position. The expanded activity
log has a bounded height and scrolls independently. Identical polls retain the
proposal controls, review details and focus. Expired or retired proposals stop
contributing to the notice. Run/workspace reset clears the old controls.

This addresses the observed installed-editor test where expanded activity pushed
the pending `hello.py` proposal out of view. The actual creation succeeded on the
older installed runtime. This renderer is now installed in client 0.1.3 and the
idle sidebar has been observed after reload. The renderer's pending-approval
layout and navigation were subsequently checked in a live VS Code
extension-host session. A TestProj file edit appeared in the sidebar with exact
before/after content and Allow edit, Deny, and Compare changes controls. Both
the edit and its reviewed inverse completed through Allow edit inside VS Code,
and the original file hash was restored. This verifies that focused approval
path in the live extension host; it does not establish broad coding reliability
or release-package acceptance. Exact evidence is in
`evidence/vscode-sidebar-approval-live-20261017.json`.

The candidate passed 66 renderer contracts and the complete isolated client gate:
217 extension and 39 browser contracts, with zero failures, skips or cancellations.
All 43 input hashes and 11 generated asset hashes remained unchanged through the
gate. DOM tests cover pending-review navigation without permission dispatch,
streamed activity without scroll changes, retained focus/control identity,
expiry and run/workspace retirement. They do not establish browser layout or live
inference acceptance. [Exact local evidence](evidence/sidebar-approval-review.json).
