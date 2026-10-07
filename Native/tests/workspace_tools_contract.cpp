#include "agentflow/workspace_tools.hpp"
#include "nlohmann/json.hpp"
#include <future>
#include <iostream>

using namespace agentflow;
using Json=nlohmann::json;
namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class Error,class Function> void rejects(Function action) {
    try {action();} catch(const Error&) {return;} throw std::runtime_error("Expected workspace rejection did not occur");
}
}
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    try {
        WorkspaceTools tools(argv[1]);
        const auto read=tools.read_file("README.txt");
        require(read.content=="first\r\nalpha[.]needle \xe4\xb8\xad\r\nlast\n","Actual UTF-8 file contents must survive");
        require(tools.read_file("README.TXT").content==read.content,"Normal Windows filename lookup must remain usable");
        require(tools.read_file("sub/inside.txt").content=="alpha[.]needle nested\n","Nested file read");
        require(Json::parse(tools.invoke("read_file",R"({"path":"README.txt"})"))["content"]==read.content,"Typed tool invocation must perform actual read");
        require(tools.definitions().size()==3,"Available definitions must match implemented tools");
        const auto search=tools.search_files("alpha[.]needle");
        bool root=false,nested=false,preview=false;
        for(const auto& match:search.matches) {
            if(match.path=="README.txt" && match.line==2) root=true;
            if(match.path=="sub/inside.txt" && match.line==1) nested=true;
            if(match.path=="long.txt") {
                require(match.text_truncated && match.text.size()<=4096,"Clipped previews must be explicit");
                Json checked=match.text;checked.dump();preview=true;
            }
            require(match.path.find("outside-link")==std::string::npos && match.path.find(".git")==std::string::npos,"Search must not traverse excluded/link paths");
        }
        require(root && nested && preview,"Literal search must return actual path/line matches");
        require(search.skipped_entries>=3 && search.truncated,"Search must report skipped and bounded coverage");
        auto listed=tools.list_files();require(!listed.entries.empty(),"Root directory handle enumeration");
        auto many=tools.list_files("many");require(many.entries.size()==1000 && many.truncated,"Listing truncation must be explicit");
        rejects<ToolAccessDenied>([&]{tools.read_file("../workspace-other/secret.txt");});
        rejects<ToolAccessDenied>([&]{tools.read_file(std::string(argv[2])+"/secret.txt");});
        rejects<ToolAccessDenied>([&]{tools.read_file("outside-link/secret.txt");});
        rejects<ToolAccessDenied>([&]{tools.list_files("outside-link");});
        rejects<ToolAccessDenied>([&]{tools.list_files("outside-link/sub");});
        rejects<ToolAccessDenied>([&]{tools.read_file("hard-link.txt");});
        rejects<ToolAccessDenied>([&]{tools.read_file("README.txt:stream");});
        rejects<ToolFileError>([&]{tools.read_file("binary.bin");});
        rejects<ToolFileError>([&]{tools.read_file("too-large.txt");});
        rejects<ToolFileError>([&]{tools.read_file("missing.txt");});
        rejects<std::invalid_argument>([&]{tools.invoke("read_file",R"({"path":"README.txt","extra":true})");});
        rejects<std::invalid_argument>([&]{tools.invoke("search_files",R"({"query":12})");});
        rejects<std::invalid_argument>([&]{tools.invoke("write_file",R"({"path":"README.txt"})");});
        rejects<std::invalid_argument>([&]{tools.read_file(std::string("README.txt\0tail",15));});
        std::stop_source cancelled;cancelled.request_stop();
        rejects<ToolCancelled>([&]{tools.read_file("README.txt",cancelled.get_token());});
        rejects<ToolCancelled>([&]{tools.search_files("needle",cancelled.get_token());});
        std::vector<std::future<void>> readers;
        for(int i=0;i<4;++i) readers.push_back(std::async(std::launch::async,[&]{for(int n=0;n<10;++n) require(tools.read_file("README.txt").content==read.content,"Concurrent read isolation");}));
        for(auto& reader:readers) reader.get();
        std::cout<<"Native workspace tools passed on real filesystem fixtures; no model was called\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
