# Native agent graphs

Graph planning, scheduling, child execution and persistence belong to the C++ backend. The earlier Python graph engine and its dependent probe were removed.

Current implementation and verification limits:

- [Graph foundation](../doc/native-graph-foundation.md).
- [Agent/tool graph runner](../doc/native-graph-runner.md).
- [Child runs](../doc/native-graph-children.md).
- [Checkpoint recovery](../doc/native-graph-checkpoints.md).
- [HTTP/CLI graph service](../doc/native-graph-service.md).
- [VS Code workflows](../doc/vscode-graph-workflows.md).

Use the native service’s registered graph definitions and command/event contract. Removed prototype endpoints are not supported product entry points.
