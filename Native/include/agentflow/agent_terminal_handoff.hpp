#pragma once
#include "agentflow/records.hpp"
#include <exception>

namespace agentflow {
// A worker journal cannot return a fabricated terminal Run or reservation while
// physical ownership is still held. Unwind to the backend supervisor instead.
// This is an intent, never a durable fact or authority to select the final reason.
class AgentTerminalHandoff final:public std::runtime_error {
public:
    explicit AgentTerminalHandoff(RunState outcome):std::runtime_error("Agent terminal observation awaits owner drain"),outcome_(outcome),cause_(std::current_exception()){
        if(outcome!=RunState::failed&&outcome!=RunState::cancelled)throw std::invalid_argument("Terminal handoff requires failure or cancellation");
    }
    RunState outcome()const noexcept{return outcome_;}
    std::exception_ptr cause()const noexcept{return cause_;}
private:RunState outcome_;std::exception_ptr cause_;
};
}
