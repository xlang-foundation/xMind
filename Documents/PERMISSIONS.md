# Tool approvals

## Current native approval contract

The C++ backend offers `edit_file` only with `--workspace-edits approved` (`Tools/agentflow.ps1 -ApprovedEdits` with the configured native server). This offers a capability, not a global grant: each exact actual-file proposal requires its own durable controller decision. Approved/stale/denied/cancelled effects, one-use claims, restart retirement and outcome-storage failure passed in [nineteen native contracts](../doc/evidence/native-model-edit-ci-ctest.log). Inference peers are labeled synthetic.

Current routes are `GET /v1/runs/{id}/operations`, `GET /v1/operations/{id}`, and `POST /v1/operations/{id}/decision` with `{ "decision": "allow" }` or `deny`. Native CLI commands are `operations RUN_ID`, `operation OP_ID`, and `decide OP_ID allow|deny`. The authenticated server supplies `local-owner`; views cannot supply an actor, proposal or effect outcome. This is local full-access authentication, not team authorization.

The right VS Code sidebar revalidates the exact reviewed record before a decision. **Compare changes** opens read-only backend snapshots and grants no permission. Unknown effects remain quarantined; inspected matching bytes never invent success. Attributed resolution, process permissions, policy presets, remote tool enforcement and team scopes remain required.

## Historical prototype contract

The startup flags/routes and Python probe below describe the retained prototype. They are not the current native interface or current validation result.

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
