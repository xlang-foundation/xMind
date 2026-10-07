#pragma once
#include "agentflow/xlang_sqlite.hpp"
// Reconstruct older schemas only in contract-owned databases, rather than
// lying about a v5 database's version while retaining its new columns/indexes.
inline void remove_graph_schema_fixture(agentflow::XlangSqlite& database){
    for(const auto* sql:{"DROP TABLE incoming_messages","DROP TABLE task_messages","DROP TABLE task_history_owners","DROP TRIGGER graph_child_boundary","DROP TABLE graph_roots","DROP INDEX graph_child_identity","DROP INDEX execution_messages","DROP INDEX one_active_run","ALTER TABLE messages DROP COLUMN execution_run_id","ALTER TABLE runs DROP COLUMN node_id","ALTER TABLE runs DROP COLUMN parent_run_id","CREATE UNIQUE INDEX one_active_run ON runs(session_id) WHERE state IN ('queued','running','paused')"})database.execute(sql);
}
