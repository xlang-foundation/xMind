#include "agentflow/agent_worker_entry.hpp"
#include "agentflow/agent_worker_runner.hpp"
#include "agentflow/agent_terminal_handoff.hpp"
#include "agentflow/http_stream_transport.hpp"
#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/agent_worker_native_execution.hpp"
#include <thread>
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
    const auto startup=channel->receive_model(bootstrap_deadline);
    const bool coding=startup.coding;
    NativeWorkerModelAccess access{startup.model.endpoint,startup.model.ticket,startup.model.pid};
    // This is only a transport ceiling. Every backend operation still checks
    // the captured original deadline, current lease and owner cancellation.
    const auto transport_deadline=Clock::now()+std::chrono::hours(1);
    const auto scope=channel->scope();
    NativeWorkerContext context(scope,std::make_shared<NativeWorkerModelClient>(std::move(access),scope.root_id,scope.run_id,scope.workspace_id,scope.generation,scope.workspace_revision));
    NativeExecutionBinding execution(scope,[&](std::stop_token stop){
        try{const auto result=coding?execute_agent_worker_coding_root(context,stop):execute_agent_worker_read_root(context,stop);return result.run.state==RunState::completed?RunState::completed:result.run.state==RunState::paused?RunState::paused:RunState::failed;}
        catch(const AgentTerminalHandoff& intent){return intent.outcome()==RunState::cancelled?RunState::cancelled:RunState::failed;}
        catch(const TransportCancelled&){return RunState::cancelled;}
        catch(const AgentWorkerRpcCancelled&){return RunState::cancelled;}
        catch(...){return RunState::failed;}
    });
    const auto control=execution.access();channel->publish_execution({control.endpoint,control.ticket,control.pid},bootstrap_deadline);
    std::jthread parent_lifetime([&](std::stop_token stop){try{channel->wait_peer_closed(transport_deadline,stop);}catch(...){}execution.close();});
    execution.wait_closed(transport_deadline);parent_lifetime.request_stop();return 0;
}
}
