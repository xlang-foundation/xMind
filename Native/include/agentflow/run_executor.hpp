#pragma once
#include "agentflow/store.hpp"

namespace agentflow {
struct RunBusy : std::runtime_error {using std::runtime_error::runtime_error;};
struct RunUnavailable : std::runtime_error {using std::runtime_error::runtime_error;};
// Transport-neutral backend execution boundary; HTTP/CLI do not own run state.
class RunExecutor {
public:
    virtual ~RunExecutor()=default;
    virtual Run submit(std::string id,std::string session_id,std::string prompt)=0;
    virtual void cancel(const std::string& id)=0;
    virtual bool healthy() const=0;
};
}
