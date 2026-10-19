#include "agentflow/language_server.hpp"
#include "agentflow/language_server_tools.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>

using namespace agentflow;
namespace {
using Json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct TempDirectory {
    std::filesystem::path root=std::filesystem::temp_directory_path(),path=root/("xmind-language-diagnostics-"+std::to_string(std::random_device{}()));
    TempDirectory(){require(std::filesystem::create_directory(path),"Diagnostics fixture must be unique");std::ofstream(path/"check.ts")<<"const greeting: string = 42;\n";}
    ~TempDirectory(){if(path.parent_path()==root&&path.filename().string().starts_with("xmind-language-diagnostics-")){std::error_code ignored;std::filesystem::remove_all(path,ignored);}}
};
LanguageServerProfile profile(const char* node,const char* peer,const std::string& mode){LanguageServerProfile p;p.id="diagnostics-fixture";p.executable=node;p.language_id="typescript";p.extensions={".ts"};p.arguments={peer,mode};p.request_timeout=std::chrono::seconds(1);return p;}
}
int main(int argc,char** argv){if(argc!=3&&argc!=4)return 2;try{
    TempDirectory temp;WorkspaceTools workspace(temp.path.string());
    if(argc==4){
        LanguageServerProfile actual;actual.id="typescript";actual.executable=argv[1];actual.language_id="typescript";actual.extensions={".ts"};actual.arguments={argv[3],"--stdio"};actual.request_timeout=std::chrono::seconds(30);
        LanguageServerClient client(workspace,actual);LanguageServerRequest request;request.path="check.ts";request.method="diagnostics";
        for(const auto* text:{"export const greeting: string = \"first\";\n","export const greeting: string = \"second\";\n","export const greeting: string = 42;\n","export const greeting: string = \"fixed\";\n"}){
            request.document_text=text;const auto result=Json::parse(client.request(request));const auto count=result["result"]["items"].size();
            require(std::string(text).find("= 42;")==std::string::npos?count==0:count>0,"Actual TypeScript server must analyze each clean/clean/error/clean buffer rather than reuse stale diagnostics");
            std::cout<<"Actual TypeScript buffer diagnostic count: "<<count<<'\n';
        }
        return 0;
    }
    for(const std::string mode:{"push","unversioned","suppress-clean"}){
        LanguageServerClient client(workspace,profile(argv[1],argv[2],mode));LanguageServerRequest request;request.path="check.ts";request.method="diagnostics";
        const auto first=Json::parse(client.request(request));require(first["result"]["kind"]=="full"&&first["result"]["items"].size()==1,"Push-only server must return its actual diagnostics without a pull request");
        require(first["result"]["items"][0]["code"]==2322&&first["result"]["items"][0]["data"]["version"]==1,"An explicit stale publication cannot satisfy the current version");
        require(first["result"]["items"][0]["data"]["clientPush"]==true,"Native client must advertise version-aware publish diagnostics");
        request.document_text="const greeting: string = \"hello\";\n";const auto fixed=Json::parse(client.request(request));require(fixed["result"]["items"].empty()&&fixed["diagnostics"].empty(),"Changed buffer must wait for its fresh empty publication instead of retaining an old error");
        require(Json::parse(client.request(request))["result"]["items"].empty(),"Unchanged buffer may reuse its observed clean diagnostics");
        request.document_text="const greeting: string = \"another clean value\";\n";
        require(Json::parse(client.request(request))["result"]["items"].empty(),"A clean-to-clean edit must obtain a fresh result even when the push server suppresses repeated empty diagnostics");
    }
    {LanguageServerClient client(workspace,profile(argv[1],argv[2],"pull"));LanguageServerRequest request;request.path="check.ts";request.method="diagnostics";require(Json::parse(client.request(request))["result"]["items"][0]["code"]==2322,"Advertised pull diagnostics must use the real request result");}
    {
        LanguageServerToolRegistry registry(workspace,{profile(argv[1],argv[2],"silent")});
        const auto args=Json{{"server_id","diagnostics-fixture"},{"method","diagnostics"},{"path","check.ts"}}.dump();
        const auto failure=Json::parse(registry.invoke(args));
        require(failure["error"]["code"]=="language_server_unavailable"&&!failure.contains("result"),"Timed-out LSP tool must report unavailable diagnostics, never a clean result or a fatal MCP run error");
        require(failure["error"]["detail"].get<std::string>().find("deadline")!=std::string::npos,"Transport failure detail must survive the read-only tool boundary");
        std::stop_source stop;stop.request_stop();bool cancelled=false;
        try{(void)registry.invoke(args,stop.get_token());}catch(const McpTransportCancelled&){cancelled=true;}catch(const ToolCancelled&){cancelled=true;}
        require(cancelled,"LSP cancellation must propagate instead of becoming a recoverable tool error");
    }
    for(const std::string mode:{"pull-error","silent"}){
        LanguageServerClient client(workspace,profile(argv[1],argv[2],mode));LanguageServerRequest request;request.path="check.ts";request.method="diagnostics";bool rejected=false;
        try{(void)client.request(request);}catch(const std::runtime_error& error){rejected=true;if(mode=="pull-error")require(std::string(error.what()).find("Explicit fixture pull failure")!=std::string::npos,"Pull error must remain visible even after a cached empty publication");}
        require(rejected,"Missing diagnostics or a failed pull must never be reported as a clean document");
    }
    std::cout<<"Native LSP diagnostics passed actual framed push/pull dispatch, version filtering, changed-buffer invalidation, observed clean results and bounded missing/error outcomes. Synthetic server only.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
