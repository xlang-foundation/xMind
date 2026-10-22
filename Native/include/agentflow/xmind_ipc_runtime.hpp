#pragma once
#include <cstdint>
#include <memory>
namespace X {class Runtime;}
namespace agentflow {
// xlang3 permits one listener runtime per process. All exported native objects
// share this host. Services are retained until after the SDK listener drains.
X::Runtime& xmind_ipc_runtime();
std::uint32_t xmind_ipc_port();
// Private child bootstrap only, before registration starts its listener.
void configure_xmind_ipc_port(std::uint32_t);
void retain_xmind_ipc_service(std::shared_ptr<void>);
}
