#include "agentflow/workspace_tools.hpp"
#include "nlohmann/json.hpp"
#include <future>
#include <iostream>
#include <fstream>
#include <filesystem>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

using namespace agentflow;
using Json=nlohmann::json;
namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class Error,class Function> void rejects(Function action) {
    try {action();} catch(const Error&) {return;} throw std::runtime_error("Expected workspace rejection did not occur");
}
}
int main(int argc,char** argv) {
    if(argc!=3 && argc!=4 && argc!=5) return 2;
    try {
        if(std::string(argv[1])=="--identity") {WorkspaceTools tools(argv[2]);std::cout<<tools.identity()<<'\n';return 0;}
        if(std::string(argv[1])=="--apply") {
            WorkspaceTools tools(argv[2]);
            const auto path=std::filesystem::path(argv[2])/"edit.txt";
            auto write=[&](const std::string& bytes) {std::ofstream output(path,std::ios::binary|std::ios::trunc);output.write(bytes.data(),bytes.size());output.close();require(bool(output),"Fixture write failed");};
            const auto original=tools.snapshot_file("edit.txt");
            auto plan=tools.plan_replacement("edit.txt","original","\xe4\xb8\xad longer replacement");
            auto bad=plan;bad.before.file_id="changed-identity";
            rejects<ToolContentConflict>([&]{tools.apply_plan(bad);});
            bad=plan;bad.before.path="edit-copy.txt";
            rejects<ToolContentConflict>([&]{tools.apply_plan(bad);});
            require(tools.read_file("edit-copy.txt").content==original.content,"Different actual file identity must preserve the other file");
            bad=plan;bad.before.workspace_id="different-workspace";
            rejects<ToolAccessDenied>([&]{tools.apply_plan(bad);});
            bad=plan;bad.before.path="../workspace-other/secret.txt";
            rejects<ToolAccessDenied>([&]{tools.apply_plan(bad);});
            bad=plan;bad.before.path="outside-link/secret.txt";
            rejects<ToolAccessDenied>([&]{tools.apply_plan(bad);});
            bad=plan;bad.before.path="hard-link.txt";
            rejects<ToolAccessDenied>([&]{tools.apply_plan(bad);});
            bad=plan;bad.after_sha256="incorrect";
            rejects<ToolContentConflict>([&]{tools.apply_plan(bad);});
            std::stop_source stopped;stopped.request_stop();
            rejects<ToolCancelled>([&]{tools.apply_plan(plan,stopped.get_token());});
            require(tools.read_file("edit.txt").content==original.content,"Rejected applications must preserve bytes");
            const auto held=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
            require(held!=INVALID_HANDLE_VALUE,"Fixture must hold an actual competing file handle");
            try {rejects<ToolFileError>([&]{tools.apply_plan(plan);});} catch(...) {CloseHandle(held);throw;}
            CloseHandle(held);
            write("external edit\n");
            rejects<ToolContentConflict>([&]{tools.apply_plan(plan);});
            require(tools.read_file("edit.txt").content=="external edit\n","Stale plan must not overwrite external edits");
            write(original.content);
            const auto longer=tools.apply_plan(plan);
            require(longer.content==plan.after_content && longer.content_sha256==plan.after_sha256 && longer.file_id==original.file_id,"Actual longer edit must preserve file identity and verify its bytes");
            plan=tools.plan_replacement("edit.txt",longer.content,"short");
            require(tools.apply_plan(plan).content=="short","Shorter application must truncate old trailing bytes");
            plan=tools.plan_replacement("edit.txt","short","");
            require(tools.apply_plan(plan).content.empty(),"Empty replacement must actually truncate the file");
            const auto raw=tools.fingerprint_file("binary.bin");
            std::cout<<Json{{"raw_hash",raw.content_sha256},{"raw_size",raw.size},{"edit_hash",tools.fingerprint_file("edit.txt").content_sha256}}.dump()<<'\n';return 0;
        }
        if(argc==4 && std::string(argv[1])=="--snapshot") {
            WorkspaceTools tools(argv[2]);const auto snapshot=tools.snapshot_file(argv[3]);
            std::cout<<Json{{"workspace_id",snapshot.workspace_id},{"file_id",snapshot.file_id},{"content_sha256",snapshot.content_sha256},{"content",snapshot.content}}.dump()<<'\n';return 0;
        }
        WorkspaceTools tools(argv[1]);
        WorkspaceTools alias(std::string(argv[1])+"/.");
        require(!tools.identity().empty() && tools.identity()==alias.identity(),"Policy identity must resolve path aliases through actual root handles");
        WorkspaceTools different(argv[2]);require(tools.identity()!=different.identity(),"Distinct directory objects must have distinct local identities");
        const auto read=tools.read_file("README.txt");
        const auto snapshot=tools.snapshot_file("README.txt");
        require(snapshot.content==read.content && snapshot.workspace_id==tools.identity() && !snapshot.file_id.empty(),"Snapshot must capture actual file/root identities with its contents");
        if(argc>=4) require(snapshot.content_sha256==argv[3],"Native hash must match independent fixture hash");
        const auto plan=tools.plan_replacement("README.txt","alpha[.]needle","beta-native");
        require(plan.before.content==read.content && plan.before.file_id==snapshot.file_id && plan.before.workspace_id==snapshot.workspace_id,"Edit plan must bind to the actual file snapshot");
        require(plan.after_content=="first\r\nbeta-native \xe4\xb8\xad\r\nlast\n" && plan.replaced_occurrences==1,"Literal replacement must preserve other actual bytes and line endings");
        if(argc==5) require(plan.after_sha256==argv[4],"Planned output hash must match independent fixture computation");
        require(tools.read_file("README.txt").content==read.content,"Planning must not mutate the file");
        rejects<ToolContentConflict>([&]{tools.plan_replacement("README.txt","missing","replacement");});
        rejects<ToolContentConflict>([&]{tools.plan_replacement("README.txt","alpha[.]needle","beta",2);});
        rejects<ToolContentConflict>([&]{tools.plan_replacement("README.txt","first","first");});
        rejects<std::invalid_argument>([&]{tools.plan_replacement("README.txt","","new");});
        rejects<std::invalid_argument>([&]{tools.plan_replacement("README.txt","first",std::string("a\0b",3));});
        rejects<ToolFileError>([&]{tools.plan_replacement("README.txt","first",std::string(1024*1024,'x'));});
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
        rejects<ToolAccessDenied>([&]{tools.snapshot_file("outside-link/secret.txt");});
        rejects<ToolAccessDenied>([&]{tools.plan_replacement("outside-link/secret.txt","outside","changed");});
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
        rejects<ToolCancelled>([&]{tools.snapshot_file("README.txt",cancelled.get_token());});
        rejects<ToolCancelled>([&]{tools.plan_replacement("README.txt","first","changed",1,cancelled.get_token());});
        rejects<ToolCancelled>([&]{tools.search_files("needle",cancelled.get_token());});
        std::vector<std::future<void>> readers;
        for(int i=0;i<4;++i) readers.push_back(std::async(std::launch::async,[&]{for(int n=0;n<10;++n) require(tools.read_file("README.txt").content==read.content,"Concurrent read isolation");}));
        for(auto& reader:readers) reader.get();
        std::cout<<"Native workspace tools passed on real filesystem fixtures; no model was called\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
