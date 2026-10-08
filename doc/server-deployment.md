# Local xMind Server deployment

xMind OSS runs the native C++ backend on the developer’s machine. CLI, browser UI and VS Code access the same authenticated local API and durable session/run events. The backend owns the workspace, permissions, model credentials and embedded-xlang3 SQLite persistence. Clients do not mount or write the database.

Team-server deployment, company session sharing, organizations, tenants, distributed workers and PostgreSQL are excluded from OSS and reserved for CantorAI’s closed-source Nexus project. Historical proposals were moved to that separate local workspace. Local-owner authentication is not team authentication.

See the current [OSS specification](architecture.md), [native server](native-server.md) and [runtime/view boundaries](runtime-view-separation.md). Actual capabilities remain limited to their recorded acceptance evidence.
