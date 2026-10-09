#include "agentflow/workspace_tools.hpp"
#include "agentflow/path_glob.hpp"
#include "agentflow/path_ignore.hpp"
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
        if(std::string(argv[1])=="--patch-plan") {
            WorkspaceTools tools(argv[2]);
            const auto files=parse_file_patch("*** Begin Patch\n*** Add File: new-parent/deep/added.txt\n+added\n*** Update File: update-source.txt\n@@\n-before\n+after\n*** Update File: move-source.txt\n*** Move to: move-parent/moved.txt\n@@\n-old move\n+new move\n*** Delete File: delete-source.txt\n*** End Patch\n");
            rejects<ToolFileError>([&]{tools.plan_patch(files);});
            const auto plan=tools.plan_patch(files,{},true);require(plan.files.size()==4&&plan.workspace_id==tools.identity(),"Patch planning must bind actual workspace");
            const auto& added=std::get<WorkspaceCreatePlan>(plan.files[0]);const auto& edited=std::get<WorkspaceEditPlan>(plan.files[1]);const auto& moved=std::get<WorkspaceMovePlan>(plan.files[2]);const auto& removed=std::get<WorkspaceRemovalPlan>(plan.files[3]);
            require(added.content=="added\n"&&added.create_directories==std::vector<std::string>{"new-parent","new-parent/deep"},"Patch creation must disclose all missing parents");
            require(edited.before.content=="before\n"&&edited.after_content=="after\n"&&!edited.parent_id.empty(),"Patch edit must use actual before/after and parent binding");
            require(moved.before.content=="old move\n"&&moved.destination.content=="new move\n"&&moved.destination.path=="move-parent/moved.txt"&&!moved.parent_id.empty(),"Patch move must bind source and destination");
            require(removed.before.content=="remove me\n"&&!removed.parent_id.empty(),"Patch removal must bind actual source");
            auto bad=edited;bad.parent_id="different-parent";rejects<ToolContentConflict>([&]{tools.apply_plan(bad);});
            for(const auto& path:{".config/providers.yaml","outside-link/secret.txt","hard-link.txt","inside-link/update-source.txt"}){
                const auto unsafe=parse_file_patch("*** Begin Patch\n*** Delete File: "+std::string(path)+"\n*** End Patch\n");rejects<ToolAccessDenied>([&]{tools.plan_patch(unsafe);});
            }
            const auto collision=parse_file_patch("*** Begin Patch\n*** Add File: \xc3\x85.txt\n+a\n*** Add File: \xc3\xa5.txt\n+b\n*** End Patch\n");rejects<std::invalid_argument>([&]{tools.plan_patch(collision);});
            const auto overlap=parse_file_patch("*** Begin Patch\n*** Add File: new-file\n+a\n*** Add File: new-file/child.txt\n+b\n*** End Patch\n");rejects<std::invalid_argument>([&]{tools.plan_patch(overlap,{},true);});
            const auto occupied=parse_file_patch("*** Begin Patch\n*** Update File: move-source.txt\n*** Move to: update-source.txt\n@@\n-old move\n+new move\n*** End Patch\n");rejects<ToolContentConflict>([&]{tools.plan_patch(occupied);});
            std::string oversized="*** Begin Patch\n";for(int n=0;n<5;++n)oversized+="*** Delete File: large"+std::to_string(n)+".txt\n";oversized+="*** End Patch\n";const auto large=parse_file_patch(oversized);rejects<ToolFileError>([&]{tools.plan_patch(large);});
            std::stop_source stopped;stopped.request_stop();rejects<ToolCancelled>([&]{tools.plan_patch(files,stopped.get_token(),true);});
            std::cout<<Json{{"files",plan.files.size()},{"workspace_id",plan.workspace_id},{"updated_before_sha256",edited.before.content_sha256},{"removed_before_sha256",removed.before.content_sha256},{"moved_before_sha256",moved.before.content_sha256},{"planning_only",true}}.dump()<<'\n';return 0;
        }
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
        require(tools.definitions().size()==5,"Available definitions must match implemented tools");
        const auto glob=tools.glob_files("glob-src/**/*.{cpp,hpp}");
        require(glob.paths==std::vector<std::string>{"glob-src/main.cpp","glob-src/main.hpp","glob-src/nested/item1.cpp","glob-src/nested/item2.hpp","glob-src/nested/\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x98\x80.cpp"},"Glob must support zero/multiple directory levels and brace alternatives with actual UTF-8 filenames");
        require(glob.truncated&&glob.skipped_entries>=2,"Unsafe hard links and junctions must be skipped with explicit incomplete coverage");
        const auto deep=tools.glob_files("**/*.deep","glob-depth");std::string edge="glob-depth";for(int i=0;i<32;++i)edge+="/d";edge+="/edge.deep";
        require(deep.paths==std::vector<std::string>{edge}&&deep.truncated&&std::find(deep.limits.begin(),deep.limits.end(),"depth_limit")!=deep.limits.end(),"Actual depth boundary must return the admitted file and explicitly skip deeper descent without exhausting the thread stack");
        const auto classes=tools.glob_files("nested/item[!2].?pp","glob-src");require(classes.paths==std::vector<std::string>{"glob-src/nested/item1.cpp"},"Character classes and question marks must match relative to the selected directory");
        const auto unicode=tools.glob_files("nested/???.[ch]pp","glob-src");require(unicode.paths==std::vector<std::string>{"glob-src/nested/\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x98\x80.cpp"},"Question marks count Unicode scalars rather than bytes or surrogate halves");
        require(tools.glob_files("*.hpp","glob-src").paths==std::vector<std::string>{"glob-src/main.hpp","glob-src/nested/item2.hpp"},"Basename patterns must find nested files");
        require(tools.glob_files("*.hpp","glob-src/").paths==tools.glob_files("*.hpp","glob-src").paths,"Directory discovery accepts a trailing separator without an empty child component");
        require(tools.glob_files("**/hidden.cpp").paths.empty(),"Hidden directories are excluded by default");
        require(tools.glob_files("**/hidden.cpp",".",true).paths==std::vector<std::string>{".glob-hidden/hidden.cpp"},"Explicit hidden discovery includes public hidden files");
        require(SetFileAttributesW((std::filesystem::path(argv[1])/L"windows-hidden.cpp").c_str(),FILE_ATTRIBUTE_HIDDEN)!=0,"Fixture must set actual Windows hidden attribute");
        require(tools.glob_files("windows-hidden.cpp").paths.empty()&&tools.glob_files("windows-hidden.cpp",".",true).paths==std::vector<std::string>{"windows-hidden.cpp"},"Hidden choice also covers actual Windows attributes");
        const auto bounded_glob=tools.glob_files("*.txt","many",false,1000);require(bounded_glob.paths.size()==1000&&bounded_glob.truncated&&std::find(bounded_glob.limits.begin(),bounded_glob.limits.end(),"result_limit")!=bounded_glob.limits.end(),"A discovered extra match must disclose result truncation");
        const auto wire_glob=Json::parse(tools.invoke("glob_files",R"({"pattern":"nested/item[1-2].{cpp,hpp}","path":"glob-src","hidden":false,"limit":10})"));require(wire_glob["paths"].size()==2&&wire_glob["skipped_entries"].get<std::size_t>()>=2,"Typed invocation must use actual native glob traversal");
        const auto private_glob=tools.glob_files("**/*",".",true,1000);for(const auto& path:private_glob.paths)require(path.find(".config/")==std::string::npos&&path.find(".CoNfIg/")==std::string::npos&&path.find(".agentflow/")==std::string::npos&&path.find(".AgEnTfLoW/")==std::string::npos&&path.find(".git/")==std::string::npos&&path.find("outside-link/")==std::string::npos,"Glob cannot expose private or outside metadata");
        for(const auto* pattern:{"","../*.cpp","/absolute/*","C:/*.cpp","src/**x","[z-a]","[abc","[]","{one}","{one,}","{one,two","dangling\\"})rejects<std::invalid_argument>([&]{tools.glob_files(pattern);});
        const auto ignored=tools.glob_files("!*.never","ignore-corpus");
        require(ignored.paths==std::vector<std::string>{"ignore-corpus/build/built.cpp","ignore-corpus/children/keep.cpp","ignore-corpus/items/a.cpp","ignore-corpus/keep.tmp","ignore-corpus/main.cpp","ignore-corpus/nested/item1.cpp","ignore-corpus/root-only.cpp"},"Native ignore hierarchy must honor source priority, scoped rules, escapes, POSIX classes and directory pruning");
        require(!ignored.truncated&&ignored.ignored_entries>0&&ignored.ignore_files==5,"Intentional ignore exclusions are complete coverage, with actual source counters");
        const auto positive=tools.glob_files("*.cpp","ignore-corpus");require(std::find(positive.paths.begin(),positive.paths.end(),"ignore-corpus/info-drop.cpp")!=positive.paths.end()&&std::find(positive.paths.begin(),positive.paths.end(),"ignore-corpus/blocked/inside.cpp")==positive.paths.end(),"Positive file globs override file exclusions but do not bypass a nonmatching ignored parent");
        const auto bypass=tools.glob_files("*.cpp","ignore-corpus",false,100,{},false);require(std::find(bypass.paths.begin(),bypass.paths.end(),"ignore-corpus/blocked/inside.cpp")!=bypass.paths.end()&&bypass.ignore_files==0,"Explicit bypass changes only ignore rules");
        require(tools.glob_files("\\!literal.cpp","ignore-corpus").paths==std::vector<std::string>{"ignore-corpus/!literal.cpp"},"Escaped leading exclamation is a literal positive glob");
        require(tools.glob_files("literal\\{x\\}.cpp","ignore-corpus").paths==std::vector<std::string>{"ignore-corpus/literal{x}.cpp"},"Escaped literal braces must not expand");
        rejects<ToolAccessDenied>([&]{tools.glob_files("*.cpp","unsafe-ignore");});
        require(tools.glob_files("*.cpp","unsafe-ignore",false,100,{},false).paths==std::vector<std::string>{"unsafe-ignore/visible.cpp"},"Bypass does not read unsafe ignore metadata");
        const auto searched=tools.search_files("OwnedIgnoreNeedle");std::vector<std::string> search_paths;for(const auto& match:searched.matches)search_paths.push_back(match.path);std::sort(search_paths.begin(),search_paths.end());require(search_paths==ignored.paths,"Content search must share actual hierarchical ignore traversal rather than the positive-glob override");
        PathIgnore dialect;dialect.begin_repository(".");dialect.add(".",0,"\xef\xbb\xbf" "# comment\r\n/root.cpp\r\n*.tmp\n!keep.tmp\nfolder/**\n!folder/keep.cpp\nname\\ \n\\#literal\n");std::size_t rule_steps=0;
        require(dialect.ignored("root.cpp",false,rule_steps)&&!dialect.ignored("sub/root.cpp",false,rule_steps)&&!dialect.ignored("keep.tmp",false,rule_steps)&&dialect.ignored("drop.tmp",false,rule_steps),"Leading anchors, BOM/CRLF and ordered negation must follow Git-style rules");
        require(!dialect.ignored("folder",true,rule_steps)&&dialect.ignored("folder/drop.cpp",false,rule_steps)&&!dialect.ignored("folder/keep.cpp",false,rule_steps)&&dialect.ignored("name ",false,rule_steps)&&dialect.ignored("#literal",false,rule_steps),"Trailing recursive stars must not prematurely prune the parent; escaped spaces/hash remain literal");
        const auto before_repository=dialect.checkpoint();dialect.begin_repository("sub");require(!dialect.ignored("sub/drop.tmp",false,rule_steps),"Nested repository boundary excludes ancestor Git rules");dialect.restore(before_repository);require(dialect.ignored("sub/drop.tmp",false,rule_steps),"Sibling traversal must restore its previous repository scope");
        dialect.add(".",1,"nested\\/literal.cpp\n");require(dialect.ignored("nested/literal.cpp",false,rule_steps),"Escaped interior separators retain path anchoring");
        rejects<ToolFileError>([&]{PathIgnore oversized;oversized.add(".",1,std::string(32769,'#'));});
        PathIgnore exhausted;for(int i=0;i<8;++i)exhausted.add(".",1,std::string(32768,'#'));rejects<IgnoreMetadataBudgetExceeded>([&]{exhausted.add(".",1,"# extra");});
        for(const auto* source:{R"({"pattern":"*","path":false})",R"({"pattern":"*","hidden":"true"})",R"({"pattern":"*","limit":0})",R"({"pattern":"*","limit":1001})",R"({"pattern":"*","limit":1.0})",R"({"pattern":"*","unknown":true})",R"({"pattern":"a","pattern":"b"})"})rejects<std::invalid_argument>([&]{tools.invoke("glob_files",source);});
        std::size_t glob_steps=0;PathGlob complex("{a,{b,c}}/**/file[1-3].cpp");require(complex.matches("c/deep/file2.cpp",glob_steps)&&!complex.matches("c/file9.cpp",glob_steps),"Nested braces and character ranges must compile deterministically");
        glob_steps=49999999;rejects<GlobMatchBudgetExceeded>([&]{complex.matches("c/file2.cpp",glob_steps);});
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
        const auto regex=Json::parse(tools.invoke("search_files",R"({"query":"^NativeRegex item([0-9]+|XYZ)$","regex":true,"path":"search-fixtures/main.txt"})"));
        require(regex["matches"].size()==2&&regex["matches"][0]["line"]==1&&regex["matches"][1]["line"]==2&&regex["matches"][0]["text"]=="NativeRegex item12"&&regex["scanned_files"]==1&&!regex["truncated"].get<bool>(),"Native regex alternation, classes and anchors must search an actual selected file with original line numbering");
        require(Json::parse(tools.invoke("search_files",R"({"query":"^NativeRegex","path":"search-fixtures/main.txt"})"))["matches"].empty(),"Regex metacharacters must remain literal by default");
        const auto folded=Json::parse(tools.invoke("search_files",R"({"query":"\u00e5ngstr\u00f6m","case_sensitive":false,"path":"search-fixtures/main.txt"})"));
        require(folded["matches"].size()==1&&folded["matches"][0]["line"]==3,"Case-insensitive literal search must use native Unicode simple folding");
        require(Json::parse(tools.invoke("search_files",R"({"query":"\u00e5ngstr\u00f6m","path":"search-fixtures/main.txt"})"))["matches"].empty(),"Case-sensitive default must preserve distinct Unicode case");
        const auto large_search=Json::parse(tools.invoke("search_files",R"({"query":"^line (99999|100000) \u4e2d$","regex":true,"path":"range-large.txt"})"));
        require(large_search["matches"].size()==2&&large_search["matches"][0]["line"]==99999&&large_search["matches"][1]["line"]==100000&&!large_search["truncated"].get<bool>(),"Content search must reach actual matching lines beyond the legacy one-MiB read limit");
        const auto capped=Json::parse(tools.invoke("search_files",R"({"query":"NativeRegex","path":"search-fixtures/main.txt","limit":1})"));
        require(capped["matches"].size()==1&&capped["truncated"]==true&&std::find(capped["limits"].begin(),capped["limits"].end(),"result_limit")!=capped["limits"].end(),"Result truncation must be disclosed when an actual extra matching line is found");
        const auto filtered=Json::parse(tools.invoke("search_files",R"({"query":"NativeRegex","path":"search-fixtures","include":"main.txt"})"));
        require(filtered["matches"].size()==2&&filtered["scanned_files"]==1&&filtered["matches"][0]["path"]=="search-fixtures/main.txt","Directory/include scopes must limit actual native file reads");
        require(Json::parse(tools.invoke("search_files",R"({"query":"NativeRegex","path":"search-fixtures/","include":"main.txt"})"))==filtered,"Search accepts a directory trailing separator and retains workspace-relative result paths");
        rejects<ToolFileError>([&]{tools.invoke("search_files",R"({"query":"first","path":"README.txt/"})");});
        const auto explicit_ignored=Json::parse(tools.invoke("search_files",R"({"query":"NativeRegex","path":"search-fixtures/ignored.txt","include":"*.cpp"})"));
        require(explicit_ignored["matches"].size()==1,"Explicit file scope bypasses ignore/include discovery filters");
        require(Json::parse(tools.invoke("search_files",R"({"query":"NativeRegex hidden","path":"search-fixtures"})"))["matches"].empty(),"Hidden discovery is disabled by default");
        require(Json::parse(tools.invoke("search_files",R"({"query":"NativeRegex hidden","path":"search-fixtures","hidden":true})"))["matches"].size()==1,"Hidden discovery can be explicitly enabled");
        require(Json::parse(tools.invoke("search_files",R"({"query":"NativeRegex ignored","path":"search-fixtures"})"))["matches"].empty(),"Search honors scoped ignore rules");
        require(Json::parse(tools.invoke("search_files",R"({"query":"NativeRegex ignored","path":"search-fixtures","respect_ignore":false})"))["matches"].size()==1,"Ignore bypass enables public ignored content");
        const auto invalid_tail=Json::parse(tools.invoke("search_files",R"({"query":"NativeRegex","path":"search-fixtures/invalid-tail.txt"})"));
        require(invalid_tail["matches"].empty()&&invalid_tail["skipped_entries"]==1&&invalid_tail["truncated"]==true,"Invalid UTF-8 after a matching prefix must reject the whole file before returning matches");
        const auto aggregate=Json::parse(tools.invoke("search_files",R"({"query":"ByteBudgetNeedle","path":"search-byte-budget"})"));
        require(aggregate["matches"].size()==1&&aggregate["matches"][0]["path"]=="search-byte-budget/c-small.txt"&&aggregate["scanned_files"]==1&&aggregate["skipped_entries"]==2&&aggregate["truncated"]==true,"Rejected binary reads must still consume the aggregate byte budget; later oversized files are skipped while smaller remaining files can be searched");
        const auto pathological=Json::parse(tools.invoke("search_files",R"({"query":"^(a+)+$","regex":true,"path":"search-fixtures/pathological.txt"})"));
        require(pathological["matches"].empty()&&pathological["scanned_files"]==1&&!pathological["truncated"].get<bool>(),"Native non-backtracking regex must finish an eight-MiB pathological nonmatch within the fixture process deadline");
        const auto wire_search=tools.invoke("search_files",R"({"query":"NativeRegex","path":"search-fixtures/budget.txt","limit":1000})");const auto search_budget=Json::parse(wire_search);
        require(wire_search.size()<=65536&&search_budget["matches"].size()>0&&search_budget["matches"].size()<100&&search_budget["truncated"]==true&&std::find(search_budget["limits"].begin(),search_budget["limits"].end(),"output_limit")!=search_budget["limits"].end(),"Escaped result text must respect the serialized output limit and disclose incomplete results");
        for(const auto* query:{"[abc","(?=secret)","(a)\\1","a{100000000}"}){
            try{tools.invoke("search_files",Json{{"query",query},{"regex",true},{"path","search-fixtures/main.txt"}}.dump());throw std::runtime_error("Invalid regex was accepted");}
            catch(const std::invalid_argument& error){require(std::string(error.what())=="Search pattern is invalid, unsupported or exceeds regex memory limits","Regex rejection must provide a fixed diagnostic without pattern contents");}
        }
        for(const auto* source:{R"({"query":"x","regex":"true"})",R"({"query":"x","case_sensitive":0})",R"({"query":"x","hidden":null})",R"({"query":"x","respect_ignore":1})",R"({"query":"x","path":false})",R"({"query":"x","include":false})",R"({"query":"x","limit":0})",R"({"query":"x","limit":1001})",R"({"query":"x","limit":1.5})",R"({"query":"x","regex":true,"regex":false})",R"({"query":"x","include":"[abc"})"})rejects<std::invalid_argument>([&]{tools.invoke("search_files",source);});
        rejects<std::invalid_argument>([&]{tools.search_files(std::string(4097,'x'));});
        for(const auto* path:{".config/providers.yaml",".agentflow/owner.token","config-alias/providers.yaml","state-alias/owner.token","../workspace-other/secret.txt","outside-link/secret.txt","inside-link/inside.txt","hard-link.txt"}){
            rejects<ToolAccessDenied>([&]{tools.invoke("search_files",Json{{"query",".*"},{"regex",true},{"hidden",true},{"respect_ignore",false},{"path",path}}.dump());});
        }
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
            rejects<ToolAccessDenied>([&]{tools.glob_files("**/*",path,true);});
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
        for(const auto* path:{"../workspace-other","outside-link", "README.txt:stream"})rejects<ToolAccessDenied>([&]{tools.glob_files("**/*",path,true);});
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
        rejects<ToolCancelled>([&]{tools.glob_files("**/*",".",true,100,cancelled.get_token());});
        rejects<ToolCancelled>([&]{tools.snapshot_file("README.txt",cancelled.get_token());});
        rejects<ToolCancelled>([&]{tools.plan_replacement("README.txt","first","changed",1,cancelled.get_token());});
        rejects<ToolCancelled>([&]{tools.search_files("needle",cancelled.get_token());});
        WorkspaceSearchOptions stopped_search;stopped_search.regex=true;stopped_search.path="search-fixtures/pathological.txt";
        rejects<ToolCancelled>([&]{tools.search_files("(a+)+$",stopped_search,cancelled.get_token());});
        std::vector<std::future<void>> readers;
        for(int i=0;i<4;++i) readers.push_back(std::async(std::launch::async,[&]{for(int n=0;n<10;++n) require(tools.read_file("README.txt").content==read.content,"Concurrent read isolation");}));
        for(auto& reader:readers) reader.get();
        std::cout<<"Native workspace tools passed on real filesystem fixtures; no model was called\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
