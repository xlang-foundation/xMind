#include "agentflow/a2a_task_control.hpp"
#include "nlohmann/json.hpp"
#include <cmath>
#include <set>
#include <vector>
namespace agentflow {
namespace {
using Json=nlohmann::json;
struct RpcError {int code;const char* message;};
void fields(const Json& value,std::initializer_list<const char*> allowed,int code){
    const auto* message=code==-32600?"Invalid Request":"Invalid params";
    if(!value.is_object())throw RpcError{code,message};
    for(auto it=value.begin();it!=value.end();++it){bool known=false;for(const auto* name:allowed)if(it.key()==name)known=true;if(!known)throw RpcError{code,message};}
}
bool terminal(RunState state){return state==RunState::completed||state==RunState::failed||state==RunState::cancelled;}
const char* state(RunState value){switch(value){case RunState::queued:return "submitted";case RunState::running:return "working";case RunState::paused:return "input-required";case RunState::completed:return "completed";case RunState::failed:return "failed";case RunState::cancelled:return "canceled";}throw RpcError{-32603,"Internal error"};}
Json task(PersistenceService& store,const Run& run){
    Json result={{"kind","task"},{"id",run.id},{"contextId",run.session_id},{"status",{{"state",state(run.state)}}}};
    if(run.state==RunState::completed){
        // Session history can contain other root runs. Only this execution's
        // committed assistant event can become its result artifact.
        std::optional<std::string> content;
        for(const auto& event:store.events(run.id,0).get())if(event.kind=="conversation.assistant"){
            const auto value=Json::parse(event.json);if(value.contains("content")&&value["content"].is_string())content=value["content"].get<std::string>();
        }
        if(content)result["artifacts"]=Json::array({{{"artifactId",run.id+"-result"},{"parts",Json::array({{{"kind","text"},{"text",*content}}})}}});
    }
    return result;
}
}
std::optional<std::string> A2aTaskControl::dispatch(const std::string& source){
    Json id=nullptr;bool notification=false;
    auto error=[&](int code,const char* message)->std::optional<std::string>{if(notification)return {};return Json{{"jsonrpc","2.0"},{"id",id},{"error",{{"code",code},{"message",message}}}}.dump();};
    try{
        if(source.size()>65536)throw RpcError{-32600,"Invalid Request"};
        Json request;std::vector<std::set<std::string>> keys;
        try{request=Json::parse(source,[&](int depth,Json::parse_event_t event,Json& value){
            if(depth>32)throw RpcError{-32600,"Invalid Request"};
            if(event==Json::parse_event_t::object_start)keys.emplace_back();else if(event==Json::parse_event_t::object_end)keys.pop_back();
            else if(event==Json::parse_event_t::key&&!keys.back().insert(value.get<std::string>()).second)throw RpcError{-32600,"Invalid Request"};return true;
        });}catch(const Json::exception&){throw RpcError{-32700,"Parse error"};}
        fields(request,{"jsonrpc","id","method","params"},-32600);
        if(request.value("jsonrpc",Json{})!="2.0"||!request.contains("method")||!request["method"].is_string())throw RpcError{-32600,"Invalid Request"};
        if(request.contains("id")){
            const auto& candidate=request["id"];
            if(candidate.is_string()){if(candidate.get_ref<const std::string&>().size()>256)throw RpcError{-32600,"Invalid Request"};}
            else if(candidate.is_number()){const auto number=candidate.get<double>();if(!std::isfinite(number)||number<-9007199254740991.0||number>9007199254740991.0)throw RpcError{-32600,"Invalid Request"};}
            else if(!candidate.is_null())throw RpcError{-32600,"Invalid Request"};id=candidate;
        }else notification=true;
        const auto method=request["method"].get<std::string>();
        if(method.starts_with("tasks/pushNotificationConfig/"))throw RpcError{-32003,"Push Notification is not supported"};
        if(method=="message/send"||method=="message/stream"||method=="tasks/resubscribe"||method=="agent/getAuthenticatedExtendedCard")throw RpcError{-32004,"This operation is not supported"};
        if(method!="tasks/get"&&method!="tasks/cancel")throw RpcError{-32601,"Method not found"};
        if(!request.contains("params"))throw RpcError{-32602,"Invalid params"};const auto& params=request["params"];
        if(method=="tasks/get")fields(params,{"id","historyLength","metadata"},-32602);else fields(params,{"id","metadata"},-32602);
        if(!params.contains("id")||!params["id"].is_string())throw RpcError{-32602,"Invalid params"};const auto task_id=params["id"].get<std::string>();
        if(task_id.empty()||task_id.size()>128||task_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw RpcError{-32602,"Invalid params"};
        if(params.contains("metadata")&&!params["metadata"].is_object())throw RpcError{-32602,"Invalid params"};
        if(params.contains("historyLength")){const auto& length=params["historyLength"];if(!length.is_number_integer()||length<0||length>256)throw RpcError{-32602,"Invalid params"};if(length!=0)throw RpcError{-32004,"Task-local history is not supported yet"};}
        auto current=store_.run(task_id).get();if(!current.parent_id.empty())throw RpcError{-32001,"Task not found"};
        if(method=="tasks/cancel"){
            if(terminal(current.state))throw RpcError{-32002,"Task cannot be canceled"};
            if(!executor_)throw RpcError{-32004,"Task cancellation is unavailable"};
            try{executor_->cancel(task_id);}catch(const NotFound&){
                // A completed owner may retire its in-memory job between the
                // initial read and this call. Its durable task still exists.
                if(terminal(store_.run(task_id).get().state))throw RpcError{-32002,"Task cannot be canceled"};
                throw RpcError{-32004,"Task cancellation is unavailable"};
            }
            current=store_.run(task_id).get();
        }
        const auto result=task(store_,current);if(notification)return {};return Json{{"jsonrpc","2.0"},{"id",id},{"result",result}}.dump();
    }catch(const RpcError& fault){return error(fault.code,fault.message);}
    catch(const NotFound&){return error(-32001,"Task not found");}
    catch(const Conflict&){return error(-32002,"Task cannot be canceled");}
    catch(const RunUnavailable&){return error(-32004,"Task control is unavailable");}
    catch(...){return error(-32603,"Internal error");}
}
}
