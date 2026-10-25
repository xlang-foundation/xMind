#include "agentflow/language_server_tools.hpp"
#include "agentflow/execution_platform.hpp"
#include "agentflow/provider_profile_runtime.hpp"
#include "agentflow/http_server.hpp"
#include "httplib.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <thread>
#include <algorithm>
#include <fstream>
#include <exception>
#include "nlohmann/json.hpp"
using namespace agentflow;
namespace {
using Json=nlohmann::json;
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
std::string env(const char* name){const auto* value=std::getenv(name);return value?value:"";}
std::string utf8(const std::filesystem::path& path){const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
struct TempDirectory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/"xmind-language-server-http-contract";
    int uncaught_at_creation=std::uncaught_exceptions();
    TempDirectory(){path+=std::to_string(std::random_device{}());require(std::filesystem::create_directory(path),"HTTP fixture directory must be unique");}
    ~TempDirectory(){if(std::uncaught_exceptions()>uncaught_at_creation){std::cerr<<"Retained failed language-server fixture: "<<utf8(path)<<'\n';return;}std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
struct Serving {
    HttpServer& server;std::thread worker;
    explicit Serving(HttpServer& value):server(value),worker([&value]{value.listen();}){}
    ~Serving(){server.stop();if(worker.joinable())worker.join();}
};
LanguageServerProfile rust_profile(const std::string& executable){
    LanguageServerProfile profile;profile.id="rust-analyzer";profile.executable=executable;profile.language_id="rust";profile.extensions={".rs"};profile.request_timeout=std::chrono::seconds(90);
    const auto path=env("PATH");if(!path.empty())profile.environment.emplace_back("PATH",std::filesystem::u8path(executable).parent_path().string()+";"+path);
    const auto rustup=env("RUSTUP_HOME");if(!rustup.empty())profile.environment.emplace_back("RUSTUP_HOME",rustup);
    const auto user=env("USERPROFILE");if(!user.empty()){profile.environment.emplace_back("USERPROFILE",user);profile.environment.emplace_back("HOME",user);profile.environment.emplace_back("CARGO_HOME",user+"/.cargo");}
    const auto diagnostic=env("XMIND_LSP_TEST_LOG_DIRECTORY");if(!diagnostic.empty()){
        const auto directory=std::filesystem::u8path(diagnostic);require(directory.is_absolute()&&std::filesystem::is_directory(directory),"Language fixture diagnostic directory must already exist");
        profile.environment.emplace_back("RA_LOG","project_model=debug,rust_analyzer=info");
        profile.environment.emplace_back("RA_LOG_FILE",utf8(directory/("rust-"+std::to_string(std::random_device{}())+".log")));
    }
    return profile;
}
// Explicit model-call fixture; language queries still use the real registered
// Rust Analyzer process and the real AgentRunner/read-owner/SQLite path.
class LanguageModelFixture final:public AgentModelGateway {
public:
    LanguageModelFixture(std::string path,bool denied,bool custom=false):path_(std::move(path)),denied_(denied),custom_(custom){}
    ModelCompletion complete(const ChatProviderConfig&,const ModelRequest& request,ChatCompletionStream::Sink,std::stop_token)override{
        ++calls;ModelCompletion reply;
        if(calls==1){require(std::any_of(request.tools.begin(),request.tools.end(),[](const auto& t){return t.name=="language_server";}),"Run must offer its registered language tool");reply.finish_reason="tool_calls";const auto args=custom_?Json{{"text","xMind"}}:Json{{"server_id","rust-analyzer"},{"method","documentSymbol"},{"path",path_}};reply.tool_calls.push_back({custom_?"custom-language-extension":denied_?"denied-language-query":"allowed-language-query","language_server",args.dump()});return reply;}
        require(calls==2&&!request.messages.empty()&&request.messages.back().role==MessageRole::tool,"Runner must deliver one actual owned language result");
        const auto value=Json::parse(request.messages.back().content);
        if(custom_)require(value.value("count",0)==5&&value.value("text",std::string{})=="XMIND","An admitted custom language_server alias must retain its actual xlang3 callable when no LSP is configured");
        else if(denied_)require(value.contains("error")&&value["error"]["code"]=="agent_permission_denied","Path-specific language denial must reach the model result");
        else require(value.contains("result")&&value["result"].is_array()&&!value["result"].empty(),"Actual Rust Analyzer symbols must cross the owned run boundary");
        reply.content="Fixture inspected the actual owned query outcome.";reply.finish_reason="stop";return reply;
    }
    ResponsesTokenCount count_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{throw std::runtime_error("Unexpected fixture context count");}
    ResponsesCompactionResult compact_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{throw std::runtime_error("Unexpected fixture compaction");}
    unsigned calls=0;
private:
    std::string path_;bool denied_,custom_;
};
void agent_owner_contract(const std::string& executable,const std::string& workspace_path,const std::string& path,const std::string& modules,const std::string& pure){
    TempDirectory temp;PersistenceService persistence(utf8(temp.path/"agent-language.sqlite"),{modules,pure});persistence.create_session("language-session","Actual owned AgentRunner language query").get();
    for(const bool denied:{false,true}){
        AgentSettings settings;settings.workspace=workspace_path;settings.language_servers.push_back(rust_profile(executable));settings.a2a_enabled=false;settings.tool_allowlist=std::vector<std::string>{"language_server"};settings.provider.endpoint="http://127.0.0.1:1/v1/chat/completions";settings.provider.model="explicit-fixture";settings.provider.tools=Capability::supported;
        if(denied)settings.permission_rules={{"language_server",path,"deny"}};
        const auto gateway=std::make_shared<LanguageModelFixture>(path,denied);AgentRunner runner(persistence,std::move(settings),{},{},false,gateway);const std::string id=denied?"denied-language-run":"allowed-language-run";
        runner.start(id,"language-session","Query the registered language server through its admitted owner.");const auto done=runner.execute(id);require(done.state==RunState::completed&&gateway->calls==2,"Real AgentRunner must complete its owned language query and denial turns");
    }
    const auto code=temp.path/"custom_language.py";std::ofstream module(code,std::ios::binary);module<<"def transform(arguments):\n    return {\"text\": arguments[\"text\"].upper(), \"count\": len(arguments[\"text\"])}\n";module.close();require(bool(module),"Trusted extension fixture must be written");
    AgentSettings custom;custom.workspace=workspace_path;custom.a2a_enabled=false;custom.tool_allowlist=std::vector<std::string>{"language_server"};custom.provider.endpoint="http://127.0.0.1:1/v1/chat/completions";custom.provider.model="explicit-fixture";custom.provider.tools=Capability::supported;
    custom.xlang_python_tools.push_back({ModelToolDefinition{"language_server","Explicit trusted alias regression",R"({"type":"object","properties":{"text":{"type":"string"}},"required":["text"],"additionalProperties":false})"},XlangPythonCallable(modules,pure,utf8(temp.path),"custom_language","transform")});
    const auto gateway=std::make_shared<LanguageModelFixture>(path,false,true);AgentRunner runner(persistence,std::move(custom),{},{},false,gateway);runner.start("custom-language-run","language-session","Invoke the registered trusted custom extension.");require(runner.execute("custom-language-run").state==RunState::completed&&gateway->calls==2,"Custom alias dispatch must remain admitted without an LSP registry");
}
void http_contract(const std::string& executable,const std::string& workspace_path,const std::string& path,const std::string& modules,const std::string& pure_python){
    TempDirectory temp;PersistenceService persistence(utf8(temp.path/"lsp.sqlite"),{modules,pure_python});AgentSettings settings;settings.workspace=workspace_path;settings.language_servers.push_back(rust_profile(executable));
    ExecutionPlatform runtime(persistence,std::move(settings),1,8);const std::string token="synthetic-language-server-http-access-token-32-bytes";HttpServer server(persistence,token,&runtime);const auto port=server.bind(0);require(port>0,"Native language-server HTTP API failed to bind");Serving serving(server);
    httplib::Client client("127.0.0.1",port);client.set_connection_timeout(2,0);client.set_read_timeout(100,0);client.set_follow_location(false);const httplib::Headers headers{{"Authorization","Bearer "+token}};
    httplib::Result workspace;
    for(int i=0;i<50;++i){workspace=client.Get("/v1/workspace",headers);if(workspace)break;std::this_thread::sleep_for(std::chrono::milliseconds(20));}
    require(workspace&&workspace->status==200,"Workspace-bound LSP endpoint requires a running native backend");const auto metadata=Json::parse(workspace->body);require(metadata["configured"]==true,"LSP HTTP runtime must own the configured workspace");
    const auto binding=Json{{"expected_workspace_id",metadata["workspace_id"]},{"expected_workspace_authority_id",metadata["authority_id"]}};
    Json input={{"path",path},{"method","documentSymbol"},{"line",0},{"character",0}};input.update(binding);
    const auto response=client.Post("/v1/language-server/query",headers,input.dump(),"application/json");require(response&&response->status==200,"Authenticated workspace-bound LSP query must reach the registered language server");const auto result=Json::parse(response->body);
    require(result["server_id"]=="rust-analyzer"&&result["path"]==path&&result["result"].is_array()&&result["result"].size()==2&&result["result"][0]["name"]=="add","HTTP editor query must return actual Rust Analyzer symbols from the opened workspace");
    auto definition_input=input;definition_input["method"]="definition";definition_input["line"]=5;definition_input["character"]=5;
    Json definition_result;
    for(int attempt=0;attempt<120;++attempt){
        const auto definition_response=client.Post("/v1/language-server/query",headers,definition_input.dump(),"application/json");require(definition_response&&definition_response->status==200,"Authenticated HTTP editor navigation query must reach Rust Analyzer");
        definition_result=Json::parse(definition_response->body)["result"];
        if(definition_result.is_array()&&!definition_result.empty())break;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    require(definition_result.is_array()&&!definition_result.empty()&&(definition_result[0].contains("targetUri")||definition_result[0].contains("uri")),"Native HTTP must preserve real Rust Analyzer definition locations for the editor");
    auto unsaved=input;unsaved["document_text"]="pub fn add(left: i32, right: i32) -> i32 { left + right }\npub fn call_add() -> i32 { add(1, 2) }\npub fn unsaved_marker() {}\n";const auto edited=client.Post("/v1/language-server/query",headers,unsaved.dump(),"application/json");require(edited&&edited->status==200,"Editor query must accept the current unsaved document buffer");const auto edited_result=Json::parse(edited->body);require(edited_result["result"].is_array()&&edited_result["result"].size()==3&&edited_result["result"][2]["name"]=="unsaved_marker","Real Rust Analyzer symbols must reflect an unsaved editor-only declaration");
    auto formatting=Json{{"path",path},{"method","formatting"},{"document_text","pub fn deliberately_unformatted( )->i32{let value=1;value}\n"},{"formatting_options",{{"tab_size",2},{"insert_spaces",true},{"trim_trailing_whitespace",true}}}};formatting.update(binding);const auto formatted=client.Post("/v1/language-server/query",headers,formatting.dump(),"application/json");require(formatted&&formatted->status==200&&Json::parse(formatted->body)["result"].is_array()&&!Json::parse(formatted->body)["result"].empty(),"Authenticated workspace-bound HTTP formatting must return real unsaved-buffer edits");
    auto invalid_formatting=formatting;invalid_formatting["formatting_options"]["insert_spaces"]="true";const auto bad_formatting=client.Post("/v1/language-server/query",headers,invalid_formatting.dump(),"application/json");require(bad_formatting&&bad_formatting->status==400,"Malformed formatting options must fail before native language-server dispatch");
    const auto restored=client.Post("/v1/language-server/query",headers,input.dump(),"application/json");if(!(restored&&restored->status==200&&Json::parse(restored->body)["result"].size()==2))throw std::runtime_error("Restoring the workspace file text must remove the unsaved symbol without changing the file; observed "+(restored?std::to_string(restored->status)+" "+restored->body.substr(0,8192):"HTTP transport error "+std::to_string(static_cast<int>(restored.error()))));
    const auto view_issue=client.Post("/v1/view-sessions",headers,Json{{"origin","http://127.0.0.1:42424"}}.dump(),"application/json");require(view_issue&&view_issue->status==200,"Local browser view must enroll before using workspace-bound LSP metadata");const auto view_token=Json::parse(view_issue->body).at("credential").get<std::string>();const httplib::Headers view_headers{{"Authorization","View "+view_token},{"X-XMind-View-Origin","http://127.0.0.1:42424"}};
    const auto profiles=client.Get("/v1/language-servers",view_headers);require(profiles&&profiles->status==200&&Json::parse(profiles->body)["profiles"].size()==1,"Authorized local views must discover sanitized language-server profiles");
    const auto view_response=client.Post("/v1/language-server/query",view_headers,input.dump(),"application/json");if(!(view_response&&view_response->status==200&&Json::parse(view_response->body)["result"]==result["result"]))throw std::runtime_error("Authorized local views must query the same native language-server process; observed "+(view_response?std::to_string(view_response->status)+" "+view_response->body.substr(0,8192):"HTTP transport error "+std::to_string(static_cast<int>(view_response.error())))+"; expected "+result["result"].dump());
    const auto view_definition=client.Post("/v1/language-server/query",view_headers,definition_input.dump(),"application/json");if(!(view_definition&&view_definition->status==200&&Json::parse(view_definition->body)["result"]==definition_result))throw std::runtime_error("Authorized browser views must preserve native semantic navigation results; observed "+(view_definition?std::to_string(view_definition->status)+" "+view_definition->body.substr(0,8192):"HTTP transport error "+std::to_string(static_cast<int>(view_definition.error())))+"; expected "+definition_result.dump());
    auto stale=input;stale["expected_workspace_authority_id"]=std::string(32,'0');const auto rejected=client.Post("/v1/language-server/query",headers,stale.dump(),"application/json");require(rejected&&rejected->status==409,"Stale workspace authority must be rejected before language-server dispatch");
    httplib::Client anonymous_client("127.0.0.1",port);const auto anonymous=anonymous_client.Post("/v1/language-server/query",input.dump(),"application/json");require(anonymous&&anonymous->status==401,"Editor LSP query must retain native backend authentication");
}
void provider_profile_contract(const std::string& executable,const std::string& workspace_path,const std::string& path,const std::string& modules,const std::string& pure_python){
    TempDirectory temp;PersistenceService persistence(utf8(temp.path/"profile-lsp.sqlite"),{modules,pure_python});
    AgentSettings settings;settings.workspace=workspace_path;settings.language_servers.push_back(rust_profile(executable));
    ProviderProfileRoute route{"openai.chat","openai","https://api.openai.com/v1/chat/completions","server","provider:setup:openai",ProviderWire::chat_completions};
    ChatProviderConfig provider;provider.endpoint=route.endpoint;provider.wire=route.wire;
    ProviderProfileExecutionPolicy policy{route,provider};ProviderProfileRuntime runtime(persistence,std::move(settings),{policy},1,8);
    const auto profiles=runtime.language_server_profiles();require(profiles.size()==1&&profiles.front().id=="rust-analyzer"&&profiles.front().extensions==std::vector<std::string>{".rs"},"Provider-profile runtime must expose sanitized trusted language-server metadata even before model selection");
    LanguageServerRequest formatting;formatting.path=path;formatting.method="formatting";formatting.document_text="pub fn profile_runtime_format( )->i32{let value=2;value}\n";formatting.formatting_options=LanguageServerRequest::FormattingOptions{4,true,true,false,true};
    const auto direct=Json::parse(runtime.language_server_query(formatting));require(direct["result"].is_array()&&!direct["result"].empty(),"Provider-profile runtime must forward user-triggered formatting through its native execution platform");
    const std::string token="provider-profile-language-server-contract-token-32-bytes";HttpServer server(persistence,token,&runtime);const auto port=server.bind(0);require(port>0,"Provider-profile language-server HTTP API failed to bind");Serving serving(server);
    httplib::Client client("127.0.0.1",port);client.set_connection_timeout(2,0);client.set_read_timeout(100,0);client.set_follow_location(false);const httplib::Headers headers{{"Authorization","Bearer "+token}};
    httplib::Result workspace;for(int i=0;i<50;++i){workspace=client.Get("/v1/workspace",headers);if(workspace)break;std::this_thread::sleep_for(std::chrono::milliseconds(20));}
    require(workspace&&workspace->status==200,"Provider-profile runtime must publish its opened workspace");const auto metadata=Json::parse(workspace->body);
    const auto catalogue=client.Get("/v1/language-servers",headers);require(catalogue&&catalogue->status==200&&Json::parse(catalogue->body)["profiles"].size()==1,"Provider-profile HTTP client must discover configured languages without provider-key metadata");
    Json input={{"path",path},{"method","formatting"},{"document_text",formatting.document_text},{"formatting_options",{{"tab_size",4},{"insert_spaces",true},{"trim_trailing_whitespace",true},{"insert_final_newline",false},{"trim_final_newlines",true}}},{"expected_workspace_id",metadata["workspace_id"]},{"expected_workspace_authority_id",metadata["authority_id"]}};
    const auto formatted=client.Post("/v1/language-server/query",headers,input.dump(),"application/json");require(formatted&&formatted->status==200&&Json::parse(formatted->body)["result"].is_array()&&!Json::parse(formatted->body)["result"].empty(),"Authenticated provider-profile HTTP must return actual Rust Analyzer formatting edits");
}
}
int main(int argc,char** argv){
    if(argc!=6)return 2;
    try{
        // Source checkouts may live inside protected backend configuration.
        // Exercise the same project in an owned model workspace; never relax
        // workspace admission merely to make the fixture source admissible.
        TempDirectory project;
        const auto fixture=std::filesystem::u8path(argv[2]);
        for(const auto* name:{"Cargo.toml","Cargo.lock"}){
            require(std::filesystem::is_regular_file(std::filesystem::symlink_status(fixture/name)),"Rust fixture manifest must be a regular source file");
            std::filesystem::copy_file(fixture/name,project.path/name);
        }
        require(std::filesystem::is_directory(std::filesystem::symlink_status(fixture/"src")),"Rust fixture source must be a real directory");
        for(const auto& entry:std::filesystem::recursive_directory_iterator(fixture/"src"))require(!entry.is_symlink()&&(entry.is_regular_file()||entry.is_directory()),"Rust fixture source must contain only regular files and directories");
        std::filesystem::copy(fixture/"src",project.path/"src",std::filesystem::copy_options::recursive);
        const auto workspace_path=utf8(project.path);
        WorkspaceTools workspace(workspace_path);LanguageServerProfile profile;
        profile=rust_profile(argv[1]);const std::string path=argv[3];
        LanguageServerToolRegistry registry(workspace,{profile});
        require(registry.definitions().size()==1&&registry.definitions()[0].name=="language_server","Agent catalogue must expose one configured read-only language-server tool");
        const auto request=[&](const char* method){return nlohmann::json::parse(registry.invoke(nlohmann::json{{"server_id",profile.id},{"method",method},{"path",path}}.dump()));};
        const auto symbols=request("documentSymbol");
        require(symbols["result"].is_array()&&symbols["result"].size()==2,"Real rust-analyzer must return the two declarations in the workspace source file");
        require(symbols["result"][0]["name"]=="add"&&symbols["result"][1]["name"]=="call_add","Real rust-analyzer symbols must match the Rust fixture");
        const auto repeated=request("documentSymbol");
        require(repeated["result"]==symbols["result"],"A persistent language-server process must answer repeated workspace requests consistently");
        Json definition;
        for(int attempt=0;attempt<120;++attempt){
            definition=Json::parse(registry.query(LanguageServerRequest{path,"definition",5,5}))["result"];
            if(definition.is_array()&&!definition.empty())break;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        if(!definition.is_array()||definition.empty())throw std::runtime_error("Real rust-analyzer must resolve a call-site definition through the native editor query; received: "+definition.dump());
        const auto& target=definition.front();
        require((target.contains("targetUri")||target.contains("uri"))&&(target.contains("targetSelectionRange")||target.contains("range")),"Definition results must contain a VS Code-mappable workspace URI and selection range");
        const auto references=Json::parse(registry.query(LanguageServerRequest{path,"references",5,5}))["result"];
        require(references.is_array()&&references.size()>=2,"Real rust-analyzer must find the declaration and call-site references");
        const auto hover=Json::parse(registry.query(LanguageServerRequest{path,"hover",5,5}))["result"];
        require(hover.is_object()&&hover.contains("contents")&&!hover["contents"].is_null(),"Real rust-analyzer must return usable hover content for the call-site symbol");
        LanguageServerRequest formatting;formatting.path=path;formatting.method="formatting";formatting.document_text="pub fn deliberately_unformatted( )->i32{let value=1;value}\n";formatting.formatting_options=LanguageServerRequest::FormattingOptions{4,true,true,false,true};
        const auto format_edits=Json::parse(registry.query(formatting))["result"];
        require(format_edits.is_array()&&!format_edits.empty(),"Native editor formatting must return real rust-analyzer edits for an unformatted unsaved buffer");
        bool model_format_denied=false;try{(void)registry.invoke(Json{{"server_id",profile.id},{"method","formatting"},{"path",path}}.dump());}catch(const ToolAccessDenied&){model_format_denied=true;}
        require(model_format_denied,"AgentRunner language-server tool must remain read-only and cannot invoke document formatting");
        const auto diagnostics=request("diagnostics");
        require(diagnostics["result"].is_object()&&diagnostics["result"]["items"].is_array()&&diagnostics["result"]["items"].empty(),"Real rust-analyzer must report the valid fixture without diagnostics");
        const auto editor=registry.query(LanguageServerRequest{path,"documentSymbol",0,0});if(Json::parse(editor)["result"]!=symbols["result"])throw std::runtime_error("Editor and AgentRunner symbol comparison changed; observed "+Json::parse(editor)["result"].dump()+"; expected "+symbols["result"].dump());
        http_contract(argv[1],workspace_path,path,argv[4],argv[5]);provider_profile_contract(argv[1],workspace_path,path,argv[4],argv[5]);agent_owner_contract(argv[1],workspace_path,path,argv[4],argv[5]);
        std::cout<<"Actual rust-analyzer verified persistent native stdio, symbols, definition, references, hover, clean diagnostics and authenticated formatting. Real AgentRunner used an explicit model fixture to verify owned language queries and path-specific denial through xlang3 SQLite; no live model or synthetic LSP peer used\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
