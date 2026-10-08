#pragma once
#include "agentflow/xlang_sqlite.hpp"
// Remove v10 structures before reconstructing genuine v7/v8/v9 fixtures.
// These contract-owned older runs have no delegation records.
inline void remove_delegation_schema_fixture(agentflow::XlangSqlite& database){
    for(const auto* sql:{"DROP TRIGGER owned_child_boundary","DROP TRIGGER delegation_task_initial_outcome","DROP TRIGGER delegation_task_identity_immutable","DROP TRIGGER delegation_task_settlement_boundary","DROP TRIGGER delegation_batch_identity_immutable","DROP TRIGGER agent_budget_identity_immutable","DROP TRIGGER model_call_identity_immutable","DROP TABLE delegation_tasks","DROP TABLE delegation_batches","DROP TABLE agent_model_call_reservations","DROP TABLE agent_execution_budgets"})database.execute(sql);
    database.execute("CREATE TRIGGER graph_child_boundary BEFORE INSERT ON runs WHEN NEW.parent_run_id IS NOT NULL BEGIN SELECT CASE WHEN NEW.node_id IS NULL OR NOT EXISTS(SELECT 1 FROM runs r JOIN graph_roots g ON g.run_id=r.id WHERE r.id=NEW.parent_run_id AND r.parent_run_id IS NULL AND r.session_id=NEW.session_id AND r.state='running') THEN RAISE(ABORT,'invalid graph child boundary') END; END");
}
// Reconstruct older schemas only in contract-owned databases, rather than
// lying about a v5 database's version while retaining its new columns/indexes.
inline void remove_graph_schema_fixture(agentflow::XlangSqlite& database){
    remove_delegation_schema_fixture(database);
    for(const auto* sql:{"DROP TABLE run_status_clock","DROP TABLE incoming_messages","DROP TABLE task_messages","DROP TABLE task_history_owners","DROP TRIGGER graph_child_boundary","DROP TABLE graph_roots","DROP INDEX graph_child_identity","DROP INDEX execution_messages","DROP INDEX one_active_run","ALTER TABLE messages DROP COLUMN execution_run_id","ALTER TABLE runs DROP COLUMN node_id","ALTER TABLE runs DROP COLUMN parent_run_id","CREATE UNIQUE INDEX one_active_run ON runs(session_id) WHERE state IN ('queued','running','paused')"})database.execute(sql);
}
