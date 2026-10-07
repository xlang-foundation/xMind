#include "agentflow/provider_setup.hpp"
#include "agentflow/mcp_wire.hpp"
#include "nlohmann/json.hpp"
#include <mutex>
#include <random>
#include <sstream>
#include <iomanip>
#include <array>
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
namespace agentflow {
namespace {
using Json=nlohmann::json;
void model_id(const std::string& model){if(model.empty() || model.size()>256 || model.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos)throw std::invalid_argument("Invalid provider model identity");}
std::string identifier(){std::random_device random;std::ostringstream value;value<<"setup-"<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)value<<std::setw(8)<<random();return value.str();}
std::string purpose(const std::string& endpoint){BCRYPT_ALG_HANDLE algorithm=nullptr;std::array<UCHAR,32> hash{};if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw DatabaseError("Cannot bind provider setup credential");const auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(endpoint.data())),static_cast<ULONG>(endpoint.size()),hash.data(),static_cast<ULONG>(hash.size()));BCryptCloseAlgorithmProvider(algorithm,0);if(status<0)throw DatabaseError("Cannot bind provider setup credential");std::ostringstream result;result<<"provider:setup:"<<std::hex<<std::setfill('0');for(auto byte:hash)result<<std::setw(2)<<static_cast<unsigned>(byte);return result.str();}
}
struct ProviderRuntime::Impl {
    PersistenceService& store;AgentSettings base;std::size_t workers,capacity;std::string endpoint,binding;mutable std::mutex mutex;ProviderSetupMetadata metadata;std::unique_ptr<AgentService> service;
    Impl(PersistenceService& persistence,AgentSettings settings,std::size_t count,std::size_t limit,std::string destination):store(persistence),base(std::move(settings)),workers(count),capacity(limit),endpoint(std::move(destination)),binding(purpose(endpoint)){
        if(count<1 || count>16 || limit<1 || limit>4096)throw std::invalid_argument("Invalid provider worker capacity");
        if(endpoint.empty() || endpoint.size()>8192)throw std::invalid_argument("Invalid provider setup endpoint");metadata.provider="openai";metadata.endpoint=endpoint;
        std::string source;try{source=store.information("native-provider","active").get();}catch(const NotFound&){return;}
        try{
            const auto value=Json::parse(mcp_compact_object(source));if(value.size()!=5 || value.at("provider")!="openai" || value.at("endpoint")!=endpoint || !value.at("revision").is_number_integer())throw DatabaseError("Stored provider setup differs from backend policy");const auto revision=value.at("revision").get<std::int64_t>();if(revision<1 || revision>9007199254740991)throw DatabaseError("Invalid stored provider revision");const auto model=value.at("model").get<std::string>(),id=value.at("credential_id").get<std::string>();model_id(model);auto secret=store.resolve_credential("server",id,binding).get();auto configured=configuration(model,id);service=std::make_unique<AgentService>(store,std::move(configured),workers,capacity);metadata={revision,"openai",model,endpoint,true};
        }catch(const DatabaseError&){throw;}catch(...){throw DatabaseError("Stored provider setup is unavailable or invalid");}
    }
    AgentSettings configuration(const std::string& model,const std::string& id){auto configured=base;configured.provider.model=model;configured.provider.endpoint=endpoint;configured.provider.tools=Capability::supported;configured.provider.stream_usage=Capability::supported;configured.selectable_models.clear();configured.credential=CredentialReference{"server",id,binding};return configured;}
};
ProviderRuntime::ProviderRuntime(PersistenceService& store,AgentSettings base,std::size_t count,std::size_t capacity,std::string endpoint):impl_(std::make_unique<Impl>(store,std::move(base),count,capacity,std::move(endpoint))){}
ProviderRuntime::~ProviderRuntime()=default;
ProviderSetupMetadata ProviderRuntime::configuration() const{std::lock_guard lock(impl_->mutex);return impl_->metadata;}
ProviderSetupMetadata ProviderRuntime::configure(std::string model,SecretBytes secret,std::int64_t expected){
    model_id(model);if(secret.view().empty() || secret.view().size()>32768)throw std::invalid_argument("Provider key exceeds limits");for(auto byte:secret.view())if(byte<33 || byte>126)throw std::invalid_argument("Provider key must contain printable bytes without spaces");
    std::unique_lock lock(impl_->mutex);if(expected<0 || expected!=impl_->metadata.revision)throw Conflict("Provider configuration revision changed");if(impl_->metadata.revision>=9007199254740991)throw std::overflow_error("Provider setup revision exhausted");if(impl_->service && !impl_->service->idle())throw Conflict("Wait for active runs before changing provider configuration");
    const auto id=identifier();const auto revision=impl_->metadata.revision+1;impl_->store.put_credential("server",id,impl_->binding,"OpenAI model provider",std::move(secret),0).get();
    // Use a new credential identity per candidate. Failed setup never rotates
    // the active credential; an unreferenced encrypted candidate is retained.
    auto candidate=std::make_unique<AgentService>(impl_->store,impl_->configuration(model,id),impl_->workers,impl_->capacity);
    ProviderSetupMetadata next{revision,"openai",model,impl_->endpoint,true};const auto result=next;
    impl_->store.put_information("native-provider","active",Json{{"provider","openai"},{"endpoint",impl_->endpoint},{"model",model},{"credential_id",id},{"revision",revision}}.dump()).get();
    auto previous=std::move(impl_->service);impl_->service=std::move(candidate);impl_->metadata=std::move(next);lock.unlock();previous.reset();return result;
}
Run ProviderRuntime::submit(std::string id,std::string session,std::string prompt){return submit_model(std::move(id),std::move(session),std::move(prompt),{});}
Run ProviderRuntime::submit_model(std::string id,std::string session,std::string prompt,std::string model){std::lock_guard lock(impl_->mutex);if(!impl_->service)throw RunUnavailable("Configure a provider before running an agent");return impl_->service->submit_model(std::move(id),std::move(session),std::move(prompt),std::move(model));}
std::vector<std::string> ProviderRuntime::models() const{std::lock_guard lock(impl_->mutex);return impl_->service?impl_->service->models():std::vector<std::string>{};}
void ProviderRuntime::cancel(const std::string& id){std::lock_guard lock(impl_->mutex);if(!impl_->service)throw RunUnavailable("Provider is not configured");impl_->service->cancel(id);}
bool ProviderRuntime::healthy() const{std::lock_guard lock(impl_->mutex);return !impl_->service || impl_->service->healthy();}
bool ProviderRuntime::available() const{std::lock_guard lock(impl_->mutex);return static_cast<bool>(impl_->service);}
}
