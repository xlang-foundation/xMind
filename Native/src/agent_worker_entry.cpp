#include "agentflow/agent_worker_entry.hpp"
#include "agentflow/agent_worker_runner.hpp"
#include "agentflow/agent_terminal_handoff.hpp"
#include "agentflow/http_stream_transport.hpp"
#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
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
    if(boot.operation!="worker.boot"||boot.payload.size()>1024)throw AgentWorkerProtocolError("Invalid worker execution bootstrap");
    const auto config=nlohmann::json::parse(mcp_compact_object(boot.payload));
    if(config.size()!=3||!config.at("version").is_number_integer()||config.at("version")!=1||!config.at("workspace").is_string()||(config.at("workspace")!="read"&&config.at("workspace")!="coding"))throw AgentWorkerProtocolError("Invalid worker execution bootstrap");
    const bool coding=config.at("workspace")=="coding";const auto& model=config.at("model");
    if(!model.is_object()||model.size()!=3||!model.at("endpoint").is_string()||!model.at("ticket").is_string()||!model.at("pid").is_number_unsigned()||model.at("pid").get<std::uint64_t>()>UINT32_MAX)throw AgentWorkerProtocolError("Invalid worker native model bootstrap");
    NativeWorkerModelAccess access{model.at("endpoint").get<std::string>(),model.at("ticket").get<std::string>(),model.at("pid").get<std::uint32_t>()};
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
            const auto result=coding?execute_agent_worker_coding_root(*self,execution_stop.get_token()):execute_agent_worker_read_root(*self,execution_stop.get_token());
            if(result.run.state!=RunState::completed)throw AgentWorkerRpcRemoteError("failed");
            return "{\"version\":1,\"outcome\":\"completed\"}";
        }catch(const AgentTerminalHandoff& intent){
            return intent.outcome()==RunState::cancelled?"{\"version\":1,\"outcome\":\"cancelled\"}":"{\"version\":1,\"outcome\":\"failed\"}";
        }catch(const TransportCancelled&){return "{\"version\":1,\"outcome\":\"cancelled\"}";}
        catch(const AgentWorkerRpcCancelled&){return "{\"version\":1,\"outcome\":\"cancelled\"}";}
        catch(...){return "{\"version\":1,\"outcome\":\"failed\"}";}
    },transport_deadline);
    const auto& scope=peer.scope();
    peer.bind_native_model(std::make_shared<NativeWorkerModelClient>(std::move(access),scope.root_id,scope.run_id,scope.workspace_id,scope.generation,scope.workspace_revision));
    self=&peer;peer.start();peer.wait_closed();peer.stop();return 0;
}
}
