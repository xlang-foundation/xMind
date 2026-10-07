# xMind Server database backends

The server can select SQLite or PostgreSQL. The C++ core owns domain rules and exposes the same repository operations for sessions, messages, runs, events, approvals, configuration and encrypted credentials. Embedded xlang3 performs database I/O. Clients select a server; they do not depend on its database dialect.

| Backend | Initial use | Access |
| --- | --- | --- |
| SQLite | Local development or one shared server | Existing xlang3 SQLite native binding; server-local file |
| PostgreSQL | Shared company server | Planned xlang3 adapter using a verified pure-Python PostgreSQL driver |

Database configuration belongs to xMind Server. PostgreSQL connection credentials are supplied privately to the server, outside public configuration and events. The database connection itself must not depend on retrieving its own credentials from that database.

## Adapter responsibilities

Keep SQL statements, parameter binding, schema migrations and database-specific types in each adapter. Preserve atomic state/event commits, one active root run per session, expected-state checks and monotonic per-session event replay. Do not convert SQLite SQL by replacing parameter markers in strings.

SQLite retains its server ownership lease and explicit transaction behavior. PostgreSQL needs database-coordinated run leases and transaction/row locking; a local sidecar file cannot establish ownership across hosts. PostgreSQL availability does not by itself make the server safe to run in multiple instances.

Encrypted credential bytes are stored as SQLite blobs or PostgreSQL bytea. Public metadata remains separate. A shared PostgreSQL database does not make Windows DPAPI ciphertext portable between server identities; multiple hosts need a defined shared key protection and rotation strategy.

## Delivery status and next acceptance

SQLite persistence passed the native C++/embedded-xlang3 milestone, including restart and event cursor replay. PostgreSQL is an architectural choice; no PostgreSQL adapter or end-to-end PostgreSQL test has passed yet.

Evaluate a pure-Python driver such as pg8000 under xlang3, installed with xlang3 pip. Verify imports, TLS and authentication against a development server. Discuss any missing xlang3 native APIs before changing runtime code. Then run the same domain contract suite against both adapters, including concurrent writes, rollback, reconnect, migration and interrupted-run ownership. Remote/team authorization remains a separate acceptance requirement.
