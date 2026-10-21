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
};
Host& host(){static Host value;return value;}
}
X::Runtime& xmind_ipc_runtime(){return host().runtime;}
void retain_xmind_ipc_service(std::shared_ptr<void> service){if(!service)throw std::invalid_argument("Native IPC service is empty");auto& state=host();std::lock_guard lock(state.mutex);state.services.push_back(std::move(service));}
std::uint32_t xmind_ipc_port(){auto& state=host();std::lock_guard lock(state.mutex);if(state.port)return state.port;std::random_device random;std::string failure;for(unsigned i=0;i<24;++i){const auto candidate=20000+random()%40000;try{state.runtime.LrpcListen(candidate);state.port=candidate;return state.port;}catch(const std::exception& error){failure=error.what();}}throw std::runtime_error("Cannot start xMind IPC listener: "+failure);}
}
