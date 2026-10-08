#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace agentflow {
enum class RunState { queued, running, paused, completed, failed, cancelled };
inline std::string to_string(RunState state) {
    switch(state) {
    case RunState::queued:return "queued"; case RunState::running:return "running";
    case RunState::paused:return "paused"; case RunState::completed:return "completed";
    case RunState::failed:return "failed"; case RunState::cancelled:return "cancelled";
    }
    throw std::invalid_argument("Invalid run state");
}
struct NotFound : std::runtime_error { using std::runtime_error::runtime_error; };
struct Conflict : std::runtime_error { using std::runtime_error::runtime_error; };
struct DatabaseError : std::runtime_error { using std::runtime_error::runtime_error; };
struct Session { std::string id, title; };
struct Run { std::string id, session_id; RunState state; std::string parent_id,node_id;bool graph_root=false;std::string provider_context_json; };
struct Event { std::int64_t sequence; std::string run_id, kind, json; };
struct Message { std::int64_t sequence; std::string role, json; };

}
