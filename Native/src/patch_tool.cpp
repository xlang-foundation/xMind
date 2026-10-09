#include "agentflow/patch_tool.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#include <type_traits>
#include <map>
namespace agentflow {
using Json=nlohmann::json;
ModelToolDefinition PatchTool::definition(){return {"apply_patch",
    "Propose a UTF-8 multi-file workspace patch. Use *** Begin Patch and *** End Patch, with *** Add File: path (+ lines), *** Update File: path (@@ and space/+/- lines), optional *** Move to: destination after Update, or *** Delete File: path. Paths must be relative. Exact context only; ambiguous/fuzzy matches are rejected. create_parents explicitly permits missing folders. Each file requires its own controller approval. The batch is not atomic: successful earlier changes remain if a later file is denied, fails or becomes uncertain. The result reports actual per-file outcomes; never claim all files changed from a partial result. Changed repository/skill guidance must reach the next model request before more effects. Never retry an uncertain effect.",
    R"({"type":"object","properties":{"patch_text":{"type":"string","minLength":1,"maxLength":65536},"create_parents":{"type":"boolean"}},"required":["patch_text"],"additionalProperties":false})"};}
std::optional<InstructionPrecondition> PatchTool::guidance(const std::vector<std::string>& scopes,std::stop_token cancel){
    if(scopes.empty()||scopes.size()>2)throw std::invalid_argument("Patch file guidance requires one or two scopes");
    bool ready=true;for(const auto& directory:scopes){const auto current=context_.ready(directory,cancel);ready=current&&ready;}
    if(!ready)return {};
    std::vector<InstructionPrecondition> conditions;for(const auto& directory:scopes)conditions.push_back(context_.precondition(directory));
    if(conditions.size()==1)return conditions.front();
    Json metadata=Json::parse(conditions.front().metadata_json);metadata["scope_directories"]=scopes;
    std::map<std::string,Json> sources;
    for(const auto& condition:conditions){
        const auto current=Json::parse(condition.metadata_json);
        if(current.value("skills",Json::array())!=metadata.value("skills",Json::array()))throw ToolGuidanceChanged("Patch skill binding changed between scopes");
        for(const auto& file:current.at("sources")){
            const auto key=file.at("path").get<std::string>();const auto [found,added]=sources.emplace(key,file);
            if(!added&&found->second!=file)throw ToolGuidanceChanged("Patch guidance changed between source and destination");
        }
    }
    if(sources.size()>66)throw ToolFileError("Patch guidance source count exceeds bounds");
    metadata["sources"]=Json::array();for(const auto& [path,file]:sources)metadata["sources"].push_back(file);
    InstructionPrecondition combined{metadata.dump(),[conditions=std::move(conditions)](std::stop_token token){for(const auto& condition:conditions)condition.verify(token);}};combined.validate();return combined;
}
std::vector<std::string> PatchTool::directories(const WorkspacePatchFilePlan& plan,std::stop_token cancel){
    return std::visit([&](const auto& file){
        using T=std::decay_t<decltype(file)>;std::vector<std::string> scopes;
        if constexpr(std::is_same_v<T,WorkspaceCreatePlan>)scopes.push_back(workspace_.creation_directory(file.path,cancel));
        else{
            scopes.push_back(RepositoryInstructionContext::file_directory(file.before.path));
            if constexpr(std::is_same_v<T,WorkspaceMovePlan>){const auto destination=workspace_.creation_directory(file.destination.path,cancel);if(destination!=scopes.front())scopes.push_back(destination);}
        }
        return scopes;
    },plan);
}
std::optional<PreparedPatch> PatchTool::prepare(const std::string& arguments,std::stop_token cancel){
    if(arguments.size()>262144)throw std::invalid_argument("Encoded patch arguments exceed bounds");
    Json input;try{input=Json::parse(mcp_compact_object(arguments));}catch(const McpProtocolError&){throw std::invalid_argument("Invalid strict patch arguments");}
    if(!input.contains("patch_text")||!input["patch_text"].is_string()||(input.contains("create_parents")&&!input["create_parents"].is_boolean()))throw std::invalid_argument("Invalid patch fields");
    for(auto it=input.begin();it!=input.end();++it)if(it.key()!="patch_text"&&it.key()!="create_parents")throw std::invalid_argument("Unsupported patch authority field");
    if(cancel.stop_requested())throw ToolCancelled("Patch preparation was cancelled");
    const auto files=parse_file_patch(input.at("patch_text").get<std::string>());const auto create_parents=input.value("create_parents",false);
    bool ready=true;
    for(const auto& file:files){
        const auto source=file.action==PatchAction::add?workspace_.creation_directory(file.path,cancel):RepositoryInstructionContext::file_directory(file.path);
        const auto source_ready=context_.ready(source,cancel);ready=source_ready&&ready;
        if(!file.move_to.empty()){const auto destination=workspace_.creation_directory(file.move_to,cancel);const auto destination_ready=context_.ready(destination,cancel);ready=destination_ready&&ready;}
    }
    if(!ready)return {};
    PreparedPatch prepared{workspace_.plan_patch(files,cancel,create_parents),{}};
    for(const auto& file:prepared.plan.files){auto condition=guidance(directories(file,cancel),cancel);if(!condition)return {};prepared.guidance.push_back(std::move(*condition));}
    return prepared;
}
std::string PatchTool::execute(std::string patch_id,std::string run,PreparedPatch prepared,std::int64_t expiry,std::stop_token cancel){
    return PatchExecutor(store_,workspace_).execute(std::move(patch_id),std::move(run),std::move(prepared.plan),expiry,cancel,std::move(prepared.guidance),[this](std::size_t,const WorkspacePatchFilePlan& plan,std::stop_token token){
        auto current=guidance(directories(plan,token),token);if(!current)throw ToolGuidanceChanged("Updated patch scope guidance must reach the model before another proposal");return std::move(*current);
    });
}
}
