#include "httplib.h"
#include "nlohmann/json.hpp"
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <chrono>
#include <thread>
#include <limits>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <set>
#include <vector>
#include <algorithm>

namespace {
std::int64_t event_cursor(const std::string& source) {
    std::int64_t value=0;const auto parsed=std::from_chars(source.data(),source.data()+source.size(),value);
    if(parsed.ec!=std::errc{} || parsed.ptr!=source.data()+source.size() || value<0)throw std::invalid_argument("Invalid cursor");
    return value;
}
nlohmann::json provider_key_fields(const std::string& variable,const std::string& revision_text) {
    auto normalized=variable;for(auto& character:normalized)if(character>='a' && character<='z')character=static_cast<char>(character-'a'+'A');
    if(variable.empty() || variable.size()>128 || variable.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos || (variable.front()>='0' && variable.front()<='9') || normalized=="XMIND_AUTH_TOKEN" || normalized.starts_with("XMIND_UI_"))throw std::invalid_argument("Select a provider key environment variable");
    const auto revision=event_cursor(revision_text);if(revision>9007199254740991)throw std::invalid_argument("Invalid provider revision");
    const auto* secret=std::getenv(variable.c_str());if(!secret || !*secret)throw std::invalid_argument("Provider key environment variable is empty");const auto length=std::strlen(secret);if(length>32768)throw std::invalid_argument("Provider key exceeds limits");
    nlohmann::json fields={{"api_key",std::string(secret,length)},{"expected_revision",revision}};
#if defined(_WIN32)
    _putenv_s(variable.c_str(),""); // Only this client process; preserve parent/user settings.
#endif
    return fields;
}
// Observation only: ending this client never grants, cancels or owns execution.
// Each flushed NDJSON record is an actual persisted backend event. Its seq can
// be supplied on reconnect; process-output hex is never written as terminal code.
int watch_run(httplib::Client& client,const httplib::Headers& headers,const std::string& run,std::int64_t cursor,bool graph=false,bool interactive=false) {
    using Json=nlohmann::json;const auto path="/v1/runs/"+run;
    auto read=[&](const std::string& route) {
        auto response=client.Get(route,headers);if(!response)throw std::runtime_error("Cannot reach xMind Server during observation");
        if(response->status<200 || response->status>=300)throw std::runtime_error("Server rejected run observation (HTTP "+std::to_string(response->status)+")");
        return Json::parse(response->body);
    };
    std::string session;
    if(graph){const auto root=read(path);if(!root.is_object() || root.value("id",std::string{})!=run || root.value("graph_root",false)!=true || !root.contains("session_id") || !root["session_id"].is_string() || root["session_id"].get<std::string>().empty())throw std::runtime_error("Select a graph root for graph observation");session=root["session_id"].get<std::string>();}
    auto owners=[&] {
        std::set<std::string> owned{run};
        if(graph){
            const auto children=read("/v1/graph-runs/"+run+"/children");
            if(!children.is_array())throw std::runtime_error("Invalid graph child batch");
            for(const auto& child:children){const auto id=child.value("id",std::string{});if(!child.is_object() || child.value("parent_id",std::string{})!=run || child.value("session_id",std::string{})!=session || id.empty() || id.size()>128 || id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos || !owned.insert(id).second)throw std::runtime_error("Invalid graph child ownership");}
        }
        return owned;
    };
    auto emit=[&] {
        const auto events=read((graph?"/v1/graph-runs/"+run:path)+"/events?after="+std::to_string(cursor));
        if(!events.is_array())throw std::runtime_error("Invalid backend event batch");
        // Ownership is read after events so newly admitted children are covered.
        const auto owned=owners();
        for(const auto& event:events) {
            if(!event.is_object() || !event.contains("seq") || !event["seq"].is_number_integer() || event["seq"]<=cursor || event["seq"]>std::numeric_limits<std::int64_t>::max() || !event.contains("run_id") || !event["run_id"].is_string() || !owned.contains(event["run_id"].get<std::string>()) || !event.contains("kind") || !event["kind"].is_string() || !event.contains("data"))throw std::runtime_error("Invalid backend event identity or cursor");
            std::cout<<event.dump()<<'\n'<<std::flush;
            if(!std::cout)throw std::runtime_error("Run observation output is unavailable");
            cursor=event["seq"].get<std::int64_t>();
        }
    };
    for(;;) {
        emit();const auto state=read(path);
        if(!state.is_object() || state.value("id",std::string{})!=run || !state.contains("state") || !state["state"].is_string())throw std::runtime_error("Invalid backend run identity or state");
        if(graph && (state.value("graph_root",false)!=true || state.value("session_id",std::string{})!=session))throw std::runtime_error("Graph observation identity changed");
        const auto value=state["state"].get<std::string>();
        if(value=="completed" || value=="failed" || value=="cancelled") {
            // A transition can commit between the event query and status query.
            emit();return value=="completed"?0:value=="cancelled"?2:1;
        }
        if(value!="queued" && value!="running" && value!="paused")throw std::runtime_error("Unknown backend run state");
        if(interactive){
            auto operations=Json::array();const auto owned=owners();
            for(const auto& owner:owned){const auto batch=read("/v1/runs/"+owner+"/operations");if(!batch.is_array())throw std::runtime_error("Invalid backend operation list");for(const auto& operation:batch){if(!operation.is_object()||operation.value("run_id",std::string{})!=owner)throw std::runtime_error("Invalid operation ownership");operations.push_back(operation);}}
            bool interacted=false;
            for(const auto& operation:operations){
                if(!operation.is_object() || !owned.contains(operation.value("run_id",std::string{})))throw std::runtime_error("Invalid operation ownership");
                if(operation.value("state",std::string{})!="awaiting_approval")continue;
                const auto operationId=operation.value("id",std::string{});
                if(operationId.empty() || operationId.size()>128 || operationId.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid approval identity");
                // JSON escaping keeps file/process content inert on the terminal.
                // The native backend retains the exact proposal and revalidates
                // authority, expiry and effect preconditions on the decision.
                std::cout<<Json{{"type","operation_review"},{"operation",operation}}.dump()<<'\n'<<std::flush;
                if(!std::cout)throw std::runtime_error("Approval review output is unavailable");
                std::cerr<<"Review the exact operation above. Enter /allow "<<operationId<<", /deny "<<operationId<<", /cancel or /exit to detach.\n";
                std::string decision;
                if(!std::getline(std::cin,decision))throw std::runtime_error("CLI detached before a decision; backend execution remains owned by the server");
                if(!decision.empty() && decision.back()=='\r')decision.pop_back();
                if(decision=="/exit")throw std::runtime_error("CLI detached before a decision; backend execution remains owned by the server");
                std::string route;Json body;
                if(decision=="/cancel"){route=path+"/cancel";body=Json::object();}
                else if(decision=="/allow "+operationId || decision=="/deny "+operationId){route="/v1/operations/"+operationId+"/decision";body={{"decision",decision.starts_with("/allow ")?"allow":"deny"}};}
                else {std::cerr<<"No decision sent. Use an exact displayed operation ID.\n";break;}
                const auto response=client.Post(route,headers,body.dump(),"application/json");
                if(!response)throw std::runtime_error("Cannot reach xMind Server for approval");
                if(response->status<200 || response->status>=300)std::cerr<<"Backend rejected the decision (HTTP "<<response->status<<"). Refreshing recorded state.\n";
                else std::cout<<Json{{"type",decision=="/cancel"?"run_cancel_result":"operation_decision_result"},{"result",Json::parse(response->body)}}.dump()<<'\n'<<std::flush;
                interacted=true;
                break; // Re-read state before reviewing another operation.
            }
            if(interacted)continue;
            if(graph){
                const auto snapshot=read("/v1/graph-runs/"+run);
                if(!snapshot.is_object()||snapshot.at("run").value("id",std::string{})!=run||snapshot.at("run").value("session_id",std::string{})!=session||!snapshot.contains("checkpoint_revision")||!snapshot["checkpoint_revision"].is_number_integer()||snapshot["checkpoint_revision"]<1||snapshot["checkpoint_revision"]>9007199254740991||!snapshot.at("checkpoint").at("nodes").is_array()||!snapshot.at("spec").at("nodes").is_array())throw std::runtime_error("Invalid graph input snapshot");
                auto steps=Json::array();std::set<std::string> waiting;
                for(const auto& node:snapshot["checkpoint"]["nodes"]){
                    if(node.value("state",std::string{})!="waiting_human")continue;
                    const auto id=node.value("id",std::string{});
                    if(id.empty()||id.size()>64||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos||!waiting.insert(id).second)throw std::runtime_error("Invalid graph human identity");
                    const auto& definitions=snapshot["spec"]["nodes"];const auto definition=std::find_if(definitions.begin(),definitions.end(),[&](const Json& value){return value.value("id",std::string{})==id&&value.value("type",std::string{})=="human";});
                    if(definition==definitions.end()||!definition->contains("prompt")||!(*definition)["prompt"].is_string())throw std::runtime_error("Invalid graph human definition");
                    steps.push_back({{"node_id",id},{"prompt",(*definition)["prompt"]}});
                }
                if(!steps.empty()){
                    const auto revision=snapshot["checkpoint_revision"].get<std::int64_t>();
                    std::cout<<Json{{"type","graph_human_review"},{"root_run_id",run},{"checkpoint_revision",revision},{"steps",steps}}.dump()<<'\n'<<std::flush;
                    if(!std::cout)throw std::runtime_error("Graph review output is unavailable");
                    std::cerr<<"Enter /input NODE_ID JSON for a displayed step, /cancel or /exit to detach.\n";
                    std::string answer;if(!std::getline(std::cin,answer))throw std::runtime_error("CLI detached before graph input; backend execution remains owned by the server");if(!answer.empty()&&answer.back()=='\r')answer.pop_back();if(answer=="/exit")throw std::runtime_error("CLI detached before graph input; backend execution remains owned by the server");
                    std::string route;Json body;
                    if(answer=="/cancel"){route=path+"/cancel";body=Json::object();}
                    else if(answer.starts_with("/input ")){const auto split=answer.find(' ',7);const auto node=split==std::string::npos?std::string{}:answer.substr(7,split-7);const auto input=split==std::string::npos?std::string{}:answer.substr(split+1);if(!waiting.contains(node)||input.empty()||input.size()>65536){std::cerr<<"No input sent. Use a displayed node and bounded JSON.\n";continue;}route="/v1/graph-runs/"+run+"/human/"+node;body={{"input_json",input},{"expected_checkpoint_revision",revision}};}
                    else {std::cerr<<"No input sent. Use /input, /cancel or /exit.\n";continue;}
                    const auto response=client.Post(route,headers,body.dump(),"application/json");if(!response)throw std::runtime_error("Cannot reach xMind Server for graph input");if(response->status<200||response->status>=300)std::cerr<<"Backend rejected graph input (HTTP "<<response->status<<"). Refreshing the checkpoint; no automatic retry.\n";else std::cout<<Json{{"type",answer=="/cancel"?"run_cancel_result":"graph_input_result"},{"result",Json::parse(response->body)}}.dump()<<'\n'<<std::flush;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}
// Interactive access client, not a second agent engine. Every request, tool
// result, approval and response remains owned by the shared native server.
int chat_session(httplib::Client& client,const httplib::Headers& headers,std::string session,std::string model) {
    using Json=nlohmann::json;
    auto request=[&](const std::string& path,const Json* body=nullptr){
        auto response=body?client.Post(path,headers,body->dump(),"application/json"):client.Get(path,headers);
        if(!response)throw std::runtime_error("Cannot reach xMind Server during chat");
        if(response->status<200 || response->status>=300)throw std::runtime_error("Server rejected chat request (HTTP "+std::to_string(response->status)+")");
        return Json::parse(response->body);
    };
    const auto health=request("/v1/health");if(!health.is_object() || !health.contains("agent_execution") || !health["agent_execution"].is_boolean())throw std::runtime_error("Invalid backend chat capabilities");
    if(!model.empty()){
        const auto catalogue=request("/v1/models");
        if(!catalogue.is_object() || !catalogue.contains("models") || !catalogue["models"].is_array())throw std::runtime_error("Invalid backend model catalogue");
        bool available=false;
        for(const auto& item:catalogue["models"])if(item.is_object() && item.value("id",std::string{})==model)available=true;
        if(!available)throw std::invalid_argument("Initial model is not enabled by this backend");
    }
    // Validate a supplied session without starting work or creating a duplicate.
    Json history;
    if(!session.empty()){history=request("/v1/sessions/"+session+"/history");if(!history.is_array())throw std::runtime_error("Invalid session history");}
    std::cerr<<"xMind chat: enter a request, /help for commands, /exit to leave. Backend runs survive disconnect.\n";
    if(!session.empty()){
        std::cout<<Json{{"type","session"},{"session_id",session}}.dump()<<'\n'
                 <<Json{{"type","history"},{"session_id",session},{"history",history}}.dump()<<'\n'<<std::flush;
        if(!std::cout)throw std::runtime_error("Chat history output is unavailable");
    }
    int last_result=0;std::string prompt;
    while(std::cerr<<"xMind > "<<std::flush,std::getline(std::cin,prompt)) {
        if(!prompt.empty() && prompt.back()=='\r')prompt.pop_back();
        if(prompt=="/exit")return last_result;
        if(prompt.find_first_not_of(" \t\r\n")==std::string::npos)continue;
        if(prompt=="/help"){
            std::cerr<<"/runs lists recorded root runs in the selected conversation; use /watch or /graph-watch to attach one.\n";
            std::cerr<<"/graphs lists registered backend graphs; /graph GRAPH_ID REQUEST starts one at its displayed catalog revision.\n";
            std::cerr<<"/watch RUN_ID attaches an existing single-agent run; /graph-watch ROOT_ID attaches a graph with explicit input/approvals. Neither submits another run.\n";
            std::cerr<<"/models lists backend-enabled models; /model ID selects one for subsequent turns; /model resets to the server default.\n/provider-models discovers account models through the backend's saved key.\n/sessions lists saved conversations; /session ID resumes one; /new starts an empty conversation on your next request.\n/title NAME renames the selected conversation; /history displays its saved messages; /exit leaves. Prefix a literal slash request with another slash.\n";continue;
        }
        if(prompt.starts_with("/watch ")||prompt.starts_with("/graph-watch ")){
            const bool graphAttachment=prompt.starts_with("/graph-watch ");const auto id=prompt.substr(graphAttachment?13:7);
            if(id.empty()||id.size()>128||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos){std::cerr<<"Use /watch with a recorded run ID.\n";continue;}
            const auto response=client.Get("/v1/runs/"+id,headers);
            if(!response)throw std::runtime_error("Cannot reach xMind Server to attach the run");
            if(response->status==404){std::cerr<<"Run was not found.\n";continue;}
            if(response->status!=200)throw std::runtime_error("Server rejected run attachment");
            const auto run=Json::parse(response->body);
            if(!run.is_object()||run.value("id",std::string{})!=id||!run.contains("session_id")||!run["session_id"].is_string())throw std::runtime_error("Invalid attached run identity");
            if(run.value("graph_root",false)!=graphAttachment||!run.value("parent_id",std::string{}).empty()){std::cerr<<"Use /graph-watch for graph roots and /watch for single-agent runs.\n";continue;}
            const auto owner=run["session_id"].get<std::string>();
            if(owner.empty()||owner.size()>128||owner.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid attached conversation identity");
            if(!session.empty()&&session!=owner){std::cerr<<"Select the run's conversation with /session first, or use /new before attaching.\n";continue;}
            const auto saved=request("/v1/sessions/"+owner+"/history");if(!saved.is_array())throw std::runtime_error("Invalid attached conversation history");
            session=owner;
            std::cout<<Json{{"type","session"},{"session_id",session}}.dump()<<'\n'<<Json{{"type","history"},{"session_id",session},{"history",saved}}.dump()<<'\n'<<Json{{"type","run_attached"},{"run",run}}.dump()<<'\n'<<std::flush;
            last_result=watch_run(client,headers,id,0,graphAttachment,true);
            std::cout<<Json{{"type","turn_finished"},{"run_id",id},{"exit_status",last_result}}.dump()<<'\n'<<std::flush;
            continue;
        }
        if(prompt=="/runs"){
            const auto saved=session.empty()?Json::array():request("/v1/sessions/"+session+"/runs");
            if(!saved.is_array())throw std::runtime_error("Invalid backend run catalogue");
            std::cout<<Json{{"type","runs"},{"session_id",session},{"runs",saved}}.dump()<<'\n'<<std::flush;continue;
        }
        if(prompt=="/sessions"){
            const auto saved=request("/v1/sessions");if(!saved.is_array())throw std::runtime_error("Invalid backend session catalogue");
            std::cout<<Json{{"type","sessions"},{"sessions",saved},{"selected_session",session}}.dump()<<'\n'<<std::flush;continue;
        }
        if(prompt=="/new"){
            session.clear();std::cout<<Json{{"type","session"},{"session_id",session}}.dump()<<'\n'<<Json{{"type","history"},{"session_id",session},{"history",Json::array()}}.dump()<<'\n'<<std::flush;continue;
        }
        if(prompt.starts_with("/title ")){
            if(session.empty()){std::cerr<<"Select a saved conversation before renaming it.\n";continue;}
            const auto saved=request("/v1/sessions");if(!saved.is_array())throw std::runtime_error("Invalid backend session catalogue");
            const auto selected=std::find_if(saved.begin(),saved.end(),[&](const Json& item){return item.is_object()&&item.value("id",std::string{})==session;});
            if(selected==saved.end()||!selected->contains("title")||!(*selected)["title"].is_string())throw std::runtime_error("Selected conversation is unavailable");
            const Json body={{"title",prompt.substr(7)},{"expected_title",(*selected)["title"]}};
            const auto renamed=client.Post("/v1/sessions/"+session+"/title",headers,body.dump(),"application/json");
            if(!renamed)throw std::runtime_error("Cannot reach xMind Server during rename");
            if(renamed->status==409){std::cerr<<"Conversation title changed. Use /sessions and retry.\n";continue;}
            if(renamed->status==400){std::cerr<<"Use a nonempty, single-line conversation title within 4096 bytes.\n";continue;}
            if(renamed->status!=200)throw std::runtime_error("Server rejected conversation rename");
            std::cout<<Json{{"type","session_renamed"},{"session",Json::parse(renamed->body)}}.dump()<<'\n'<<std::flush;continue;
        }
        if(prompt.starts_with("/session ")){
            const auto selected=prompt.substr(9);
            if(selected.empty()||selected.size()>128||selected.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos){std::cerr<<"Invalid session ID. Use /sessions to see saved conversations.\n";continue;}
            const auto saved=request("/v1/sessions");if(!saved.is_array())throw std::runtime_error("Invalid backend session catalogue");bool found=false;
            for(const auto& item:saved)if(item.is_object()&&item.value("id",std::string{})==selected)found=true;
            if(!found){std::cerr<<"Session was not found. Use /sessions to see saved conversations.\n";continue;}
            const auto selectedHistory=request("/v1/sessions/"+selected+"/history");if(!selectedHistory.is_array())throw std::runtime_error("Invalid session history");
            session=selected;std::cout<<Json{{"type","session"},{"session_id",session}}.dump()<<'\n'<<Json{{"type","history"},{"session_id",session},{"history",selectedHistory}}.dump()<<'\n'<<std::flush;continue;
        }
        if(prompt=="/provider-models"){
            const auto setup=request("/v1/provider/configuration");
            if(!setup.is_object() || setup.value("configured",false)!=true || setup.value("provider",std::string{})!="openai" || !setup.contains("revision") || !setup["revision"].is_number_integer() || setup["revision"]<1 || setup["revision"]>9007199254740991){std::cerr<<"Configure a backend provider key before discovering account models.\n";continue;}
            const Json body={{"expected_revision",setup["revision"]}};
            const auto catalogue=request("/v1/provider/models",&body);
            if(!catalogue.is_object() || !catalogue.contains("models") || !catalogue["models"].is_array())throw std::runtime_error("Invalid provider model catalogue");
            std::cout<<Json{{"type","provider_models"},{"catalogue",catalogue},{"provider_revision",setup["revision"]}}.dump()<<'\n'<<std::flush;
            std::cerr<<"These are discovered account models. /models shows models currently enabled for execution. Discovery does not change shared provider settings.\n";
            continue;
        }
        if(prompt=="/history"){
            const auto saved=session.empty()?Json::array():request("/v1/sessions/"+session+"/history");
            if(!saved.is_array())throw std::runtime_error("Invalid session history");
            std::cout<<Json{{"type","history"},{"session_id",session},{"history",saved}}.dump()<<'\n'<<std::flush;continue;
        }
        if(prompt=="/models" || prompt=="/model" || prompt.starts_with("/model ")){
            const auto catalogue=request("/v1/models");
            if(!catalogue.is_object() || !catalogue.contains("models") || !catalogue["models"].is_array() || !catalogue.contains("default_model") || !catalogue["default_model"].is_string())throw std::runtime_error("Invalid backend model catalogue");
            if(prompt=="/models")std::cout<<Json{{"type","models"},{"catalogue",catalogue},{"selected_model",model}}.dump()<<'\n'<<std::flush;
            else {
                const auto selected=prompt=="/model"?std::string{}:prompt.substr(7);
                bool available=selected.empty();
                for(const auto& item:catalogue["models"])if(item.is_object() && item.value("id",std::string{})==selected)available=true;
                if(!available){std::cerr<<"Model is not enabled by this backend. Use /models to see available IDs.\n";continue;}
                model=selected;
                std::cout<<Json{{"type","model"},{"model_id",model},{"default_model",catalogue["default_model"]}}.dump()<<'\n'<<std::flush;
            }
            continue;
        }
        bool graphSubmission=false;std::string graphId;std::int64_t graphRevision=0;
        if(prompt=="/graphs"||prompt.starts_with("/graph ")){
            const auto catalogue=request("/v1/graphs");
            if(!catalogue.is_object()||!catalogue.contains("graphs")||!catalogue["graphs"].is_array())throw std::runtime_error("Invalid backend graph catalogue");
            if(prompt=="/graphs"){std::cout<<Json{{"type","graphs"},{"catalogue",catalogue}}.dump()<<'\n'<<std::flush;continue;}
            const auto split=prompt.find(' ',7);graphId=split==std::string::npos?std::string{}:prompt.substr(7,split-7);
            const auto task=split==std::string::npos?std::string{}:prompt.substr(split+1);
            if(graphId.empty()||graphId.size()>64||graphId.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos||task.find_first_not_of(" \t\r\n")==std::string::npos){std::cerr<<"Use /graph GRAPH_ID REQUEST with a registered graph and nonempty request.\n";continue;}
            const auto& entries=catalogue["graphs"];const auto selected=std::find_if(entries.begin(),entries.end(),[&](const Json& item){return item.is_object()&&item.value("id",std::string{})==graphId;});
            if(selected==entries.end()){std::cerr<<"Graph was not found. Use /graphs to see registered graphs.\n";continue;}
            if(!selected->contains("executable")||!(*selected)["executable"].is_boolean()||!selected->contains("revision")||!(*selected)["revision"].is_number_integer()||(*selected)["revision"]<1||(*selected)["revision"]>9007199254740991)throw std::runtime_error("Invalid registered graph capabilities");
            if(!(*selected)["executable"].get<bool>()){std::cerr<<"Graph cannot execute with the current backend configuration. Use /graphs to inspect availability.\n";continue;}
            graphRevision=(*selected)["revision"].get<std::int64_t>();graphSubmission=true;prompt=task;
        }
        if(!graphSubmission&&prompt.front()=='/'){
            if(prompt.starts_with("//"))prompt.erase(0,1);
            else {std::cerr<<"Unknown chat command. Use /help, or // to send a literal slash request.\n";continue;}
        }
        if(prompt.size()>1024*1024)throw std::invalid_argument("Prompt exceeds limits");
        if(!graphSubmission){const auto current=request("/v1/health");
        if(!current.is_object()||!current.contains("agent_execution")||!current["agent_execution"].is_boolean())throw std::runtime_error("Invalid backend chat capabilities");
        if(!current["agent_execution"].get<bool>()){std::cerr<<"Configure a backend model before submitting a request. Saved conversations and runs remain available for inspection.\n";continue;}}
        if(session.empty()){
            const auto first=prompt.find_first_not_of(" \t\r\n");auto end=std::min(prompt.size(),first+80);
            // Truncate only at a UTF-8 boundary, preserving the original prompt.
            while(end<prompt.size()&&end>first&&(static_cast<unsigned char>(prompt[end])&0xc0)==0x80)--end;
            auto title=prompt.substr(first,end-first);for(auto& byte:title)if(static_cast<unsigned char>(byte)<32)byte=' ';
            const Json body={{"title",title}};const auto created=request("/v1/sessions",&body);
            if(!created.is_object() || !created.contains("id") || !created["id"].is_string())throw std::runtime_error("Invalid created session");session=created["id"].get<std::string>();
            if(session.empty() || session.size()>128 || session.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid created session identity");
            std::cout<<Json{{"type","session"},{"session_id",session}}.dump()<<'\n'<<std::flush;
        }
        Json body={{"session_id",session},{"prompt",prompt}};if(!model.empty())body["model_id"]=model;
        if(graphSubmission){body["graph_id"]=graphId;body["graph_revision"]=graphRevision;}
        const auto admitted=client.Post(graphSubmission?"/v1/graph-runs":"/v1/runs",headers,body.dump(),"application/json");
        if(!admitted)throw std::runtime_error("Cannot reach xMind Server during run admission; admission outcome is unknown. Inspect /runs before resubmitting.");
        if(admitted->status==400||admitted->status==404||admitted->status==409||admitted->status==429||admitted->status==503){
            std::cout<<Json{{"type","run_rejected"},{"session_id",session},{"http_status",admitted->status},{"prompt",prompt},{"graph_id",graphSubmission?graphId:std::string{}}}.dump()<<'\n'<<std::flush;
            std::cerr<<"Backend rejected run admission (HTTP "<<admitted->status<<"). Request retained above; no automatic retry. Use /runs to inspect existing work.\n";
            last_result=1;continue;
        }
        if(admitted->status!=202)throw std::runtime_error("Unexpected run admission response; inspect /runs before resubmitting");
        const auto run=Json::parse(admitted->body);
        if(!run.is_object() || run.value("session_id",std::string{})!=session || run.value("graph_root",false)!=graphSubmission || !run.contains("id") || !run["id"].is_string())throw std::runtime_error("Invalid chat run admission");const auto id=run["id"].get<std::string>();
        if(id.empty() || id.size()>128 || id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid chat run identity");
        std::cout<<Json{{"type","run"},{"run",run}}.dump()<<'\n'<<std::flush;
        last_result=watch_run(client,headers,id,0,graphSubmission,true);
        std::cout<<Json{{"type","turn_finished"},{"run_id",id},{"exit_status",last_result}}.dump()<<'\n'<<std::flush;
    }
    if(!std::cin.eof())throw std::runtime_error("Chat input is unavailable");
    return last_result;
}
}

int main(int argc,char** argv) {
    try {
        if(argc<3) throw std::invalid_argument("Usage: xmind_cli PORT COMMAND [ARGS] (commands: health, chat [SESSION [MODEL]], sessions, create-session, rename-session SESSION TITLE EXPECTED_TITLE, history, runs, run, cancel, status, events, watch, models, provider, provider-models [KEY_ENV REVISION], configure-provider MODEL KEY_ENV REVISION, graphs, graph-run SESSION GRAPH REV PROMPT [MODEL], graph ROOT, graph-input ROOT NODE REV JSON_FILE, graph-events ROOT [AFTER], graph-watch ROOT [AFTER], graph-children ROOT, instructions, mcp-servers, process-profiles, operations, operation, inspect-edit, decide, append-message)");
        const std::string port_text=argv[1],command=argv[2];int port=0;
        const auto parsed=std::from_chars(port_text.data(),port_text.data()+port_text.size(),port);
        if(parsed.ec!=std::errc{} || parsed.ptr!=port_text.data()+port_text.size() || port<1 || port>65535) throw std::invalid_argument("Invalid port");
        auto id=[](const std::string& value) {
            if(value.empty() || value.size()>128) throw std::invalid_argument("Invalid ID");
            for(unsigned char c:value) if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-')) throw std::invalid_argument("Invalid ID");
            return value;
        };
        using Json=nlohmann::json;std::string path,chat_model;Json body;bool post=false,watch=false,graph_watch=false,chat=false,saved_provider_key=false;std::int64_t watch_cursor=0;
        if(command=="health" && argc==3) path="/v1/health";
        else if(command=="chat" && argc>=3 && argc<=5){chat=true;if(argc>=4)path=id(argv[3]);if(argc==5)chat_model=argv[4];}
        else if(command=="sessions" && argc==3) path="/v1/sessions";
        else if(command=="create-session" && argc==4) {path="/v1/sessions";body={{"title",argv[3]}};post=true;}
        else if(command=="rename-session" && argc==6) {path="/v1/sessions/"+id(argv[3])+"/title";body={{"title",argv[4]},{"expected_title",argv[5]}};post=true;}
        else if(command=="history" && argc==4) path="/v1/sessions/"+id(argv[3])+"/history";
        else if(command=="runs" && argc==4) path="/v1/sessions/"+id(argv[3])+"/runs";
        else if(command=="status" && argc==4) path="/v1/runs/"+id(argv[3]);
        else if(command=="operations" && argc==4) path="/v1/runs/"+id(argv[3])+"/operations";
        else if(command=="operation" && argc==4) path="/v1/operations/"+id(argv[3]);
        else if(command=="inspect-edit" && argc==4) path="/v1/operations/"+id(argv[3])+"/inspection";
        else if(command=="decide" && argc==5) {
            const std::string decision=argv[4];
            if(decision!="allow" && decision!="deny") throw std::invalid_argument("Decision must be allow or deny");
            path="/v1/operations/"+id(argv[3])+"/decision";body={{"decision",decision}};post=true;
        }
        else if(command=="models" && argc==3) path="/v1/models";
        else if(command=="provider" && argc==3) path="/v1/provider/configuration";
        else if(command=="provider-models" && (argc==3 || argc==5)){
            path="/v1/provider/models";post=true;
            if(argc==3)saved_provider_key=true;
            else body=provider_key_fields(argv[3],argv[4]);
        }
        else if(command=="configure-provider" && argc==6){
            path="/v1/provider/configuration";body=provider_key_fields(argv[4],argv[5]);body["model"]=argv[3];post=true;
        }
        else if(command=="mcp-servers" && argc==3) path="/v1/mcp/servers";
        else if(command=="process-profiles" && argc==3) path="/v1/process/profiles";
        else if(command=="instructions" && argc==3) path="/v1/agent/instructions";
        else if(command=="graphs" && argc==3)path="/v1/graphs";
        else if(command=="graph" && argc==4)path="/v1/graph-runs/"+id(argv[3]);
        else if(command=="graph-children" && argc==4)path="/v1/graph-runs/"+id(argv[3])+"/children";
        else if(command=="graph-child-history" && argc==5)path="/v1/graph-runs/"+id(argv[3])+"/children/"+id(argv[4])+"/history";
        else if(command=="graph-events" && (argc==4 || argc==5)){const auto after=event_cursor(argc==5?argv[4]:"0");path="/v1/graph-runs/"+id(argv[3])+"/events?after="+std::to_string(after);}
        else if(command=="graph-run" && (argc==7 || argc==8)){
            const auto revision=event_cursor(argv[5]);if(revision<1 || revision>9007199254740991)throw std::invalid_argument("Invalid graph revision");
            path="/v1/graph-runs";body={{"session_id",id(argv[3])},{"graph_id",argv[4]},{"graph_revision",revision},{"prompt",argv[6]}};if(argc==8)body["model_id"]=argv[7];post=true;
        }
        else if(command=="graph-input" && argc==7){
            const std::string node=argv[4];if(node.empty() || node.size()>64 || node.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)throw std::invalid_argument("Invalid graph node identity");
            const auto revision=event_cursor(argv[5]);if(revision<1 || revision>9007199254740991)throw std::invalid_argument("Invalid graph checkpoint revision");
            std::ifstream file(std::filesystem::u8path(argv[6]),std::ios::binary);if(!file)throw std::invalid_argument("Cannot read graph input file");std::string source;char byte;
            while(file.get(byte)){if(source.size()>=65536)throw std::invalid_argument("Graph input file exceeds limits");source.push_back(byte);}if(!file.eof())throw std::invalid_argument("Cannot read graph input file");
            std::vector<std::set<std::string>> objects;
            auto input=Json::parse(source,[&](int depth,Json::parse_event_t event,Json& value){
                if(depth>16)throw std::invalid_argument("Graph input JSON nesting exceeds limits");
                if(event==Json::parse_event_t::object_start)objects.emplace_back();
                else if(event==Json::parse_event_t::object_end)objects.pop_back();
                else if(event==Json::parse_event_t::key && !objects.back().insert(value.get<std::string>()).second)throw std::invalid_argument("Graph input JSON contains duplicate keys");
                return true;
            });if(!input.is_object())throw std::invalid_argument("Graph input file must contain a JSON object");
            path="/v1/graph-runs/"+id(argv[3])+"/human/"+node;body={{"input",std::move(input)},{"expected_checkpoint_revision",revision}};post=true;
        }
        else if(command=="run" && (argc==5 || argc==6)) {path="/v1/runs";body={{"session_id",id(argv[3])},{"prompt",argv[4]}};if(argc==6)body["model_id"]=argv[5];post=true;}
        else if(command=="cancel" && argc==4) {path="/v1/runs/"+id(argv[3])+"/cancel";body=Json::object();post=true;}
        else if(command=="watch" && (argc==4 || argc==5)) {path=id(argv[3]);watch=true;watch_cursor=event_cursor(argc==5?argv[4]:"0");}
        else if(command=="graph-watch" && (argc==4 || argc==5)) {path=id(argv[3]);watch=true;graph_watch=true;watch_cursor=event_cursor(argc==5?argv[4]:"0");}
        else if(command=="events" && (argc==4 || argc==5)) {
            const std::string cursor=argc==5?argv[4]:"0";event_cursor(cursor);
            path="/v1/runs/"+id(argv[3])+"/events?after="+cursor;
        }
        else if(command=="append-message" && argc==5) {path="/v1/sessions/"+id(argv[3])+"/messages";body={{"role","user"},{"data",{{"content",argv[4]}}}};post=true;}
        else throw std::invalid_argument("Unknown command or incorrect arguments");
        const auto* token=std::getenv("XMIND_AUTH_TOKEN");
        if(!token) throw std::invalid_argument("Set XMIND_AUTH_TOKEN for the local client");
        httplib::Client client("127.0.0.1",port);
        client.set_connection_timeout(5,0);client.set_read_timeout(15,0);client.set_write_timeout(5,0);client.set_follow_location(false);
        const httplib::Headers headers{{"Authorization",std::string("Bearer ")+token}};
        if(chat)return chat_session(client,headers,path,chat_model);
        if(saved_provider_key){
            const auto metadata=client.Get("/v1/provider/configuration",headers);
            if(!metadata)throw std::runtime_error("Cannot reach xMind Server for provider discovery");
            if(metadata->status<200 || metadata->status>=300)throw std::runtime_error("Server rejected provider metadata (HTTP "+std::to_string(metadata->status)+")");
            const auto setup=Json::parse(metadata->body);
            if(!setup.is_object() || !setup.contains("configured") || setup["configured"]!=true || setup.value("provider",std::string{})!="openai" || !setup.contains("revision") || !setup["revision"].is_number_integer() || setup["revision"]<1 || setup["revision"]>9007199254740991)throw std::runtime_error("Configure a provider key before discovering saved-account models");
            body={{"expected_revision",setup["revision"]}};
        }
        if(watch)return watch_run(client,headers,path,watch_cursor,graph_watch);
        auto response=post?client.Post(path,headers,body.dump(),"application/json"):client.Get(path,headers);
        if(!response) throw std::runtime_error("Cannot reach xMind Server");
        const auto result=Json::parse(response->body);
        if(response->status<200 || response->status>=300) {std::cerr<<result.dump()<<'\n';return 1;}
        std::cout<<result.dump(2)<<'\n';return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
