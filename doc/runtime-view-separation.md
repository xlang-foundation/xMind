# OSS runtime and view separation

The C++/xlang3 backend owns agent execution, graphs, workspace tools, permissions, providers and SQLite persistence. Native CLI, HTML/browser UI and VS Code are clients of the same local command/event API. A view renders observed state; closing or refreshing it must not replay a model/tool operation or destroy an admitted backend run. Durable events permit reconnect and inspection.

Access adapters carry authenticated requests and subscriptions and remain separate from the core. Provider keys remain encrypted on the backend; browser connection credentials follow the native view-session policy. No client fabricates run outcomes, assistant content or usage metrics.

WebRTC transport/signaling, a standalone Electron IDE and team-server topology are excluded from OSS and reserved for Cantor Nexus. Their historical diagrams and design were moved to the private workspace. This scope change preserves runtime/view separation without requiring those transports or clients.

See the current [OSS architecture](architecture.md) and [diagram](architecture.svg).
