#include "agentflow/graph_service.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace agentflow {
namespace {
using Json=nlohmann::json;
bool terminal(RunState state) {
    return state==RunState::completed || state==RunState::failed || state==RunState::cancelled;
}
void controller(const std::string& actor) {
    if(actor.empty() || actor.size()>128 || actor.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::invalid_argument("Invalid graph controller identity");
}
}
struct GraphService::Impl {
    enum class Phase {queued,working,waiting,faulted};
    struct Job {std::string id,cancel_reason;Phase phase=Phase::queued;std::stop_source stop;bool cancelled=false;};
    PersistenceService& store;
    GraphRunner runner;
    GraphCatalog catalog;
    std::size_t capacity;
    mutable std::mutex mutex;
    std::mutex closing;
    std::condition_variable changed;
    std::map<std::string,std::shared_ptr<Job>> active;
    std::deque<std::shared_ptr<Job>> pending;
    std::vector<std::thread> workers;
    bool accepting=true,faulted=false;

    Impl(PersistenceService& persistence,AgentSettings settings,std::size_t count,
        std::size_t limit,std::size_t parallel)
        :store(persistence),runner(store,std::move(settings),parallel),
         catalog(GraphCatalogStore(store).load()),capacity(limit) {
        if(count<1 || count>8 || limit<1 || limit>4096)
            throw std::invalid_argument("Invalid graph service capacity");
        // Repository recovery already quarantines interrupted executable roots.
        // Adopt only durable human pauses; never replay queued/running work.
        for(const auto& session:store.sessions().get())for(const auto& run:store.runs(session.id).get()) {
            if(!run.graph_root || run.state!=RunState::paused)continue;
            if(active.size()>=capacity)throw RunBusy("Paused graph ownership exceeds service capacity");
            const auto root=store.graph_run(run.id).get();
            if(root.input_json.empty())throw RunUnavailable("Paused legacy graph input requires review");
            const auto input=Json::parse(root.input_json);
            if(!input.contains("content") || !input["content"].is_string())throw DatabaseError("Paused graph has invalid task input");
            const GraphPlan plan(root.specification_json);runner.validate(plan,input.value("model_id",std::string{}));
            const auto decision=GraphCoordinator(plan,root.checkpoint_json,GraphRestoreMode::live).inspect();
            if(decision.halted || decision.waiting_human.empty() || !decision.ready.empty() ||
                !decision.running.empty() || !decision.skippable.empty())throw DatabaseError("Paused graph has executable or inconsistent work");
            for(const auto& child:store.children(run.id).get())if(!terminal(child.state))throw DatabaseError("Paused graph has an unfinished child");
            auto job=std::make_shared<Job>();job->id=run.id;job->phase=Phase::waiting;
            active.emplace(run.id,std::move(job));
        }
        try {for(std::size_t i=0;i<count;++i)workers.emplace_back([this]{work();});}
        catch(...) {close();throw;}
    }
    void require_available() const {
        if(!accepting || faulted)throw RunUnavailable("Graph execution service is unavailable");
    }
    void fail_locked() {
        faulted=true;accepting=false;
        for(const auto& [id,job]:active){(void)id;job->stop.request_stop();}
        changed.notify_all();
    }
    void work() {
        for(;;) {
            std::shared_ptr<Job> job;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock,[&]{return !accepting || !pending.empty();});
                if(!accepting)return;
                job=std::move(pending.front());pending.pop_front();job->phase=Phase::working;
            }
            try {
                const auto observed=runner.execute(job->id,job->stop.get_token(),true);
                std::lock_guard lock(mutex);
                if(terminal(observed.state)){active.erase(job->id);continue;}
                if(observed.state!=RunState::paused)throw DatabaseError("Graph worker returned without a terminal outcome or human pause");
                // Human input may commit between the runner's pause observation
                // and worker retirement. Check the durable state under the same
                // lock used by input scheduling so that wakeup cannot be lost.
                const auto current=store.run(job->id).get();
                if(terminal(current.state)){active.erase(job->id);continue;}
                if(current.state==RunState::paused){
                    if(job->cancelled){store.retire_graph_run(job->id,RunState::cancelled,job->cancel_reason).get();active.erase(job->id);}
                    else job->phase=Phase::waiting;
                    continue;
                }
                if(current.state!=RunState::running)throw DatabaseError("Graph pause ownership changed unexpectedly");
                if(!accepting || job->stop.stop_requested()) {
                    store.retire_graph_run(job->id,RunState::cancelled,R"({"reason":"graph_service_shutdown"})").get();
                    active.erase(job->id);
                } else {pending.push_back(job);job->phase=Phase::queued;changed.notify_one();}
            } catch(...) {
                std::lock_guard lock(mutex);job->phase=Phase::faulted;fail_locked();
                // The runner drained its dispatched children. Preserve its
                // unfinished/uncertain ledger rather than inventing retirement.
            }
        }
    }
    void close() {
        std::lock_guard close_lock(closing);
        {
            std::lock_guard lock(mutex);accepting=false;
            for(const auto& [id,job]:active){(void)id;if(job->phase==Phase::working)job->stop.request_stop();}
            // Accepted but not dispatched roots can be retired without effects.
            // Already-paused roots are retained for the next backend owner.
            for(const auto& job:pending) {
                try {store.retire_graph_run(job->id,RunState::cancelled,R"({"reason":"graph_service_shutdown_before_dispatch"})").get();active.erase(job->id);}
                catch(...) {job->phase=Phase::faulted;faulted=true;}
            }
            pending.clear();changed.notify_all();
        }
        for(auto& worker:workers)if(worker.joinable())worker.join();
    }
};
GraphService::GraphService(PersistenceService& store,AgentSettings settings,std::size_t workers,
    std::size_t capacity,std::size_t parallel):impl_(std::make_unique<Impl>(store,std::move(settings),workers,capacity,parallel)) {}
GraphService::~GraphService(){impl_->close();}
std::vector<GraphExecutionMetadata> GraphService::graphs() const {
    std::lock_guard lock(impl_->mutex);std::vector<GraphExecutionMetadata> result;
    for(const auto& graph:impl_->catalog.entries) {
        bool executable=impl_->accepting && !impl_->faulted;
        try {impl_->runner.validate(graph.plan);}catch(const RunUnavailable&){executable=false;}catch(const std::invalid_argument&){executable=false;}
        result.push_back({graph.id,graph.revision,graph.plan.nodes().size(),executable});
    }
    return result;
}
Run GraphService::submit_graph(std::string id,std::string session,std::string graph,
    std::int64_t revision,std::string prompt,std::string model) {
    if(prompt.empty() || prompt.size()>1024*1024 || prompt.find('\0')!=std::string::npos)throw std::invalid_argument("Graph task must contain valid bounded content");
    std::unique_lock lock(impl_->mutex);impl_->require_available();
    const auto entry=std::find_if(impl_->catalog.entries.begin(),impl_->catalog.entries.end(),[&](const auto& item){return item.id==graph;});
    if(entry==impl_->catalog.entries.end())throw NotFound("Graph is not registered");
    if(revision!=entry->revision)throw Conflict("Graph catalog revision changed");
    if(impl_->active.size()>=impl_->capacity)throw RunBusy("Graph ownership capacity is full");
    if(model.empty()) {
        const bool agent=std::any_of(entry->plan.nodes().begin(),entry->plan.nodes().end(),[](const auto& node){return node.kind==GraphNodeKind::agent;});
        const auto models=impl_->runner.models();if(agent && !models.empty())model=models.front();
    }
    impl_->runner.validate(entry->plan,model);
    auto input_record=Json{{"content",std::move(prompt)},{"model_id",model}};const auto context=impl_->runner.provider_context(model);if(!context.empty())input_record["provider_context"]=Json::parse(context);const auto input=input_record.dump();
    auto job=std::make_shared<Impl::Job>();job->id=id;
    const auto [owned,inserted]=impl_->active.emplace(id,job);if(!inserted)throw Conflict("Graph is already owned");
    try {impl_->pending.push_back(job);}catch(...){impl_->active.erase(owned);throw;}
    Run result;
    try {result=impl_->store.start_graph_run(id,session,graph,revision,entry->plan,input).get();}
    catch(...){impl_->pending.pop_back();impl_->active.erase(owned);throw;}
    lock.unlock();impl_->changed.notify_one();return result;
}
GraphRootRecord GraphService::human_input(const std::string& id,const std::string& node,
    const std::string& input,const std::string& actor,std::int64_t revision) {
    std::unique_lock lock(impl_->mutex);impl_->require_available();
    const auto owned=impl_->active.find(id);if(owned==impl_->active.end())throw NotFound("Graph is not owned by this service");
    const auto job=owned->second;const bool waiting=job->phase==Impl::Phase::waiting;
    if(job->cancelled)throw Conflict("Graph cancellation is pending");
    // Reserve a possible wakeup before the transaction. Allocation failure
    // cannot leave committed runnable work without its scheduler entry.
    if(waiting)impl_->pending.push_back(job);
    GraphRootRecord result;
    try {result=impl_->store.input_graph_human(id,node,input,actor,revision).get();}
    catch(...){if(waiting)impl_->pending.pop_back();throw;}
    if(waiting) {
        if(result.run.state==RunState::running)job->phase=Impl::Phase::queued;
        else impl_->pending.pop_back();
    }
    lock.unlock();impl_->changed.notify_one();return result;
}
void GraphService::cancel(const std::string& id,const std::string& actor) {
    controller(actor);
    std::lock_guard lock(impl_->mutex);impl_->require_available();
    const auto owned=impl_->active.find(id);if(owned==impl_->active.end())throw NotFound("Graph is not owned by this service");
    const auto job=owned->second;
    const auto reason=Json{{"reason","graph_cancelled_by_controller"},{"actor",actor}}.dump();
    if(job->phase==Impl::Phase::working) {
        job->cancel_reason=reason;
        impl_->store.append_event(id,"graph.cancel.requested",reason).get();job->cancelled=true;job->stop.request_stop();
    } else {
        impl_->store.retire_graph_run(id,RunState::cancelled,reason).get();
        const auto queued=std::find(impl_->pending.begin(),impl_->pending.end(),job);if(queued!=impl_->pending.end())impl_->pending.erase(queued);
        impl_->active.erase(owned);
    }
}
bool GraphService::healthy() const {std::lock_guard lock(impl_->mutex);return impl_->accepting && !impl_->faulted;}
bool GraphService::idle() const {std::lock_guard lock(impl_->mutex);return impl_->accepting && !impl_->faulted && impl_->active.empty();}
void GraphService::close(){impl_->close();}
}
