# A2A integration

Protocol baseline: [A2A 0.3.0 JSON-RPC specification](https://a2a-protocol.org/v0.3.0/specification/).

The backend exposes `GET /.well-known/agent-card.json` and `POST /a2a`. `agentflow/a2a.py` maps messages and tasks to the shared SQLite sessions and agent runs. The adapter implements text-only message/send, message/stream, tasks/get, tasks/cancel and tasks/resubscribe. Unsupported push notifications and extended cards return the corresponding protocol errors.

Task IDs are backend run IDs and context IDs are session IDs. Input messages and their message IDs are persisted with run creation; duplicate message IDs reuse the same run and reject changed content. Completed output is represented as a text artifact. Blocking sends wait for a terminal state; nonblocking sends allow later polling. SSE maps persisted run progress to artifact/status updates. Canceling observation does not cancel a task; tasks/cancel is explicit.

The HTTP client in `agentflow/a2a_client.py` supports discovery, send/get/cancel and streamed message results. It does not change its configured endpoint based on a returned card or forward credentials through redirects.

## Validation and incomplete behavior

`tests/a2a_probe.py` checks discovery, errors and malformed request rejection before attempting a real shared-engine task. The end-to-end task currently failed; the underlying agent/persistence failures must be resolved before claiming reliable A2A execution. Independent SDK interoperability and live outbound networking remain unverified.

`tests/a2a_client_probe.py` separately checks request/response, SSE assembly and errors using an HTTP test double. This is not live interoperability evidence.

File/data parts, push notifications, authenticated extended cards, active-task multi-turn input, graph remote-agent nodes, production authentication and additional transports remain pending. The current backend is a local development service. Do not claim full A2A compliance or production readiness.
