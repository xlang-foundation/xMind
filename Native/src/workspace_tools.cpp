#include "agentflow/workspace_tools.hpp"
#include "nlohmann/json.hpp"

namespace agentflow {
namespace {
using Json=nlohmann::json;
Json arguments(const std::string& source,const std::string& field) {
    if(source.size()>65536) throw std::invalid_argument("Tool arguments exceed configured limits");
    Json result;
    try {result=Json::parse(source);} catch(const Json::exception&) {throw std::invalid_argument("Invalid tool argument JSON");}
    if(!result.is_object() || result.size()!=1 || !result.contains(field) || !result[field].is_string()) throw std::invalid_argument("Invalid tool argument fields");
    return result;
}
}
std::vector<ModelToolDefinition> WorkspaceTools::definitions() {
    return {
        {"read_file","Read a UTF-8 text file within the authorized workspace. Limit 1 MiB.",R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"],"additionalProperties":false})"},
        {"list_files","List one workspace directory with explicit truncation.",R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"],"additionalProperties":false})"},
        {"search_files","Find literal text in workspace UTF-8 files. Results include path and line number.",R"({"type":"object","properties":{"query":{"type":"string"}},"required":["query"],"additionalProperties":false})"}
    };
}
std::string WorkspaceTools::invoke(const std::string& name,const std::string& source,std::stop_token cancel) const {
    Json result;
    if(name=="read_file") {
        const auto args=arguments(source,"path");const auto file=read_file(args["path"].get<std::string>(),cancel);
        result={{"path",file.path},{"content",file.content}};
    } else if(name=="list_files") {
        const auto args=arguments(source,"path");const auto listing=list_files(args["path"].get<std::string>(),cancel);
        auto entries=Json::array();for(const auto& entry:listing.entries) entries.push_back({{"name",entry.name},{"kind",entry.kind}});
        result={{"entries",std::move(entries)},{"truncated",listing.truncated}};
    } else if(name=="search_files") {
        const auto args=arguments(source,"query");const auto search=search_files(args["query"].get<std::string>(),cancel);
        auto matches=Json::array();for(const auto& match:search.matches) matches.push_back({{"path",match.path},{"line",match.line},{"text",match.text},{"text_truncated",match.text_truncated}});
        result={{"matches",std::move(matches)},{"scanned_files",search.scanned_files},{"skipped_entries",search.skipped_entries},{"truncated",search.truncated}};
    } else throw std::invalid_argument("Unknown workspace tool");
    return result.dump();
}
}
