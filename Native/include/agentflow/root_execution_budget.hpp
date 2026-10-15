#pragma once
#include "agentflow/persistence_service.hpp"
#include "agentflow/dynamic_plan_records.hpp"
#include <chrono>
#include <stop_token>
#include <atomic>
#include <vector>

namespace agentflow {
struct BudgetOutcomeUnrecorded : std::runtime_error {using std::runtime_error::runtime_error;};
struct RootBudgetDeadlineExceeded : std::runtime_error {using std::runtime_error::runtime_error;};
// One live owner clock shared by a normal parent and all of its admitted leaves.
// Durable counters/attempt ownership remain in Repository; no transport retry.
class RootExecutionBudget {
public:
    RootExecutionBudget(PersistenceService& store,RootBudgetRecord record,
        std::chrono::steady_clock::time_point deadline,std::stop_token cancel,
        std::optional<DynamicBudgetSegment> segment={},std::string backend_identity={},
        std::vector<std::string> resumable_owners={});
    const std::string& root_id() const{return root_;}
    std::chrono::steady_clock::time_point deadline() const{return deadline_;}
    void check(std::stop_token cancel={}) const;
    RootBudgetRecord snapshot() const;
    ModelCallReservation reserve(const std::string& owner,ModelCallRole role,std::stop_token cancel={});
    void start(const ModelCallReservation& reservation,std::stop_token cancel={});
    void finish(const ModelCallReservation& reservation,const std::string& actual_assistant_json={});
    ModelCallReservation reserve_continuation(const std::string& plan_call,std::stop_token cancel={});
    void pin_context(const ContextSnapshot& snapshot);
    void adopt_context_pin(const ContextPausePin& pin);
    Run suspend_for_human(const DynamicPlanRecord& plan,const std::string& plan_call);
    std::int64_t active_elapsed_ms() const;
    const std::string& segment_id() const;
    bool segment_open() const noexcept{return segment_&&segment_->state=="open";}
    bool may_resume_owner(const std::string& owner) const noexcept;
    bool try_acquire_leaf() noexcept;
    bool yield_leaf() noexcept;
    void wait_for_leaves_drained(std::stop_token cancel={}) const;
    void reacquire_leaf(std::stop_token cancel={});
    void release_leaf() noexcept;
private:
    PersistenceService& store_;
    std::string root_;
    std::string policy_;
    std::chrono::steady_clock::time_point deadline_;
    std::stop_token cancel_;
    std::size_t parallel_;
    std::atomic<std::size_t> active_leaves_{0};
    std::optional<DynamicBudgetSegment> segment_;
    std::string backend_identity_;
    std::vector<std::string> resumable_owners_;
    std::chrono::steady_clock::time_point active_started_;
    std::optional<ContextPausePin> context_pin_;
};
}
