#include "agentflow/agent_worker_native_execution.hpp"
#include "agentflow/agent_worker_native_links.hpp"
#include "agentflow/agent_worker_native_planning.hpp"
#include "agentflow/xmind_ipc_api.hpp"
#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/xmind_ipc_runtime.hpp"
#include "agentflow/python_tool_ipc_api.hpp"
#include "agentflow/schema_worker_service.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/run_executor.hpp"
#include "agentflow/local_auth.hpp"
#include "agentflow/backend_owner_control.hpp"
#include "agentflow/connection_profiles.hpp"
#include "agent_worker_sdk_values.hpp"
#include <optional>
#include <random>
#include <algorithm>
#include <mutex>
#include "xlang3/xlang3.h"
#include <stdexcept>
namespace agentflow {
namespace {
struct ApiContext {
    PersistenceService& store;
    RunExecutor* executor;
    std::string token;
    ConnectionProfiles connections;
    std::string generation=[] {std::random_device random;std::string id;for(int i=0;i<4;++i)id+=std::to_string(random())+"-";return id;}();
    BackendOwnerControl* owner=nullptr;
    mutable std::shared_mutex lifetime;
    bool active=true;
    ApiContext(PersistenceService& persistence,RunExecutor* execution,const std::string& auth,BackendOwnerControl* control):store(persistence),executor(execution),token(auth),connections(persistence),owner(control){if(owner)generation=owner->status().generation;}
    auto enter()const{std::shared_lock lock(lifetime);if(!active)throw RunUnavailable("unavailable");return lock;}
    std::optional<std::shared_lock<std::shared_mutex>> admit(){if(owner)return owner->admit();return {};}
    void authorize(const std::string& supplied)const {
        // The existing local owner token grants integration access, never
        // arbitrary SQL, credentials or a worker's private execution authority.
        auto lifetime_lock=enter();
        if(supplied.size()!=token.size())throw std::runtime_error("unauthorized");
        unsigned different=0;for(std::size_t i=0;i<token.size();++i)different|=static_cast<unsigned char>(token[i])^static_cast<unsigned char>(supplied[i]);
        if(different)throw std::runtime_error("unauthorized");
    }
};
template<class F>auto api_call(F action)->decltype(action()){
    try{return action();}catch(const NotFound&){throw std::runtime_error("not_found");}catch(const Conflict&){throw std::runtime_error("conflict");}catch(const BackendQuiesced&){throw std::runtime_error("backend_quiesced");}catch(const RunBusy&){throw std::runtime_error("busy");}catch(const RunUnavailable&){throw std::runtime_error("unavailable");}catch(const std::invalid_argument&){throw std::runtime_error("invalid_arguments");}catch(...){throw std::runtime_error("internal_error");}
}
void set(X::Value& value,const char* name,const X::Value& field){if(!value.Set(name,field))throw std::runtime_error("Cannot construct xMind API record");}
X::Value session_value(X3PackageHost* host,const Session& session){auto value=X::Value::Dict(host);set(value,"id",X::Value::String(host,session.id));set(value,"title",X::Value::String(host,session.title));set(value,"parent_id",X::Value::String(host,session.parent_id));return value;}
X::Value run_value(X3PackageHost* host,const Run& run){auto value=X::Value::Dict(host);set(value,"id",X::Value::String(host,run.id));set(value,"session_id",X::Value::String(host,run.session_id));set(value,"state",X::Value::String(host,to_string(run.state)));set(value,"parent_id",X::Value::String(host,run.parent_id));set(value,"node_id",X::Value::String(host,run.node_id));set(value,"graph_root",X::Value(run.graph_root));return value;}
struct RootApi;
struct HostApi {
    explicit HostApi(RootApi*){}
    std::int64_t ApiVersion()const{return 1;}
    std::string Transport()const{return "xlang3-lrpc";}
    BEGIN_PACKAGE(HostApi)
        APISET().AddFunc<0>("ApiVersion",&HostApi::ApiVersion);
        APISET().AddFunc<0>("Transport",&HostApi::Transport);
    END_PACKAGE
};
struct SessionsApi {
    std::shared_ptr<ApiContext> context;
    SessionsApi(RootApi*,std::string token);
    X::Value Open(std::string id){return api_call([&]{auto lifetime_lock=context->enter();return session_value(Host(),context->store.session(std::move(id)).get());});}
    X::Value Create(std::string id,std::string title){return api_call([&]{auto lifetime_lock=context->enter();auto admission=context->admit();return session_value(Host(),context->store.create_session(std::move(id),std::move(title)).get());});}
    BEGIN_PACKAGE(SessionsApi)
        APISET().AddFunc<1>("Open",&SessionsApi::Open);
        APISET().AddFunc<2>("Create",&SessionsApi::Create);
    END_PACKAGE
};
struct RunsApi {
    std::shared_ptr<ApiContext> context;
    RunsApi(RootApi*,std::string token);
    X::Value Open(std::string id){return api_call([&]{auto lifetime_lock=context->enter();return run_value(Host(),context->store.run(std::move(id)).get());});}
    X::Value Start(std::string id,std::string session,std::string prompt){return api_call([&]{auto lifetime_lock=context->enter();auto admission=context->admit();if(!context->executor)throw RunUnavailable("Run execution unavailable");return run_value(Host(),context->executor->submit(std::move(id),std::move(session),std::move(prompt)));});}
    void Cancel(std::string id){api_call([&]{auto lifetime_lock=context->enter();auto admission=context->admit();if(!context->executor)throw RunUnavailable("Run execution unavailable");context->executor->cancel(id);});}
    BEGIN_PACKAGE(RunsApi)
        APISET().AddFunc<1>("Open",&RunsApi::Open);
        APISET().AddFunc<3>("Start",&RunsApi::Start);
        APISET().AddFunc<1>("Cancel",&RunsApi::Cancel);
    END_PACKAGE
};
struct CapturedConnection {
    std::shared_ptr<ApiContext> context;std::int64_t revision;std::string server,generation;
    auto enter()const{auto life=context->enter();if(context->owner&&context->owner->status().generation!=generation)throw Conflict("Captured backend generation changed");return life;}
    auto admit(){return context->connections.admit_local(revision);}
    X::Value identity(X3PackageHost* host)const{return sdk_values::to_value(host,nlohmann::json{{"connection_id","local"},{"profile_revision",revision},{"server_id",server},{"backend_generation",generation},{"protocol_version",1},{"kind","local"}});}
    X::Value wrap(X3PackageHost* host,const char* name,X::Value record)const{auto out=X::Value::Dict(host);set(out,"binding",identity(host));set(out,name,record);return out;}
};
struct BoundSessionsApi {
    std::shared_ptr<CapturedConnection> state;BoundSessionsApi()=default;explicit BoundSessionsApi(std::shared_ptr<CapturedConnection> value):state(std::move(value)){}
    auto enter(){if(!state)throw RunUnavailable("Unbound connection service");return state->enter();}
    X::Value Open(std::string id){return api_call([&]{auto life=enter();return state->wrap(Host(),"session",session_value(Host(),state->context->store.session(id).get()));});}
    X::Value Create(std::string id,std::string title){return api_call([&]{auto life=enter();auto profile=state->admit();auto admission=state->context->admit();return state->wrap(Host(),"session",session_value(Host(),state->context->store.create_session(id,title).get()));});}
    BEGIN_PACKAGE(BoundSessionsApi) APISET().AddFunc<1>("Open",&BoundSessionsApi::Open);APISET().AddFunc<2>("Create",&BoundSessionsApi::Create); END_PACKAGE
};
struct BoundRunsApi {
    std::shared_ptr<CapturedConnection> state;BoundRunsApi()=default;explicit BoundRunsApi(std::shared_ptr<CapturedConnection> value):state(std::move(value)){}
    auto enter(){if(!state)throw RunUnavailable("Unbound connection service");return state->enter();}
    X::Value Open(std::string id){return api_call([&]{auto life=enter();return state->wrap(Host(),"run",run_value(Host(),state->context->store.run(id).get()));});}
    X::Value Start(std::string id,std::string session,std::string prompt){return api_call([&]{auto life=enter();auto profile=state->admit();auto admission=state->context->admit();if(!state->context->executor)throw RunUnavailable("Run execution unavailable");return state->wrap(Host(),"run",run_value(Host(),state->context->executor->submit(id,session,prompt)));});}
    void Cancel(std::string id){api_call([&]{auto life=enter();auto admission=state->context->admit();if(!state->context->executor)throw RunUnavailable("Run execution unavailable");state->context->executor->cancel(id);});}
    BEGIN_PACKAGE(BoundRunsApi) APISET().AddFunc<1>("Open",&BoundRunsApi::Open);APISET().AddFunc<3>("Start",&BoundRunsApi::Start);APISET().AddFunc<1>("Cancel",&BoundRunsApi::Cancel); END_PACKAGE
};
struct BoundEventsApi {
    std::shared_ptr<CapturedConnection> state;std::string session,root;BoundEventsApi()=default;BoundEventsApi(std::shared_ptr<CapturedConnection> value,std::string session_id,std::string run_id):state(std::move(value)),session(std::move(session_id)),root(std::move(run_id)){}
    auto enter(){if(!state)throw RunUnavailable("Unbound connection service");return state->enter();}
    nlohmann::json cursor(std::int64_t after)const{return {{"server_id",state->server},{"connection_id","local"},{"session_id",session},{"run_id",root},{"sequence",after}};}
    X::Value Cursor(){return api_call([&]{auto life=enter();return sdk_values::to_value(Host(),cursor(0));});}
    X::Value Read(X::Value position){return api_call([&]{auto life=enter();const auto input=sdk_values::from_value(position);if(!input.is_object()||input.size()!=5||!input.contains("sequence")||!input["sequence"].is_number_integer())throw std::invalid_argument("Invalid connection event cursor");const auto after=input["sequence"].get<std::int64_t>();if(after<0||input!=cursor(after))throw Conflict("Event cursor differs from captured connection/session/run");auto events=nlohmann::json::array();auto next=after;for(const auto& event:state->context->store.tree_events(root,after,256).get()){events.push_back({{"sequence",event.sequence},{"run_id",event.run_id},{"kind",event.kind},{"json",event.json}});next=event.sequence;}return sdk_values::to_value(Host(),nlohmann::json{{"events",std::move(events)},{"cursor",cursor(next)}});});}
    BEGIN_PACKAGE(BoundEventsApi) APISET().AddFunc<0>("Cursor",&BoundEventsApi::Cursor);APISET().AddFunc<1>("Read",&BoundEventsApi::Read); END_PACKAGE
};
struct BoundConnectionApi {
    std::shared_ptr<CapturedConnection> state;BoundConnectionApi()=default;explicit BoundConnectionApi(std::shared_ptr<CapturedConnection> value):state(std::move(value)){}
    auto enter(){if(!state)throw RunUnavailable("Unbound connection");return state->enter();}
    X::Value Identity(){return api_call([&]{auto life=enter();return state->identity(Host());});}
    X::Value Sessions();X::Value Runs();X::Value Events(std::string session,std::string root);
    BEGIN_PACKAGE(BoundConnectionApi) APISET().AddFunc<0>("Identity",&BoundConnectionApi::Identity);APISET().AddFunc<0>("Sessions",&BoundConnectionApi::Sessions);APISET().AddFunc<0>("Runs",&BoundConnectionApi::Runs);APISET().AddFunc<2>("Events",&BoundConnectionApi::Events); END_PACKAGE
};
struct ConnectionObjects {BEGIN_PACKAGE(ConnectionObjects) APISET().AddClass<0,BoundConnectionApi>("Binding");APISET().AddClass<0,BoundSessionsApi>("Sessions");APISET().AddClass<0,BoundRunsApi>("Runs");APISET().AddClass<0,BoundEventsApi>("Events"); END_PACKAGE};
ConnectionObjects& connection_objects(){static auto value=[] {auto owner=std::make_shared<ConnectionObjects>();retain_xmind_ipc_service(owner);(void)xmind_ipc_runtime().RegisterPackage("_xmind_connection_objects",*owner);return owner;}();return *value;}
X::Value BoundConnectionApi::Sessions(){return api_call([&]{auto life=enter();return connection_objects().__xlang3_package_->CreateInstance("Sessions",std::make_shared<BoundSessionsApi>(state));});}
X::Value BoundConnectionApi::Runs(){return api_call([&]{auto life=enter();return connection_objects().__xlang3_package_->CreateInstance("Runs",std::make_shared<BoundRunsApi>(state));});}
X::Value BoundConnectionApi::Events(std::string session,std::string root){return api_call([&]{auto life=enter();const auto run=state->context->store.run(root).get();if(run.session_id!=session||!run.parent_id.empty())throw Conflict("Event stream requires its captured root session");return connection_objects().__xlang3_package_->CreateInstance("Events",std::make_shared<BoundEventsApi>(state,session,root));});}
struct ConnectionsApi {
    std::shared_ptr<ApiContext> context;
    ConnectionsApi(RootApi*,std::string token);
    X::Value value(const ConnectionProfileCatalog& catalog){
        auto profiles=X::Value::List(Host());
        for(const auto& profile:catalog.profiles){const bool local=profile.kind==ConnectionKind::local;auto entry=X::Value::Dict(Host());
            set(entry,"id",X::Value::String(Host(),profile.id));set(entry,"name",X::Value::String(Host(),profile.name));set(entry,"kind",X::Value::String(Host(),local?"local":"nexus"));set(entry,"origin",X::Value::String(Host(),profile.origin));set(entry,"revision",X::Value(profile.revision));set(entry,"status",X::Value::String(Host(),local?"connected":"not_connected"));set(entry,"command_routing",X::Value(local));set(entry,"authenticated",X::Value(local));if(!profiles.Append(entry))throw Conflict("Cannot construct connection catalog");
        }
        auto result=X::Value::Dict(Host());set(result,"revision",X::Value(catalog.revision));set(result,"active_id",X::Value::String(Host(),catalog.active_id));set(result,"profiles",profiles);return result;
    }
    X::Value Open(std::string id,std::int64_t revision){return api_call([&]{auto life=context->enter();auto admission=context->admit();const auto catalog=context->connections.load_or_initialize();const auto selected=std::find_if(catalog.profiles.begin(),catalog.profiles.end(),[&](const auto& profile){return profile.id==id;});if(selected==catalog.profiles.end())throw NotFound("Connection profile not found");if(selected->kind!=ConnectionKind::local)throw RunUnavailable("Connection is not authenticated");auto profile=context->connections.admit_local(revision);auto state=std::make_shared<CapturedConnection>(CapturedConnection{context,revision,context->connections.local_server_identity(),context->generation});return connection_objects().__xlang3_package_->CreateInstance("Binding",std::make_shared<BoundConnectionApi>(std::move(state)));});}
    X::Value List(){return api_call([&]{auto life=context->enter();auto admission=context->admit();return value(context->connections.load_or_initialize());});}
    X::Value Save(X::Value profiles,std::int64_t revision){return api_call([&]{auto life=context->enter();auto admission=context->admit();if(!profiles.IsList()||profiles.Size()>32)throw std::invalid_argument("Connection profiles require a bounded list");return value(context->connections.apply(nlohmann::json{{"profiles",sdk_values::from_value(profiles)}}.dump(),revision));});}
    X::Value Select(std::string id,std::int64_t revision){return api_call([&]{auto life=context->enter();auto admission=context->admit();return value(context->connections.select(id,revision));});}
    BEGIN_PACKAGE(ConnectionsApi)
        APISET().AddFunc<2>("Open",&ConnectionsApi::Open);
        APISET().AddFunc<0>("List",&ConnectionsApi::List);
        APISET().AddFunc<2>("Save",&ConnectionsApi::Save);
        APISET().AddFunc<2>("Select",&ConnectionsApi::Select);
    END_PACKAGE
};
struct RootApi {
    std::mutex binding;
    std::shared_ptr<ApiContext> context;
    std::shared_ptr<ApiContext> capture(){std::lock_guard lock(binding);if(!context)throw RunUnavailable("Public backend services are unavailable");return context;}
    BEGIN_PACKAGE(RootApi)
        APISET().AddClass<0,HostApi,RootApi>("Host");
        APISET().AddClass<0,PythonToolsApi>("PythonTools");
        APISET().AddClass<0,SchemasApi>("Schemas");
        APISET().AddClass<0,NativeWorkersApi>("Workers");
        APISET().AddClass<0,NativeExecutionsApi>("Executions");
        APISET().AddClass<0,NativeWorkerLinksApi>("WorkerLinks");
        APISET().AddClass<0,NativePlanningApi>("Planning");
        APISET().AddClass<1,ConnectionsApi,RootApi>("Connections");
        APISET().AddClass<1,SessionsApi,RootApi>("Sessions");
        APISET().AddClass<1,RunsApi,RootApi>("Runs");
    END_PACKAGE
};
ConnectionsApi::ConnectionsApi(RootApi* root,std::string token):context(root->capture()){context->authorize(token);}
SessionsApi::SessionsApi(RootApi* root,std::string token):context(root->capture()){context->authorize(token);}
RunsApi::RunsApi(RootApi* root,std::string token):context(root->capture()){context->authorize(token);}
RootApi& api_root(){
    static auto root=[] {auto value=std::make_shared<RootApi>();auto& runtime=xmind_ipc_runtime();retain_xmind_ipc_service(value);auto package=runtime.RegisterPackage("xmind",*value);runtime.RegisterRemoteObject("xmind",package);(void)xmind_ipc_port();return value;}();return *root;
}
}
void ensure_xmind_ipc_api(){(void)api_root();}
struct XMindIpcServer::Impl {
    RootApi& root;
    std::shared_ptr<ApiContext> context;
    std::string endpoint;
    Impl(PersistenceService& store,RunExecutor* executor,const std::string& token,BackendOwnerControl* owner):root(api_root()),context(std::make_shared<ApiContext>(store,executor,token,owner)){
        validate_local_auth_token(token);endpoint="lrpc:"+std::to_string(xmind_ipc_port());
        std::lock_guard lock(root.binding);if(root.context)throw Conflict("Public xMind services already have an active owner");root.context=context;
    }
    ~Impl(){
        {std::unique_lock lock(context->lifetime);context->active=false;}
        std::lock_guard lock(root.binding);if(root.context==context)root.context.reset();
    }
};
XMindIpcServer::XMindIpcServer(PersistenceService& store,RunExecutor* executor,const std::string& token,BackendOwnerControl* owner):impl_(std::make_unique<Impl>(store,executor,token,owner)){}
XMindIpcServer::~XMindIpcServer()=default;
const std::string& XMindIpcServer::endpoint()const noexcept{return impl_->endpoint;}
}
