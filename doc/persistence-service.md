# Native persistence service

`PersistenceService` is the C++ request boundary between server/agent workers and the repository. Its private thread acquires the backend lease, constructs the embedded-xlang3 repository, performs startup recovery, handles queued requests, destroys the repository and releases the lease. It accepts and returns owned C++ values through typed methods and futures; neither callers nor views receive runtime objects or a raw repository callback.

Requests execute in FIFO admission order. Repository failures propagate through the corresponding future; later requests can still execute. The pending queue has a configurable positive request-count limit (default 1024). Full queues reject synchronously with `PersistenceBusy`; callers must apply backpressure. This limit is not a byte quota or a global server memory limit. HTTP request/body limits and client rate limits remain service-layer work.

`close()` stops admission, completes all accepted requests, joins the thread and releases ownership. Later submissions throw `PersistenceClosed`. Concurrent close calls are serialized. Callers must keep the service alive until their calls finish. Startup failures propagate to the constructor after the worker joins, releasing any acquired lease. An unexpected worker failure destroys queued tasks so their futures become broken promises rather than wait forever.

The credential methods are internal backend operations. Scope authorization must be enforced by the calling service; returning a move-only secret to a provider worker does not authorize exposing it through HTTP or events. Input/request copies remain their owner's responsibility.

## Historical component verification

The Release build and all five native contracts passed. `persistence_service_contract` exercised four concurrent callers writing 100 messages, owned argument copies, request failure isolation, competing transitions, exclusive ownership, restart recovery, preserved paused runs, move-only Windows credentials, concurrent close/draining, bounded admission under a held write transaction and lease cleanup after failed initialization. Evidence: [persistence-service-ctest.log](evidence/persistence-service-ctest.log).

This five-contract checkpoint verified the in-process component. Native HTTP/CLI, local-owner authentication and provider/tool execution were added in later checkpoints; see [current validation](VALIDATION_STATUS.md). PostgreSQL belongs to private Nexus. Recovery preserves recorded effect uncertainty and does not automatically replay interrupted model/tool work.
