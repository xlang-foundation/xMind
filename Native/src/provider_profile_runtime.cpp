#include "agentflow/provider_profile_runtime.hpp"
#include <algorithm>
#include <mutex>
namespace agentflow {
namespace {
std::vector<ProviderProfileRoute> routes(const std::vector<ProviderProfileExecutionPolicy>& policy){
    std::vector<ProviderProfileRoute> result;
    for(const auto& value:policy){
        if(value.provider.endpoint!=value.route.endpoint||value.provider.wire!=value.route.wire||
            !value.provider.model.empty()||value.provider.deadline.count()<=0||value.provider.idle_timeout.count()<=0)
            throw std::invalid_argument("Provider execution policy differs from profile route");
        result.push_back(value.route);
    }
    return result;
}
}
struct ProviderProfileRuntime::Impl {
    PersistenceService& store;AgentSettings base;std::vector<ProviderProfileExecutionPolicy> policy;
    std::size_t workers,capacity;ProviderProfiles profiles;ProviderProfileSnapshot state;
    std::unique_ptr<ExecutionPlatform> service;mutable std::mutex mutex;
    Impl(PersistenceService& persistence,AgentSettings settings,std::vector<ProviderProfileExecutionPolicy> allowed,std::size_t count,std::size_t limit)
        :store(persistence),base(std::move(settings)),policy(std::move(allowed)),workers(count),capacity(limit),profiles(store,routes(policy)){
        if(count<1||count>16||limit<1||limit>4096)throw std::invalid_argument("Invalid provider execution capacity");
        state=profiles.snapshot();
        if(!state.active.empty()){
            auto owned=profiles.credential(state.active);
            const auto selected=std::find_if(state.profiles.begin(),state.profiles.end(),[&](const auto& value){return value.id==state.active;});
            service=prepare(*selected);
        }else{
            auto unconfigured=base;unconfigured.provider.model.clear();unconfigured.selectable_models.clear();unconfigured.credential.reset();
            service=std::make_unique<ExecutionPlatform>(store,std::move(unconfigured),workers,capacity);
        }
    }
    const ProviderProfileExecutionPolicy& route(const std::string& id)const{
        const auto found=std::find_if(policy.begin(),policy.end(),[&](const auto& value){return value.route.id==id;});
        if(found==policy.end())throw std::invalid_argument("Provider execution route is not allowed");return *found;
    }
    std::unique_ptr<ExecutionPlatform> prepare(const SavedProviderProfile& profile){
        const auto& allowed=route(profile.route_id);auto settings=base;settings.provider=allowed.provider;settings.provider.model=profile.model;
        settings.selectable_models.clear();settings.credential=CredentialReference{allowed.route.credential_scope,profile.credential_id,allowed.route.credential_purpose};
        return std::make_unique<ExecutionPlatform>(store,std::move(settings),workers,capacity);
    }
    void mutable_state(std::int64_t expected)const{
        if(expected<0||expected!=state.revision)throw Conflict("Provider profile runtime revision changed");
        if(!service->healthy())throw RunUnavailable("Reconcile execution faults before changing provider profiles");
        if(!service->idle())throw Conflict("Wait for active runs before changing provider profiles");
    }
    ProviderProfileRuntimeMetadata metadata()const{
        ProviderProfileRuntimeMetadata result{state.revision,state.active,{}};
        for(const auto& value:state.profiles)result.profiles.push_back({value.id,value.route_id,route(value.route_id).route.provider,value.model,value.revision});
        return result;
    }
};
ProviderProfileRuntime::ProviderProfileRuntime(PersistenceService& store,AgentSettings base,std::vector<ProviderProfileExecutionPolicy> policy,std::size_t workers,std::size_t capacity)
    :impl_(std::make_unique<Impl>(store,std::move(base),std::move(policy),workers,capacity)){}
ProviderProfileRuntime::~ProviderProfileRuntime()=default;
ProviderProfileRuntimeMetadata ProviderProfileRuntime::configuration()const{std::lock_guard lock(impl_->mutex);return impl_->metadata();}
ProviderProfileRuntimeMetadata ProviderProfileRuntime::save_profile(std::string id,std::string route,std::string model,SecretBytes key,std::int64_t expected,bool activate){
    std::unique_lock lock(impl_->mutex);impl_->mutable_state(expected);
    const bool replace=activate||impl_->state.active==id;std::unique_ptr<ExecutionPlatform> candidate;
    auto next=impl_->profiles.save(std::move(id),std::move(route),std::move(model),std::move(key),expected,activate,
        [&](const auto& profile,const auto&){candidate=impl_->prepare(profile);});
    impl_->state=std::move(next);std::unique_ptr<ExecutionPlatform> previous;
    if(replace){previous=std::move(impl_->service);impl_->service=std::move(candidate);}
    const auto result=impl_->metadata();lock.unlock();previous.reset();candidate.reset();return result;
}
ProviderProfileRuntimeMetadata ProviderProfileRuntime::select_profile(std::string id,std::int64_t expected){
    std::unique_lock lock(impl_->mutex);impl_->mutable_state(expected);std::unique_ptr<ExecutionPlatform> candidate;
    auto next=impl_->profiles.select(std::move(id),expected,[&](const auto& profile,const auto&){candidate=impl_->prepare(profile);});
    impl_->state=std::move(next);auto previous=std::move(impl_->service);impl_->service=std::move(candidate);
    const auto result=impl_->metadata();lock.unlock();previous.reset();return result;
}
ProviderProfileRuntimeMetadata ProviderProfileRuntime::import_existing_profile(std::string id,std::string route,std::string model,std::string credential,std::int64_t revision){
    std::unique_lock lock(impl_->mutex);impl_->mutable_state(0);std::unique_ptr<ExecutionPlatform> candidate;
    auto next=impl_->profiles.import_existing(std::move(id),std::move(route),std::move(model),std::move(credential),revision,
        [&](const auto& profile,const auto&){candidate=impl_->prepare(profile);});
    impl_->state=std::move(next);auto previous=std::move(impl_->service);impl_->service=std::move(candidate);
    const auto result=impl_->metadata();lock.unlock();previous.reset();return result;
}
Run ProviderProfileRuntime::submit(std::string id,std::string session,std::string prompt){return submit_model(std::move(id),std::move(session),std::move(prompt),{});}
Run ProviderProfileRuntime::submit_model(std::string id,std::string session,std::string prompt,std::string model){std::lock_guard lock(impl_->mutex);return impl_->service->submit_model(std::move(id),std::move(session),std::move(prompt),std::move(model));}
Run ProviderProfileRuntime::submit_message(std::string id,std::string context,std::string message,std::string content,std::string identity){std::lock_guard lock(impl_->mutex);return impl_->service->submit_message(std::move(id),std::move(context),std::move(message),std::move(content),std::move(identity));}
std::vector<std::string> ProviderProfileRuntime::models()const{std::lock_guard lock(impl_->mutex);return impl_->service->models();}
void ProviderProfileRuntime::cancel(const std::string& id){std::lock_guard lock(impl_->mutex);impl_->service->cancel(id);}
bool ProviderProfileRuntime::healthy()const{std::lock_guard lock(impl_->mutex);return impl_->service->healthy();}
bool ProviderProfileRuntime::available()const{std::lock_guard lock(impl_->mutex);return impl_->service->available();}
std::vector<GraphExecutionMetadata> ProviderProfileRuntime::graphs()const{std::lock_guard lock(impl_->mutex);return impl_->service->graphs();}
Run ProviderProfileRuntime::submit_graph(std::string id,std::string session,std::string graph,std::int64_t revision,std::string prompt,std::string model){std::lock_guard lock(impl_->mutex);return impl_->service->submit_graph(std::move(id),std::move(session),std::move(graph),revision,std::move(prompt),std::move(model));}
GraphRootRecord ProviderProfileRuntime::human_input(const std::string& root,const std::string& node,const std::string& input,const std::string& actor,std::int64_t revision){std::lock_guard lock(impl_->mutex);return impl_->service->human_input(root,node,input,actor,revision);}
}
