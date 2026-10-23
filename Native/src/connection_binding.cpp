#include "agentflow/connection_binding.hpp"
#include "agentflow/connection_profiles.hpp"
#include "agentflow/backend_owner_control.hpp"
#include "agentflow/run_executor.hpp"
#include <algorithm>
#include <random>
namespace agentflow {
struct LocalConnectionContext {
    PersistenceService& store;RunExecutor* executor;BackendOwnerControl* owner;
    ConnectionProfiles profiles;std::string generation;
    mutable std::shared_mutex lifetime;bool active=true;
    LocalConnectionContext(PersistenceService& persistence,RunExecutor* execution,BackendOwnerControl* control)
        :store(persistence),executor(execution),owner(control),profiles(persistence){
        if(owner)generation=owner->status().generation;
        else{std::random_device random;for(int i=0;i<4;++i)generation+=std::to_string(random())+"-";}
    }
    auto enter()const{std::shared_lock lock(lifetime);if(!active)throw RunUnavailable("Captured connection backend retired");if(owner&&owner->status().generation!=generation)throw Conflict("Captured backend generation changed");return lock;}
    std::optional<std::shared_lock<std::shared_mutex>> admit(){if(owner)return owner->admit();return {};}
};
LocalConnectionBindings::LocalConnectionBindings(PersistenceService& store,RunExecutor* executor,BackendOwnerControl* owner)
    :context_(std::make_shared<LocalConnectionContext>(store,executor,owner)){}
LocalConnectionBindings::~LocalConnectionBindings(){close();}
void LocalConnectionBindings::close(){std::unique_lock lock(context_->lifetime);context_->active=false;}
std::shared_ptr<CapturedLocalConnection> LocalConnectionBindings::open(std::string id,std::int64_t revision){
    auto life=context_->enter();auto admission=context_->admit();const auto catalog=context_->profiles.load_or_initialize();
    const auto selected=std::find_if(catalog.profiles.begin(),catalog.profiles.end(),[&](const auto& profile){return profile.id==id;});
    if(selected==catalog.profiles.end())throw NotFound("Connection profile not found");
    if(selected->kind!=ConnectionKind::local)throw RunUnavailable("Connection is not authenticated");
    auto profile=context_->profiles.admit_local(revision);auto captured=std::shared_ptr<CapturedLocalConnection>(new CapturedLocalConnection);captured->context_=context_;
    captured->identity_={"local",context_->profiles.local_server_identity(),context_->generation,revision,1};return captured;
}
ConnectionBindingIdentity CapturedLocalConnection::identity()const{auto life=context_->enter();return identity_;}
BoundConnectionSession CapturedLocalConnection::open_session(std::string id)const{auto life=context_->enter();return {identity_,context_->store.session(id).get()};}
BoundConnectionSession CapturedLocalConnection::create_session(std::string id,std::string title)const{auto life=context_->enter();auto profile=context_->profiles.admit_local(identity_.profile_revision);auto admission=context_->admit();return {identity_,context_->store.create_session(id,title).get()};}
BoundConnectionRun CapturedLocalConnection::open_run(std::string id)const{auto life=context_->enter();return {identity_,context_->store.run(id).get()};}
BoundConnectionRun CapturedLocalConnection::start_run(std::string id,std::string session,std::string prompt)const{auto life=context_->enter();auto profile=context_->profiles.admit_local(identity_.profile_revision);auto admission=context_->admit();if(!context_->executor)throw RunUnavailable("Run execution unavailable");return {identity_,context_->executor->submit(id,session,prompt)};}
void CapturedLocalConnection::cancel_run(const std::string& id)const{auto life=context_->enter();auto admission=context_->admit();if(!context_->executor)throw RunUnavailable("Run execution unavailable");context_->executor->cancel(id);}
std::shared_ptr<ConnectionEventStream> CapturedLocalConnection::events(std::string session,std::string root)const{
    auto life=context_->enter();const auto run=context_->store.run(root).get();if(run.session_id!=session||!run.parent_id.empty())throw Conflict("Event stream requires its captured root session");
    auto stream=std::shared_ptr<ConnectionEventStream>(new ConnectionEventStream);stream->context_=context_;stream->identity_=identity_;stream->session_=std::move(session);stream->root_=std::move(root);return stream;
}
ConnectionEventCursor ConnectionEventStream::cursor()const{auto life=context_->enter();return {identity_.server_id,identity_.connection_id,session_,root_,0};}
ConnectionEventPage ConnectionEventStream::read(ConnectionEventCursor input)const{
    auto life=context_->enter();const ConnectionEventCursor expected{identity_.server_id,identity_.connection_id,session_,root_,input.sequence};
    if(input.sequence<0||input.sequence>9007199254740991LL||input!=expected)throw Conflict("Event cursor differs from captured connection/session/run");
    if(input.sequence){const auto boundary=context_->store.tree_events(root_,input.sequence-1,1).get();if(boundary.empty()||boundary.front().sequence!=input.sequence)throw Conflict("Cursor sequence does not belong to the captured stream");}
    ConnectionEventPage page{context_->store.tree_events(root_,input.sequence,256).get(),input};if(!page.events.empty())page.cursor.sequence=page.events.back().sequence;return page;
}
}
