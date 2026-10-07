#include "agentflow/process.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <cstdlib>
using namespace agentflow;
using namespace std::chrono_literals;
namespace {
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class Error,class Action> void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected process rejection did not occur");}
std::string utf8(const std::filesystem::path& p){const auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
std::string file(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
ProcessConfiguration config(char** argv,const std::string& mode,const std::filesystem::path& marker) {
    const std::string directory=argv[3];
    return {argv[1],directory,ForegroundProcess::directory_identity(directory),{argv[2],mode,utf8(marker)},{{"XMIND_PROCESS_FIXTURE","configured actual fixture"}},10s,1024*1024};
}
}
int main(int argc,char** argv) {
    if(argc!=4)return 2;
    std::string stage="normal";
    try {
        const auto directory=std::filesystem::u8path(argv[3]);
        _putenv_s("XMIND_AUTH_TOKEN","synthetic private parent access");_putenv_s("XMIND_API_KEY","synthetic private parent model");_putenv_s("XMIND_PARENT_ONLY","synthetic parent sentinel");
        {
            auto c=config(argv,"normal",directory/"normal.txt");c.arguments.insert(c.arguments.end(),{"space argument","\"quoted\"","trailing\\","你好 🌍",""});
            std::string observed_out,observed_err;
            const auto result=ForegroundProcess::run(c,{},[&](bool err,std::string_view bytes){(err?observed_err:observed_out).append(bytes);});
            const auto j=nlohmann::json::parse(result.stdout_bytes);
            require(result.termination==ProcessTermination::exited && result.exit_code==0 && result.pid!=0,"Observed actual normal exit required");
            require(j["args"]==nlohmann::json::array({"space argument","\"quoted\"","trailing\\","你好 🌍",""}),"Literal arguments must survive Windows quoting");
            require(std::filesystem::equivalent(std::filesystem::u8path(j["cwd"].get<std::string>()),directory),"Child must run in the identified directory");
            require(j["configured"]=="configured actual fixture" && j["inherited"]==false,"Child environment must be explicit and exclude backend secrets");
            require(file(directory/"normal.txt")=="actual child effect","Normal child must perform actual file effect");
            require(result.stderr_bytes=="stderr 🌍" && observed_out==result.stdout_bytes && observed_err==result.stderr_bytes,"Both channels and observer bytes must survive exactly");
            require(result.stdout_count==result.stdout_bytes.size() && result.stderr_count==result.stderr_bytes.size() && !result.truncated,"Byte counts cannot be fabricated");
        }
        {
            const auto result=ForegroundProcess::run(config(argv,"bytes",directory/"unused.txt"));
            require(result.exit_code==7 && result.termination==ProcessTermination::exited,"Actual nonzero exit required");
            require(result.stdout_bytes==std::string("\xff\0\xfe\n",4) && result.stderr_bytes==std::string("\x80\r\0",3),"Raw invalid UTF-8 and NUL bytes must not be rewritten");
        }
        {
            auto c=config(argv,"flood",directory/"flood.txt");c.output_limit=8192;std::size_t observed=0;
            const auto result=ForegroundProcess::run(c,{},[&](bool,std::string_view bytes){observed+=bytes.size();});
            require(result.exit_code==0 && result.stdout_count==512*1024 && result.stderr_count==512*1024,"Both actual high-volume pipes must drain without deadlock");
            require(result.truncated && result.stdout_bytes.size()+result.stderr_bytes.size()==8192 && observed==8192,"Retention and observer must obey one bounded output budget");
            require(file(directory/"flood.txt")=="both channels drained","Child must finish both actual writes");
        }
        {
            const auto result=ForegroundProcess::run(config(argv,"input-eof",directory/"eof.txt"));
            require(result.exit_code==0 && result.stdout_bytes==R"({"size":0})" && file(directory/"eof.txt")=="input EOF","Noninteractive input must actually reach EOF");
        }
        {
            const auto sentinel=directory/"unrelated-handle.txt",ack=directory/"handle-ready.txt";
            SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
            HANDLE unrelated=CreateFileW(sentinel.c_str(),GENERIC_READ|GENERIC_WRITE,0,&inheritable,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            require(unrelated!=INVALID_HANDLE_VALUE,"Actual exclusive inheritable fixture handle required");
            try {
                auto c=config(argv,"handle",sentinel);c.arguments.push_back(utf8(ack));std::string ready;
                const auto result=ForegroundProcess::run(c,{},[&](bool err,std::string_view bytes){if(err || unrelated==nullptr)return;ready.append(bytes);if(ready.find('\n')!=std::string::npos){CloseHandle(unrelated);unrelated=nullptr;std::ofstream signal(ack);signal<<"parent released its own handle";}});
                require(result.exit_code==0 && file(sentinel)=="unrelated handle was not inherited","Explicit handle allowlist must exclude actual unrelated inheritable handle");
            }catch(...){if(unrelated)CloseHandle(unrelated);throw;}
            if(unrelated)CloseHandle(unrelated);
        }
        {
            std::stop_source stop;stop.request_stop();
            rejects<ProcessCancelledBeforeDispatch>([&]{ForegroundProcess::run(config(argv,"normal",directory/"cancel-before.txt"),stop.get_token());});
            require(!std::filesystem::exists(directory/"cancel-before.txt"),"Pre-dispatch cancellation must not execute child effect");
        }
        {
            const auto original=directory/"stale",moved=directory/"stale-original";std::filesystem::create_directory(original);
            auto c=config(argv,"normal",original/"effect.txt");c.working_directory=utf8(original);c.working_directory_id=ForegroundProcess::directory_identity(c.working_directory);
            std::filesystem::rename(original,moved);std::filesystem::create_directory(original);
            rejects<ProcessBeforeDispatchError>([&]{ForegroundProcess::run(c);});
            require(!std::filesystem::exists(original/"effect.txt") && !std::filesystem::exists(moved/"effect.txt"),"Changed directory identity must reject before effect");
        }
        {
            auto c=config(argv,"normal",directory/"junction"/"effect.txt");c.working_directory=utf8(directory/"junction");
            rejects<ProcessBeforeDispatchError>([&]{ForegroundProcess::directory_identity(c.working_directory);});
            require(!std::filesystem::exists(directory.parent_path()/"outside"/"effect.txt"),"Directory junction must not permit process dispatch");
        }
        for(const auto mode:{"tree","parent-exit"}) {
            stage=mode;
            const auto marker=directory/(std::string(mode)+".txt");auto c=config(argv,mode,marker);c.timeout=1500ms;
            HANDLE descendant=nullptr;std::string ready;
            const auto result=ForegroundProcess::run(c,{},[&](bool err,std::string_view bytes){if(err || descendant)return;ready.append(bytes);try{if(ready.find('\n')!=std::string::npos){const auto pid=nlohmann::json::parse(ready)["descendant"].get<DWORD>();descendant=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!descendant)throw std::runtime_error("Actual descendant handle unavailable: Windows error "+std::to_string(GetLastError()));require(WaitForSingleObject(descendant,0)==WAIT_TIMEOUT,"Actual descendant must be alive before native timeout cleanup");}}catch(const std::exception& e){std::cerr<<"Fixture readiness "<<mode<<": "<<e.what()<<" payload="<<ready.substr(0,512)<<'\n';throw;}});
            require(descendant!=nullptr,"Fixture must create a real descendant");const auto waited=WaitForSingleObject(descendant,0);CloseHandle(descendant);
            require(result.termination==ProcessTermination::timed_out && waited==WAIT_OBJECT_0,"Timeout must reap the actual descendant tree");
            require(!std::filesystem::exists(marker),"Terminated tree must not write delayed marker");
            if(std::string(mode)=="parent-exit")require(result.exit_code==0,"Parent zero exit must survive later descendant termination");
        }
        {
            stage="cancel-tree";
            const auto marker=directory/"cancel-tree.txt";auto c=config(argv,"tree",marker);std::stop_source stop;std::string ready;HANDLE descendant=nullptr;
            const auto result=ForegroundProcess::run(c,stop.get_token(),[&](bool err,std::string_view bytes){if(err || descendant)return;ready.append(bytes);if(ready.find('\n')!=std::string::npos){const auto pid=nlohmann::json::parse(ready)["descendant"].get<DWORD>();descendant=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);require(descendant!=nullptr,"Actual descendant required before cancellation");stop.request_stop();}});
            require(descendant!=nullptr,"Cancelled fixture must have an actual child");const auto waited=WaitForSingleObject(descendant,0);CloseHandle(descendant);
            require(result.termination==ProcessTermination::cancelled && waited==WAIT_OBJECT_0 && !std::filesystem::exists(marker),"Cancellation must actually stop the whole job");
        }
        {
            stage="observer-fault";
            auto c=config(argv,"normal",directory/"observer-fault.txt");
            rejects<ProcessEffectUncertain>([&]{ForegroundProcess::run(c,{},[](bool,std::string_view){throw std::runtime_error("Actual output sink fault");});});
            require(file(directory/"observer-fault.txt")=="actual child effect","Output failure must not conceal actual dispatched effect");
        }
        std::cout<<"Native foreground process adapter passed actual Windows child effects, literal arguments, separate/raw/bounded output, environment isolation, input EOF, stale directory/junction rejection, pre-dispatch cancellation, timeout/cancelled descendant trees and output-sink uncertainty. Approval/journal/model integration is not covered by this component.\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Stage "<<stage<<": "<<e.what()<<'\n';return 1;}
}
