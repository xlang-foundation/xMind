#pragma once
#include "agentflow/xlang_sqlite.hpp"
#include <stdexcept>
#include <utility>

// Only initial EMPTY skill and agent selections can be removed from these
// disposable legacy fixtures. Activated or later-revised guidance must never
// be discarded.
inline void remove_skill_schema_fixture(agentflow::XlangSqlite& database){
    const auto scalar=[&](const std::string& sql){return std::get<std::int64_t>(database.execute(sql).rows.at(0).at(0));};
    const auto version=scalar("PRAGMA user_version");if(version==12)return;
    if((version!=13&&version!=14)||scalar("SELECT count(*) FROM session_skills")!=0||scalar("SELECT count(*) FROM run_skills WHERE json_array_length(selections_json,'$.ids')!=0 OR revision!=1")!=0||(version==14&&scalar("SELECT count(*) FROM session_agents")!=0))throw std::runtime_error("Legacy fixture cannot discard actual skill or agent selection authority");
    database.execute("SAVEPOINT reconstruct_legacy_skills");try{
        for(const auto* trigger:{"run_skill_identity","run_skill_insert_owner","run_skill_update_owner"})database.execute(std::string("DROP TRIGGER ")+trigger);
        database.execute("DROP TABLE run_skills");database.execute("DROP TABLE session_skills");if(version==14)database.execute("DROP TABLE session_agents");database.execute("PRAGMA user_version=12");database.execute("RELEASE reconstruct_legacy_skills");
    }catch(...){database.execute("ROLLBACK TO reconstruct_legacy_skills");database.execute("RELEASE reconstruct_legacy_skills");throw;}
}
// Reverse only schema12 additions in disposable legacy-migration fixtures.
// Exact original v11 parent DDL is retained: preserving the v12 CHECK/FK with a
// lower version label would not exercise the genuine historical migration.
// SAVEPOINT supports both existing fixture transactions and autocommit callers.
inline void remove_context_schema_fixture(agentflow::XlangSqlite& database){
    remove_skill_schema_fixture(database);
    const auto scalar=[&](const std::string& sql){return std::get<std::int64_t>(database.execute(sql).rows.at(0).at(0));};
    const auto quoted=[](const std::string& name){if(name.empty()||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)throw std::runtime_error("Invalid disposable fixture schema identity");return "\""+name+"\"";};
    if(scalar("PRAGMA user_version")!=12||scalar("PRAGMA foreign_keys")!=1||!database.execute("PRAGMA foreign_key_check").rows.empty())throw std::runtime_error("Legacy reconstruction requires valid schema12 fixture ownership");
    for(const auto* table:{"inference_steps","context_compactions","context_manual_requests","context_measures","context_idle_owners","graph_context_owners"})if(scalar(std::string("SELECT count(*) FROM ")+table)!=0)throw std::runtime_error("Legacy fixture cannot discard actual schema12 authority");
    if(scalar("SELECT count(*) FROM agent_model_call_reservations WHERE purpose!='inference' OR inference_step_id IS NOT NULL")!=0||scalar("SELECT count(*) FROM agent_execution_budgets WHERE max_children NOT BETWEEN 1 AND 8 OR max_parallel NOT BETWEEN 1 AND 2 OR policy_id NOT IN ('native.delegation','native.dynamic-plan')")!=0)throw std::runtime_error("Legacy fixture contains a modern execution owner");
    const auto was_deferred=scalar("PRAGMA defer_foreign_keys");
    database.execute("SAVEPOINT reconstruct_legacy_context");
    try{
        database.execute("PRAGMA defer_foreign_keys=ON");
        for(const auto& row:database.execute("SELECT name,sql FROM sqlite_master WHERE type='trigger'").rows){const auto& sql=std::get<std::string>(row[1]);if(sql.find("context_")!=std::string::npos||sql.find("conversation_groups")!=std::string::npos||sql.find("inference_")!=std::string::npos)database.execute("DROP TRIGGER "+quoted(std::get<std::string>(row[0])));}
        for(const auto* table:{"inference_output_evidence","inference_attempt_members","inference_steps","context_manual_requests","context_measures","context_checkpoints","context_events","context_compactions","context_heads","context_group_members","conversation_groups","context_idle_owners","context_scopes","graph_context_owners"})database.execute(std::string("DROP TABLE ")+table);
        database.execute("ALTER TABLE agent_model_call_reservations DROP COLUMN inference_step_id");
        database.execute("ALTER TABLE agent_model_call_reservations DROP COLUMN purpose");
        database.execute("ALTER TABLE dynamic_owner_pauses DROP COLUMN context_pin");
        std::vector<std::pair<std::string,std::string>> budget_triggers;
        for(const auto& row:database.execute("SELECT name,sql FROM sqlite_master WHERE type='trigger' AND instr(sql,'agent_execution_budgets')>0 ORDER BY name").rows){budget_triggers.emplace_back(std::get<std::string>(row[0]),std::get<std::string>(row[1]));database.execute("DROP TRIGGER "+quoted(budget_triggers.back().first));}
        database.execute("CREATE TABLE fixture_v11_budget_copy AS SELECT * FROM agent_execution_budgets");
        database.execute("DROP TABLE agent_execution_budgets");
        database.execute("CREATE TABLE agent_execution_budgets(root_run_id TEXT PRIMARY KEY NOT NULL REFERENCES runs(id),policy_id TEXT NOT NULL,policy_revision INTEGER NOT NULL CHECK(policy_revision>0),workspace_identity TEXT NOT NULL,provider_identity_json TEXT NOT NULL CHECK(json_valid(provider_identity_json) AND json_type(provider_identity_json)='object'),max_children INTEGER NOT NULL CHECK(max_children BETWEEN 1 AND 8),max_parallel INTEGER NOT NULL CHECK(max_parallel BETWEEN 1 AND 2),max_model_calls INTEGER NOT NULL CHECK(max_model_calls BETWEEN 1 AND 32),wall_limit_ms INTEGER NOT NULL CHECK(wall_limit_ms BETWEEN 1 AND 3600000),children_admitted INTEGER NOT NULL DEFAULT 0 CHECK(children_admitted BETWEEN 0 AND max_children),model_calls_reserved INTEGER NOT NULL DEFAULT 0 CHECK(model_calls_reserved BETWEEN 0 AND max_model_calls),parent_calls_held INTEGER NOT NULL DEFAULT 0 CHECK(parent_calls_held>=0),revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0),CHECK(model_calls_reserved+parent_calls_held<=max_model_calls))");
        database.execute("ALTER TABLE agent_execution_budgets ADD COLUMN planned_children_reserved INTEGER NOT NULL DEFAULT 0 CHECK(planned_children_reserved>=0 AND planned_children_reserved+children_admitted<=max_children)");
        database.execute("INSERT INTO agent_execution_budgets SELECT * FROM fixture_v11_budget_copy");
        database.execute("DROP TABLE fixture_v11_budget_copy");
        for(const auto& trigger:budget_triggers)database.execute(trigger.second);
        std::vector<std::pair<std::string,std::string>> segment_triggers;
        for(const auto& row:database.execute("SELECT name,sql FROM sqlite_master WHERE type='trigger' AND instr(sql,'agent_budget_segments')>0 ORDER BY name").rows){segment_triggers.emplace_back(std::get<std::string>(row[0]),std::get<std::string>(row[1]));database.execute("DROP TRIGGER "+quoted(segment_triggers.back().first));}
        database.execute("CREATE TABLE fixture_v11_segment_copy AS SELECT * FROM agent_budget_segments");
        database.execute("DROP TABLE agent_budget_segments");
        database.execute("CREATE TABLE agent_budget_segments(root_run_id TEXT NOT NULL REFERENCES dynamic_capabilities(root_run_id),id TEXT NOT NULL UNIQUE,ordinal INTEGER NOT NULL CHECK(ordinal>0),state TEXT NOT NULL CHECK(state IN ('open','closed','interrupted')),active_elapsed_ms INTEGER NOT NULL DEFAULT 0 CHECK(active_elapsed_ms>=0),remaining_active_ms INTEGER NOT NULL CHECK(remaining_active_ms>=0),opened_event_seq INTEGER NOT NULL REFERENCES events(seq),closed_event_seq INTEGER REFERENCES events(seq),PRIMARY KEY(root_run_id,ordinal),CHECK((state='open')=(closed_event_seq IS NULL)))");
        database.execute("INSERT INTO agent_budget_segments SELECT * FROM fixture_v11_segment_copy");
        database.execute("DROP TABLE fixture_v11_segment_copy");
        database.execute("CREATE UNIQUE INDEX one_open_budget_segment ON agent_budget_segments(root_run_id) WHERE state='open'");
        for(const auto& trigger:segment_triggers)database.execute(trigger.second);
        if(!database.execute("PRAGMA foreign_key_check").rows.empty())throw std::runtime_error("Legacy reconstruction changed original foreign key ownership");
        // Only clear SQLite's obsolete DROP-parent deferred counter after the
        // complete rebuilt database passes FK validation; no writes follow.
        database.execute("PRAGMA defer_foreign_keys=OFF");
        if(scalar("PRAGMA foreign_keys")!=1||scalar("PRAGMA defer_foreign_keys")!=0)throw std::runtime_error("Legacy fixture lost foreign key enforcement");
        database.execute("RELEASE reconstruct_legacy_context");
        if(was_deferred!=0)database.execute("PRAGMA defer_foreign_keys=ON");
    }catch(...){database.execute("ROLLBACK TO reconstruct_legacy_context");database.execute("RELEASE reconstruct_legacy_context");database.execute(was_deferred!=0?"PRAGMA defer_foreign_keys=ON":"PRAGMA defer_foreign_keys=OFF");throw;}
}
// Remove v11 relations and additive columns in disposable contract databases.
// This reconstructs the real old schema; a version-number lie is insufficient.
inline void remove_dynamic_schema_fixture(agentflow::XlangSqlite& database){
    remove_context_schema_fixture(database);
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
