# Local xMind Server deployment

xMind OSS runs the native C++ backend on the developer’s machine. CLI, browser UI and VS Code access the same authenticated local API and durable session/run events. The backend owns the workspace, permissions, model credentials and embedded-xlang3 SQLite persistence. Clients do not mount or write the database.

Team-server deployment, company session sharing, organizations, tenants, distributed workers and PostgreSQL are excluded from OSS and reserved for CantorAI’s closed-source Nexus project. Historical proposals were moved to that separate local workspace. Local-owner authentication is not team authentication.

See the current [OSS specification](architecture.md), [native server](native-server.md) and [runtime/view boundaries](runtime-view-separation.md). Actual capabilities remain limited to their recorded acceptance evidence.

The revised client/agent target supports Local and Nexus connection profiles.
Local runs stay single-user with SQLite. A Nexus profile binds the local xMind
agent/workspace to the private team server using the shared command/event
protocol. Nexus owns shared sessions, PostgreSQL state, authenticated worker
enrollment and distributed coordination. Those server implementations remain
private; generic connection/worker bindings belong in OSS. Cantor Cluster is a
later Nexus scheduler integration. None of these profile/team connections has
passed implementation acceptance yet.
