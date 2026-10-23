#include "agentflow/schema_worker_service.hpp"
#include "agentflow/xmind_ipc_api.hpp"
#include "agentflow/xmind_ipc_runtime.hpp"
#include <array>
#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#define NOMINMAX
#include <windows.h>
namespace agentflow {
namespace {
struct State {
    std::array<unsigned char,32> capability{};
    SchemaWorkerEvaluator evaluate;
    std::atomic<bool> started=false;
    ~State(){SecureZeroMemory(capability.data(),capability.size());}
};
std::mutex binding_mutex;
std::shared_ptr<State> binding;
std::string key(const std::array<unsigned char,32>& bytes){static constexpr char digits[]="0123456789abcdef";std::string result;for(auto b:bytes){result+=digits[b>>4];result+=digits[b&15];}return result;}
struct Validator {
    std::shared_ptr<State> state;
    Validator()=default;
    explicit Validator(std::shared_ptr<State> value):state(std::move(value)){}
    X::Value Identity(){if(!state)throw std::runtime_error("Unbound schema validator");auto value=X::Value::Dict(Host());value.Set("pid",X::Value(static_cast<std::int64_t>(GetCurrentProcessId())));value.Set("version",X::Value(1));return value;}
    std::int64_t Validate(std::string schema,bool has_instance,std::string instance){
        if(!state||schema.empty()||schema.size()>256*1024||(has_instance?(instance.empty()||instance.size()>65536):!instance.empty()))throw std::invalid_argument("Invalid schema validation input");
        if(state->started.exchange(true))throw std::runtime_error("Schema validation cannot be replayed");
        return static_cast<std::int64_t>(state->evaluate(schema,has_instance,instance));
    }
    BEGIN_PACKAGE(Validator)
        APISET().AddFunc<0>("Identity",&Validator::Identity);
        APISET().AddFunc<3>("Validate",&Validator::Validate);
    END_PACKAGE
};
struct Objects {BEGIN_PACKAGE(Objects) APISET().AddClass<0,Validator>("Validator"); END_PACKAGE};
Objects& objects(){static auto value=[] {auto owner=std::make_shared<Objects>();retain_xmind_ipc_service(owner);(void)xmind_ipc_runtime().RegisterPackage("_xmind_schema_objects",*owner);return owner;}();return *value;}
}
X::Value SchemasApi::Open(std::string capability){std::shared_ptr<State> owner;{std::lock_guard lock(binding_mutex);owner=binding;}if(!owner||capability.size()!=64||capability!=key(owner->capability))throw std::invalid_argument("Schema capability unavailable");return objects().__xlang3_package_->CreateInstance("Validator",std::make_shared<Validator>(std::move(owner)));}
int run_schema_worker_host(SchemaWorkerEvaluator evaluate){
    if(!evaluate)return 2;
    auto owner=std::make_shared<State>();owner->evaluate=std::move(evaluate);
    const auto input=GetStdHandle(STD_INPUT_HANDLE);std::size_t offset=0;
    while(offset<owner->capability.size()){DWORD count=0;if(!ReadFile(input,owner->capability.data()+offset,static_cast<DWORD>(owner->capability.size()-offset),&count,nullptr)||!count)return 2;offset+=count;}
    {std::lock_guard lock(binding_mutex);if(binding)return 2;binding=owner;}
    (void)objects();ensure_xmind_ipc_api();
    std::cout<<"lrpc:"<<xmind_ipc_port()<<'\n';std::cout.flush();if(!std::cout)return 2;
    // Pipe is startup capability/endpoint and parent lifetime only.
    unsigned char extra=0;DWORD count=0;
    const auto ended=!ReadFile(input,&extra,1,&count,nullptr)||count==0;
    return ended?0:2;
}
}
