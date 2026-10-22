#pragma once
#include "xlang3/xlang3.h"
namespace agentflow {
// Private startup/liveness rendezvous. SDK values own the wire representation.
struct NativeWorkerLinksApi {
    X::Value Open(std::string ticket);
    BEGIN_PACKAGE(NativeWorkerLinksApi)
        APISET().AddFunc<1>("Open",&NativeWorkerLinksApi::Open);
    END_PACKAGE
};
}
