#include "agentflow/agent_worker_entry.hpp"
#include "agentflow/agent_worker_runner.hpp"
#include "agentflow/agent_terminal_handoff.hpp"
#include "agentflow/http_stream_transport.hpp"
#include <atomic>
#include <charconv>
#include <string_view>

namespace agentflow {
int run_agent_channel_worker(int argc,char** argv){
    using Clock=std::chrono::steady_clock;
    if(argc!=3||!argv||!argv[1]||!argv[2]||std::string_view(argv[1])!="--channel-handle")
        throw std::invalid_argument("Worker entry requires its private inherited handle");
    const std::string_view argument=argv[2];std::uintptr_t handle=0;
    const auto number=std::from_chars(argument.data(),argument.data()+argument.size(),handle);
    if(argument.empty()||number.ec!=std::errc{}||number.ptr!=argument.data()+argument.size()||!handle)
        throw std::invalid_argument("Invalid worker inherited handle");
    const auto bootstrap_deadline=Clock::now()+std::chrono::seconds(10);
    auto channel=AgentWorkerChannel::accept_inherited(handle,bootstrap_deadline);
    const auto boot=channel->receive(bootstrap_deadline);
    if(boot.operation!="worker.boot"||boot.payload!="{\"version\":1}")
        throw AgentWorkerProtocolError("Invalid worker execution bootstrap");
    // This is only a transport ceiling. Every backend operation still checks
    // the captured original deadline, current lease and owner cancellation.
    const auto transport_deadline=Clock::now()+std::chrono::hours(1);
    AgentWorkerRpcPeer* self=nullptr;std::stop_source execution_stop;std::atomic<bool> executed=false;
    AgentWorkerRpcPeer peer(*channel,[&](const std::string& method,const std::string& payload,AgentWorkerRpcSink,std::stop_token handler_cancel)->std::string{
        if(method!="worker.execute"&&method!="worker.cancel")throw AgentWorkerRpcRemoteError("unsupported");
        if(payload!="{}")throw AgentWorkerRpcRemoteError("rejected");
        if(method=="worker.cancel"){execution_stop.request_stop();return "{\"version\":1,\"stop_requested\":true}";}
        if(executed.exchange(true))throw AgentWorkerRpcRemoteError("rejected");
        std::stop_callback propagate(handler_cancel,[&]{execution_stop.request_stop();});
        try{
            const auto result=execute_agent_worker_read_root(*self,execution_stop.get_token());
            if(result.run.state!=RunState::completed)throw AgentWorkerRpcRemoteError("failed");
            return "{\"version\":1,\"outcome\":\"completed\"}";
        }catch(const AgentTerminalHandoff& intent){
            return intent.outcome()==RunState::cancelled?"{\"version\":1,\"outcome\":\"cancelled\"}":"{\"version\":1,\"outcome\":\"failed\"}";
        }catch(const TransportCancelled&){return "{\"version\":1,\"outcome\":\"cancelled\"}";}
        catch(const AgentWorkerRpcCancelled&){return "{\"version\":1,\"outcome\":\"cancelled\"}";}
        catch(...){return "{\"version\":1,\"outcome\":\"failed\"}";}
    },transport_deadline);
    self=&peer;peer.start();peer.wait_closed();peer.stop();return 0;
}
}
