#include "agentflow/agent_worker_native_execution.hpp"
#include "agentflow/agent_worker_native_links.hpp"
#include "agentflow/agent_worker_native_planning.hpp"
#include "agentflow/xmind_ipc_api.hpp"
#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/xmind_ipc_runtime.hpp"
#include "agentflow/python_tool_ipc_api.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/run_executor.hpp"
#include "agentflow/local_auth.hpp"
#include "agentflow/backend_owner_control.hpp"
#include <optional>
#include <mutex>
#include "xlang3/xlang3.h"
#include <stdexcept>
namespace agentflow {
namespace {
struct ApiContext {
    PersistenceService& store;
    RunExecutor* executor;
    std::string token;
    BackendOwnerControl* owner=nullptr;
    mutable std::shared_mutex lifetime;
    bool active=true;
    ApiContext(PersistenceService& persistence,RunExecutor* execution,const std::string& auth,BackendOwnerControl* control):store(persistence),executor(execution),token(auth),owner(control){}
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
struct RootApi {
    std::mutex binding;
    std::shared_ptr<ApiContext> context;
    std::shared_ptr<ApiContext> capture(){std::lock_guard lock(binding);if(!context)throw RunUnavailable("Public backend services are unavailable");return context;}
    BEGIN_PACKAGE(RootApi)
        APISET().AddClass<0,HostApi,RootApi>("Host");
        APISET().AddClass<0,PythonToolsApi>("PythonTools");
        APISET().AddClass<0,NativeWorkersApi>("Workers");
        APISET().AddClass<0,NativeExecutionsApi>("Executions");
        APISET().AddClass<0,NativeWorkerLinksApi>("WorkerLinks");
        APISET().AddClass<0,NativePlanningApi>("Planning");
        APISET().AddClass<1,SessionsApi,RootApi>("Sessions");
        APISET().AddClass<1,RunsApi,RootApi>("Runs");
    END_PACKAGE
};
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
