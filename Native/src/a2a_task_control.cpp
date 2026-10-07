#include "agentflow/a2a_task_control.hpp"
#include "nlohmann/json.hpp"
#include <cmath>
#include <set>
#include <vector>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <limits>
namespace agentflow {
namespace {
using Json=nlohmann::json;
struct RpcError {int code;const char* message;};
void fields(const Json& value,std::initializer_list<const char*> allowed,int code){
    const auto* message=code==-32600?"Invalid Request":"Invalid params";
    if(!value.is_object())throw RpcError{code,message};
    for(auto it=value.begin();it!=value.end();++it){bool known=false;for(const auto* name:allowed)if(it.key()==name)known=true;if(!known)throw RpcError{code,message};}
}
std::string new_id(){std::random_device random;std::ostringstream text;text<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)text<<std::setw(8)<<random();return text.str();}
std::size_t history_length(const Json& params){if(!params.contains("historyLength"))return 0;const auto& value=params["historyLength"];if(!value.is_number_integer()||value<0||value>256)throw RpcError{-32602,"Invalid params"};return value.get<std::size_t>();}
std::int64_t timestamp_ms(const Json& input){
    if(!input.is_string())throw RpcError{-32602,"Invalid params"};const auto text=input.get<std::string>();if(text.size()<20||text.size()>35)throw RpcError{-32602,"Invalid params"};
    auto number=[&](std::size_t at,std::size_t size){if(at+size>text.size())throw RpcError{-32602,"Invalid params"};int result=0;for(std::size_t i=at;i<at+size;++i){if(text[i]<'0'||text[i]>'9')throw RpcError{-32602,"Invalid params"};result=result*10+text[i]-'0';}return result;};
    if(text[4]!='-'||text[7]!='-'||text[10]!='T'||text[13]!=':'||text[16]!=':')throw RpcError{-32602,"Invalid params"};
    const std::chrono::year_month_day date{std::chrono::year{number(0,4)},std::chrono::month{static_cast<unsigned>(number(5,2))},std::chrono::day{static_cast<unsigned>(number(8,2))}};
    const int hour=number(11,2),minute=number(14,2),second=number(17,2);if(!date.ok()||int(date.year())<1||hour>23||minute>59||second>59)throw RpcError{-32602,"Invalid params"};
    std::size_t at=19;std::int64_t fraction=0;bool round_up=false;
    if(at<text.size()&&text[at]=='.'){++at;const auto begin=at;while(at<text.size()&&text[at]>='0'&&text[at]<='9'){if(at-begin<3)fraction=fraction*10+text[at]-'0';else if(text[at]!='0')round_up=true;++at;}if(at==begin||at-begin>9)throw RpcError{-32602,"Invalid params"};for(auto digits=at-begin;digits<3;++digits)fraction*=10;}
    int offset=0;if(at<text.size()&&text[at]=='Z'&&at+1==text.size()){}else if(at+6==text.size()&&(text[at]=='+'||text[at]=='-')&&text[at+3]==':'){const auto hours=number(at+1,2),minutes=number(at+4,2);if(hours>23||minutes>59)throw RpcError{-32602,"Invalid params"};offset=(hours*60+minutes)*(text[at]=='+'?1:-1);}else throw RpcError{-32602,"Invalid params"};
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::sys_days(date).time_since_epoch()).count()+((hour*60+minute-offset)*60+second)*1000LL+fraction+(round_up?1:0);
}
std::string timestamp(std::int64_t ms){
    const auto point=std::chrono::sys_time<std::chrono::milliseconds>{std::chrono::milliseconds(ms)};const auto days=std::chrono::floor<std::chrono::days>(point);const std::chrono::year_month_day date{days};const std::chrono::hh_mm_ss time{point-days};
    std::ostringstream text;text<<std::setfill('0')<<std::setw(4)<<int(date.year())<<'-'<<std::setw(2)<<unsigned(date.month())<<'-'<<std::setw(2)<<unsigned(date.day())<<'T'<<std::setw(2)<<time.hours().count()<<':'<<std::setw(2)<<time.minutes().count()<<':'<<std::setw(2)<<time.seconds().count()<<'.'<<std::setw(3)<<time.subseconds().count()<<'Z';return text.str();
}
std::string page_token(const Json& value){static const char digits[]="0123456789abcdef";std::string result;for(const unsigned char byte:value.dump()){result+=digits[byte>>4];result+=digits[byte&15];}return result;}
Json page_cursor(const Json& value){
    if(!value.is_string())throw RpcError{-32602,"Invalid params"};const auto text=value.get<std::string>();if(text.empty()||text.size()>2048||text.size()%2)throw RpcError{-32602,"Invalid page token"};
    auto nibble=[](char value){if(value>='0'&&value<='9')return value-'0';if(value>='a'&&value<='f')return value-'a'+10;throw RpcError{-32602,"Invalid page token"};};std::string data;for(std::size_t i=0;i<text.size();i+=2)data+=static_cast<char>((nibble(text[i])<<4)|nibble(text[i+1]));
    try{auto result=Json::parse(data);fields(result,{"v","filter","ms","seq","bound"},-32602);if(result.value("v",Json{})!=1)throw RpcError{-32602,"Invalid page token"};for(const auto* key:{"ms","seq","bound"})if(!result.contains(key)||!result[key].is_number_integer()||result[key]>std::numeric_limits<std::int64_t>::max()||result[key]<(std::string_view(key)=="ms"?-1:1))throw RpcError{-32602,"Invalid page token"};return result;}catch(const Json::exception&){throw RpcError{-32602,"Invalid page token"};}
}
bool terminal(RunState state){return state==RunState::completed||state==RunState::failed||state==RunState::cancelled;}
std::string assistant_text(const Json& value){
    auto content=value.value("content",std::string{});const auto refusal=value.value("refusal",std::string{});
    if(!refusal.empty()){if(!content.empty())content+='\n';content+=refusal;}return content;
}
const char* state(RunState value){switch(value){case RunState::queued:return "submitted";case RunState::running:return "working";case RunState::paused:return "input-required";case RunState::completed:return "completed";case RunState::failed:return "failed";case RunState::cancelled:return "canceled";}throw RpcError{-32603,"Internal error"};}
const char* wire_state(RunState value,A2aVersion version){if(version==A2aVersion::legacy)return state(value);switch(value){case RunState::queued:return "TASK_STATE_SUBMITTED";case RunState::running:return "TASK_STATE_WORKING";case RunState::paused:return "TASK_STATE_INPUT_REQUIRED";case RunState::completed:return "TASK_STATE_COMPLETED";case RunState::failed:return "TASK_STATE_FAILED";case RunState::cancelled:return "TASK_STATE_CANCELED";}throw RpcError{-32603,"Internal error"};}
Json text_part(const std::string& text,A2aVersion version){if(version==A2aVersion::legacy)return {{"kind","text"},{"text",text}};return {{"text",text}};}
Json task(PersistenceService& store,const Run& run,std::size_t count=0,A2aVersion version=A2aVersion::legacy,bool include_artifacts=true){
    Json result={{"kind","task"},{"id",run.id},{"contextId",run.session_id},{"status",{{"state",wire_state(run.state,version)}}}};if(version==A2aVersion::v1)result.erase("kind");
    if(run.state==RunState::completed&&include_artifacts){
        // Session history can contain other root runs. Only this execution's
        // committed assistant event can become its result artifact.
        std::optional<std::string> content;
        for(const auto& event:store.events(run.id,0).get())if(event.kind=="conversation.assistant"){
            const auto value=Json::parse(event.json);if(value.contains("content")&&value["content"].is_string())content=assistant_text(value);
        }
        if(content)result["artifacts"]=Json::array({{{"artifactId",run.id+"-result"},{"parts",Json::array({text_part(*content,version)})}}});
    }
    if(count){
        const auto history=store.task_history(run.id).get();if(!history)throw RpcError{-32004,"Legacy task history is unavailable"};auto messages=Json::array();
        for(const auto& item:*history){if(item.role!="user"&&item.role!="assistant")continue;const auto value=Json::parse(item.json);if(!value.contains("content")||!value["content"].is_string())continue;
            Json message={{"messageId",value.value("a2a_message_id",run.id+"-message-"+std::to_string(item.sequence))},{"taskId",run.id},{"contextId",run.session_id},{"role",version==A2aVersion::v1?(item.role=="user"?"ROLE_USER":"ROLE_AGENT"):(item.role=="user"?"user":"agent")},{"parts",Json::array({text_part(item.role=="assistant"?assistant_text(value):value["content"].get<std::string>(),version)})}};if(version==A2aVersion::legacy)message["kind"]="message";messages.push_back(std::move(message));
        }
        if(messages.size()>count)messages.erase(messages.begin(),messages.end()-static_cast<Json::difference_type>(count));result["history"]=std::move(messages);
    }
    return result;
}
}
struct A2aTaskStream::Impl {
    PersistenceService& store;A2aStreamStart start;Json id;
    std::int64_t cursor=0;bool first=true,ended=false,artifact_started=false,suppress_replay=false;
    std::string observed_state;
    Impl(PersistenceService& persistence,const A2aStreamStart& initial):store(persistence),start(initial),id(Json::parse(initial.id_json)){
        auto task=Json::parse(start.initial_response).at("result");if(start.version==A2aVersion::v1)task=task.at("task");observed_state=task.at("status").at("state").get<std::string>();
        suppress_replay=task.contains("artifacts");
    }
    std::string response(const Json& result){return Json{{"jsonrpc","2.0"},{"id",id},{"result",result}}.dump();}
    std::string artifact(const std::string& text,bool append,bool last){Json value={{"taskId",start.task_id},{"contextId",start.context_id},{"artifact",{{"artifactId",start.task_id+"-result"},{"parts",Json::array({text_part(text,start.version)})}}},{"append",append},{"lastChunk",last}};if(start.version==A2aVersion::legacy){value["kind"]="artifact-update";return response(value);}return response({{"artifactUpdate",std::move(value)}});}
};
A2aTaskStream::A2aTaskStream(PersistenceService& store,const A2aStreamStart& start):impl_(std::make_unique<Impl>(store,start)){}
A2aTaskStream::~A2aTaskStream()=default;
A2aStreamBatch A2aTaskStream::poll(){
    auto& value=*impl_;A2aStreamBatch batch;if(value.ended){batch.final=true;return batch;}
    if(value.first){value.first=false;batch.responses.push_back(value.start.initial_response);return batch;}
    // Read state before the event batch. A terminal state is committed together
    // with its final events, so draining those events precedes final:true.
    const auto run=value.store.run(value.start.task_id).get();
    const auto events=value.store.event_batch(value.start.task_id,value.cursor,128).get();
    for(const auto& event:events){
        value.cursor=event.sequence;
        if(value.suppress_replay)continue;
        if(event.kind=="model.text"||event.kind=="model.refusal"){
            const auto data=Json::parse(event.json);const auto text=data.at("text").get<std::string>();
            if(!text.empty()){batch.responses.push_back(value.artifact(text,value.artifact_started,false));value.artifact_started=true;}
        }else if(event.kind=="conversation.tool_turn")value.artifact_started=false;
        else if(event.kind=="conversation.assistant"){
            batch.responses.push_back(value.artifact(assistant_text(Json::parse(event.json)),false,true));value.artifact_started=true;
        }
    }
    if(events.size()==128)return batch;
    const auto current=std::string(wire_state(run.state,value.start.version));const bool final=terminal(run.state)||run.state==RunState::paused;
    if(current!=value.observed_state||final){
        Json update={{"taskId",run.id},{"contextId",run.session_id},{"status",{{"state",current}}}};if(value.start.version==A2aVersion::legacy){update["kind"]="status-update";update["final"]=final;batch.responses.push_back(value.response(update));}else batch.responses.push_back(value.response({{"statusUpdate",std::move(update)}}));
        value.observed_state=current;
    }
    value.ended=final;batch.final=final;return batch;
}
std::string A2aTaskControl::snapshot(const A2aStreamStart& start){const auto value=task(store_,store_.run(start.task_id).get(),start.history_count,start.version);return Json{{"jsonrpc","2.0"},{"id",Json::parse(start.id_json)},{"result",start.version==A2aVersion::v1?Json{{"task",value}}:value}}.dump();}
std::optional<std::string> A2aTaskControl::dispatch(const std::string& source,A2aStreamStart* stream,const std::function<bool()>& reserve_stream){
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
        if(version_==A2aVersion::unsupported)throw RpcError{-32009,"Protocol version is not supported"};
        const bool v1=version_==A2aVersion::v1;auto method=request["method"].get<std::string>();
        if(v1){if(method=="SendMessage")method="message/send";else if(method=="SendStreamingMessage")method="message/stream";else if(method=="GetTask")method="tasks/get";else if(method=="CancelTask")method="tasks/cancel";else if(method=="SubscribeToTask")method="tasks/resubscribe";else if(method=="ListTasks")method="tasks/list";else if(method=="GetExtendedAgentCard")throw RpcError{-32007,"Extended Agent Card is not configured"};else if(method=="CreateTaskPushNotificationConfig"||method=="GetTaskPushNotificationConfig"||method=="ListTaskPushNotificationConfigs"||method=="DeleteTaskPushNotificationConfig")throw RpcError{-32003,"Push Notification is not supported"};else throw RpcError{-32601,"Method not found"};}
        if(method.starts_with("tasks/pushNotificationConfig/"))throw RpcError{-32003,"Push Notification is not supported"};
        if(method=="agent/getAuthenticatedExtendedCard")throw RpcError{-32004,"This operation is not supported"};
        const bool streaming=method=="message/stream"||method=="tasks/resubscribe";
        if(streaming&&(notification||!stream))throw RpcError{-32602,"Streaming requires a response ID and transport"};
        if(streaming&&reserve_stream&&!reserve_stream())throw RpcError{-32004,"Stream capacity is unavailable"};
        auto response=[&](const Run& run,const Json& value,bool blocking=false,std::size_t count=0){const bool wrapped=v1&&(method=="message/send"||streaming);const auto output=Json{{"jsonrpc","2.0"},{"id",id},{"result",wrapped?Json{{"task",value}}:value}}.dump();if(stream&&(streaming||blocking)){*stream={streaming,run.id,run.session_id,id.dump(),output};stream->version=version_;stream->blocking=blocking;stream->history_count=count;}return output;};
        if(method=="tasks/list"){
            if(!request.contains("params"))throw RpcError{-32602,"Invalid params"};const auto& params=request["params"];fields(params,{"tenant","contextId","status","pageSize","pageToken","historyLength","statusTimestampAfter","includeArtifacts"},-32602);
            if(params.contains("tenant")&&params["tenant"]!="")throw RpcError{-32004,"Tenant access is not configured"};
            std::string context;if(params.contains("contextId")){if(!params["contextId"].is_string())throw RpcError{-32602,"Invalid params"};context=params["contextId"].get<std::string>();if(context.size()>128||context.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw RpcError{-32602,"Invalid params"};}
            int status=0;const char* names[]={"TASK_STATE_UNSPECIFIED","TASK_STATE_SUBMITTED","TASK_STATE_WORKING","TASK_STATE_COMPLETED","TASK_STATE_FAILED","TASK_STATE_CANCELED","TASK_STATE_INPUT_REQUIRED","TASK_STATE_REJECTED","TASK_STATE_AUTH_REQUIRED"};
            if(params.contains("status")){const auto& value=params["status"];if(value.is_number_integer()&&value>=0&&value<=8)status=value.get<int>();else if(value.is_string()){status=-1;for(int i=0;i<9;++i)if(value==names[i])status=i;if(status<0)throw RpcError{-32602,"Invalid params"};}else throw RpcError{-32602,"Invalid params"};}
            const char* states[]={"","queued","running","completed","failed","cancelled","paused","unrepresented","unrepresented"};const std::string state=states[status];
            std::size_t count=50;if(params.contains("pageSize")){const auto& value=params["pageSize"];if(!value.is_number_integer()||value<1||value>100)throw RpcError{-32602,"Invalid params"};count=value.get<std::size_t>();}
            const auto history=history_length(params);bool artifacts=false;if(params.contains("includeArtifacts")){if(!params["includeArtifacts"].is_boolean())throw RpcError{-32602,"Invalid params"};artifacts=params["includeArtifacts"].get<bool>();}
            std::optional<std::int64_t> since;if(params.contains("statusTimestampAfter"))since=timestamp_ms(params["statusTimestampAfter"]);
            const Json filter=Json::array({context,state,since?Json(*since):Json(nullptr)});std::int64_t bound=0,sequence=0;std::optional<std::int64_t> ms;
            if(params.contains("pageToken")){if(!params["pageToken"].is_string())throw RpcError{-32602,"Invalid params"};if(params["pageToken"]!=""){const auto cursor=page_cursor(params["pageToken"]);if(cursor.value("filter",Json{})!=filter)throw RpcError{-32602,"Page token filters changed"};bound=cursor["bound"].get<std::int64_t>();sequence=cursor["seq"].get<std::int64_t>();ms=cursor["ms"].get<std::int64_t>();}}
            RootRunPage page{{},0,0,false};if(status<7)page=store_.list_root_runs(context,state,since,count,bound,ms,sequence).get();auto tasks=Json::array();
            for(const auto& entry:page.entries){auto value=task(store_,entry.run,history,A2aVersion::v1,artifacts);if(entry.status_ms)value["status"]["timestamp"]=timestamp(*entry.status_ms);if(artifacts&&!value.contains("artifacts"))value["artifacts"]=Json::array();tasks.push_back(std::move(value));}
            std::string next;if(page.more){const auto& entry=page.entries.back();next=page_token({{"v",1},{"filter",filter},{"ms",entry.status_ms.value_or(-1)},{"seq",entry.status_sequence},{"bound",page.watermark}});}
            if(notification)return {};return Json{{"jsonrpc","2.0"},{"id",id},{"result",{{"tasks",std::move(tasks)},{"pageSize",count},{"totalSize",page.total},{"nextPageToken",next}}}}.dump();
        }
        if(method=="message/send"||method=="message/stream"){
            if(!request.contains("params"))throw RpcError{-32602,"Invalid params"};const auto& params=request["params"];if(v1)fields(params,{"message","configuration","metadata","tenant"},-32602);else fields(params,{"message","configuration","metadata"},-32602);if(params.contains("tenant")&&(!params["tenant"].is_string()||params["tenant"]!=""))throw RpcError{-32004,"Tenant access is not configured"};
            if(params.contains("metadata")&&!params["metadata"].is_object())throw RpcError{-32602,"Invalid params"};
            Json config=Json::object();if(params.contains("configuration"))config=params["configuration"];if(v1)fields(config,{"returnImmediately","historyLength","acceptedOutputModes","taskPushNotificationConfig"},-32602);else fields(config,{"blocking","historyLength","acceptedOutputModes","pushNotificationConfig"},-32602);
            const auto option=v1?"returnImmediately":"blocking";if(config.contains(option)&&!config[option].is_boolean())throw RpcError{-32602,"Invalid params"};
            const bool blocking=!streaming&&!notification&&(v1?!config.value(option,false):config.value(option,false));
            if(blocking&&(!stream||(reserve_stream&&!reserve_stream())))throw RpcError{-32004,"Blocking capacity is unavailable"};
            if(config.contains("pushNotificationConfig")||config.contains("taskPushNotificationConfig"))throw RpcError{-32003,"Push Notification is not supported"};
            if(config.contains("acceptedOutputModes")){const auto& modes=config["acceptedOutputModes"];if(!modes.is_array()||modes.size()>16)throw RpcError{-32602,"Invalid params"};for(const auto& mode:modes)if(!mode.is_string())throw RpcError{-32602,"Invalid params"};if(!modes.empty()&&std::find(modes.begin(),modes.end(),Json("text/plain"))==modes.end())throw RpcError{-32005,"Incompatible content types"};}
            const auto count=history_length(config);if(!params.contains("message"))throw RpcError{-32602,"Invalid params"};auto message=params["message"];if(v1){fields(message,{"role","messageId","parts","contextId","taskId","metadata","referenceTaskIds","extensions"},-32602);if(message.value("role",Json{})!="ROLE_USER"&&message.value("role",Json{})!=1)throw RpcError{-32602,"Invalid params"};message["kind"]="message";message["role"]="user";if(message.contains("extensions")){if(!message["extensions"].is_array())throw RpcError{-32602,"Invalid params"};if(!message["extensions"].empty())throw RpcError{-32008,"Extensions are not configured"};message.erase("extensions");}
                if(message.contains("taskId")&&message["taskId"]=="")message.erase("taskId");if(message.contains("contextId")&&message["contextId"]=="")message.erase("contextId");if(message.contains("referenceTaskIds")&&message["referenceTaskIds"]==Json::array())message.erase("referenceTaskIds");
                if(message.contains("parts")&&message["parts"].is_array())for(auto& part:message["parts"]){fields(part,{"text","raw","url","data","metadata","mediaType","filename"},-32602);if(part.contains("raw")||part.contains("url")||part.contains("data")||!part.contains("text"))throw RpcError{-32005,"Incompatible content types"};if(part.contains("mediaType")){if(part["mediaType"]!=""&&part["mediaType"]!="text/plain")throw RpcError{-32005,"Incompatible content types"};part.erase("mediaType");}if(part.contains("filename")){if(part["filename"]!="")throw RpcError{-32005,"File input is not supported yet"};part.erase("filename");}part["kind"]="text";}
            }else fields(message,{"kind","role","messageId","parts","contextId","taskId","metadata","referenceTaskIds"},-32602);
            if(message.value("kind",Json{})!="message"||message.value("role",Json{})!="user"||!message.contains("messageId")||!message["messageId"].is_string())throw RpcError{-32602,"Invalid params"};
            if(message.contains("taskId")||message.contains("referenceTaskIds"))throw RpcError{-32004,"Task continuation is not supported yet"};if(message.contains("metadata")&&!message["metadata"].is_object())throw RpcError{-32602,"Invalid params"};
            const auto message_id=message["messageId"].get<std::string>();if(message_id.empty()||message_id.size()>256||message_id.find('\0')!=std::string::npos)throw RpcError{-32602,"Invalid params"};std::string context;
            if(message.contains("contextId")){if(!message["contextId"].is_string())throw RpcError{-32602,"Invalid params"};context=message["contextId"].get<std::string>();if(context.empty()||context.size()>128||context.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw RpcError{-32602,"Invalid params"};}
            if(!message.contains("parts")||!message["parts"].is_array()||message["parts"].empty()||message["parts"].size()>64)throw RpcError{-32602,"Invalid params"};std::string content;
            bool first_part=true;for(const auto& part:message["parts"]){if(!part.is_object())throw RpcError{-32602,"Invalid params"};if(part.value("kind",Json{})!="text")throw RpcError{-32005,"Incompatible content types"};fields(part,{"kind","text","metadata"},-32602);if(!part.contains("text")||!part["text"].is_string()||(part.contains("metadata")&&!part["metadata"].is_object()))throw RpcError{-32602,"Invalid params"};if(!first_part)content+='\n';first_part=false;content+=part["text"].get<std::string>();}
            if(content.empty()||content.size()>65536||content.find('\0')!=std::string::npos)throw RpcError{-32602,"Invalid params"};message.erase("contextId");const auto identity=message.dump();
            auto replay=store_.incoming_message(message_id,context,identity,content).get();Run run;
            if(replay)run=*replay;else {if(!executor_)throw RpcError{-32004,"Agent admission is unavailable"};run=executor_->submit_message(new_id(),context,message_id,content,identity);}
            const auto result=task(store_,run,count,version_);if(notification)return {};return response(run,result,blocking,count);
        }
        if(method!="tasks/get"&&method!="tasks/cancel"&&method!="tasks/resubscribe")throw RpcError{-32601,"Method not found"};
        if(!request.contains("params"))throw RpcError{-32602,"Invalid params"};auto params=request["params"];if(v1&&params.is_object()&&params.contains("tenant")){if(!params["tenant"].is_string()||params["tenant"]!="")throw RpcError{-32004,"Tenant access is not configured"};params.erase("tenant");}
        if(method=="tasks/get")fields(params,{"id","historyLength","metadata"},-32602);else fields(params,{"id","metadata"},-32602);
        if(!params.contains("id")||!params["id"].is_string())throw RpcError{-32602,"Invalid params"};const auto task_id=params["id"].get<std::string>();
        if(task_id.empty()||task_id.size()>128||task_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw RpcError{-32602,"Invalid params"};
        if(params.contains("metadata")&&!params["metadata"].is_object())throw RpcError{-32602,"Invalid params"};
        const auto count=history_length(params);
        auto current=store_.run(task_id).get();if(!current.parent_id.empty())throw RpcError{-32001,"Task not found"};
        if(v1&&method=="tasks/resubscribe"&&terminal(current.state))throw RpcError{-32004,"Terminal task cannot be subscribed"};
        if(method=="tasks/cancel"){
            if(terminal(current.state))throw RpcError{-32002,"Task cannot be canceled"};
            if(!executor_)throw RpcError{-32004,"Task cancellation is unavailable"};
            try{executor_->cancel(task_id);}catch(const Conflict&){throw RpcError{-32002,"Task cannot be canceled"};}catch(const NotFound&){
                // A completed owner may retire its in-memory job between the
                // initial read and this call. Its durable task still exists.
                if(terminal(store_.run(task_id).get().state))throw RpcError{-32002,"Task cannot be canceled"};
                throw RpcError{-32004,"Task cancellation is unavailable"};
            }
            current=store_.run(task_id).get();
        }
        const auto result=task(store_,current,count,version_);if(notification)return {};return response(current,result);
    }catch(const RpcError& fault){return error(fault.code,fault.message);}
    catch(const NotFound&){return error(-32001,"Task not found");}
    catch(const StatusTimeUnavailable&){return error(-32004,"Legacy status timestamps are unavailable");}
    catch(const Conflict&){return error(-32004,"Operation conflicts with current task or message identity");}
    catch(const RunBusy&){return error(-32004,"Agent admission capacity is unavailable");}
    catch(const std::invalid_argument&){return error(-32602,"Invalid params");}
    catch(const RunUnavailable&){return error(-32004,"Task control is unavailable");}
    catch(...){return error(-32603,"Internal error");}
}
}
