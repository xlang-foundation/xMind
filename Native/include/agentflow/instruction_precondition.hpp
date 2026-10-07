#pragma once
#include <functional>
#include <stop_token>
#include <string>
#include <stdexcept>
namespace agentflow {
// Backend-produced condition bound into the durable approval payload. It grants
// no access. The synchronous executor checks it before dispatch, after approval.
struct InstructionPrecondition {
    std::string metadata_json;
    std::function<void(std::stop_token)> verify;
    void validate() const {
        const bool has_metadata=!metadata_json.empty();
        if(has_metadata!=static_cast<bool>(verify) || metadata_json.size()>24576)throw std::invalid_argument("Invalid repository instruction precondition");
    }
};
}
