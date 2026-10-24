#include "agentflow/session_inbox.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/provider_setup.hpp"
#include "agentflow/provider_profile_setup.hpp"
#if defined(_WIN32)
#include "agentflow/backend_owner_control.hpp"
#endif
#include "prompt_file_values.hpp"
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
namespace agentflow {
namespace {using Json=nlohmann::json;
std::string new_id(){std::random_device random;std::ostringstream value;value<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)value<<std::setw(8)<<random();return value.str();}
}
struct SessionInboxService::Impl {
    PersistenceService& persistence;RunExecutor* executor;BackendOwnerControl* owner_control;ProviderSetup* provider_setup;ProviderProfileSetup* provider_profiles;
    mutable std::shared_mutex lifetime;std::mutex closing;std::atomic<bool> stopping=false;
    struct QueuedSession {std::uint64_t generation=0;std::chrono::steady_clock::time_point retry_at{};};
    std::mutex queued_sessions_mutex;std::condition_variable_any queued_sessions_changed;std::map<std::string,QueuedSession> queued_sessions;std::jthread queued_session_dispatcher;
    Impl(PersistenceService& store,RunExecutor* execution,BackendOwnerControl* owner,ProviderSetup* setup,ProviderProfileSetup* profiles):persistence(store),executor(execution),owner_control(owner),provider_setup(setup?setup:dynamic_cast<ProviderSetup*>(execution)),provider_profiles(profiles?profiles:dynamic_cast<ProviderProfileSetup*>(execution)){}
    auto enter()const{std::shared_lock lock(lifetime);if(stopping)throw RunUnavailable("Inbox backend retired");return lock;}
    std::optional<std::shared_lock<std::shared_mutex>> admit()const{
#if defined(_WIN32)
        if(owner_control)return owner_control->admit();
#endif
        return {};
    }
    bool owner_admission_open()const{
#if defined(_WIN32)
        if(owner_control)return !owner_control->status().quiesced;
#endif
        return true;
    }
    void enqueue_session_for_dispatch(const std::string& session,std::chrono::milliseconds delay=std::chrono::milliseconds::zero()){
        {std::lock_guard lock(queued_sessions_mutex);auto& entry=queued_sessions[session];++entry.generation;entry.retry_at=std::chrono::steady_clock::now()+delay;}
        queued_sessions_changed.notify_one();
    }
    bool session_has_active_root(const std::string& session){
        const auto active=[](RunState state){return state==RunState::queued||state==RunState::running||state==RunState::paused;};
        const auto runs=persistence.runs(session).get();return std::any_of(runs.begin(),runs.end(),[&](const auto& run){return run.parent_id.empty()&&active(run.state);});
    }
    void recover_queued_sessions(){
        try{for(const auto& session:persistence.sessions().get()){
            const auto items=persistence.session_inbox(session.id).get();
            if(std::any_of(items.begin(),items.end(),[](const auto& item){return item.state=="queued";}))enqueue_session_for_dispatch(session.id);
        }}catch(...){}
    }
    void dispatch_queued_session(const std::string& session,std::uint64_t generation){
        bool retained=false;
        try{
            auto life=enter();
            const auto items=persistence.session_inbox(session).get();
            const auto next=std::find_if(items.begin(),items.end(),[](const auto& item){return item.state=="queued";});
            if(next==items.end()){
                std::lock_guard lock(queued_sessions_mutex);const auto found=queued_sessions.find(session);if(found!=queued_sessions.end()&&found->second.generation==generation)queued_sessions.erase(found);return;
            }
            retained=true;auto admission=admit();
            if(stopping||!executor||!executor->healthy()||!executor->available()||!owner_admission_open())throw RunUnavailable("Queued session execution is not currently available");
            std::optional<ProviderProfileAdmission> profile;std::string model;
            if(executor->supports_profile_admission()){
                if(!provider_profiles)throw RunUnavailable("Queued session provider binding is unavailable");
                const auto state=provider_profiles->configuration();if(state.active.empty())throw RunUnavailable("Select a provider profile before queued input can run");
                profile=ProviderProfileAdmission{state.active,state.revision};
            }else if(provider_setup){
                const auto state=provider_setup->configuration();if(!state.configured)throw RunUnavailable("Configure a provider before queued input can run");model=state.model;
            }
            std::vector<PromptFileAttachment> files;std::optional<WorkspaceAdmission> workspace;
            if(!next->files_json.empty()&&next->files_json!="[]"){
                const auto metadata=executor->execution_workspace();const auto membership=executor->workspace_membership_snapshot();
                if(!metadata.configured||metadata.workspace_id!=next->workspace_id||membership.workspace_id!=next->workspace_id||membership.revision!=next->workspace_revision)throw Conflict("Queued attachments belong to a workspace membership that has changed");
                workspace=WorkspaceAdmission{metadata.workspace_id,metadata.authority_id,next->workspace_revision};
                files=prompt_input::prompt_file_attachments(Json{{"files",Json::parse(next->files_json)}});
            }
            executor->submit_queued(new_id(),session,next->content,model,next->id,workspace,profile,std::move(files));
        }catch(...){}
        if(retained){std::lock_guard lock(queued_sessions_mutex);const auto found=queued_sessions.find(session);if(found!=queued_sessions.end()&&found->second.generation==generation)found->second.retry_at=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);}
    }
    void start_queued_session_dispatcher(){
        if(!executor||queued_session_dispatcher.joinable())return;
        queued_session_dispatcher=std::jthread([this](std::stop_token stop){
            recover_queued_sessions();
            while(!stop.stop_requested()){
                std::vector<std::pair<std::string,std::uint64_t>> due;
                {
                    std::unique_lock lock(queued_sessions_mutex);
                    queued_sessions_changed.wait_for(lock,stop,std::chrono::milliseconds(500),[&]{
                        const auto now=std::chrono::steady_clock::now();return std::any_of(queued_sessions.begin(),queued_sessions.end(),[&](const auto& value){return value.second.retry_at<=now;});
                    });
                    if(stop.stop_requested())return;
                    const auto now=std::chrono::steady_clock::now();for(const auto& [session,pending]:queued_sessions)if(pending.retry_at<=now)due.emplace_back(session,pending.generation);
                }
                for(const auto& [session,generation]:due){if(stop.stop_requested())return;dispatch_queued_session(session,generation);}
            }
        });
    }
    void stop_queued_session_dispatcher(){
        if(!queued_session_dispatcher.joinable())return;queued_session_dispatcher.request_stop();queued_sessions_changed.notify_all();queued_session_dispatcher.join();
    }
    void start(){std::lock_guard serialize(closing);if(stopping)throw RunUnavailable("Inbox backend retired");start_queued_session_dispatcher();}
    void close(){std::lock_guard serialize(closing);{std::unique_lock lock(lifetime);if(stopping.exchange(true))return;}stop_queued_session_dispatcher();}
};
SessionInboxService::SessionInboxService(PersistenceService& store,RunExecutor* execution,BackendOwnerControl* owner,ProviderSetup* setup,ProviderProfileSetup* profiles):impl_(std::make_unique<Impl>(store,execution,owner,setup,profiles)){}
SessionInboxService::~SessionInboxService(){close();}
void SessionInboxService::start(){impl_->start();}
void SessionInboxService::close(){impl_->close();}
std::vector<SessionInboxItem> SessionInboxService::list(const std::string& session)const{auto life=impl_->enter();return impl_->persistence.session_inbox(session).get();}
SessionInboxItem SessionInboxService::enqueue(const std::string& session,SessionInboxRequest request){
    auto life=impl_->enter();auto admission=impl_->admit();auto files_json=Json::array();auto validation=Json::array();for(const auto& file:request.files)validation.push_back({{"path",file.path},{"folder_id",file.folder_id},{"name",file.name},{"description",file.description},{"mention",file.mention}});request.files=prompt_input::prompt_file_attachments(Json{{"files",validation}});
    if(request.workspace){if(!impl_->executor)throw RunUnavailable("Queued workspace admission requires an opened root");validate_workspace_admission(*request.workspace,impl_->executor->execution_workspace());}
    std::string workspace_id;std::int64_t revision=0;
    if(!request.files.empty()){if(!request.workspace||request.workspace->revision<1)throw std::invalid_argument("Queued file attachments require observed workspace membership admission");const auto membership=impl_->executor->workspace_membership_snapshot();if(membership.workspace_id!=request.workspace->workspace_id||membership.revision!=request.workspace->revision)throw Conflict("Workspace membership changed while queuing file attachments");workspace_id=membership.workspace_id;revision=membership.revision;}
    for(const auto& file:request.files)files_json.push_back({{"path",file.path},{"folder_id",file.folder_id},{"name",file.name}});
    const auto saved=impl_->persistence.enqueue_session_input(request.id,session,request.content,files_json.dump(),workspace_id,revision).get();
    try{impl_->enqueue_session_for_dispatch(session,impl_->session_has_active_root(session)?std::chrono::milliseconds::zero():std::chrono::seconds(1));}catch(...){}return saved;
}
void SessionInboxService::cancel(const std::string& session,const std::string& id){auto life=impl_->enter();auto admission=impl_->admit();impl_->persistence.cancel_session_input(session,id).get();}
}
