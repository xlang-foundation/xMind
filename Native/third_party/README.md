# Native transport dependencies

These independently licensed libraries implement HTTP transport and JSON serialization. They are outside `agentflow_core`; no OpenCode or LiteLLM engine is embedded.

| Library | Pin | SHA-256 of header | License |
| --- | --- | --- | --- |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib/tree/v0.60.0) | v0.60.0 | AC84B5A041BE99A5E7616B05C907681B75C157314A22D7681F71E73A6C2554D1 | MIT, accompanying LICENSE |
| [nlohmann/json](https://github.com/nlohmann/json/tree/v3.12.0) | v3.12.0 | AAF127C04CB31C406E5B04A63F1AE89369FCCDE6D8FA7CDDA1ED4F32DFC5DE63 | MIT, accompanying LICENSE.MIT |

Headers were downloaded from the tagged upstream source and preserved unchanged. cpp-httplib uses blocking HTTP/1.1 I/O; this adapter configures four workers, a bounded transport task queue, timeouts and a 1 MiB request-body limit. TLS and shared deployment are not enabled in this local adapter. Dependency upgrades require reviewing upstream changes, licenses, header hashes and the independent HTTP contract.

## Native schema validation

[jsoncons v1.9.0](https://github.com/danielaparker/jsoncons/tree/bcb44594c50c495ee1e690602cdd71455942ad0e), commit `bcb44594c50c495ee1e690602cdd71455942ad0e`, provides the native JSON Schema 2020-12 validator. The unmodified include tree and Boost Software License 1.0 are preserved under `jsoncons/`; no scripts/interpreters or network resolver are used. `jsoncons/manifest.json` records LF-normalized SHA-256 hashes for every vendored source/license file. The runtime bundle's existing recursive license collection includes this license.
