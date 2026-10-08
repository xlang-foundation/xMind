#pragma once
#include "agentflow/root_execution_budget.hpp"
#include <functional>
#include <future>
#include <memory>
#include <vector>

namespace agentflow {
struct NativeChildCapacityUnavailable : std::runtime_error {using std::runtime_error::runtime_error;};
struct NativeChildOutcomeUnrecorded : std::runtime_error {using std::runtime_error::runtime_error;};
struct NativeChildWork {
    std::shared_ptr<RootExecutionBudget> budget;
    std::function<Run(std::stop_token)> action;
    std::stop_token cancel;
};
// One bounded pool shared by ordinary delegation and dynamic children in an
// execution generation. Its workers are separate from occupied root workers.
class NativeChildExecutor {
    struct Impl;
public:
    class Batch {
    public:
        ~Batch();
        Batch(const Batch&)=delete;
        Batch& operator=(const Batch&)=delete;
        const std::vector<std::shared_future<Run>>& results() const;
        void dispatch(); // Only after the owning admission transaction commits.
        void request_stop();
    private:
        friend class NativeChildExecutor;
        struct State;
        explicit Batch(std::unique_ptr<State> state);
        std::unique_ptr<State> state_;
    };
    explicit NativeChildExecutor(std::size_t workers=4,std::size_t capacity=64);
    ~NativeChildExecutor();
    NativeChildExecutor(const NativeChildExecutor&)=delete;
    NativeChildExecutor& operator=(const NativeChildExecutor&)=delete;
    // Reserves capacity, promises and job tracking before durable admission.
    // Close/fault retires undispatched reservations and their futures without
    // waiting for Batch destruction; a later dispatch rejects before launch.
    // Undispatched destruction releases reservations. Dispatched destruction
    // stop-requests and drains actual jobs; it never abandons physical work.
    std::unique_ptr<Batch> stage(std::vector<NativeChildWork> work);
    bool healthy() const;
    void fail();
    void close();
private:
    std::shared_ptr<Impl> impl_;
};
}
