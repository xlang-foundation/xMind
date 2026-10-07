#include "agentflow/persistence_service.hpp"
#include "agentflow/backend_lease.hpp"
#include "agentflow/graph.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>

namespace agentflow {
struct PersistenceService::Impl {
    using Request=std::function<void(Repository&)>;
    std::mutex mutex,close_mutex;
    std::condition_variable changed;
    std::deque<Request> pending;
    std::thread worker;
    std::size_t limit;
    bool accepting=true;

    Impl(std::string path,std::vector<std::string> roots,std::size_t max_pending):limit(max_pending) {
        if(limit==0) throw std::invalid_argument("Persistence queue capacity must be positive");
        std::promise<void> ready;auto initialized=ready.get_future();
        worker=std::thread([this,path=std::move(path),roots=std::move(roots),ready=std::move(ready)]() mutable {
            bool started=false;
            try {
                BackendLease lease(path);
                Repository repository(path,roots);
                repository.recover_interrupted(lease);
                ready.set_value();started=true;
                for(;;) {
                    Request request;
                    {
                        std::unique_lock lock(mutex);
                        changed.wait(lock,[this]{return !pending.empty() || !accepting;});
                        if(pending.empty()) break;
                        request=std::move(pending.front());pending.pop_front();
                    }
                    // packaged_task captures operation exceptions in its future.
                    request(repository);
                }
                // Repository destruction precedes lease release, on this thread.
            } catch(...) {
                if(!started) ready.set_exception(std::current_exception());
                std::lock_guard lock(mutex);accepting=false;
                // Destroying tasks resolves their futures with broken_promise.
                pending.clear();
            }
        });
        try {initialized.get();} catch(...) {worker.join();throw;}
    }
    ~Impl() {close();}
    void close() {
        std::lock_guard closing(close_mutex);
        {
            std::lock_guard lock(mutex);accepting=false;
        }
        changed.notify_all();
        if(worker.joinable()) worker.join();
    }
    template<class Function> auto submit(Function action) {
        using Result=std::invoke_result_t<Function,Repository&>;
        auto task=std::make_shared<std::packaged_task<Result(Repository&)>>(std::move(action));
        auto result=task->get_future();
        {
            std::lock_guard lock(mutex);
            if(!accepting) throw PersistenceClosed("Persistence service is closed");
            if(pending.size()>=limit) throw PersistenceBusy("Persistence queue is full");
            pending.emplace_back([task=std::move(task)](Repository& repository){(*task)(repository);});
        }
        changed.notify_one();return result;
    }
};
PersistenceService::PersistenceService(std::string database,std::vector<std::string> roots,std::size_t limit)
    :impl_(std::make_unique<Impl>(std::move(database),std::move(roots),limit)) {}
PersistenceService::~PersistenceService()=default;
void PersistenceService::close() {impl_->close();}
std::future<Session> PersistenceService::create_session(std::string id,std::string title) {
    return impl_->submit([id=std::move(id),title=std::move(title)](Repository& repository){return repository.create_session(id,title);});
}
std::future<Session> PersistenceService::session(std::string id) {
    return impl_->submit([id=std::move(id)](Repository& repository){return repository.session(id);});
}
std::future<std::vector<Session>> PersistenceService::sessions() {
    return impl_->submit([](Repository& repository){return repository.sessions();});
}
std::future<Run> PersistenceService::create_run(std::string id,std::string session_id) {
    return impl_->submit([id=std::move(id),session_id=std::move(session_id)](Repository& repository){return repository.create_run(id,session_id);});
}
std::future<Run> PersistenceService::run(std::string id) {
    return impl_->submit([id=std::move(id)](Repository& repository){return repository.run(id);});
}
std::future<std::optional<Run>> PersistenceService::incoming_message(std::string message,std::string context,std::string identity,std::string content){return impl_->submit([message=std::move(message),context=std::move(context),identity=std::move(identity),content=std::move(content)](Repository& repository){return repository.incoming_message(message,context,identity,content);});}
std::future<Run> PersistenceService::start_incoming_message(std::string id,std::string context,std::string message,std::string prompt,std::string identity){return impl_->submit([id=std::move(id),context=std::move(context),message=std::move(message),prompt=std::move(prompt),identity=std::move(identity)](Repository& repository){return repository.start_incoming_message(id,context,message,prompt,identity);});}
std::future<std::optional<std::vector<Message>>> PersistenceService::task_history(std::string id){return impl_->submit([id=std::move(id)](Repository& repository){return repository.task_history(id);});}
std::future<std::vector<Event>> PersistenceService::event_batch(std::string id,std::int64_t after,std::size_t count){return impl_->submit([id=std::move(id),after,count](Repository& repository){return repository.event_batch(id,after,count);});}
std::future<Run> PersistenceService::start_prompt_run(std::string id,std::string session_id,std::string json) {
    return impl_->submit([id=std::move(id),session_id=std::move(session_id),json=std::move(json)](Repository& repository){return repository.start_prompt_run(id,session_id,json);});
}
std::future<void> PersistenceService::append_user_message(std::string id,std::string json) {
    return impl_->submit([id=std::move(id),json=std::move(json)](Repository& repository){repository.append_user_message(id,json);});
}
std::future<Run> PersistenceService::start_graph_run(std::string id,std::string session,std::string graph,std::int64_t revision,GraphPlan plan,std::string prompt){return impl_->submit([id=std::move(id),session=std::move(session),graph=std::move(graph),revision,plan=std::move(plan),prompt=std::move(prompt)](Repository& repository){return repository.start_graph_run(id,session,graph,revision,plan,prompt);});}
std::future<GraphRootRecord> PersistenceService::graph_run(std::string id){return impl_->submit([id=std::move(id)](Repository& repository){return repository.graph_run(id);});}
std::future<Run> PersistenceService::start_graph_child(std::string id,std::string parent,std::string node,std::string prompt,std::int64_t expected){return impl_->submit([id=std::move(id),parent=std::move(parent),node=std::move(node),prompt=std::move(prompt),expected](Repository& repository){return repository.start_graph_child(id,parent,node,prompt,expected);});}
std::future<GraphRootRecord> PersistenceService::settle_graph_child(std::string id,std::int64_t expected){return impl_->submit([id=std::move(id),expected](Repository& repository){return repository.settle_graph_child(id,expected);});}
std::future<GraphRootRecord> PersistenceService::start_graph_human(std::string id,std::string node,std::int64_t expected){return impl_->submit([id=std::move(id),node=std::move(node),expected](Repository& repository){return repository.start_graph_human(id,node,expected);});}
std::future<GraphRootRecord> PersistenceService::input_graph_human(std::string id,std::string node,std::string input,std::string actor,std::int64_t expected){return impl_->submit([id=std::move(id),node=std::move(node),input=std::move(input),actor=std::move(actor),expected](Repository& repository){return repository.input_graph_human(id,node,input,actor,expected);});}
std::future<GraphRootRecord> PersistenceService::skip_graph_node(std::string id,std::string node,std::int64_t expected){return impl_->submit([id=std::move(id),node=std::move(node),expected](Repository& repository){return repository.skip_graph_node(id,node,expected);});}
std::future<Run> PersistenceService::retire_graph_run(std::string id,RunState state,std::string reason){return impl_->submit([id=std::move(id),state,reason=std::move(reason)](Repository& repository){return repository.retire_graph_run(id,state,reason);});}
std::future<std::vector<Run>> PersistenceService::children(std::string id){return impl_->submit([id=std::move(id)](Repository& repository){return repository.children(id);});}
std::future<std::vector<Message>> PersistenceService::run_history(std::string id){return impl_->submit([id=std::move(id)](Repository& repository){return repository.run_history(id);});}
std::future<std::vector<Event>> PersistenceService::graph_events(std::string id,std::int64_t after){return impl_->submit([id=std::move(id),after](Repository& repository){return repository.graph_events(id,after);});}
std::future<void> PersistenceService::record_tool_turn(std::string id,std::string assistant,std::vector<std::string> tools) {
    return impl_->submit([id=std::move(id),assistant=std::move(assistant),tools=std::move(tools)](Repository& repository){repository.record_tool_turn(id,assistant,tools);});
}
std::future<Run> PersistenceService::complete_run(std::string id,std::string assistant) {
    return impl_->submit([id=std::move(id),assistant=std::move(assistant)](Repository& repository){return repository.complete_run(id,assistant);});
}
std::future<std::vector<Run>> PersistenceService::runs(std::string session_id) {
    return impl_->submit([session_id=std::move(session_id)](Repository& repository){return repository.runs(session_id);});
}
std::future<Run> PersistenceService::transition(std::string id,RunState expected,RunState next,std::string json) {
    return impl_->submit([id=std::move(id),expected,next,json=std::move(json)](Repository& repository){return repository.transition(id,expected,next,json);});
}
std::future<Event> PersistenceService::append_event(std::string id,std::string kind,std::string json) {
    return impl_->submit([id=std::move(id),kind=std::move(kind),json=std::move(json)](Repository& repository){return repository.append_event(id,kind,json);});
}
std::future<std::vector<Event>> PersistenceService::events(std::string id,std::int64_t after) {
    return impl_->submit([id=std::move(id),after](Repository& repository){return repository.events(id,after);});
}
std::future<void> PersistenceService::append_message(std::string id,std::string role,std::string json) {
    return impl_->submit([id=std::move(id),role=std::move(role),json=std::move(json)](Repository& repository){repository.append_message(id,role,json);});
}
std::future<std::vector<Message>> PersistenceService::history(std::string id) {
    return impl_->submit([id=std::move(id)](Repository& repository){return repository.history(id);});
}
std::future<void> PersistenceService::put_information(std::string category,std::string id,std::string json) {
    return impl_->submit([category=std::move(category),id=std::move(id),json=std::move(json)](Repository& repository){repository.put_information(category,id,json);});
}
std::future<std::string> PersistenceService::information(std::string category,std::string id) {
    return impl_->submit([category=std::move(category),id=std::move(id)](Repository& repository){return repository.information(category,id);});
}
std::future<CredentialMetadata> PersistenceService::put_credential(std::string scope,std::string id,
    std::string purpose,std::string label,SecretBytes secret,std::int64_t revision) {
    return impl_->submit([scope=std::move(scope),id=std::move(id),purpose=std::move(purpose),label=std::move(label),secret=std::move(secret),revision](Repository& repository){
        return repository.put_credential(scope,id,purpose,label,secret,revision);
    });
}
std::future<Operation> PersistenceService::request_operation(std::string id,OperationSpec spec,std::int64_t expiry) {
    return impl_->submit([id=std::move(id),spec=std::move(spec),expiry](Repository& repository){return repository.request_operation(id,spec,expiry);});
}
std::future<Operation> PersistenceService::operation(std::string id) {
    return impl_->submit([id=std::move(id)](Repository& repository){return repository.operation(id);});
}
std::future<std::vector<Operation>> PersistenceService::operations(std::string id) {
    return impl_->submit([id=std::move(id)](Repository& repository){return repository.operations(id);});
}
std::future<Operation> PersistenceService::decide_operation(std::string id,OperationDecision decision,std::string actor) {
    return impl_->submit([id=std::move(id),decision,actor=std::move(actor)](Repository& repository){return repository.decide_operation(id,decision,actor);});
}
std::future<Operation> PersistenceService::claim_operation(std::string id,OperationSpec spec) {
    return impl_->submit([id=std::move(id),spec=std::move(spec)](Repository& repository){return repository.claim_operation(id,spec);});
}
std::future<Operation> PersistenceService::finish_operation(std::string id,OperationState outcome,std::string result) {
    return impl_->submit([id=std::move(id),outcome,result=std::move(result)](Repository& repository){return repository.finish_operation(id,outcome,result);});
}
std::future<Operation> PersistenceService::cancel_operation(std::string id,OperationSpec spec) {
    return impl_->submit([id=std::move(id),spec=std::move(spec)](Repository& repository){return repository.cancel_operation(id,spec);});
}
std::future<Operation> PersistenceService::expire_operation(std::string id) {
    return impl_->submit([id=std::move(id)](Repository& repository){return repository.expire_operation(id);});
}
std::future<std::vector<CredentialMetadata>> PersistenceService::credentials(std::string scope) {
    return impl_->submit([scope=std::move(scope)](Repository& repository){return repository.credentials(scope);});
}
std::future<SecretBytes> PersistenceService::resolve_credential(std::string scope,std::string id,std::string purpose) {
    return impl_->submit([scope=std::move(scope),id=std::move(id),purpose=std::move(purpose)](Repository& repository){return repository.resolve_credential(scope,id,purpose);});
}
std::future<void> PersistenceService::delete_credential(std::string scope,std::string id,std::int64_t revision) {
    return impl_->submit([scope=std::move(scope),id=std::move(id),revision](Repository& repository){repository.delete_credential(scope,id,revision);});
}
}
