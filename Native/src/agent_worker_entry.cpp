#include "agentflow/agent_worker_entry.hpp"
#include "agentflow/agent_worker_runner.hpp"
#include "agentflow/agent_terminal_handoff.hpp"
#include "agentflow/http_stream_transport.hpp"
#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/agent_worker_native_execution.hpp"
#include <thread>
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
    // The peer is a transitional local scope/client adapter only. No packet
    // reader or packet execution handler is installed in the product child.
    AgentWorkerRpcPeer peer(*channel,[](const std::string&,const std::string&,AgentWorkerRpcSink,std::stop_token)->std::string{throw AgentWorkerRpcRemoteError("unsupported");},transport_deadline);
    const auto& scope=peer.scope();
    peer.bind_native_model(std::make_shared<NativeWorkerModelClient>(std::move(access),scope.root_id,scope.run_id,scope.workspace_id,scope.generation,scope.workspace_revision));
    NativeExecutionBinding execution(scope,[&](std::stop_token stop){
        try{const auto result=coding?execute_agent_worker_coding_root(peer,stop):execute_agent_worker_read_root(peer,stop);return result.run.state==RunState::completed?RunState::completed:RunState::failed;}
        catch(const AgentTerminalHandoff& intent){return intent.outcome()==RunState::cancelled?RunState::cancelled:RunState::failed;}
        catch(const TransportCancelled&){return RunState::cancelled;}
        catch(const AgentWorkerRpcCancelled&){return RunState::cancelled;}
        catch(...){return RunState::failed;}
    });
    const auto control=execution.access();channel->send({"worker.execution.ready",nlohmann::json{{"version",1},{"endpoint",control.endpoint},{"ticket",control.ticket},{"pid",control.pid}}.dump()},bootstrap_deadline);
    std::jthread parent_lifetime([&](std::stop_token stop){try{(void)channel->receive(transport_deadline,stop);}catch(...){}execution.close();});
    execution.wait_closed(transport_deadline);parent_lifetime.request_stop();return 0;
}
}
