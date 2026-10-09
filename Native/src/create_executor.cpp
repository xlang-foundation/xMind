#include "agentflow/create_executor.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
namespace agentflow {
ModelToolDefinition CreateExecutor::definition(){return {"create_file","Propose a new UTF-8 workspace file. Set create_parents=true to include missing parent directories in the same approval. Requires controller approval. Never overwrites existing entries; report only the returned actual outcome.",R"({"type":"object","properties":{"path":{"type":"string","minLength":1},"content":{"type":"string"},"create_parents":{"type":"boolean"}},"required":["path","content"],"additionalProperties":false})"};}
std::string CreateExecutor::invoke(const std::string& id,const std::string& run,const std::string& source,std::int64_t expiry,std::stop_token cancel,InstructionPrecondition guidance){
    using Json=nlohmann::json;if(source.size()>65536)throw std::invalid_argument("Creation arguments exceed limits");
    Json args;try{args=Json::parse(mcp_compact_object(source));}catch(const McpProtocolError&){throw std::invalid_argument("Invalid creation argument JSON");}
    if(args.size()<2||args.size()>3 || !args.contains("path") || !args["path"].is_string() || !args.contains("content") || !args["content"].is_string())throw std::invalid_argument("Invalid creation argument fields");
    for(const auto& [key,value]:args.items())if(key!="path"&&key!="content"&&(key!="create_parents"||!value.is_boolean()))throw std::invalid_argument("Invalid creation argument fields");
    const auto actual=execute(id,run,workspace_.plan_creation(args["path"].get<std::string>(),args["content"].get<std::string>(),cancel,args.value("create_parents",false)),expiry,cancel,std::move(guidance));
    return Json{{"operation_id",id},{"path",actual.path},{"file_id",actual.file_id},{"content_sha256",actual.content_sha256},{"size",actual.content.size()}}.dump();
}
WorkspaceSnapshot CreateExecutor::execute(const std::string& id,const std::string& run,WorkspaceCreatePlan plan,std::int64_t expiry,std::stop_token cancel,InstructionPrecondition guidance){
    using Json=nlohmann::json;if(plan.workspace_id!=workspace_.identity())throw ToolAccessDenied("Creation belongs to another workspace");
    OperationSpec spec{run,plan.workspace_id,"create_file",Json{{"path",plan.path},{"parent_id",plan.parent_id},{"before_exists",false},{"before_content",""},{"after_content",plan.content},{"after_sha256",plan.content_sha256}}.dump()};
    if(!plan.create_directories.empty()){auto payload=Json::parse(spec.arguments_json);payload["create_directories"]=plan.create_directories;spec.arguments_json=payload.dump();}
    guidance.validate();if(guidance.verify){auto payload=Json::parse(spec.arguments_json);payload["repository_guidance"]=Json::parse(guidance.metadata_json);spec.arguments_json=payload.dump();}
    PermissionWaiter(store_).acquire(id,spec,expiry,cancel);
    try {
    auto finish=[&](OperationState state,const std::string& result){try{store_.finish_operation(id,state,result).get();}catch(...){throw EditOutcomeUnrecorded("Creation outcome could not be recorded; claimed-effect recovery is required");}};
    WorkspaceSnapshot actual;
    if(guidance.verify){try{guidance.verify(cancel);}catch(...){finish(OperationState::failed,R"({"reason":"repository_guidance_changed_or_unavailable_before_effect"})");throw;}}
    try{actual=workspace_.apply_creation(plan,cancel);}
    catch(const ToolMutationUncertain&){finish(OperationState::uncertain,R"({"reason":"file_creation_uncertain"})");throw;}
    catch(const ToolContentConflict&){finish(OperationState::failed,R"({"reason":"creation_precondition_conflict"})");throw;}
    catch(const ToolAccessDenied&){finish(OperationState::failed,R"({"reason":"creation_access_denied_before_effect"})");throw;}
    catch(const ToolFileError&){finish(OperationState::failed,R"({"reason":"creation_parent_unavailable_before_effect"})");throw;}
    catch(const ToolCancelled&){finish(OperationState::failed,R"({"reason":"cancelled_before_creation"})");throw;}
    catch(const std::invalid_argument&){finish(OperationState::failed,R"({"reason":"invalid_creation_before_effect"})");throw;}
    catch(...){finish(OperationState::uncertain,R"({"reason":"unexpected_creation_failure"})");throw;}
    try{Json receipt={{"path",actual.path},{"workspace_id",actual.workspace_id},{"parent_id",plan.parent_id},{"file_id",actual.file_id},{"after_sha256",actual.content_sha256},{"size",actual.content.size()}};if(!plan.create_directories.empty())receipt["created_directories"]=plan.create_directories;finish(OperationState::succeeded,receipt.dump());}
    catch(const EditOutcomeUnrecorded&){throw;}
    catch(...){throw EditOutcomeUnrecorded("Created file outcome could not be encoded; recovery is required");}
    return actual;
    }catch(const EditOutcomeUnrecorded&){throw;}
    catch(const ToolMutationUncertain&){throw;}
    catch(const ToolContentConflict&){throw;}
    catch(const ToolAccessDenied&){throw;}
    catch(const ToolFileError&){throw;}
    catch(const ToolCancelled&){throw;}
    catch(const std::invalid_argument&){throw;}
    catch(...){throw EditOutcomeUnrecorded("Claimed creation outcome could not be preserved; recovery is required");}
}
}
