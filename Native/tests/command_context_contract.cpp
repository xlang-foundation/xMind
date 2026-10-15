#include "agentflow/command_context.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>

using namespace agentflow;
using Json=nlohmann::json;
namespace {
void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected native command rejection did not occur");}
void write(const std::filesystem::path& path,const std::string& text){std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(text.data(),static_cast<std::streamsize>(text.size()));out.close();need(bool(out),"Command fixture write failed");}
}
int main(int argc,char**){if(argc!=1)return 2;try{
    const auto root=std::filesystem::temp_directory_path()/("xmind-native-command-context-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    {
    WorkspaceTools workspace(root.string());CommandContext commands(workspace);
    need(Json::parse(commands.catalogue_json())["commands"].empty(),"An absent command directory must produce an empty catalogue");
    const auto path=root/".agents"/"commands"/"review"/"security.md";
    write(path,"---\ndescription: Review a requested change\n---\nReview $1 and $2 (${1}). Full request: $ARGUMENTS\n");
    auto catalogue=Json::parse(commands.catalogue_json());need(catalogue["commands"].size()==1,"Nested Markdown commands must be discovered");
    const auto& item=catalogue["commands"][0];need(item["id"]=="review/security"&&item["description"]=="Review a requested change"&&item["content_sha256"].get<std::string>().size()==64,"Catalogue metadata must bind the workspace-relative source snapshot");
    const auto prompt=commands.expand("review/security",R"("src/app file.cpp" --strict)");
    need(prompt.find("Review src/app file.cpp and --strict (src/app file.cpp).")!=std::string::npos&&prompt.find(R"(Full request: "src/app file.cpp" --strict)")!=std::string::npos,"Argument placeholders must expand quoted words and retain the original argument string");
    need(commands.expand("review/security",R"("literal $2" target)").find("Review literal $2 and target (literal $2).")!=std::string::npos,"Argument values must not be reinterpreted as further placeholders");
    rejects<ToolAccessDenied>([&]{commands.expand("missing","input");});
    rejects<std::invalid_argument>([&]{commands.expand("../outside","input");});
    rejects<std::invalid_argument>([&]{commands.expand("review/security",R"("unfinished)");});
    std::string repeated;for(int i=0;i<1100;++i)repeated+="$ARGUMENTS ";write(root/".agents"/"commands"/"large.md",repeated);
    rejects<ToolFileError>([&]{commands.expand("large",std::string(8192,'x'));});
    write(root/".agents"/"commands"/"broken.md","---\nunknown: field\n---\nPrompt\n");
    rejects<ToolFileError>([&]{commands.catalogue_json();});
    }
    const auto safe_root=std::filesystem::weakly_canonical(root),safe_temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    need(safe_root.parent_path()==safe_temp,"Test cleanup must stay within the temporary directory");std::filesystem::remove_all(safe_root);
    std::cout<<"Native workspace commands passed: verified nested discovery, source-bound catalogue, bounded quoted argument expansion and rejection of malformed definitions; no command code executed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
