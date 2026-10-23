#include "agent_worker_rpc_fixture.hpp"
#include "agentflow/agent_worker_native_execution.hpp"
#include "nlohmann/json.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <vector>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::size_t max_calls=8,max_queued_bytes=8*1024*1024;
constexpr std::uint64_t max_id=9007199254740991;
void identifier(const std::string& value){if(value.empty()||value.size()>64||value.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_.")!=std::string::npos)throw AgentWorkerProtocolError("Invalid native RPC method");}
bool error_code(const std::string& code){return std::set<std::string>{"rejected","unsupported","cancelled","timeout","failed","outcome_unrecorded","provider_http","model_protocol","capacity","budget_exhausted"}.contains(code);}
Json parse(const std::string& bytes){
    if(bytes.empty()||bytes.size()>AgentWorkerChannel::max_message_bytes)throw AgentWorkerProtocolError("Native RPC envelope exceeds limits");std::vector<std::set<std::string>> keys;
    return Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& value){if(depth>8)throw AgentWorkerProtocolError("Invalid native RPC envelope");if(event==Json::parse_event_t::object_start)keys.emplace_back();if(event==Json::parse_event_t::key&&(keys.empty()||!keys.back().insert(value.get<std::string>()).second))throw AgentWorkerProtocolError("Duplicate native RPC field");if(event==Json::parse_event_t::object_end)keys.pop_back();return true;});
}
std::uint64_t envelope_id(const Json& value,std::size_t fields){if(!value.is_object()||value.size()!=fields||!value.at("version").is_number_integer()||value.at("version")!=1||!value.at("id").is_number_unsigned())throw AgentWorkerProtocolError("Invalid native RPC envelope");const auto id=value.at("id").get<std::uint64_t>();if(!id||id>max_id)throw AgentWorkerProtocolError("Invalid native RPC identity");return id;}
}
struct FixtureWorkerRpcPeer::Impl {
    struct Pending {std::deque<std::string> events;bool done=false,local_failure=false;std::string result,code;};
    struct Incoming {std::stop_source stop;std::atomic<bool> done=false;};
    struct Flight {std::shared_ptr<Incoming> owner;std::jthread thread;};
    const std::shared_ptr<NativeWorkerModelClient> sdk_services;
    std::shared_ptr<NativeExecutionClient> native_execution;
    AgentWorkerChannel& channel;FixtureWorkerRpcHandler handler;AgentWorkerChannel::Deadline deadline;
    std::mutex state,writer,request_order;std::condition_variable changed;bool closed=false,started=false;
    std::uint64_t outgoing=1,incoming=1;std::size_t queued=0;
    std::map<std::uint64_t,std::shared_ptr<Pending>> pending;std::map<std::uint64_t,std::shared_ptr<Incoming>> active;
    std::vector<Flight> flights;std::stop_source session_stop;std::jthread reader;
    Impl(AgentWorkerChannel& channel,FixtureWorkerRpcHandler callback,AgentWorkerChannel::Deadline deadline,std::shared_ptr<NativeWorkerModelClient> sdk):sdk_services(std::move(sdk)),channel(channel),handler(std::move(callback)),deadline(deadline){if(!handler||std::chrono::steady_clock::now()>=deadline)throw std::invalid_argument("Native RPC requires its handler and live deadline");}
    void start(){std::lock_guard lock(state);if(started||closed)throw AgentWorkerRpcClosed("Native worker RPC cannot restart");started=true;reader=std::jthread([this]{read();});}
    void fail(const std::string& reason){
        std::vector<std::stop_source> stops;{std::lock_guard lock(state);if(closed)return;closed=true;for(auto& [id,call]:pending)if(!call->done){call->done=true;call->local_failure=true;call->code=reason;}for(const auto& [id,owner]:active)stops.push_back(owner->stop);}
        session_stop.request_stop();for(auto& stop:stops)stop.request_stop();changed.notify_all();
    }
    void send(const std::string& operation,const Json& value){std::lock_guard lock(writer);{std::lock_guard guard(state);if(closed)throw AgentWorkerRpcClosed("Native worker RPC is closed");}channel.send({operation,value.dump()},deadline,session_stop.get_token());}
    void reap(){for(auto it=flights.begin();it!=flights.end();)if(it->owner->done.load()){it->thread.join();it=flights.erase(it);}else ++it;}
    void accept(const Json& value){
        const auto id=envelope_id(value,4);const auto method=value.at("method").get<std::string>(),payload=value.at("payload").get<std::string>();identifier(method);std::shared_ptr<Incoming> owner;
        {std::lock_guard lock(state);if(closed)return;if(id!=incoming||incoming==max_id)throw AgentWorkerProtocolError("Native RPC request identity was replayed or reordered");++incoming;if(active.size()<max_calls){owner=std::make_shared<Incoming>();active.emplace(id,owner);}}
        if(!owner){send("rpc.error",Json{{"version",1},{"id",id},{"code","capacity"},{"payload",""}});return;}
        flights.push_back({owner,std::jthread([this,owner,id,method,payload]{
            try{
                if(owner->stop.stop_requested())throw AgentWorkerRpcRemoteError("cancelled");
                const auto result=handler(method,payload,[&](const std::string& event){if(owner->stop.stop_requested())throw AgentWorkerRpcCancelled("Native worker RPC cancelled");send("rpc.event",Json{{"version",1},{"id",id},{"payload",event}});},owner->stop.get_token());
                send("rpc.response",Json{{"version",1},{"id",id},{"payload",result}});
            }catch(const AgentWorkerRpcRemoteError& error){try{send("rpc.error",Json{{"version",1},{"id",id},{"code",error_code(error.code())?error.code():"failed"},{"payload",error.detail().size()<=4096?error.detail():std::string{}}});}catch(...){fail("closed");}}
             catch(const AgentWorkerRpcCancelled&){try{send("rpc.error",Json{{"version",1},{"id",id},{"code","cancelled"},{"payload",""}});}catch(...){fail("closed");}}
             catch(...){try{send("rpc.error",Json{{"version",1},{"id",id},{"code","failed"},{"payload",""}});}catch(...){fail("closed");}}
            {std::lock_guard lock(state);active.erase(id);}owner->done=true;changed.notify_all();
        })});
    }
    void read(){try{while(!session_stop.stop_requested()){
        auto message=channel.receive(deadline,session_stop.get_token());const auto value=parse(message.payload);reap();
        if(message.operation=="rpc.request"){accept(value);continue;}
        if(message.operation=="rpc.cancel"){const auto id=envelope_id(value,2);std::optional<std::stop_source> stop;{std::lock_guard lock(state);const auto found=active.find(id);if(found!=active.end())stop=found->second->stop;else if(id>=incoming)throw AgentWorkerProtocolError("Cancellation has no admitted native RPC");}if(stop)stop->request_stop();continue;}
        const bool event=message.operation=="rpc.event",response=message.operation=="rpc.response",error=message.operation=="rpc.error";if(!event&&!response&&!error)throw AgentWorkerProtocolError("Unsupported native RPC envelope");const auto id=envelope_id(value,error?4:3);const auto payload=value.at("payload").get<std::string>();std::string code;if(error){code=value.at("code").get<std::string>();if(!error_code(code)||payload.size()>4096)throw AgentWorkerProtocolError("Invalid native RPC error");}
        {std::lock_guard lock(state);const auto found=pending.find(id);if(found==pending.end()||found->second->done)throw AgentWorkerProtocolError("Native RPC response has no pending owner");if(payload.size()>max_queued_bytes-queued)throw AgentWorkerProtocolError("Native RPC queue exceeds limits");queued+=payload.size();auto& call=*found->second;if(event){if(call.events.size()>=8192)throw AgentWorkerProtocolError("Native RPC event queue exceeds limits");call.events.push_back(payload);}else{call.done=true;call.result=payload;call.code=code;}}
        changed.notify_all();
    }}catch(const AgentWorkerChannelTimeout&){fail("timeout");}catch(...){fail("closed");}}
    std::string call(const std::string& method,const std::string& payload,AgentWorkerRpcSink sink,std::stop_token cancel){
        identifier(method);if(cancel.stop_requested())throw AgentWorkerRpcCancelled("Native worker RPC cancelled before dispatch");auto call=std::make_shared<Pending>();std::uint64_t id=0;
        {std::lock_guard order(request_order);{std::lock_guard lock(state);if(closed||!started)throw AgentWorkerRpcClosed("Native worker RPC is not live");if(pending.size()>=max_calls||outgoing>=max_id)throw AgentWorkerRpcClosed("Native worker RPC capacity exhausted");id=outgoing++;pending.emplace(id,call);}try{send("rpc.request",Json{{"version",1},{"id",id},{"method",method},{"payload",payload}});}catch(...){fail("closed");}}
        std::stop_callback notify(cancel,[&]{changed.notify_all();});bool cancellation_sent=false;std::exception_ptr sink_failure;
        try{for(;;){
            if((cancel.stop_requested()||sink_failure)&&!cancellation_sent){cancellation_sent=true;try{send("rpc.cancel",Json{{"version",1},{"id",id}});}catch(...){fail("closed");}}
            std::string event,result,code;bool has_event=false,done=false,local=false;
            {std::unique_lock lock(state);if(call->events.empty()&&!call->done){changed.wait_for(lock,std::chrono::milliseconds(10));continue;}if(!call->events.empty()){event=std::move(call->events.front());call->events.pop_front();queued-=event.size();has_event=true;}else{done=call->done;result=call->result;code=call->code;local=call->local_failure;}}
            if(has_event){if(sink&&!sink_failure)try{sink(event);}catch(...){sink_failure=std::current_exception();}continue;}
            if(!done)continue;{std::lock_guard lock(state);queued-=call->result.size();pending.erase(id);}if(sink_failure)std::rethrow_exception(sink_failure);if(local){if(code=="timeout")throw AgentWorkerRpcTimeout("Native worker RPC deadline elapsed");throw AgentWorkerRpcClosed("Native worker RPC closed before acknowledgement");}if(!code.empty())throw AgentWorkerRpcRemoteError(code,result);if(cancel.stop_requested())throw AgentWorkerRpcCancelled("Native worker RPC cancelled");return result;
        }}catch(...){std::lock_guard lock(state);const auto found=pending.find(id);if(found!=pending.end()){for(const auto& event:call->events)queued-=event.size();queued-=call->result.size();pending.erase(found);}throw;}
    }
    void stop(){if(native_execution)native_execution->close();fail("closed");if(reader.joinable())reader.join();for(auto& flight:flights){flight.owner->stop.request_stop();if(flight.thread.joinable())flight.thread.join();}flights.clear();std::lock_guard lock(writer);channel.close();}
};
FixtureWorkerRpcPeer::FixtureWorkerRpcPeer(AgentWorkerChannel& channel,FixtureWorkerRpcHandler handler,AgentWorkerChannel::Deadline deadline,std::shared_ptr<NativeWorkerModelClient> sdk):impl_(std::make_unique<Impl>(channel,std::move(handler),deadline,std::move(sdk))){}
FixtureWorkerRpcPeer::~FixtureWorkerRpcPeer(){impl_->stop();}
std::shared_ptr<NativeWorkerModelClient> FixtureWorkerRpcPeer::native_model()const{return impl_->sdk_services;}
void FixtureWorkerRpcPeer::bind_native_execution(std::shared_ptr<NativeExecutionClient> client){std::lock_guard lock(impl_->state);if(!client||impl_->native_execution||impl_->started||impl_->closed)throw AgentWorkerRpcClosed("Native execution binding requires fresh worker");impl_->native_execution=std::move(client);}
std::shared_ptr<NativeExecutionClient> FixtureWorkerRpcPeer::native_execution()const{std::lock_guard lock(impl_->state);return impl_->native_execution;}
AgentWorkerChannel::Deadline FixtureWorkerRpcPeer::execution_deadline()const noexcept{return impl_->deadline;}
void FixtureWorkerRpcPeer::start(){impl_->start();}
std::string FixtureWorkerRpcPeer::call(const std::string& method,const std::string& payload,AgentWorkerRpcSink sink,std::stop_token cancel){if(native_execution())throw AgentWorkerRpcRemoteError("unsupported");return impl_->call(method,payload,std::move(sink),cancel);}
bool FixtureWorkerRpcPeer::alive()const{std::lock_guard lock(impl_->state);return impl_->started&&!impl_->closed;}
void FixtureWorkerRpcPeer::wait_closed(){std::unique_lock lock(impl_->state);if(!impl_->started)throw AgentWorkerRpcClosed("Native worker RPC has not started");impl_->changed.wait(lock,[&]{return impl_->closed;});}
void FixtureWorkerRpcPeer::stop(){impl_->stop();}
std::uint32_t FixtureWorkerRpcPeer::drain(AgentWorkerProcess& worker,AgentWorkerChannel::Deadline deadline){
    if(&impl_->channel!=&worker.channel())throw std::invalid_argument("RPC drain requires its exact owned child channel");
    stop();impl_->channel.close();return worker.wait(deadline);
}
const AgentWorkerScope& FixtureWorkerRpcPeer::scope()const noexcept{return impl_->channel.scope();}
}
