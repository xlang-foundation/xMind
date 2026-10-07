# Tool approvals

Workspace tools support three startup modes:

- Default: read-only; mutation tools return permission errors.
- `serve --allow-write`: explicitly authorize workspace mutation tools for the backend lifetime.
- `serve --ask-write`: request an explicit allow/deny decision for each mutation call. The two write flags are mutually exclusive.

Approved execution uses a fresh one-operation tool registry and does not mutate shared permission flags. Agents and graph tool nodes use the same approval gate. MCP server calls still use startup permissions directly; they do not wait on interactive approval. Remote MCP tools require their explicit configuration allowlist and are not currently covered by local mutation prompts.

## API and CLI

- `GET /v1/runs/RUN_ID/approvals`: pending operation details, including exact tool arguments.
- `POST /v1/approvals/APPROVAL_ID`: `{ "decision": "allow" }` or `{ "decision": "deny" }`.
- `Tools/agentflow.ps1 approvals --run RUN_ID`.
- `Tools/agentflow.ps1 decide --approval APPROVAL_ID --decision allow` (or deny).

Requests and decisions are durable and emit permission events. Decisions cannot be changed after resolution. Approval waiting has no automatic grant or timeout; actual tool execution retains its timeout. Cancelling a waiting operation cancels its request. Restart recovery expires pending approvals for terminal/interrupted runs rather than reusing an old authorization.

## VS Code

The panel displays pending tool arguments with **Allow this operation** and **Deny** buttons. Decisions are checked against the selected run's current pending requests before submission. The updated VSIX packaged successfully and extension syntax/client tests passed. Actual editor installation and UI interaction remain unverified.

## Validation status

`tests/permissions_probe.py` is an end-to-end check of one-operation grants, denial, cancellation and permission isolation. It currently fails at the initial run transition with SQLite affected rows `-1`, before approval behavior executes. Keep this as unverified functionality until the native persistence issue is resolved and the full test passes. Do not disable transition checks to obtain a pass.

Interactive diff preview, policy rules, authentication, shell permissions, remote tool approvals and production access controls remain pending. The development backend currently binds to loopback.
