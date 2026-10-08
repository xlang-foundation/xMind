# xMind OSS database backend

xMind OSS uses local SQLite. C++ owns repository contracts, schema migration, transactions, durable sequence numbers, startup ownership and credential encryption; embedded xlang3 performs all database I/O. The selected backend process owns the database file and handle lifetime. Clients use its API. Public metadata and encrypted credential blobs remain separate.

PostgreSQL is excluded from OSS and belongs to Nexus. The candidate driver lockfile and historical multi-server database design were moved to the private workspace; no implemented PostgreSQL adapter was found or claimed. SQLite tests do not establish PostgreSQL support.

See the [OSS specification](architecture.md), [persistence service](persistence-service.md) and recorded native persistence contracts for current implementation scope.
