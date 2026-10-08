# Native permissions

C++ owns tool-effect authorization and persisted operation decisions. The earlier Python permission gate and its dependent probe were removed.

See [native server behavior](../doc/native-server.md), [architecture](../doc/architecture.md), and [development instructions](DEVELOPMENT.md) for the current implementation. Filesystem/process/MCP effects must follow their native policy and approval contracts; clients do not grant permission by changing displayed state.
