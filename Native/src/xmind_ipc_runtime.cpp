#include "agentflow/xmind_ipc_runtime.hpp"
#include "xlang3/cpp/runtime.h"
#include <mutex>
#include <random>
#include <stdexcept>
#include <vector>
namespace agentflow {
namespace {
struct Host {
    std::mutex mutex;
    std::vector<std::shared_ptr<void>> services;
    X::Runtime runtime;
    std::uint32_t port=0;
    std::uint32_t requested_port=0;
};
Host& host(){static Host value;return value;}
}
X::Runtime& xmind_ipc_runtime(){return host().runtime;}
void configure_xmind_ipc_port(std::uint32_t port){auto& state=host();std::lock_guard lock(state.mutex);if(!port||port>65535||state.port||state.requested_port)throw std::invalid_argument("Cannot change the process IPC listener");state.requested_port=port;}
void retain_xmind_ipc_service(std::shared_ptr<void> service){if(!service)throw std::invalid_argument("Native IPC service is empty");auto& state=host();std::lock_guard lock(state.mutex);state.services.push_back(std::move(service));}
std::uint32_t xmind_ipc_port(){auto& state=host();std::lock_guard lock(state.mutex);if(state.port)return state.port;std::random_device random;std::string failure;for(unsigned i=0;i<(state.requested_port?1u:24u);++i){const auto candidate=state.requested_port?state.requested_port:20000+random()%40000;try{state.runtime.LrpcListen(candidate);state.port=candidate;return state.port;}catch(const std::exception& error){failure=error.what();}}throw std::runtime_error("Cannot start xMind IPC listener: "+failure);}
}
