#pragma once
#include "agentflow/persistence_service.hpp"
#include <chrono>
#include <stop_token>
#include <atomic>

namespace agentflow {
struct BudgetOutcomeUnrecorded : std::runtime_error {using std::runtime_error::runtime_error;};
struct RootBudgetDeadlineExceeded : std::runtime_error {using std::runtime_error::runtime_error;};
// One live owner clock shared by a normal parent and all of its admitted leaves.
// Durable counters/attempt ownership remain in Repository; no transport retry.
class RootExecutionBudget {
public:
    RootExecutionBudget(PersistenceService& store,RootBudgetRecord record,
        std::chrono::steady_clock::time_point deadline,std::stop_token cancel);
    const std::string& root_id() const{return root_;}
    std::chrono::steady_clock::time_point deadline() const{return deadline_;}
    void check(std::stop_token cancel={}) const;
    RootBudgetRecord snapshot() const;
    ModelCallReservation reserve(const std::string& owner,ModelCallRole role,std::stop_token cancel={});
    void start(const ModelCallReservation& reservation,std::stop_token cancel={});
    void finish(const ModelCallReservation& reservation);
    bool try_acquire_leaf() noexcept;
    void release_leaf() noexcept;
private:
    PersistenceService& store_;
    std::string root_;
    std::chrono::steady_clock::time_point deadline_;
    std::stop_token cancel_;
    std::size_t parallel_;
    std::atomic<std::size_t> active_leaves_{0};
};
}
