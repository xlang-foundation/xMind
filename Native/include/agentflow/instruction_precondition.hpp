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
    // Backend-owned live authority, separate from repository metadata. It is
    // never serialized into approval payloads or supplied by a tool request.
    // The executor supplies its actual claimed operation ID before mutation.
    std::function<void(const std::string&,std::stop_token)> before_effect;
    void validate() const {
        const bool has_metadata=!metadata_json.empty();
        if(has_metadata!=static_cast<bool>(verify) || metadata_json.size()>24576)throw std::invalid_argument("Invalid repository instruction precondition");
    }
};
}
