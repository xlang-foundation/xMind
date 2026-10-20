#pragma once
#include "agentflow/xlang_sqlite.hpp"
#include <stdexcept>
#include <utility>

// Only initial EMPTY skill and agent selections can be removed from these
// disposable legacy fixtures. Activated or later-revised guidance must never
// be discarded.
inline void remove_dynamic_plan_multiplicity_fixture(agentflow::XlangSqlite& database){
    const auto scalar=[&](const std::string& sql){return std::get<std::int64_t>(database.execute(sql).rows.at(0).at(0));};
    if(scalar("PRAGMA user_version")<26)return;
    if(scalar("SELECT count(*) FROM dynamic_plan_calls WHERE owner_run_id!=root_run_id")!=0)
        throw std::runtime_error("Legacy fixture cannot discard non-root dynamic call owners");
    std::string call_stage="defer foreign keys";
    database.execute("SAVEPOINT downgrade_dynamic_call_owner");
    try{
        database.execute("PRAGMA defer_foreign_keys=ON");
        std::vector<std::pair<std::string,std::string>> call_triggers;
        call_stage="collect call triggers";
        for(const auto& row:database.execute("SELECT name,sql FROM sqlite_master WHERE type='trigger' AND (tbl_name='dynamic_plan_calls' OR instr(sql,'dynamic_plan_calls')>0) ORDER BY name").rows){
            const auto name=std::get<std::string>(row[0]);if(name.empty()||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)throw std::runtime_error("Legacy fixture found an invalid dynamic call trigger identity");
            call_triggers.emplace_back(name,std::get<std::string>(row[1]));database.execute("DROP TRIGGER \""+name+"\"");
        }
        call_stage="preserve v26 calls";
        database.execute("CREATE TABLE dynamic_call_fixture_v25 AS SELECT id,plan_id,root_run_id,provider_tool_call_id,origin_attempt_id,arguments_json,parent_assistant_json,state,accepted_revision,accepted_event_seq,result_json,result_event_seq,conversation_commit_seq,continuation_attempt_id FROM dynamic_plan_calls");
        call_stage="replace call relation";database.execute("DROP TABLE dynamic_plan_calls");
        database.execute("CREATE TABLE dynamic_plan_calls(id TEXT PRIMARY KEY NOT NULL,plan_id TEXT NOT NULL REFERENCES dynamic_plans(id),root_run_id TEXT NOT NULL REFERENCES dynamic_capabilities(root_run_id),provider_tool_call_id TEXT NOT NULL,origin_attempt_id TEXT NOT NULL,arguments_json TEXT NOT NULL CHECK(json_valid(arguments_json) AND json_type(arguments_json)='object'),parent_assistant_json TEXT NOT NULL CHECK(json_valid(parent_assistant_json) AND json_type(parent_assistant_json)='object'),state TEXT NOT NULL CHECK(state IN ('accepted','report_ready','turn_committed','interrupted','cancelled')),accepted_revision INTEGER NOT NULL,accepted_event_seq INTEGER NOT NULL REFERENCES events(seq),result_json TEXT CHECK(result_json IS NULL OR (json_valid(result_json) AND json_type(result_json)='object')),result_event_seq INTEGER REFERENCES events(seq),conversation_commit_seq INTEGER REFERENCES events(seq),continuation_attempt_id TEXT,UNIQUE(root_run_id,provider_tool_call_id),UNIQUE(root_run_id,continuation_attempt_id),FOREIGN KEY(root_run_id,origin_attempt_id) REFERENCES agent_model_call_reservations(root_run_id,attempt_id),FOREIGN KEY(root_run_id,continuation_attempt_id) REFERENCES agent_model_call_reservations(root_run_id,attempt_id),CHECK((result_json IS NULL)=(result_event_seq IS NULL)),CHECK(conversation_commit_seq IS NULL OR result_event_seq IS NOT NULL))");
        database.execute("INSERT INTO dynamic_plan_calls SELECT * FROM dynamic_call_fixture_v25");database.execute("DROP TABLE dynamic_call_fixture_v25");
        database.execute("CREATE UNIQUE INDEX dynamic_one_pending_call ON dynamic_plan_calls(root_run_id) WHERE state IN ('accepted','report_ready')");
        call_stage="restore v25 call triggers";
        for(const auto& trigger:call_triggers){
            auto sql=trigger.second;const auto replace_all=[&](const std::string& from,const std::string& to){std::size_t at=0;while((at=sql.find(from,at))!=std::string::npos){sql.replace(at,from.size(),to);at+=to.size();}};
            if(trigger.first=="dynamic_call_identity")sql="CREATE TRIGGER dynamic_call_identity BEFORE UPDATE OF id,plan_id,root_run_id,provider_tool_call_id,origin_attempt_id,arguments_json,parent_assistant_json,accepted_revision,accepted_event_seq ON dynamic_plan_calls BEGIN SELECT RAISE(ABORT,'signed dynamic call is immutable'); END";
            else if(trigger.first=="dynamic_call_result"||trigger.first=="dynamic_call_commit"||trigger.first=="dynamic_call_tool_completion"||trigger.first=="dynamic_call_continuation")replace_all("owner_run_id", "root_run_id");
            if(trigger.first=="dynamic_signed_call_boundary"){
                // The v25 boundary binds all calls to the ordinary root. The
                // exact historical definition is restored below.
                sql="CREATE TRIGGER dynamic_signed_call_boundary BEFORE INSERT ON dynamic_plan_calls BEGIN SELECT CASE WHEN NEW.state!='accepted' OR NEW.result_json IS NOT NULL OR NEW.result_event_seq IS NOT NULL OR NEW.conversation_commit_seq IS NOT NULL OR NEW.continuation_attempt_id IS NOT NULL OR json_array_length(NEW.parent_assistant_json,'$.tool_calls')!=1 OR json_extract(NEW.parent_assistant_json,'$.tool_calls[0].id')!=NEW.provider_tool_call_id OR json_extract(NEW.parent_assistant_json,'$.tool_calls[0].arguments')!=NEW.arguments_json OR json_extract(NEW.parent_assistant_json,'$.tool_calls[0].name')!=CASE WHEN NEW.accepted_revision=1 THEN 'plan_tasks' ELSE 'revise_plan' END OR NOT EXISTS(SELECT 1 FROM dynamic_plans p JOIN runs r ON r.id=p.root_run_id JOIN dynamic_capabilities a ON a.root_run_id=r.id JOIN agent_model_call_reservations m ON m.root_run_id=r.id JOIN events e ON e.seq=NEW.accepted_event_seq WHERE p.id=NEW.plan_id AND p.root_run_id=NEW.root_run_id AND p.revision=NEW.accepted_revision AND r.state='running' AND a.catalogue_finalized=1 AND m.attempt_id=NEW.origin_attempt_id AND m.owner_run_id=r.id AND m.role='parent' AND m.state='finished' AND m.actual_assistant_json=NEW.parent_assistant_json AND e.run_id=r.id AND e.kind='plan.call.accepted' AND json_extract(e.payload,'$.plan_call_id')=NEW.id AND EXISTS(SELECT 1 FROM events t WHERE t.run_id=r.id AND t.kind='budget.model_call.finished' AND json_extract(t.payload,'$.attempt_id')=m.attempt_id AND json_extract(t.payload,'$.was_started')=1)) THEN RAISE(ABORT,'invalid signed dynamic call boundary') END; END";
            }
            database.execute(sql);
        }
        call_stage="check call foreign keys";if(!database.execute("PRAGMA foreign_key_check").rows.empty())throw std::runtime_error("Legacy fixture call downgrade violated foreign-key ownership");
        database.execute("PRAGMA defer_foreign_keys=OFF");database.execute("PRAGMA user_version=25");database.execute("RELEASE downgrade_dynamic_call_owner");
    }catch(const std::exception& error){database.execute("ROLLBACK TO downgrade_dynamic_call_owner");database.execute("RELEASE downgrade_dynamic_call_owner");throw std::runtime_error("Legacy fixture v26 downgrade failed at "+call_stage+": "+error.what());}
    if(scalar("SELECT count(*) FROM (SELECT root_run_id FROM dynamic_plans GROUP BY root_run_id HAVING count(*)>1)")!=0)
        throw std::runtime_error("Legacy fixture cannot discard multiple dynamic plans for one root");
    std::string stage="defer foreign keys";
    database.execute("SAVEPOINT downgrade_dynamic_plan_multiplicity");
    try{
        database.execute("PRAGMA defer_foreign_keys=ON");
        std::vector<std::pair<std::string,std::string>> plan_triggers;
        stage="collect dependent triggers";
        for(const auto& row:database.execute("SELECT name,sql FROM sqlite_master WHERE type='trigger' AND (tbl_name='dynamic_plans' OR instr(sql,'dynamic_plans')>0) ORDER BY name").rows){
            const auto name=std::get<std::string>(row[0]);if(name.empty()||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)throw std::runtime_error("Legacy fixture found an invalid dynamic plan trigger identity");
            plan_triggers.emplace_back(name,std::get<std::string>(row[1]));database.execute("DROP TRIGGER \""+name+"\"");
        }
        stage="preserve v25 plans";
        database.execute("CREATE TABLE dynamic_plan_fixture_v24 AS SELECT id,root_run_id,revision,state_sequence,state,retired_labels_json,planned_children_reserved,planned_humans_reserved,humans_published FROM dynamic_plans");
        stage="drop v25 plan table";
        database.execute("DROP TABLE dynamic_plans");
        stage="restore v24 plan table";
        database.execute("CREATE TABLE dynamic_plans(id TEXT PRIMARY KEY NOT NULL,root_run_id TEXT NOT NULL UNIQUE REFERENCES dynamic_capabilities(root_run_id),revision INTEGER NOT NULL CHECK(revision>0),state_sequence INTEGER NOT NULL CHECK(state_sequence>0),state TEXT NOT NULL CHECK(state IN ('active','waiting_human','completed','failed','cancelled','uncertain','interrupted')),retired_labels_json TEXT NOT NULL CHECK(json_valid(retired_labels_json) AND json_type(retired_labels_json)='array'),planned_children_reserved INTEGER NOT NULL CHECK(planned_children_reserved>=0),planned_humans_reserved INTEGER NOT NULL CHECK(planned_humans_reserved>=0),humans_published INTEGER NOT NULL DEFAULT 0 CHECK(humans_published>=0))");
        stage="copy v24 plans";
        database.execute("INSERT INTO dynamic_plans SELECT id,root_run_id,revision,state_sequence,state,retired_labels_json,planned_children_reserved,planned_humans_reserved,humans_published FROM dynamic_plan_fixture_v24");
        database.execute("DROP TABLE dynamic_plan_fixture_v24");
        stage="restore triggers";
        for(const auto& trigger:plan_triggers)database.execute(trigger.second);
        stage="check foreign keys";
        if(!database.execute("PRAGMA foreign_key_check").rows.empty())throw std::runtime_error("Legacy fixture multiplicity downgrade violated foreign-key ownership");
        database.execute("PRAGMA defer_foreign_keys=OFF");
        database.execute("PRAGMA user_version=24");
        database.execute("RELEASE downgrade_dynamic_plan_multiplicity");
    }catch(const std::exception& error){database.execute("ROLLBACK TO downgrade_dynamic_plan_multiplicity");database.execute("RELEASE downgrade_dynamic_plan_multiplicity");throw std::runtime_error("Legacy fixture v25 downgrade failed at "+stage+": "+error.what());}
}
inline void remove_graph_invocation_schema_fixture(agentflow::XlangSqlite& database){
    if(std::get<std::int64_t>(database.execute("PRAGMA user_version").rows[0][0])<34)return;
    if(std::get<std::int64_t>(database.execute("SELECT count(*) FROM graph_invocations").rows[0][0])!=0)throw std::runtime_error("Legacy fixture cannot discard actual nested graph ownership");
    auto trigger=std::get<std::string>(database.execute("SELECT sql FROM sqlite_master WHERE name='owned_child_boundary'").rows[0][0]);
    const std::string current="(p.parent_run_id IS NULL OR EXISTS(SELECT 1 FROM graph_invocations gi JOIN runs root ON root.id=gi.root_run_id WHERE gi.run_id=p.id AND root.parent_run_id IS NULL AND root.state='running' AND root.session_id=NEW.session_id AND NOT EXISTS(SELECT 1 FROM graph_context_owners co WHERE co.root_run_id=root.id)))";
    const auto at=trigger.find(current);if(at==std::string::npos)throw std::runtime_error("Legacy fixture found an incompatible graph child boundary");trigger.replace(at,current.size(),"p.parent_run_id IS NULL");
    database.execute("DROP TRIGGER owned_child_boundary");database.execute(trigger);database.execute("DROP TRIGGER immutable_graph_invocations");database.execute("DROP TRIGGER retain_graph_invocations");database.execute("DROP TRIGGER graph_invocation_admission");database.execute("DROP TABLE graph_invocations");database.execute("PRAGMA user_version=33");
}
inline void remove_dynamic_plan_owner_scope_fixture(agentflow::XlangSqlite& database){
    remove_graph_invocation_schema_fixture(database);
    const auto scalar=[&](const std::string& sql){return std::get<std::int64_t>(database.execute(sql).rows.at(0).at(0));};
    if(scalar("PRAGMA user_version")>=33){if(scalar("SELECT count(*) FROM mcp_input_requests")!=0)throw std::runtime_error("Legacy fixture cannot discard actual pending MCP input requests");database.execute("DROP TRIGGER retain_mcp_input_requests");database.execute("DROP TRIGGER mcp_input_request_settlement");database.execute("DROP TRIGGER mcp_input_request_identity");database.execute("DROP TRIGGER mcp_input_request_insert");database.execute("DROP INDEX mcp_input_requests_run");database.execute("DROP TABLE mcp_input_requests");database.execute("PRAGMA user_version=32");}
    if(scalar("PRAGMA user_version")>=32){if(scalar("SELECT count(*) FROM session_agent_events")!=0)throw std::runtime_error("Legacy fixture cannot discard actual agent-switch events");database.execute("DROP INDEX session_agent_events_cursor");database.execute("DROP TABLE session_agent_events");database.execute("PRAGMA user_version=31");}
    if(scalar("PRAGMA user_version")>=29){database.execute("DROP TRIGGER IF EXISTS retain_dynamic_owner_pause_waits");database.execute("DROP TABLE IF EXISTS dynamic_owner_pause_waits");}
    remove_dynamic_plan_multiplicity_fixture(database);
    if(scalar("PRAGMA user_version")<24)return;
    const auto owners=scalar("SELECT count(*) FROM dynamic_plan_owners");
    const auto root_owners=scalar("SELECT count(*) FROM dynamic_plan_owners WHERE owner_run_id=root_run_id AND parent_plan_id IS NULL AND parent_node_label IS NULL AND depth=0");
    if(owners!=root_owners)throw std::runtime_error("Legacy fixture cannot discard nested dynamic plan ownership");
    database.execute("DROP TRIGGER dynamic_plan_owner_admission");
    database.execute("DROP TRIGGER dynamic_plan_owner_identity");
    database.execute("DROP TRIGGER retain_dynamic_plan_owners");
    database.execute("DROP INDEX dynamic_plan_owner_root_order");
    database.execute("DROP TABLE dynamic_plan_owners");
    database.execute("PRAGMA user_version=23");
}
inline void remove_skill_schema_fixture(agentflow::XlangSqlite& database){
    const auto scalar=[&](const std::string& sql){return std::get<std::int64_t>(database.execute(sql).rows.at(0).at(0));};
    auto version=scalar("PRAGMA user_version");if(version==12)return;
    remove_dynamic_plan_owner_scope_fixture(database);version=scalar("PRAGMA user_version");
    if(version>=21){if(scalar("SELECT count(*) FROM session_models")!=0)throw std::runtime_error("Legacy fixture cannot discard actual session model selection");database.execute("DROP TABLE session_models");database.execute("PRAGMA user_version=20");version=20;}
    if(version==20){if(scalar("SELECT count(*) FROM session_inbox")!=0)throw std::runtime_error("Legacy fixture cannot discard actual session inbox state");database.execute("DROP INDEX session_inbox_order");database.execute("DROP TABLE session_inbox");database.execute("PRAGMA user_version=18");version=18;}
    if(version==18){if(scalar("SELECT count(*) FROM a2a_continuation_messages")!=0)throw std::runtime_error("Legacy fixture cannot discard actual A2A continuation history");database.execute("DROP TABLE a2a_continuation_messages");database.execute("PRAGMA user_version=17");version=17;}
    // Schema 17 only widens the direct-agent budget CHECK. These fixtures
    // contain no direct-agent rows; the context reconstruction below replaces
    // that table with the exact historical schema when it needs to.
    if(version==17){database.execute("PRAGMA user_version=16");version=16;}
    if(version==16){
        if(scalar("SELECT count(*) FROM session_forks")!=0)
            throw std::runtime_error("Legacy fixture cannot discard actual session fork lineage");
        database.execute("DROP TABLE session_forks");database.execute("PRAGMA user_version=15");version=15;
    }
    if(version==15){
        if(scalar("SELECT count(*) FROM process_output_artifacts")!=0||scalar("SELECT count(*) FROM process_output_chunks")!=0)
            throw std::runtime_error("Legacy fixture cannot discard actual process output artifacts");
        database.execute("DROP TABLE process_output_chunks");database.execute("DROP TABLE process_output_artifacts");database.execute("PRAGMA user_version=14");version=14;
    }
    const auto session_skills=scalar("SELECT count(*) FROM session_skills");
    const auto changed_run_skills=scalar("SELECT count(*) FROM run_skills WHERE json_array_length(selections_json,'$.ids')!=0 OR revision!=1");
    const auto session_agents=version==14?scalar("SELECT count(*) FROM session_agents"):0;
    if((version!=13&&version!=14)||session_skills!=0||changed_run_skills!=0||session_agents!=0)
        throw std::runtime_error("Legacy fixture cannot discard actual skill or agent selection authority (schema="+std::to_string(version)+", session_skills="+std::to_string(session_skills)+", changed_run_skills="+std::to_string(changed_run_skills)+", session_agents="+std::to_string(session_agents)+")");
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
    for(const auto* trigger:{"actual_model_response_once","dynamic_capability_identity","dynamic_revision_immutable","dynamic_revision_node_immutable","dynamic_edge_immutable","dynamic_definition_protected","dynamic_claim_immutable","dynamic_child_settlement","dynamic_call_identity","dynamic_call_result","dynamic_call_commit","dynamic_call_tool_completion","dynamic_call_continuation","dynamic_human_identity","dynamic_human_input","dynamic_human_settlement","dynamic_plan_identity","dynamic_initial_node","dynamic_claim_boundary","dynamic_capability_admission","dynamic_signed_call_boundary","dynamic_plan_owner_admission","dynamic_plan_owner_identity","retain_dynamic_plan_owners"})
        database.execute(std::string("DROP TRIGGER IF EXISTS ")+trigger);
    for(const auto* table:{"dynamic_capabilities","dynamic_plans","dynamic_plan_calls","dynamic_plan_revisions","dynamic_plan_nodes","dynamic_revision_nodes","dynamic_plan_edges","dynamic_human_requests","agent_budget_segments","dynamic_owner_pauses","dynamic_plan_owners","dynamic_owner_pause_waits"})
        database.execute(std::string("DROP TRIGGER IF EXISTS retain_")+table);
    for(const auto* table:{"dynamic_owner_pause_waits","dynamic_owner_pauses","dynamic_plan_edges","dynamic_revision_nodes","dynamic_human_requests","dynamic_plan_nodes","dynamic_plan_revisions","dynamic_plan_calls","dynamic_plan_owners","dynamic_plans","agent_budget_segments","dynamic_capabilities"})
        database.execute(std::string("DROP TABLE IF EXISTS ")+table);
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
