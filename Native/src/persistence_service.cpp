#include "agentflow/persistence_service.hpp"
#include "agentflow/backend_lease.hpp"
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
