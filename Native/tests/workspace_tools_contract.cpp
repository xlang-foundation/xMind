#include "agentflow/workspace_tools.hpp"
#include "agentflow/repository_instruction_context.hpp"
#include "nlohmann/json.hpp"
#include <future>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
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
        if(std::string(argv[1])=="--guidance") {WorkspaceTools tools(argv[2]);std::cout<<tools.invoke("read_repository_instructions",Json{{"directory",argv[3]}}.dump())<<'\n';return 0;}
        if(std::string(argv[1])=="--guidance-context") {
            WorkspaceTools tools(argv[2]);RepositoryInstructionContext context(tools,tools.repository_instructions());
            const auto initial=context.prepare();require(context.ready("."),"Root guidance must be delivered");
            require(!context.ready("src"),"New nested guidance must defer dispatch");require(!context.ready("src"),"Same model-response batch must still defer dispatch");
            const auto updated=context.prepare();require(updated!=initial && context.ready("src"),"Next provider context must contain scoped guidance");
            const auto guard=context.precondition("src");guard.verify({});require(guard.metadata_json.find("Synthetic src guidance")==std::string::npos,"Proposal metadata must not archive source text");
            require(RepositoryInstructionContext::file_directory("src/deep/file.txt")=="src/deep","File scope must use its parent");
            rejects<ToolAccessDenied>([&]{RepositoryInstructionContext::file_directory("src/../outside.txt");});
            {std::ofstream output(std::filesystem::path(argv[2])/"src"/"AGENTS.md",std::ios::binary|std::ios::trunc);output<<"Changed guidance after provider request";require(bool(output),"Context fixture update failed");}
            require(!context.ready("src"),"Changed guidance must defer dispatch again");require(!context.ready("src"),"Changed guidance cannot become ready in the same batch");
            rejects<ToolGuidanceChanged>([&]{guard.verify({});});
            require(context.prepare().find("Changed guidance after provider request")!=std::string::npos && context.ready("src"),"Fresh bytes must reach the next provider context");
            std::filesystem::remove(std::filesystem::path(argv[2])/"src"/"AGENTS.md");require(!context.ready("src"),"Deleted guidance must defer dispatch");
            context.prepare();require(context.ready("src"),"Removal must be delivered before proceeding");require(context.metadata().find("Changed guidance")==std::string::npos,"Metadata must not contain source text");
            const auto absent=context.precondition("src");{std::ofstream output(std::filesystem::path(argv[2])/"src"/"AGENTS.md");output<<"Added guidance after proposal";}rejects<ToolGuidanceChanged>([&]{absent.verify({});});
            std::cout<<"Native scope delivery state passed on actual changed/deleted files; no model or effect invoked\n";return 0;
        }
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
        const auto page=Json::parse(tools.invoke("read_file",R"({"path":"README.txt","offset":2,"limit":1})"));
        require(page["content"]=="alpha[.]needle \xe4\xb8\xad\r\n"&&page["offset"]==2&&page["lines_read"]==1&&page["next_offset"]==3&&page["has_more"]==true&&page["truncated"]==true&&page["truncated_lines"].empty(),"Pages preserve exact bytes and provide an actual continuation");
        const auto tail=Json::parse(tools.invoke("read_file",R"({"path":"README.txt","offset":3})"));
        require(tail["content"]=="last\n"&&tail["lines_read"]==1&&!tail.contains("next_offset")&&tail["has_more"]==false&&tail["truncated"]==false,"A trailing newline must not fabricate an extra line");
        const auto first=Json::parse(tools.invoke("read_file",R"({"path":"README.txt","limit":1})"));require(first["offset"]==1&&first["content"]=="first\r\n","Limit alone defaults to the first line");
        const auto empty=tools.read_file_page("range-empty.txt");require(empty.content.empty()&&!empty.lines_read&&!empty.next_offset,"An empty file at offset one is an empty page");
        const auto endings=tools.read_file_page("range-ending.txt");require(endings.content=="\n\r\nlast"&&endings.lines_read==3&&!endings.next_offset,"Empty lines and unterminated last line remain exact");
        rejects<ToolFileError>([&]{tools.read_file_page("README.txt",4);});rejects<ToolFileError>([&]{tools.read_file_page("range-empty.txt",2);});
        const auto large=tools.read_file_page("range-large.txt",99999,2);require(large.content=="line 99999 \xe4\xb8\xad\r\nline 100000 \xe4\xb8\xad\r\n"&&large.lines_read==2&&!large.next_offset,"Native streaming must reach requested lines in a file above one MiB");
        const auto boundary=tools.read_file_page("range-boundary.txt",2,1);require(boundary.content=="\xf0\x9f\x98\x80" "end"&&boundary.lines_read==1,"UTF-8 validation must span OS read-buffer boundaries even on skipped lines");
        const auto astral=tools.read_file_page("range-astral.txt",1,1);require(astral.content.size()==8001&&astral.content.back()=='\n'&&astral.truncated_lines==std::vector<std::size_t>{1}&&!astral.next_offset,"Clipping uses complete Unicode characters and reports a clipped EOF line without a false continuation");
        for(std::size_t i=0;i<astral.content.size()-1;i+=4)require(astral.content.substr(i,4)=="\xf0\x9f\x98\x80","Clipped UTF-8 stays intact");
        const auto bounded_source=tools.invoke("read_file",R"({"path":"range-budget.txt","limit":2000})");const auto bounded=Json::parse(bounded_source);
        require(bounded_source.size()<=65536&&bounded["lines_read"].get<std::size_t>()>0&&bounded["lines_read"].get<std::size_t>()<100&&bounded["next_offset"]==bounded["lines_read"].get<std::size_t>()+1&&bounded["truncated_lines"].empty(),"Escaped JSON output must be bounded with a resumable next line");
        for(const auto* path:{"range-invalid-0.txt","range-invalid-1.txt","range-invalid-2.txt","range-invalid-3.txt","range-invalid-4.txt","range-invalid-clipped.txt","binary.bin","range-oversize.txt"})rejects<ToolFileError>([&]{tools.read_file_page(path);});
        rejects<ToolFileError>([&]{tools.read_file_page("range-invalid-skipped.txt",2,1);});
        for(const auto* source:{R"({"path":"README.txt","offset":0})",R"({"path":"README.txt","offset":-1})",R"({"path":"README.txt","offset":1.0})",R"({"path":"README.txt","offset":true})",R"({"path":"README.txt","offset":67108865})",R"({"path":"README.txt","offset":18446744073709551615})",R"({"path":"README.txt","limit":null})",R"({"path":"README.txt","limit":0})",R"({"path":"README.txt","limit":2001})",R"({"path":"README.txt","offset":1,"offset":2})"})rejects<std::invalid_argument>([&]{tools.invoke("read_file",source);});
        rejects<std::invalid_argument>([&]{tools.invoke("list_files",R"({"path":"sub","offset":1})");});
        rejects<std::invalid_argument>([&]{tools.read_file_page("README.txt",0);});rejects<std::invalid_argument>([&]{tools.read_file_page("README.txt",1,2001);});
        require(tools.definitions().size()==4,"Available definitions must match implemented tools");
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
        require(std::none_of(listed.entries.begin(),listed.entries.end(),[](const auto& entry){return WorkspaceTools::backend_private_component(entry.name);}),"Root listing must not expose backend configuration entries");
        require(std::any_of(listed.entries.begin(),listed.entries.end(),[](const auto& entry){return entry.name==".git";}),"Existing public Git listing policy must remain unchanged");
        const auto sub_listing=tools.list_files("sub");require(std::none_of(sub_listing.entries.begin(),sub_listing.entries.end(),[](const auto& entry){return WorkspaceTools::backend_private_component(entry.name);}),"Nested case-aliased configuration entries must remain private");
        require(tools.read_file(".configurable/visible.txt").content=="Ordinary configuration documentation\n","Only the reserved component is private, not similarly named project files");
        require(tools.read_file(".agentflow-guide/visible.txt").content=="Ordinary agent documentation\n","A similarly named public directory must not acquire the private-state classification");
        require(tools.search_files("SyntheticPrivateConfigValue-workspace-only").matches.empty(),"Literal search cannot expose synthetic backend key/guidance markers");
        require(tools.search_files("SyntheticBackendStateValue-workspace-only").matches.empty(),"Search cannot expose private-state markers at any supported case or nesting");
        const auto public_listing=Json::parse(tools.invoke("list_files",R"({"path":"sub"})"));for(const auto& entry:public_listing["entries"])require(!WorkspaceTools::backend_private_component(entry["name"].get<std::string>()),"Model listing must use the same private-directory boundary");
        for(const auto* path:{".config/providers.yaml","./.CONFIG/providers.yaml",".config./providers.yaml",".config /providers.yaml","sub/.config/nested.yaml","config-alias/providers.yaml",".agentflow/owner.token","./.AGENTFLOW/owner.token",".agentflow./owner.token",".agentflow /owner.token","sub/.agentflow/owner.token","state-alias/owner.token"}){
            rejects<ToolAccessDenied>([&]{tools.read_file(path);});rejects<ToolAccessDenied>([&]{tools.snapshot_file(path);});rejects<ToolAccessDenied>([&]{tools.fingerprint_file(path);});
            rejects<ToolAccessDenied>([&]{tools.read_file_page(path,1,1);});
            rejects<ToolAccessDenied>([&]{tools.plan_replacement(path,"api_key","changed");});rejects<ToolAccessDenied>([&]{tools.invoke("read_file",Json{{"path",path}}.dump());});
        }
        for(const auto* path:{".config",".CONFIG",".config.",".config ","sub/.config","config-alias",".agentflow",".AGENTFLOW",".agentflow.",".agentflow ","sub/.agentflow","state-alias"}){
            rejects<ToolAccessDenied>([&]{tools.list_files(path);});rejects<ToolAccessDenied>([&]{tools.directory_identity(path);});rejects<ToolAccessDenied>([&]{tools.repository_instructions(path);});
        }
        rejects<ToolAccessDenied>([&]{tools.instruction_file(".config/AGENTS.md");});rejects<ToolAccessDenied>([&]{tools.instruction_file("sub/.config/AGENTS.md");});
        rejects<ToolAccessDenied>([&]{tools.plan_creation(".config/created.txt","Synthetic forbidden configuration mutation");});
        rejects<ToolAccessDenied>([&]{WorkspaceTools private_root(std::string(argv[1])+"/.config");});
        rejects<ToolAccessDenied>([&]{tools.instruction_file(".agentflow/AGENTS.md");});rejects<ToolAccessDenied>([&]{tools.instruction_file("sub/.agentflow/AGENTS.md");});
        rejects<ToolAccessDenied>([&]{tools.plan_creation(".agentflow/created.txt","Synthetic forbidden state mutation");});
        rejects<ToolAccessDenied>([&]{WorkspaceTools private_root(std::string(argv[1])+"/.agentflow");});
        rejects<ToolAccessDenied>([&]{WorkspaceTools nested_private_root(std::string(argv[1])+"/sub/.AgEnTfLoW");});
        require(tools.directory_identity(".")==tools.identity()&&!tools.directory_identity("sub").empty(),"Public directory identity remains usable for exact native process workdir binding");
        auto many=tools.list_files("many");require(many.entries.size()==1000 && many.truncated,"Listing truncation must be explicit");
        rejects<ToolAccessDenied>([&]{tools.read_file("../workspace-other/secret.txt");});
        rejects<ToolAccessDenied>([&]{tools.read_file(std::string(argv[2])+"/secret.txt");});
        rejects<ToolAccessDenied>([&]{tools.read_file("outside-link/secret.txt");});
        rejects<ToolAccessDenied>([&]{tools.snapshot_file("outside-link/secret.txt");});
        rejects<ToolAccessDenied>([&]{tools.plan_replacement("outside-link/secret.txt","outside","changed");});
        rejects<ToolAccessDenied>([&]{tools.list_files("outside-link");});
        rejects<ToolAccessDenied>([&]{tools.list_files("outside-link/sub");});
        rejects<ToolAccessDenied>([&]{tools.read_file("hard-link.txt");});
        for(const auto* path:{"../workspace-other/secret.txt","outside-link/secret.txt","hard-link.txt","README.txt:stream"})rejects<ToolAccessDenied>([&]{tools.read_file_page(path,1,1);});
        rejects<ToolAccessDenied>([&]{tools.read_file("README.txt:stream");});
        rejects<ToolFileError>([&]{tools.read_file("binary.bin");});
        rejects<ToolFileError>([&]{tools.read_file("too-large.txt");});
        rejects<ToolFileError>([&]{tools.read_file("missing.txt");});
        rejects<std::invalid_argument>([&]{tools.invoke("read_file",R"({"path":"README.txt","extra":true})");});
        rejects<std::invalid_argument>([&]{tools.invoke("search_files",R"({"query":12})");});
        rejects<std::invalid_argument>([&]{tools.invoke("read_file",R"({"path":"missing.txt","path":"README.txt"})");});
        rejects<std::invalid_argument>([&]{tools.invoke("search_files",R"({"query":"first","query":"second"})");});
        const auto deep_arguments=std::string("{\"path\":")+std::string(10000,'[')+"0"+std::string(10000,']')+"}";
        try {tools.invoke("read_file",deep_arguments);throw std::runtime_error("Deep tool arguments were accepted");}
        catch(const std::invalid_argument& error) {require(std::string(error.what())=="Tool argument nesting exceeds limits","Direct callers must use bounded argument parsing before file access");}
        rejects<std::invalid_argument>([&]{tools.invoke("write_file",R"({"path":"README.txt"})");});
        rejects<std::invalid_argument>([&]{tools.read_file(std::string("README.txt\0tail",15));});
        std::stop_source cancelled;cancelled.request_stop();
        rejects<ToolCancelled>([&]{tools.read_file("README.txt",cancelled.get_token());});
        rejects<ToolCancelled>([&]{tools.read_file_page("range-large.txt",99999,2,cancelled.get_token());});
        rejects<ToolCancelled>([&]{tools.snapshot_file("README.txt",cancelled.get_token());});
        rejects<ToolCancelled>([&]{tools.plan_replacement("README.txt","first","changed",1,cancelled.get_token());});
        rejects<ToolCancelled>([&]{tools.search_files("needle",cancelled.get_token());});
        std::vector<std::future<void>> readers;
        for(int i=0;i<4;++i) readers.push_back(std::async(std::launch::async,[&]{for(int n=0;n<10;++n) require(tools.read_file("README.txt").content==read.content,"Concurrent read isolation");}));
        for(auto& reader:readers) reader.get();
        std::cout<<"Native workspace tools passed on real filesystem fixtures; no model was called\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
