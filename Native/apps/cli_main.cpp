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
#include <string_view>

namespace {
std::string provider_profile_identity(const std::string& value){
    if(value.empty()||value.size()>256||value.starts_with("sk-")||value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos)
        throw std::invalid_argument("Invalid provider profile identity");
    return value;
}
 nlohmann::json provider_admission_binding(const nlohmann::json& metadata){
    using Json=nlohmann::json;
    if(!metadata.is_object()||!metadata.contains("revision")||!metadata["revision"].is_number_integer()||metadata["revision"]<0||metadata["revision"]>9007199254740991||!metadata.contains("active")||!metadata["active"].is_string()||!metadata.contains("profiles")||!metadata["profiles"].is_array())throw std::runtime_error("Invalid backend provider admission metadata");
    const auto id=metadata["active"].get<std::string>();if(id.size()>256||id.starts_with("sk-")||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos)throw std::runtime_error("Invalid backend provider profile identity");
    bool exists=id.empty();for(const auto& profile:metadata["profiles"])if(profile.is_object()&&profile.value("id",std::string{})==id)exists=true;if(!exists)throw std::runtime_error("Active backend provider profile is missing");
    return Json{{"provider_profile_id",id},{"expected_provider_revision",metadata["revision"]}};
 }
std::int64_t event_cursor(const std::string& source) {
    std::int64_t value=0;const auto parsed=std::from_chars(source.data(),source.data()+source.size(),value);
    if(parsed.ec!=std::errc{} || parsed.ptr!=source.data()+source.size() || value<0)throw std::invalid_argument("Invalid cursor");
    return value;
}
std::int64_t provider_revision(const std::string& source){
    const auto value=event_cursor(source);if(value>9007199254740991)throw std::invalid_argument("Invalid provider revision");return value;
}
std::int64_t plan_revision(const std::string& source){const auto value=provider_revision(source);if(value<1)throw std::invalid_argument("Plan revision and state sequence must be positive");return value;}
void validate_plan_input(const std::string& source){
    if(source.empty()||source.size()>16384)throw std::invalid_argument("Plan input must contain at most 16384 bytes");
    using Json=nlohmann::json;std::vector<std::set<std::string>> objects;
    const auto parsed=Json::parse(source,[&](int depth,Json::parse_event_t event,Json& value){
        if(depth>64)throw std::invalid_argument("Plan input nesting exceeds limits");
        if(event==Json::parse_event_t::object_start)objects.emplace_back();
        else if(event==Json::parse_event_t::object_end)objects.pop_back();
        else if(event==Json::parse_event_t::key&&!objects.back().insert(value.get<std::string>()).second)throw std::invalid_argument("Duplicate plan input field");return true;
    });if(!parsed.is_object())throw std::invalid_argument("Plan input must be a JSON object");
}
void observed_dynamic_child(const nlohmann::json& record){
    const auto kind=record.value("kind",std::string{});
    if(kind=="delegated_leaf"){if(record.value("batch_id",std::string{}).empty()||record.value("task_id",std::string{}).empty()||record.value("preset_id",std::string{})!="workspace.inspect")throw std::runtime_error("Invalid delegated child metadata");}
    else if(kind=="dynamic_agent"){
        for(const auto* key:{"plan_id","claim_id"}){const auto value=record.value(key,std::string{});if(value.empty()||value.size()>128||value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid dynamic child identity");}
        const auto label=record.value("node_label",std::string{}),preset=record.value("preset_id",std::string{});
        if(label.empty()||label.size()>32||label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos||(preset!="workspace.inspect"&&preset!="workspace.coding")||!record.contains("definition_revision")||!record["definition_revision"].is_number_integer()||record["definition_revision"]<1||record["definition_revision"]>9007199254740991||!record.contains("claim_revision")||record["claim_revision"]!=record["definition_revision"]||record.value("batch_id",std::string{})!=""||record.value("task_id",std::string{})!=""||record.at("run").value("node_id",std::string{}).empty())throw std::runtime_error("Invalid dynamic child claim");
    }else throw std::runtime_error("Invalid owned child kind");
    if(!record.contains("preset_revision")||!record["preset_revision"].is_number_integer()||record["preset_revision"]<1||record["preset_revision"]>9007199254740991)throw std::runtime_error("Invalid child preset revision");
}
nlohmann::json active_profile_discovery(const nlohmann::json& metadata){
    using Json=nlohmann::json;const auto binding=provider_admission_binding(metadata);const auto id=binding.at("provider_profile_id").get<std::string>();
    if(id.empty())throw std::runtime_error("Select a saved provider profile before discovering account models");
    const auto& profiles=metadata.at("profiles");const auto active=std::find_if(profiles.begin(),profiles.end(),[&](const Json& profile){return profile.is_object()&&profile.value("id",std::string{})==id;});
    if(active==profiles.end()||!active->contains("route_id")||!(*active)["route_id"].is_string())throw std::runtime_error("Invalid backend provider discovery metadata");
    return Json{{"id",provider_profile_identity(id)},{"route_id",provider_profile_identity((*active)["route_id"].get<std::string>())},{"expected_revision",binding.at("expected_provider_revision")}};
}
nlohmann::json provider_key_fields(const std::string& variable,const std::string& revision_text) {
    auto normalized=variable;for(auto& character:normalized)if(character>='a' && character<='z')character=static_cast<char>(character-'a'+'A');
    if(variable.empty() || variable.size()>128 || variable.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos || (variable.front()>='0' && variable.front()<='9') || normalized=="XMIND_AUTH_TOKEN" || normalized.starts_with("XMIND_UI_"))throw std::invalid_argument("Select a provider key environment variable");
    const auto revision=provider_revision(revision_text);
    const auto* secret=std::getenv(variable.c_str());if(!secret || !*secret)throw std::invalid_argument("Provider key environment variable is empty");const auto length=std::strlen(secret);if(length>32768)throw std::invalid_argument("Provider key exceeds limits");
    nlohmann::json fields={{"api_key",std::string(secret,length)},{"expected_revision",revision}};
#if defined(_WIN32)
    _putenv_s(variable.c_str(),""); // Only this client process; preserve parent/user settings.
#endif
    return fields;
}
// Native catalogue discovery has a bounded 30-second provider deadline. Keep
// its client wait large enough, without extending later chat/watch requests.
struct ProviderDiscoveryTimeout {
    httplib::Client& client;
    explicit ProviderDiscoveryTimeout(httplib::Client& value):client(value){client.set_read_timeout(35,0);}
    ~ProviderDiscoveryTimeout(){client.set_read_timeout(15,0);}
    ProviderDiscoveryTimeout(const ProviderDiscoveryTimeout&)=delete;
    ProviderDiscoveryTimeout& operator=(const ProviderDiscoveryTimeout&)=delete;
};
auto provider_discovery_request(httplib::Client& client,const httplib::Headers& headers,const std::string& path,const nlohmann::json& body){
    ProviderDiscoveryTimeout timeout(client);return client.Post(path,headers,body.dump(),"application/json");
}
nlohmann::json provider_rejection(const std::string& source){
    using Json=nlohmann::json;
    const Json unavailable={{"detail","Backend returned invalid provider diagnostics"}};
    if(source.empty()||source.size()>16384)return unavailable;
    try{
        std::vector<std::set<std::string>> fields;
        const auto value=Json::parse(source,[&](int depth,Json::parse_event_t event,Json& parsed){
            if(depth>8)throw std::invalid_argument("Invalid provider diagnostic depth");
            if(event==Json::parse_event_t::object_start)fields.emplace_back();
            else if(event==Json::parse_event_t::object_end)fields.pop_back();
            else if(event==Json::parse_event_t::key&&!fields.back().insert(parsed.get<std::string>()).second)throw std::invalid_argument("Duplicate provider diagnostic");
            return true;
        });
        if(!value.is_object())return unavailable;Json result=Json::object();
        // The native server already sanitizes provider bodies and credentials.
        // Retain only its public diagnostic fields, never request/raw-body data.
        for(const auto* name:{"detail","provider_error_type","provider_error_code","provider_error_param"})if(value.contains(name)){
            if(!value[name].is_string()||value[name].get_ref<const std::string&>().size()>(std::string_view(name)=="detail"?4096:256))return unavailable;
            result[name]=value[name];
        }
        if(value.contains("provider_status")){
            if(!value["provider_status"].is_number_integer()||value["provider_status"]<100||value["provider_status"]>599)return unavailable;
            result["provider_status"]=value["provider_status"];
        }
        return result.empty()?unavailable:result;
    }catch(...){return unavailable;}
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
    std::string session;bool tree=false,plan_observation=false;
    const auto initial=read(path);if(!initial.is_object()||initial.value("id",std::string{})!=run||!initial.contains("session_id")||!initial["session_id"].is_string()||initial["session_id"].get<std::string>().empty())throw std::runtime_error("Invalid observed run identity");session=initial["session_id"].get<std::string>();
    if(graph&&initial.value("graph_root",false)!=true)throw std::runtime_error("Select a graph root for graph observation");
    if(!graph&&initial.value("parent_id",std::string{}).empty()&&!initial.value("graph_root",false)){const auto health=read("/v1/health");tree=health.value("owned_child_observation",false);plan_observation=health.contains("agent_planning")&&health["agent_planning"].is_boolean();}
    auto owners=[&] {
        std::set<std::string> owned{run};
        if(graph){
            const auto children=read("/v1/graph-runs/"+run+"/children");
            if(!children.is_array())throw std::runtime_error("Invalid graph child batch");
            for(const auto& child:children){const auto id=child.value("id",std::string{});if(!child.is_object() || child.value("parent_id",std::string{})!=run || child.value("session_id",std::string{})!=session || id.empty() || id.size()>128 || id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos || !owned.insert(id).second)throw std::runtime_error("Invalid graph child ownership");}
        }
        if(tree){const auto children=read(path+"/children");if(!children.is_array()||children.size()>8)throw std::runtime_error("Invalid owned child batch");for(const auto& record:children){if(!record.is_object()||!record.contains("run")||!record["run"].is_object())throw std::runtime_error("Invalid owned child metadata");observed_dynamic_child(record);const auto& child=record["run"];const auto id=child.value("id",std::string{});if(child.value("parent_id",std::string{})!=run||child.value("session_id",std::string{})!=session||child.value("graph_root",false)||id.empty()||id.size()>128||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos||!owned.insert(id).second)throw std::runtime_error("Invalid owned child identity");}}
        return owned;
    };
    auto emit=[&] {
        for(;;){const auto events=read((graph?"/v1/graph-runs/"+run:path)+(tree?"/tree-events?after=":"/events?after=")+std::to_string(cursor));
        if(!events.is_array())throw std::runtime_error("Invalid backend event batch");
        if(tree&&events.size()>256)throw std::runtime_error("Invalid tree event page");
        // Ownership is read after events so newly admitted children are covered.
        const auto owned=owners();
        for(const auto& event:events) {
            if(!event.is_object() || !event.contains("seq") || !event["seq"].is_number_integer() || event["seq"]<=cursor || event["seq"]>std::numeric_limits<std::int64_t>::max() || !event.contains("run_id") || !event["run_id"].is_string() || !owned.contains(event["run_id"].get<std::string>()) || !event.contains("kind") || !event["kind"].is_string() || !event.contains("data"))throw std::runtime_error("Invalid backend event identity or cursor");
            std::cout<<event.dump()<<'\n'<<std::flush;
            if(!std::cout)throw std::runtime_error("Run observation output is unavailable");
            cursor=event["seq"].get<std::int64_t>();
        }
        if(!tree||events.size()<256)break;}
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
            if(plan_observation){
                const auto response=client.Get(path+"/plan",headers);if(!response)throw std::runtime_error("Cannot inspect the current native plan");
                if(response->status==409){std::this_thread::sleep_for(std::chrono::milliseconds(250));continue;}
                if(response->status!=200)throw std::runtime_error("Server rejected plan inspection");
                const auto snapshot=Json::parse(response->body);
                if(!snapshot.is_object()||snapshot.at("run").value("id",std::string{})!=run||snapshot.at("run").value("session_id",std::string{})!=session||snapshot.at("run").value("parent_id",std::string{})!=""||snapshot.at("run").value("graph_root",false)||!snapshot.at("questions").is_array())throw std::runtime_error("Plan observation ownership changed");
                if(!snapshot.at("plan").is_null()){
                    const auto& plan=snapshot.at("plan");const auto revision=plan_revision(plan.at("revision").dump()),sequence=plan_revision(plan.at("state_sequence").dump());
                    if(plan.value("root_run_id",std::string{})!=run||!plan.at("ready").is_array()||!plan.at("claimed").is_array()||!plan.at("waiting_human").is_array()||!plan.at("report_ready").is_boolean()||!plan.at("halted").is_boolean()||snapshot["questions"].size()>8)throw std::runtime_error("Invalid current plan readiness");
                    auto questions=Json::array();std::set<std::string> waiting;
                    for(const auto& question:snapshot["questions"]){if(question.value("state",std::string{})!="waiting")continue;const auto id=question.value("id",std::string{});
                        if(id.empty()||id.size()>128||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos||question.value("root_run_id",std::string{})!=run||question.value("plan_id",std::string{})!=plan.value("id",std::string{})||!question.contains("question")||!question["question"].is_string()||!waiting.insert(id).second)throw std::runtime_error("Invalid plan human ownership");questions.push_back(question);
                    }
                    const bool resume=snapshot["run"].value("state",std::string{})=="paused"&&waiting.empty()&&plan["claimed"].empty()&&plan["waiting_human"].empty()&&!plan["halted"].get<bool>()&&(!plan["ready"].empty()||plan["report_ready"].get<bool>());
                    if(!questions.empty()||resume){
                        std::cout<<Json{{"type","plan_human_review"},{"snapshot",snapshot}}.dump()<<'\n'<<std::flush;if(!std::cout)throw std::runtime_error("Plan review output is unavailable");
                        std::cerr<<"Enter /input REQUEST_ID JSON for a displayed question"<<(resume?", /resume":"")<<", /cancel or /exit to detach.\n";
                        std::string answer;if(!std::getline(std::cin,answer))throw std::runtime_error("CLI detached before plan input; backend execution remains owned by the server");if(!answer.empty()&&answer.back()=='\r')answer.pop_back();if(answer=="/exit")throw std::runtime_error("CLI detached before plan input; backend execution remains owned by the server");
                        std::string route;Json input;
                        if(answer=="/cancel"){route=path+"/cancel";input=Json::object();}
                        else if(answer=="/resume"&&resume){route=path+"/plan/resume";input={{"expected_revision",revision},{"expected_state_sequence",sequence}};}
                        else if(answer.starts_with("/input ")){const auto split=answer.find(' ',7);const auto request=split==std::string::npos?std::string{}:answer.substr(7,split-7),raw=split==std::string::npos?std::string{}:answer.substr(split+1);if(!waiting.contains(request)){std::cerr<<"No input sent. Use an exact displayed question ID.\n";continue;}try{validate_plan_input(raw);}catch(...){std::cerr<<"No input sent. Use a bounded strict JSON object.\n";continue;}route=path+"/plan/human/"+request;input={{"input_json",raw},{"expected_revision",revision},{"expected_state_sequence",sequence}};}
                        else {std::cerr<<"No input sent. Use a displayed question, /resume when offered, /cancel or /exit.\n";continue;}
                        const auto result=client.Post(route,headers,input.dump(),"application/json");if(!result)throw std::runtime_error("Cannot reach xMind Server for plan input");if(result->status<200||result->status>=300)std::cerr<<"Backend rejected plan input (HTTP "<<result->status<<"). Refreshing the recorded head; no automatic retry.\n";else std::cout<<Json{{"type",answer=="/cancel"?"run_cancel_result":answer=="/resume"?"plan_resume_result":"plan_input_result"},{"result",Json::parse(result->body)}}.dump()<<'\n'<<std::flush;
                        continue;
                    }
                }
            }
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
    const bool profile_admission=health.value("provider_profile_admission",false)||health.value("graph_provider_profile_admission",false);
    auto provider_binding=profile_admission?provider_admission_binding(request("/v1/provider/profiles")):Json::object();
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
    int last_result=0,profile_result=0;std::string prompt;
    const auto exit_status=[&]{return last_result!=0?last_result:profile_result;};
    while(std::cerr<<"xMind > "<<std::flush,std::getline(std::cin,prompt)) {
        if(!prompt.empty() && prompt.back()=='\r')prompt.pop_back();
        if(prompt=="/exit")return exit_status();
        if(prompt.find_first_not_of(" \t\r\n")==std::string::npos)continue;
        if(prompt=="/help"){
            std::cerr<<"/runs lists recorded root runs in the selected conversation; use /watch or /graph-watch to attach one.\n";
            std::cerr<<"Agent /watch includes actual owned inspection or coding children and native plan questions when supported. Direct commands: children RUN, child-history PARENT CHILD, tree-events RUN [CURSOR], delegation, planning, inspect-plan RUN.\n";
            std::cerr<<"/graphs lists registered backend graphs; /graph GRAPH_ID REQUEST starts one at its displayed catalog revision.\n";
            std::cerr<<"/watch RUN_ID attaches an existing single-agent run; /graph-watch ROOT_ID attaches a graph with explicit input/approvals. Neither submits another run.\n";
            std::cerr<<"/profiles lists saved provider metadata without changing this chat's admission binding; /profile ID REVISION explicitly selects a shared profile and clears this chat's model override.\n";
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
            last_result=watch_run(client,headers,id,0,graphAttachment,true);profile_result=0;
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
        if(prompt=="/profiles"){
            if(!profile_admission){std::cerr<<"This backend does not support provider profile admission.\n";profile_result=1;continue;}
            const auto metadata=request("/v1/provider/profiles");(void)provider_admission_binding(metadata);
            std::cout<<Json{{"type","provider_profiles"},{"metadata",metadata}}.dump()<<'\n'<<std::flush;profile_result=0;continue;
        }
        if(prompt=="/profile"||prompt.starts_with("/profile ")){
            std::string selected;std::int64_t revision=0;
            try{
                const auto fields=prompt.size()>9?prompt.substr(9):std::string{};const auto split=fields.find(' ');
                if(split==std::string::npos)throw std::invalid_argument("Use /profile ID REVISION");
                selected=provider_profile_identity(fields.substr(0,split));revision=provider_revision(fields.substr(split+1));
            }catch(const std::invalid_argument&){std::cerr<<"Use /profile ID REVISION with a valid saved profile ID and safe revision. No selection sent.\n";profile_result=1;continue;}
            if(!profile_admission){std::cerr<<"This backend does not support provider profile admission. No selection sent.\n";profile_result=1;continue;}
            const Json body={{"id",selected},{"expected_revision",revision}};
            const auto response=client.Post("/v1/provider/profiles/select",headers,body.dump(),"application/json");
            if(!response)throw std::runtime_error("Cannot reach xMind Server for profile selection; outcome is unknown. Inspect /profiles before selecting again.");
            if(response->status<200||response->status>=300){
                std::cout<<Json{{"type","provider_profile_rejected"},{"http_status",response->status},{"error",provider_rejection(response->body)}}.dump()<<'\n'<<std::flush;
                std::cerr<<"Backend rejected profile selection (HTTP "<<response->status<<"). Chat binding and model are unchanged; no automatic retry. Use /profiles to inspect the shared revision.\n";profile_result=1;continue;
            }
            const auto metadata=Json::parse(response->body);const auto committed=provider_admission_binding(metadata);
            if(committed.at("provider_profile_id")!=selected||committed.at("expected_provider_revision")!=revision+1)throw std::runtime_error("Invalid committed profile selection; inspect /profiles before continuing");
            provider_binding=committed;model.clear();profile_result=0;
            std::cout<<Json{{"type","provider_profile"},{"metadata",metadata},{"provider_profile_id",committed.at("provider_profile_id")},{"expected_provider_revision",committed.at("expected_provider_revision")},{"selected_model",model}}.dump()<<'\n'<<std::flush;continue;
        }
        if(prompt=="/provider-models"){
            Json body;std::string route;
            if(profile_admission){
                const auto metadata=request("/v1/provider/profiles");
                if(metadata.value("active",std::string{}).empty()){std::cerr<<"Select a saved provider profile before discovering account models.\n";profile_result=1;continue;}
                body=active_profile_discovery(metadata);route="/v1/provider/profiles/models";
            }else{
                const auto setup=request("/v1/provider/configuration");
                if(!setup.is_object() || setup.value("configured",false)!=true || setup.value("provider",std::string{})!="openai" || !setup.contains("revision") || !setup["revision"].is_number_integer() || setup["revision"]<1 || setup["revision"]>9007199254740991){std::cerr<<"Configure a backend provider key before discovering account models.\n";continue;}
                body={{"expected_revision",setup["revision"]}};route="/v1/provider/models";
            }
            const auto response=provider_discovery_request(client,headers,route,body);
            if(!response)throw std::runtime_error("Cannot reach xMind Server for provider discovery");
            if(response->status<200||response->status>=300){
                std::cout<<Json{{"type","provider_models_rejected"},{"http_status",response->status},{"error",provider_rejection(response->body)}}.dump()<<'\n'<<std::flush;
                std::cerr<<"Backend rejected provider discovery (HTTP "<<response->status<<"). Chat binding and model are unchanged; no automatic retry.\n";profile_result=1;continue;
            }
            const auto catalogue=Json::parse(response->body);
            if(!catalogue.is_object() || !catalogue.contains("models") || !catalogue["models"].is_array())throw std::runtime_error("Invalid provider model catalogue");
            std::cout<<Json{{"type","provider_models"},{"catalogue",catalogue},{"provider_revision",body.at("expected_revision")}}.dump()<<'\n'<<std::flush;profile_result=0;
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
        if(health.value(graphSubmission?"graph_provider_profile_admission":"provider_profile_admission",false))body.update(provider_binding);
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
        last_result=watch_run(client,headers,id,0,graphSubmission,true);profile_result=0;
        std::cout<<Json{{"type","turn_finished"},{"run_id",id},{"exit_status",last_result}}.dump()<<'\n'<<std::flush;
    }
    if(!std::cin.eof())throw std::runtime_error("Chat input is unavailable");
    return exit_status();
}
}

int main(int argc,char** argv) {
    try {
        if(argc<3) throw std::invalid_argument("Usage: xmind_cli PORT COMMAND [ARGS] (commands: health, chat [SESSION [MODEL]], sessions, create-session, rename-session SESSION TITLE EXPECTED_TITLE, history, runs, run, cancel, status, events, watch, models, provider-profiles, profile-models ID ROUTE REVISION [KEY_ENV], save-profile ID ROUTE MODEL REVISION [KEY_ENV] [--activate], select-profile ID REVISION, provider, provider-models [KEY_ENV REVISION], configure-provider MODEL KEY_ENV REVISION, graphs, graph-run SESSION GRAPH REV PROMPT [MODEL], graph ROOT, graph-input ROOT NODE REV JSON_FILE, graph-events ROOT [AFTER], graph-watch ROOT [AFTER], graph-children ROOT, planning, inspect-plan ROOT, plan-input ROOT REQUEST REV SEQUENCE JSON_FILE, resume-plan ROOT REV SEQUENCE, instructions, mcp-servers, process-profiles, operations, operation, inspect-edit, decide, append-message)");
        const std::string port_text=argv[1],command=argv[2];int port=0;
        const auto parsed=std::from_chars(port_text.data(),port_text.data()+port_text.size(),port);
        if(parsed.ec!=std::errc{} || parsed.ptr!=port_text.data()+port_text.size() || port<1 || port>65535) throw std::invalid_argument("Invalid port");
        auto id=[](const std::string& value) {
            if(value.empty() || value.size()>128) throw std::invalid_argument("Invalid ID");
            for(unsigned char c:value) if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-')) throw std::invalid_argument("Invalid ID");
            return value;
        };
        using Json=nlohmann::json;std::string path,chat_model;Json body;bool post=false,watch=false,graph_watch=false,chat=false,saved_provider_key=false,profile_operation=false;std::int64_t watch_cursor=0;
        if(command=="health" && argc==3) path="/v1/health";
        else if(command=="chat" && argc>=3 && argc<=5){chat=true;if(argc>=4)path=id(argv[3]);if(argc==5)chat_model=argv[4];}
        else if(command=="sessions" && argc==3) path="/v1/sessions";
        else if(command=="create-session" && argc==4) {path="/v1/sessions";body={{"title",argv[3]}};post=true;}
        else if(command=="rename-session" && argc==6) {path="/v1/sessions/"+id(argv[3])+"/title";body={{"title",argv[4]},{"expected_title",argv[5]}};post=true;}
        else if(command=="history" && argc==4) path="/v1/sessions/"+id(argv[3])+"/history";
        else if(command=="runs" && argc==4) path="/v1/sessions/"+id(argv[3])+"/runs";
        else if(command=="status" && argc==4) path="/v1/runs/"+id(argv[3]);
        else if(command=="operations" && argc==4) path="/v1/runs/"+id(argv[3])+"/operations";
        else if(command=="children"&&argc==4)path="/v1/runs/"+id(argv[3])+"/children";
        else if(command=="child-history"&&argc==5)path="/v1/runs/"+id(argv[3])+"/children/"+id(argv[4])+"/history";
        else if(command=="tree-events"&&(argc==4||argc==5)){const auto after=event_cursor(argc==5?argv[4]:"0");path="/v1/runs/"+id(argv[3])+"/tree-events?after="+std::to_string(after);}
        else if(command=="operation" && argc==4) path="/v1/operations/"+id(argv[3]);
        else if(command=="inspect-edit" && argc==4) path="/v1/operations/"+id(argv[3])+"/inspection";
        else if(command=="decide" && argc==5) {
            const std::string decision=argv[4];
            if(decision!="allow" && decision!="deny") throw std::invalid_argument("Decision must be allow or deny");
            path="/v1/operations/"+id(argv[3])+"/decision";body={{"decision",decision}};post=true;
        }
        else if(command=="models" && argc==3) path="/v1/models";
        else if(command=="provider-profiles" && argc==3){path="/v1/provider/profiles";profile_operation=true;}
        else if(command=="profile-models" && (argc==6||argc==7)){
            const auto profile=provider_profile_identity(argv[3]),route=provider_profile_identity(argv[4]);const auto revision=provider_revision(argv[5]);
            body=argc==7?provider_key_fields(argv[6],argv[5]):Json{{"expected_revision",revision}};
            body["id"]=profile;body["route_id"]=route;path="/v1/provider/profiles/models";post=true;profile_operation=true;
        }
        else if(command=="save-profile" && argc>=7&&argc<=9){
            const auto profile=provider_profile_identity(argv[3]),route=provider_profile_identity(argv[4]),model=provider_profile_identity(argv[5]);const auto revision=provider_revision(argv[6]);
            std::string variable;bool activate=false,has_key_env=false;
            if(argc==8){if(std::string(argv[7])=="--activate")activate=true;else{variable=argv[7];has_key_env=true;}}
            else if(argc==9){if(std::string(argv[8])!="--activate")throw std::invalid_argument("Use save-profile ID ROUTE MODEL REVISION [KEY_ENV] [--activate]");variable=argv[7];has_key_env=true;activate=true;}
            body=has_key_env?provider_key_fields(variable,argv[6]):Json{{"expected_revision",revision}};
            body["id"]=profile;body["route_id"]=route;body["model"]=model;if(activate)body["activate"]=true;
            path="/v1/provider/profiles";post=true;profile_operation=true;
        }
        else if(command=="select-profile" && argc==5){
            const auto profile=provider_profile_identity(argv[3]);const auto revision=provider_revision(argv[4]);
            path="/v1/provider/profiles/select";body={{"id",profile},{"expected_revision",revision}};post=true;profile_operation=true;
        }
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
        else if(command=="delegation"&&argc==3)path="/v1/agent/delegation";
        else if(command=="planning"&&argc==3)path="/v1/agent/planning";
        else if(command=="inspect-plan"&&argc==4)path="/v1/runs/"+id(argv[3])+"/plan";
        else if(command=="resume-plan"&&argc==6){path="/v1/runs/"+id(argv[3])+"/plan/resume";body={{"expected_revision",plan_revision(argv[4])},{"expected_state_sequence",plan_revision(argv[5])}};post=true;}
        else if(command=="plan-input"&&argc==8){
            const auto root=id(argv[3]),request=id(argv[4]);const auto revision=plan_revision(argv[5]),sequence=plan_revision(argv[6]);
            std::ifstream file(std::filesystem::u8path(argv[7]),std::ios::binary);if(!file)throw std::invalid_argument("Cannot read plan input file");std::string source;char byte;while(file.get(byte)){if(source.size()>=16384)throw std::invalid_argument("Plan input file exceeds limits");source.push_back(byte);}if(!file.eof())throw std::invalid_argument("Cannot read plan input file");validate_plan_input(source);
            path="/v1/runs/"+root+"/plan/human/"+request;body={{"input_json",source},{"expected_revision",revision},{"expected_state_sequence",sequence}};post=true;
        }
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
            const auto current=client.Get("/v1/health",headers);if(!current||current->status!=200)throw std::runtime_error("Cannot inspect backend provider capabilities");
            const auto capability=Json::parse(current->body);
            if(capability.value("provider_profile_admission",false)||capability.value("graph_provider_profile_admission",false)){
                const auto metadata=client.Get("/v1/provider/profiles",headers);if(!metadata||metadata->status!=200)throw std::runtime_error("Cannot inspect backend provider profiles for discovery");
                body=active_profile_discovery(Json::parse(metadata->body));path="/v1/provider/profiles/models";profile_operation=true;
            }else{
                const auto metadata=client.Get("/v1/provider/configuration",headers);
                if(!metadata)throw std::runtime_error("Cannot reach xMind Server for provider discovery");
                if(metadata->status<200 || metadata->status>=300)throw std::runtime_error("Server rejected provider metadata (HTTP "+std::to_string(metadata->status)+")");
                const auto setup=Json::parse(metadata->body);
                if(!setup.is_object() || !setup.contains("configured") || setup["configured"]!=true || setup.value("provider",std::string{})!="openai" || !setup.contains("revision") || !setup["revision"].is_number_integer() || setup["revision"]<1 || setup["revision"]>9007199254740991)throw std::runtime_error("Configure a provider key before discovering saved-account models");
                body={{"expected_revision",setup["revision"]}};
            }
        }
        if(watch)return watch_run(client,headers,path,watch_cursor,graph_watch);
        if(post&&(path=="/v1/runs"||path=="/v1/graph-runs")){
            const auto current=client.Get("/v1/health",headers);if(!current||current->status!=200)throw std::runtime_error("Cannot inspect backend admission capabilities");
            const auto capability=Json::parse(current->body);if(capability.value(path=="/v1/graph-runs"?"graph_provider_profile_admission":"provider_profile_admission",false)){
                const auto metadata=client.Get("/v1/provider/profiles",headers);if(!metadata||metadata->status!=200)throw std::runtime_error("Cannot inspect backend provider profile");body.update(provider_admission_binding(Json::parse(metadata->body)));
            }
        }
        auto response=post?((path=="/v1/provider/profiles/models"||path=="/v1/provider/models")?provider_discovery_request(client,headers,path,body):client.Post(path,headers,body.dump(),"application/json")):client.Get(path,headers);
        if(!response) throw std::runtime_error("Cannot reach xMind Server");
        if(response->status<200 || response->status>=300) {
            if(profile_operation)std::cerr<<provider_rejection(response->body).dump()<<'\n'<<"Backend rejected provider profile request (HTTP "<<response->status<<"). No automatic retry or selection fallback.\n";
            else std::cerr<<Json::parse(response->body).dump()<<'\n';
            return 1;
        }
        const auto result=Json::parse(response->body);
        std::cout<<result.dump(2)<<'\n';return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
