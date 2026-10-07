# AgentFlow for VS Code

Connect to the local xlang3 AgentFlow backend. Run **AgentFlow: Open Workspace** to create/select sessions, submit prompts, inspect run events, and cancel execution. **AgentFlow: Ask About Selection** adds selected code to a draft prompt for review before sending.

Start the backend separately with `Tools/agentflow.ps1 serve --workspace YOUR_PROJECT`. Configure its model environment variables as described in `Documents/DEVELOPMENT.md`. Set `agentflow.backendUrl` in VS Code if the port differs. This development extension accepts loopback HTTP only.

Build a VSIX with `npm install` and `npm run package`, then use **Extensions: Install from VSIX**. The publisher name is local packaging metadata, not a registered Marketplace identity. No Marketplace publication has occurred.

Sessions are shared with CLI and persist on the backend. Closing the panel disconnects observation; it does not cancel the agent. Reopening restores the selected session and attaches to its latest run. Start the backend with `--ask-write` for per-operation Allow/Deny controls, or `--allow-write` for an explicit startup grant. Approval source is implemented, but full validation is pending the SQLite runtime fix. Diff review, graph inspection and richer coding UI remain pending.
