# Official MCP SDK interoperability fixtures

These are independent subprocess peers for the native C++ contracts. They are test dependencies only; no SDK implementation runs inside the product core or ships in the native runtime bundle.

Pinned npm packages and all transitive integrity records are in `package-lock.json`: modern `@modelcontextprotocol/server` **2.3.1**, legacy `@modelcontextprotocol/sdk` **1.32.1**, and Zod **4.2.0**. The peer checks installed versions. Modern server/core packages retain Apache-2.0 notices in their npm distribution; the legacy SDK retains its MIT notice. The upstream [release ledger](https://github.com/modelcontextprotocol/typescript-sdk/releases) and [modern](https://ts.sdk.modelcontextprotocol.io/v2/)/[legacy](https://ts.sdk.modelcontextprotocol.io/server) guides describe their APIs.

The guarded build and isolated CI install with `npm.cmd ci --ignore-scripts --no-audit --no-fund`. Product CMake builds with `BUILD_TESTING=OFF` need no SDK package installation. Test builds refuse to omit the two SDK contracts when their peers are absent.

`peer.mjs` uses the actual SDK server and transport. Its tool appends to an isolated real file, reports structured output, and deliberately advertises an untrusted read-only hint. A raw-input observer checks exact approved numeric bytes without replacing the SDK parser/transport. The disconnect mode exits after an actual effect, before a response. Credentials are labeled synthetic test values supplied through the real encrypted native repository.

`native_mcp_sdk_modern_contract` and `native_mcp_sdk_legacy_contract` use the same real server/admin/CLI integration driver as the independent wire peer. They verify selected protocol, discovery, configuration restore, scoped environment, approval, denial/cancellation, model continuation and persisted uncertainty without replay. Inference is synthetic. These tests do not establish all SDK features, HTTP/OAuth, resources/prompts/subscriptions, modern interactive continuations, full schema conformance, live-model or populated editor execution.
