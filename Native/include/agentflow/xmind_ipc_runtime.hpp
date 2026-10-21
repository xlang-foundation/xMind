#pragma once
#include <cstdint>
#include <memory>
namespace X {class Runtime;}
namespace agentflow {
// xlang3 permits one listener runtime per process. All exported native objects
// share this host. Services are retained until after the SDK listener drains.
X::Runtime& xmind_ipc_runtime();
std::uint32_t xmind_ipc_port();
void retain_xmind_ipc_service(std::shared_ptr<void>);
}
