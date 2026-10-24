#include "agentflow/command_context.hpp"
#include "agentflow/connection_binding.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <cctype>
#include <ranges>
#include <vector>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::size_t max_file_bytes=64*1024;

[[noreturn]] void invalid(){throw ToolFileError("Workspace command is invalid or exceeds its bounds");}

std::string trim(std::string value){
    const auto first=value.find_first_not_of(" \t\r\n");
    if(first==std::string::npos)return {};
    const auto last=value.find_last_not_of(" \t\r\n");
    return value.substr(first,last-first+1);
}

std::string command_id(const std::string& path,const std::string& root){
    if(!path.starts_with(root+"/"))invalid();
    auto id=path.substr(root.size()+1);
    if(id.size()>256||id.empty()||id.ends_with(".md")==false)invalid();
    id.resize(id.size()-3);
    if(id.empty()||id.front()=='/'||id.back()=='/'||id.find("//")!=std::string::npos||id.find('\\')!=std::string::npos)invalid();
    for(const auto part:std::views::split(id,'/')){
        const std::string segment(part.begin(),part.end());
        if(segment.empty()||segment=="."||segment==".."||segment.size()>64)invalid();
        for(const unsigned char c:segment)if(!(std::isalnum(c)||c=='_'||c=='-'))invalid();
    }
    return id;
}

void validate_id(const std::string& id){
    if(id.empty()||id.size()>256||id.front()=='/'||id.back()=='/'||id.find("//")!=std::string::npos||id.find('\\')!=std::string::npos||id.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid workspace command id");
    for(const auto part:std::views::split(id,'/')){const std::string segment(part.begin(),part.end());if(segment.empty()||segment=="."||segment==".."||segment.size()>64)throw std::invalid_argument("Invalid workspace command id");for(const unsigned char c:segment)if(!(std::isalnum(c)||c=='_'||c=='-'))throw std::invalid_argument("Invalid workspace command id");}
}

LocalCommand parse(WorkspaceSnapshot source,const std::string& root){
    if(source.content.size()>max_file_bytes||source.content.find('\0')!=std::string::npos)invalid();
    LocalCommand result;result.id=command_id(source.path,root);result.source=std::move(source);
    const auto& content=result.source.content;
    std::size_t body=0;
    if(content.starts_with("---\n")||content.starts_with("---\r\n")){
        const auto first_end=content.find('\n');if(first_end==std::string::npos)invalid();
        const auto end=content.find("\n---",first_end+1);if(end==std::string::npos)invalid();
        const auto after=end+4;if(after<content.size()&&content[after]!='\n'&&content[after]!='\r')invalid();
        auto metadata=content.substr(first_end+1,end-first_end-1);if(metadata.size()>8192)invalid();
        std::size_t offset=0;bool seen_description=false;
        while(offset<metadata.size()){
            const auto line_end=metadata.find('\n',offset);auto line=trim(metadata.substr(offset,line_end==std::string::npos?std::string::npos:line_end-offset));
            if(!line.empty()){
                const auto colon=line.find(':');if(colon==std::string::npos)invalid();
                const auto key=trim(line.substr(0,colon)),value=trim(line.substr(colon+1));
                if(key!="description"||seen_description||value.size()>1024)invalid();
                seen_description=true;
                if(!value.empty()){
                    std::string description=value;
                    if(description.front()=='\''||description.front()=='"'){
                        if(description.size()<2||description.back()!=description.front())invalid();description=description.substr(1,description.size()-2);
                    }
                    if(description.empty()||description.size()>1024)invalid();
                    result.description=std::move(description);
                }
            }
            if(line_end==std::string::npos)break;offset=line_end+1;
        }
        body=after;while(body<content.size()&&(content[body]=='\r'||content[body]=='\n'))++body;
    }
    result.prompt_template=content.substr(body);
    if(trim(result.prompt_template).empty())invalid();
    return result;
}

std::vector<std::string> arguments(const std::string& input){
    if(input.size()>8192||input.find('\0')!=std::string::npos)throw std::invalid_argument("Command arguments exceed their bounds");
    std::vector<std::string> result;std::string token;char quote=0;bool escaped=false,started=false;
    for(const char c:input){
        if(escaped){token.push_back(c);escaped=false;started=true;continue;}
        if(c=='\\'&&quote!='\''){escaped=true;started=true;continue;}
        if(quote){if(c==quote)quote=0;else token.push_back(c);started=true;continue;}
        if(c=='\''||c=='"'){quote=c;started=true;continue;}
        if(std::isspace(static_cast<unsigned char>(c))){if(started){result.push_back(std::move(token));token.clear();started=false;if(result.size()>32)throw std::invalid_argument("A command accepts at most 32 arguments");}continue;}
        token.push_back(c);started=true;
    }
    if(escaped||quote)throw std::invalid_argument("Command arguments contain an unfinished escape or quote");
    if(started)result.push_back(std::move(token));if(result.size()>32)throw std::invalid_argument("A command accepts at most 32 arguments");return result;
}

std::string expand_template(const std::string& source,const std::string& original,const std::vector<std::string>& values){
    std::string output;output.reserve(source.size()+original.size());
    const auto append=[&](std::string_view value){if(value.size()>1024*1024-output.size())throw ToolFileError("Expanded command exceeds the run prompt limit");output.append(value);};
    for(std::size_t i=0;i<source.size();){
        if(source.compare(i,10,"$ARGUMENTS")==0){append(original);i+=10;continue;}
        if(source[i]=='$'&&i+2<source.size()&&source[i+1]=='{'&&source[i+2]>='1'&&source[i+2]<='9'&&i+3<source.size()&&source[i+3]=='}'){
            const auto index=static_cast<std::size_t>(source[i+2]-'1');if(index<values.size())append(values[index]);i+=4;continue;
        }
        if(source[i]=='$'&&i+1<source.size()&&source[i+1]>='1'&&source[i+1]<='9'){
            const auto index=static_cast<std::size_t>(source[i+1]-'1');if(index<values.size())append(values[index]);i+=2;continue;
        }
        append(std::string_view(source.data()+i,1));++i;
    }
    return output;
}
}

std::map<std::string,LocalCommand> CommandContext::discover(std::stop_token cancel) const{
    constexpr std::string_view root=".agents/commands";
    const auto directory=workspace_.instruction_directory_identity(std::string(root),cancel);
    if(!directory)return {};
    const auto workspace=workspace_.identity();std::map<std::string,LocalCommand> result;
    std::vector<std::string> pending{std::string(root)};std::size_t entries=0,total=0;
    for(std::size_t index=0;index<pending.size();++index){
        const auto current=pending[index];const auto before=workspace_.instruction_directory_identity(current,cancel);
        if(!before)throw ToolFileError("Command directory changed during discovery");
        const auto listing=workspace_.list_files(current,cancel);entries+=listing.entries.size();
        if(listing.truncated||entries>4096||pending.size()>256)throw ToolFileError("Command discovery exceeds its bounds");
        for(const auto& entry:listing.entries){
            if(entry.kind=="link")throw ToolAccessDenied("Command discovery does not follow linked sources");
            const auto path=current+'/'+entry.name;
            if(entry.kind=="directory"){if(pending.size()>=256)throw ToolFileError("Command directory traversal exceeds its bounds");pending.push_back(path);continue;}
            if(entry.kind!="file"||entry.name.size()<4||!entry.name.ends_with(".md"))continue;
            auto snapshot=workspace_.instruction_file(path,cancel);if(!snapshot)continue;
            total+=snapshot->content.size();if(total>2*1024*1024||snapshot->workspace_id!=workspace)throw ToolFileError("Command catalogue changed or exceeds its bounds");
            auto command=parse(std::move(*snapshot),std::string(root));const auto id=command.id;
            if(result.size()>=128)throw ToolFileError("Workspace command catalogue exceeds 128 entries");
            if(!result.emplace(id,std::move(command)).second)throw ToolFileError("Workspace command identities collide");
        }
        if(workspace_.instruction_directory_identity(current,cancel)!=before)throw ToolAccessDenied("Command directory changed during discovery");
    }
    if(workspace_.identity()!=workspace||workspace_.instruction_directory_identity(std::string(root),cancel)!=directory)throw ToolAccessDenied("Workspace or command root changed during discovery");
    return result;
}

std::string CommandContext::catalogue_json(std::stop_token cancel) const{
    auto entries=Json::array();for(const auto& [id,command]:discover(cancel)){
        auto item=Json{{"id",id},{"path",command.source.path},{"workspace_id",command.source.workspace_id},
            {"file_id",command.source.file_id},{"content_sha256",command.source.content_sha256}};
        if(command.description)item["description"]=*command.description;entries.push_back(std::move(item));
    }
    const auto encoded=Json{{"commands",std::move(entries)}}.dump();if(encoded.size()>65536)throw ToolFileError("Serialized command catalogue exceeds 64 KiB");return encoded;
}

std::string CommandContext::expand(const std::string& selected,const std::string& input,std::stop_token cancel) const{
    validate_id(selected);
    const auto catalogue=discover(cancel);const auto found=catalogue.find(selected);if(found==catalogue.end())throw ToolAccessDenied("Workspace command is unavailable");
    const auto values=arguments(input);auto prompt=expand_template(found->second.prompt_template,input,values);
    if(prompt.size()>1024*1024)throw ToolFileError("Expanded command exceeds the run prompt limit");
    return prompt;
}
ConnectionCommandCatalogue read_local_connection_commands(RunExecutor& executor){if(!executor.supports_workspace_browsing())throw RunUnavailable("Workspace command inspection requires an opened workspace");const auto before=executor.execution_workspace();if(!before.configured)throw RunUnavailable("Workspace command inspection requires an opened workspace");WorkspaceTools workspace(before.root);auto catalogue=CommandContext(workspace).catalogue_json();validate_workspace_admission({before.workspace_id,before.authority_id},executor.execution_workspace());return {before,std::move(catalogue)};}
Run submit_local_connection_command(PersistenceService& store,RunExecutor& executor,GraphExecution* graphs,ConnectionCommandRequest request){if(!executor.supports_workspace_browsing())throw RunUnavailable("Workspace commands require an opened workspace");const auto before=executor.execution_workspace();validate_workspace_admission(request.workspace,before);if(request.arguments.size()>8192||request.arguments.find('\0')!=std::string::npos)throw std::invalid_argument("Command arguments exceed their bounds");WorkspaceTools workspace(before.root);auto prompt=CommandContext(workspace).expand(request.command,request.arguments);validate_workspace_admission(request.workspace,executor.execution_workspace());(void)store.session(request.session).get();if(!request.graph.empty()){if(!graphs||request.graph_revision<1||request.graph_revision>=9007199254740991LL)throw std::invalid_argument("Invalid graph command selection");return graphs->submit_graph_workspace(std::move(request.id),std::move(request.session),std::move(request.graph),request.graph_revision,std::move(prompt),std::move(request.model),std::move(request.workspace),std::move(request.provider));}return executor.submit_workspace(std::move(request.id),std::move(request.session),std::move(prompt),std::move(request.model),std::move(request.workspace),std::move(request.provider));}

}
