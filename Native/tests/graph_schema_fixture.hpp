#pragma once
#include "agentflow/xlang_sqlite.hpp"
// Remove v11 relations and additive columns in disposable contract databases.
// This reconstructs the real old schema; a version-number lie is insufficient.
inline void remove_dynamic_schema_fixture(agentflow::XlangSqlite& database){
    // Remove this reference before dropping tables: SQLite reparses the whole
    // schema when the two additive v11 columns are removed below.
    database.execute("DROP TRIGGER owned_child_boundary");
    for(const auto* trigger:{"actual_model_response_once","dynamic_capability_identity","dynamic_revision_immutable","dynamic_revision_node_immutable","dynamic_edge_immutable","dynamic_definition_protected","dynamic_claim_immutable","dynamic_child_settlement","dynamic_call_identity","dynamic_call_result","dynamic_call_commit","dynamic_call_tool_completion","dynamic_call_continuation","dynamic_human_identity","dynamic_human_input","dynamic_human_settlement","dynamic_plan_identity","dynamic_initial_node","dynamic_claim_boundary","dynamic_capability_admission","dynamic_signed_call_boundary"})
        database.execute(std::string("DROP TRIGGER ")+trigger);
    for(const auto* table:{"dynamic_capabilities","dynamic_plans","dynamic_plan_calls","dynamic_plan_revisions","dynamic_plan_nodes","dynamic_revision_nodes","dynamic_plan_edges","dynamic_human_requests","agent_budget_segments","dynamic_owner_pauses"})
        database.execute(std::string("DROP TRIGGER retain_")+table);
    for(const auto* table:{"dynamic_owner_pauses","dynamic_plan_edges","dynamic_revision_nodes","dynamic_human_requests","dynamic_plan_nodes","dynamic_plan_revisions","dynamic_plan_calls","dynamic_plans","agent_budget_segments","dynamic_capabilities"})
        database.execute(std::string("DROP TABLE ")+table);
    database.execute("ALTER TABLE agent_model_call_reservations DROP COLUMN actual_assistant_json");
    database.execute("ALTER TABLE agent_execution_budgets DROP COLUMN planned_children_reserved");
    database.execute("CREATE TRIGGER owned_child_boundary BEFORE INSERT ON runs WHEN NEW.parent_run_id IS NOT NULL BEGIN SELECT CASE WHEN NEW.node_id IS NULL OR NOT (EXISTS(SELECT 1 FROM runs p JOIN graph_roots g ON g.run_id=p.id WHERE p.id=NEW.parent_run_id AND p.parent_run_id IS NULL AND p.session_id=NEW.session_id AND p.state='running') OR EXISTS(SELECT 1 FROM delegation_tasks t JOIN delegation_batches b ON b.id=t.batch_id JOIN agent_execution_budgets e ON e.root_run_id=b.root_run_id JOIN runs p ON p.id=b.parent_run_id WHERE t.child_run_id=NEW.id AND t.node_id=NEW.node_id AND b.parent_run_id=NEW.parent_run_id AND b.root_run_id=b.parent_run_id AND b.state IN ('accepted','working') AND p.parent_run_id IS NULL AND p.state='running' AND p.session_id=NEW.session_id AND NEW.state='queued' AND NOT EXISTS(SELECT 1 FROM graph_roots g WHERE g.run_id=p.id))) THEN RAISE(ABORT,'invalid owned child boundary') END; END");
}
// Remove v10 structures before reconstructing genuine v7/v8/v9 fixtures.
// These contract-owned older runs have no delegation records.
inline void remove_delegation_schema_fixture(agentflow::XlangSqlite& database){
    remove_dynamic_schema_fixture(database);
    for(const auto* sql:{"DROP TRIGGER owned_child_boundary","DROP TRIGGER delegation_task_initial_outcome","DROP TRIGGER delegation_task_identity_immutable","DROP TRIGGER delegation_task_settlement_boundary","DROP TRIGGER delegation_batch_identity_immutable","DROP TRIGGER agent_budget_identity_immutable","DROP TRIGGER model_call_identity_immutable","DROP TABLE delegation_tasks","DROP TABLE delegation_batches","DROP TABLE agent_model_call_reservations","DROP TABLE agent_execution_budgets"})database.execute(sql);
    database.execute("CREATE TRIGGER graph_child_boundary BEFORE INSERT ON runs WHEN NEW.parent_run_id IS NOT NULL BEGIN SELECT CASE WHEN NEW.node_id IS NULL OR NOT EXISTS(SELECT 1 FROM runs r JOIN graph_roots g ON g.run_id=r.id WHERE r.id=NEW.parent_run_id AND r.parent_run_id IS NULL AND r.session_id=NEW.session_id AND r.state='running') THEN RAISE(ABORT,'invalid graph child boundary') END; END");
}
// Reconstruct older schemas only in contract-owned databases, rather than
// lying about a v5 database's version while retaining its new columns/indexes.
inline void remove_graph_schema_fixture(agentflow::XlangSqlite& database){
    remove_delegation_schema_fixture(database);
    for(const auto* sql:{"DROP TABLE run_status_clock","DROP TABLE incoming_messages","DROP TABLE task_messages","DROP TABLE task_history_owners","DROP TRIGGER graph_child_boundary","DROP TABLE graph_roots","DROP INDEX graph_child_identity","DROP INDEX execution_messages","DROP INDEX one_active_run","ALTER TABLE messages DROP COLUMN execution_run_id","ALTER TABLE runs DROP COLUMN node_id","ALTER TABLE runs DROP COLUMN parent_run_id","CREATE UNIQUE INDEX one_active_run ON runs(session_id) WHERE state IN ('queued','running','paused')"})database.execute(sql);
}
