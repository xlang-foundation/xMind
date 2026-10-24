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
BoundConnectionMessages CapturedLocalConnection::messages(std::string session,std::int64_t before,std::int64_t count)const{
    auto life=context_->enter();
    if(before<0||before>9007199254740991LL||count<1||count>1000)throw std::invalid_argument("Invalid captured message page");
    (void)context_->store.session(session).get();
    if(before)(void)context_->store.message(session,before).get();
    return {identity_,session,context_->store.history_page(session,before?std::optional<std::int64_t>(before):std::nullopt,static_cast<std::size_t>(count)).get()};
}
BoundConnectionSession CapturedLocalConnection::create_session(std::string id,std::string title)const{auto life=context_->enter();auto profile=context_->profiles.admit_local(identity_.profile_revision);auto admission=context_->admit();return {identity_,context_->store.create_session(id,title).get()};}
BoundConnectionRun CapturedLocalConnection::open_run(std::string id)const{auto life=context_->enter();return {identity_,context_->store.run(id).get()};}
BoundConnectionRunMessages CapturedLocalConnection::run_messages(std::string session,std::string run,std::int64_t before,std::int64_t count)const{
    auto life=context_->enter();
    if(before<0||before>9007199254740991LL||count<1||count>1000)throw std::invalid_argument("Invalid captured run message page");
    if(context_->store.run(run).get().session_id!=session)throw Conflict("Run history differs from its captured session");
    auto messages=context_->store.run_history(run).get();
    if(before){const auto boundary=std::find_if(messages.begin(),messages.end(),[&](const auto& message){return message.sequence==before;});if(boundary==messages.end())throw Conflict("Message cursor belongs to another captured run");messages.erase(boundary,messages.end());}
    MessagePage page;const auto retained=std::min(messages.size(),static_cast<std::size_t>(count));
    if(messages.size()>retained){messages.erase(messages.begin(),messages.end()-retained);page.next_before_sequence=messages.front().sequence;}
    page.messages=std::move(messages);return {identity_,std::move(session),std::move(run),std::move(page)};
}
Run submit_local_connection_run(RunExecutor& executor,ConnectionRunRequest request){
    if(!request.files.empty()&&(!request.workspace||request.workspace->revision<1))throw std::invalid_argument("File attachments require the observed workspace membership revision");
    if(request.workspace)validate_workspace_admission(*request.workspace,executor.execution_workspace());
    if(request.inbox)return executor.submit_queued(request.id,request.session,request.prompt,request.model,*request.inbox,request.workspace,request.provider,std::move(request.files));
    if(!request.files.empty())return executor.submit_with_attachments(request.id,request.session,request.prompt,request.model,std::move(request.files),request.workspace,request.provider);
    if(request.workspace)return executor.submit_workspace(request.id,request.session,request.prompt,request.model,*request.workspace,request.provider);
    if(request.provider)return executor.submit_profile(request.id,request.session,request.prompt,request.model,*request.provider);
    return executor.submit_model(request.id,request.session,request.prompt,request.model);
}
BoundConnectionRun CapturedLocalConnection::start_run(std::string id,std::string session,std::string prompt)const{return start_run(ConnectionRunRequest{std::move(id),std::move(session),std::move(prompt)});}
BoundConnectionRun CapturedLocalConnection::start_run(ConnectionRunRequest request)const{auto life=context_->enter();auto profile=context_->profiles.admit_local(identity_.profile_revision);auto admission=context_->admit();if(!context_->executor)throw RunUnavailable("Run execution unavailable");return {identity_,submit_local_connection_run(*context_->executor,std::move(request))};}

void CapturedLocalConnection::cancel_run(const std::string& id)const{auto life=context_->enter();auto admission=context_->admit();if(!context_->executor)throw RunUnavailable("Run execution unavailable");context_->executor->cancel(id);}
namespace {
void operation_scope(LocalConnectionContext& context,const std::string& session,const std::string& run){if(context.store.run(run).get().session_id!=session)throw Conflict("Operation scope differs from its captured session");}
Operation scoped_operation(LocalConnectionContext& context,const std::string& session,const std::string& run,const std::string& id){operation_scope(context,session,run);auto value=context.store.operation(id).get();if(value.spec.run_id!=run)throw Conflict("Operation belongs to another captured run");return value;}
}
std::shared_ptr<CapturedRunOperations> CapturedLocalConnection::operations(std::string session,std::string run)const{auto life=context_->enter();operation_scope(*context_,session,run);auto result=std::shared_ptr<CapturedRunOperations>(new CapturedRunOperations);result->context_=context_;result->identity_=identity_;result->session_=std::move(session);result->run_=std::move(run);return result;}
BoundConnectionOperations CapturedRunOperations::list()const{auto life=context_->enter();operation_scope(*context_,session_,run_);return {identity_,session_,run_,context_->store.operations(run_).get()};}
BoundConnectionOperation CapturedRunOperations::open(const std::string& id)const{auto life=context_->enter();return {identity_,scoped_operation(*context_,session_,run_,id)};}
BoundConnectionOperation CapturedRunOperations::decide(const std::string& id,OperationDecision decision)const{auto life=context_->enter();auto admission=context_->admit();if(decision!=OperationDecision::allow&&decision!=OperationDecision::deny)throw std::invalid_argument("Invalid operation decision");(void)scoped_operation(*context_,session_,run_,id);return {identity_,context_->store.decide_operation(id,decision,"local-owner").get()};}
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
