# Current native validation

Newer Gemini SSE source adds a 59th required native contract. That stream
component has not yet been compiled/executed; the 58-contract evidence below
excludes it. See [stream scope](native-gemini-stream.md).

The local native source `9e3a104b27414fc61c6e346c75e41cf630c21991` passed all **58 contracts** in 98.49 seconds, with **88 extension and 17 browser checks**, no skips, and the native/browser integration contract passing. See [provenance](../doc/evidence/native-gemini-local-provenance.json) and [CTest output](../doc/evidence/native-gemini-local-ctest.log). Synthetic peers verify mechanics; this gate does not establish new live provider coverage.

The installed browser preview retains its separately verified backend/view versions. See [provider setup](../doc/provider-setup.md) for hosted and installed scope. The new Gemini component handles request serialization and history metadata; transport, engine integration and live acceptance remain incomplete.

The Python prototype and original xlang-based runtime, service/plugin assets, launchers and dependent probes have been removed. The root build now delegates to Native/ and configures the same 58-contract manifest. Generic xlang3 dependency setup and its runtime probe remain independent of the native agent engine.

Complete coding parity, broad native provider support, remaining MCP/A2A capabilities and Local/Nexus connection profiles are still active requirements. Team-server implementation, PostgreSQL, WebRTC and Electron belong to private Nexus. [Architecture](../doc/architecture.md), [parity baseline](PARITY.md).
