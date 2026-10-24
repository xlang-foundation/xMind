#pragma once
#include "agentflow/remote_negotiation.hpp"
#include "agentflow/secret_protection.hpp"
#include <chrono>
#include <stop_token>

namespace agentflow {
enum class RemoteTransportPolicy {verified_https,isolated_loopback_test};
// Connector primitive only: does not persist credentials or activate profiles.
// No redirect, protocol downgrade or automatic retry; SDK objects stay local.
RemoteNegotiatedGrant negotiate_remote_connection(const std::string& origin,const SecretBytes& credential,
    RemoteNegotiationRequirements requirements,std::chrono::milliseconds timeout,
    RemoteTransportPolicy policy=RemoteTransportPolicy::verified_https,std::stop_token stop={});
}
