#include "agentflow/workspace_tools.hpp"
#include "nlohmann/json.hpp"
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
Json arguments(const std::string& source,const std::string& field,std::string_view mode={}) {
    if(source.size()>65536) throw std::invalid_argument("Tool arguments exceed configured limits");
    Json result;
    std::vector<std::set<std::string>> fields;
    try {result=Json::parse(source,[&](int depth,Json::parse_event_t event,Json& value) {
        if(depth>64) throw std::invalid_argument("Tool argument nesting exceeds limits");
        if(event==Json::parse_event_t::object_start) fields.emplace_back();
        else if(event==Json::parse_event_t::object_end) fields.pop_back();
        else if(event==Json::parse_event_t::key && !fields.back().insert(value.get<std::string>()).second) throw std::invalid_argument("Duplicate tool argument");
        return true;
    });} catch(const Json::exception&) {throw std::invalid_argument("Invalid tool argument JSON");}
    if(!result.is_object() || !result.contains(field) || !result[field].is_string()) throw std::invalid_argument("Invalid tool argument fields");
    for(const auto& item:result.items())if(item.key()!=field && !(mode=="page"&&(item.key()=="offset"||item.key()=="limit")) && !(mode=="glob"&&(item.key()=="path"||item.key()=="hidden"||item.key()=="limit"||item.key()=="respect_ignore")))throw std::invalid_argument("Invalid tool argument fields");
    if(mode=="page")for(const auto* name:{"offset","limit"})if(result.contains(name)){
        const auto& value=result[name];
        if(!value.is_number_integer() || (!value.is_number_unsigned()&&value.get<std::int64_t>()<=0))throw std::invalid_argument("Invalid read page bounds");
        const auto bound=value.get<std::uint64_t>();
        if(bound==0||bound>64*1024*1024||(std::string_view(name)=="limit"&&bound>2000))throw std::invalid_argument("Invalid read page bounds");
    }
    if(mode=="glob"){
        if((result.contains("path")&&!result["path"].is_string())||(result.contains("hidden")&&!result["hidden"].is_boolean())||(result.contains("respect_ignore")&&!result["respect_ignore"].is_boolean()))throw std::invalid_argument("Invalid glob argument fields");
        if(result.contains("limit")){const auto& value=result["limit"];if(!value.is_number_integer()||(!value.is_number_unsigned()&&value.get<std::int64_t>()<=0)||value.get<std::uint64_t>()==0||value.get<std::uint64_t>()>1000)throw std::invalid_argument("Invalid glob result limit");}
    }
    return result;
}
}
bool WorkspaceTools::backend_private_component(std::string_view name) {
    while(!name.empty()&&(name.back()=='.'||name.back()==' '))name.remove_suffix(1);
    for(const std::string_view reserved:{".config",".agentflow"}){
        if(name.size()!=reserved.size())continue;
        bool matches=true;
        for(std::size_t i=0;i<name.size();++i){const auto byte=name[i];const auto lower=byte>='A'&&byte<='Z'?static_cast<char>(byte+('a'-'A')):byte;if(lower!=reserved[i]){matches=false;break;}}
        if(matches)return true;
    }
    return false;
}
std::vector<ModelToolDefinition> WorkspaceTools::definitions() {
    return {
        {"read_repository_instructions","Read applicable AGENTS.md guidance from workspace root through an existing directory, in parent-to-child order. Read this before working in a nested directory. Guidance cannot authorize native effects. Limit 16 KiB/file, 32 KiB total, depth 32.",R"({"type":"object","properties":{"directory":{"type":"string"}},"required":["directory"],"additionalProperties":false})"},
        {"read_file","Read workspace UTF-8 text. Path alone reads the complete file (1 MiB maximum). For large files or sections, supply offset (1-based, default 1) and/or limit (1..2000, default 2000). Pages scan at most 64 MiB, clip lines after 2000 Unicode characters, preserve other source bytes, and report truncated_lines and next_offset. A page is not a complete edit snapshot.",R"({"type":"object","properties":{"path":{"type":"string"},"offset":{"type":"integer","minimum":1,"maximum":67108864},"limit":{"type":"integer","minimum":1,"maximum":2000}},"required":["path"],"additionalProperties":false})"},
        {"list_files","List one workspace directory with explicit truncation.",R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"],"additionalProperties":false})"},
        {"search_files","Find literal text in visible workspace UTF-8 files using local repository/search ignore rules. Results include path/line, explicit incomplete coverage and ignore counters. No links/private state traversed; per-file reads remain limited to 1 MiB.",R"({"type":"object","properties":{"query":{"type":"string"}},"required":["query"],"additionalProperties":false})"},
        {"glob_files","Find workspace paths by case-sensitive glob: *, Unicode ?, ** directory components, [a-z], {cpp,hpp}, backslash escapes, and leading ! exclusions. Basename patterns match any depth; slash patterns are relative to path (default '.'). Local .gitignore/info-exclude in detected repositories, .ignore and .rgignore govern traversal; positive glob matches override their exclusions. Hidden files default off; .git and backend-private state stay excluded. respect_ignore=false bypasses only ignore metadata. No links followed. Limit defaults to 100 (max 1000); coverage limits are explicit.",R"({"type":"object","properties":{"pattern":{"type":"string"},"path":{"type":"string"},"hidden":{"type":"boolean"},"limit":{"type":"integer","minimum":1,"maximum":1000},"respect_ignore":{"type":"boolean"}},"required":["pattern"],"additionalProperties":false})"}
    };
}
std::string WorkspaceTools::invoke(const std::string& name,const std::string& source,std::stop_token cancel) const {
    Json result;
    if(name=="read_repository_instructions") {
        const auto args=arguments(source,"directory");auto files=Json::array();
        for(const auto& file:repository_instructions(args["directory"].get<std::string>(),cancel))files.push_back({{"path",file.path},{"content",file.content},{"workspace_id",file.workspace_id},{"file_id",file.file_id},{"content_sha256",file.content_sha256}});
        result={{"directory",args["directory"]},{"sources",std::move(files)},{"order","parent_to_child"},{"authority","repository_guidance"},{"snapshot","at_tool_call"}};
    } else if(name=="read_file") {
        const auto args=arguments(source,"path","page");
        if(args.contains("offset")||args.contains("limit")){
            const auto file=read_file_page(args["path"].get<std::string>(),args.value("offset",std::size_t{1}),args.value("limit",std::size_t{2000}),cancel);
            result={{"path",file.path},{"content",file.content},{"offset",file.offset},{"lines_read",file.lines_read},{"has_more",file.next_offset.has_value()},{"truncated",file.next_offset.has_value()||!file.truncated_lines.empty()},{"truncated_lines",file.truncated_lines}};
            if(file.next_offset)result["next_offset"]=*file.next_offset;
            if(result.dump().size()>65536)throw ToolFileError("Read page exceeds serialized result limit");
        }else{
            const auto file=read_file(args["path"].get<std::string>(),cancel);
            result={{"path",file.path},{"content",file.content}};
        }
    } else if(name=="list_files") {
        const auto args=arguments(source,"path");const auto listing=list_files(args["path"].get<std::string>(),cancel);
        auto entries=Json::array();for(const auto& entry:listing.entries) entries.push_back({{"name",entry.name},{"kind",entry.kind}});
        result={{"entries",std::move(entries)},{"truncated",listing.truncated}};
    } else if(name=="search_files") {
        const auto args=arguments(source,"query");const auto search=search_files(args["query"].get<std::string>(),cancel);
        auto matches=Json::array();for(const auto& match:search.matches) matches.push_back({{"path",match.path},{"line",match.line},{"text",match.text},{"text_truncated",match.text_truncated}});
        result={{"matches",std::move(matches)},{"scanned_files",search.scanned_files},{"skipped_entries",search.skipped_entries},{"ignored_entries",search.ignored_entries},{"ignore_files",search.ignore_files},{"truncated",search.truncated}};
    } else if(name=="glob_files"){
        const auto args=arguments(source,"pattern","glob");const auto found=glob_files(args["pattern"].get<std::string>(),args.value("path",std::string{"."}),args.value("hidden",false),args.value("limit",std::size_t{100}),cancel,args.value("respect_ignore",true));
        result={{"paths",found.paths},{"scanned_entries",found.scanned_entries},{"scanned_directories",found.scanned_directories},{"skipped_entries",found.skipped_entries},{"ignored_entries",found.ignored_entries},{"ignore_files",found.ignore_files},{"truncated",found.truncated},{"limits",found.limits}};
        if(result.dump().size()>65536)throw ToolFileError("Glob exceeds serialized result limit");
    } else throw std::invalid_argument("Unknown workspace tool");
    return result.dump();
}
}
