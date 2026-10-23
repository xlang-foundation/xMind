Current candidate implements the typed boundary below; focused 25/25 passes.
Full-suite and package qualification remain pending. See [validation status](VALIDATION_STATUS.md).

# Remaining worker client transport cleanup

The installed 0.1.61 runtime uses xlang3 SDK remote objects. It does not use the
packet fixture as a product transport. The shared adapter source nevertheless
retains 22 generic call sites in nine files (eight translation units and one
internal header). The source-hashed inventory is in
[evidence/worker-client-fallback-audit-20261022.json](evidence/worker-client-fallback-audit-20261022.json).
This is static evidence, not additional runtime acceptance.

## Implementation boundary

Replace transport-shaped AgentWorkerClientContext access with typed worker service
contracts for model, journal, reads, files, processes, remotes and callables. Keep
captured scope and service lifetime together. Product implementations bind these
contracts to existing NativeWorkerModelClient SDK objects; they must reject absent
services at construction. Do not introduce another string method router, optional
packet fallback, public transport selector or mutable client binder.

Move packet invocation, packet response parsing and packet error translation into
BUILD_TESTING-only fixture implementations of those contracts. FixtureWorkerRpcPeer
owns transport and fault injection; it must no longer be the product runner's
transport interface. SDK-backed fixtures retain actual SDK services. Preserve
streaming sinks, cancellation tokens, deadlines and nested journal calls.

Keep domain validation shared: captured owner/root/workspace/generation, budget
reservation purpose and role, journal sequence/order, acknowledgement bindings,
effect uncertainty and process join evidence. Moving transport parsing must not
remove these checks or make fixture success stand in for SDK acceptance.

## Verification required before commit

1. Inspect benchmark and native build/test processes; run only one native gate.
2. Freeze current project, SDK, pure libraries, runtime and dependency file sets
   and hashes, including newly added/deleted files, around build and testing.
3. Run worker model/journal/read/coding/remote/callable/process/ledger/retirement
   contracts, including real SDK child execution, malformed results, cancellation,
   uncertain acknowledgement and owner rejection. Preserve any failed attempt.
4. Verify product source and generated links exclude generic packet clients and
   fixture implementations; retain fixture-only packet fault coverage.
5. Compare the raw cached Native patch SHA-256 with the accepted gate before
   committing. Run the complete Native inventory before a new delivery package.
6. Repackage and perform a fresh isolated coding acceptance task when runtime bytes
   change. Never replay any completed acceptance task. Preserve installed 0.1.61
   and existing settings/history until a verified replacement is ready.

This cleanup does not close actual VS Code rendering, generic connection binding,
nested child planning or recovery requirements. Those remain part of delivery.
