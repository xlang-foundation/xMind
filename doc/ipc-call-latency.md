# xlang3 remote method latency

The measured client and native xMind backend are separate processes. The client
uses the public C++ xlang3 SDK to bind the registered xmind root, obtain Host and
authenticated Connections, then capture the Local binding. The SDK owns remote
object references, method dispatch, values and transport request correlation.
This adds no xMind peer.call route or application-owned transport serializer.

On SDK8c7972d1/native58bb2671,100 warmups precede1000 sequential calls per method
on one established connection. Method execution and SDK return-value transfer
are timed; remote member lookup and process/cold binding are outside that timer.

| Actual remote method | Median ms | p95 ms | p99 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: |
| Host.ApiVersion() | 0.0784 | 0.0946 | 0.1158 | 0.1778 |
| CapturedBinding.Identity() | 0.091 | 0.116 | 0.1726 | 0.5889 |

Cold SDK runtime creation/import, service binding and first captured identity
measurement take11.8277ms combined. These observations address
small read calls on an existing local connection, not a whole process contract,
model/tool effects or concurrent-worker throughput. The user target of about10ms
per simple IPC call is met by both measured methods in this fixture.

Every scalar response equals API version1; every identity response retains the
same server/generation/protocol. Client/backend PIDs differ; source/runtime
hashes remain unchanged. No sessions or model calls are created. Only the exact
spawned backend is birth/image checked and retired, with exit observed.

[Raw call measurements](evidence/xlang3-ipc-call-latency-20261023.json) and
[separate startup measurement](evidence/xlang3-ipc-call-latency-20261023-measurement.json)
are preserved. This is latency evidence, not new-runtime delivery acceptance:
the earlier full Rust Analyzer failure remains unresolved; installed62 is
unchanged. No CPython or paid inference is used.
