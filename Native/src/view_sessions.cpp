#include "agentflow/view_sessions.hpp"
#include "nlohmann/json.hpp"
#include <array>
#include <charconv>
#include <algorithm>
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr auto scope="local-view-sessions";
std::int64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::string hex(std::span<const unsigned char> bytes){constexpr char digits[]="0123456789abcdef";std::string out;out.reserve(bytes.size()*2);for(auto byte:bytes){out+=digits[byte>>4];out+=digits[byte&15];}return out;}
std::string random_token(){std::array<unsigned char,32> bytes{};if(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw DatabaseError("Cannot create view credential");return hex(bytes);}
std::string binding(std::string_view authority){
    if(authority.size()<32||authority.size()>256)throw std::invalid_argument("Invalid view authority");
    for(unsigned char byte:authority)if(byte<33||byte>126)throw std::invalid_argument("Invalid view authority bytes");
    BCRYPT_ALG_HANDLE algorithm=nullptr;std::array<unsigned char,32> hash{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw DatabaseError("Cannot bind view authority");
    const auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(authority.data())),static_cast<ULONG>(authority.size()),hash.data(),static_cast<ULONG>(hash.size()));BCryptCloseAlgorithmProvider(algorithm,0);
    if(status<0)throw DatabaseError("Cannot bind view authority");return "view:"+hex(hash);
}
void check_origin(const std::string& origin){
    constexpr std::string_view prefix="http://127.0.0.1:";
    if(!origin.starts_with(prefix))throw std::invalid_argument("View origin must be loopback");
    const auto port=std::string_view(origin).substr(prefix.size());unsigned value=0;const auto result=std::from_chars(port.data(),port.data()+port.size(),value);
    if(result.ec!=std::errc{}||result.ptr!=port.data()+port.size()||value<1||value>65535||std::to_string(value)!=port)throw std::invalid_argument("Invalid view origin port");
}
bool parse(std::string_view credential){return credential.size()==129 && credential[64]=='.' && credential.substr(0,64).find_first_not_of("0123456789abcdef")==std::string_view::npos && credential.substr(65).find_first_not_of("0123456789abcdef")==std::string_view::npos;}
bool secret_matches(const SecretBytes& expected,std::string_view supplied){auto bytes=expected.view();std::size_t difference=bytes.size()^supplied.size();for(std::size_t i=0;i<bytes.size();++i)difference|=bytes[i]^(i<supplied.size()?static_cast<unsigned char>(supplied[i]):0);return difference==0;}
bool accepted(PersistenceService& store,const std::string& purpose,std::string_view credential,const std::string& origin){
    if(!parse(credential))return false;
    const auto id=std::string(credential.substr(0,64));
    try{
        const auto record=Json::parse(store.information(scope,id).get());
        if(record.size()!=3||record.at("origin")!=origin||record.at("binding")!=purpose||!record.at("expires_unix_ms").is_number_integer()||record.at("expires_unix_ms").get<std::int64_t>()<=now())return false;
        const auto secret=store.resolve_credential(scope,id,purpose).get();return secret_matches(secret,credential.substr(65));
    }catch(const NotFound&){return false;}catch(const Json::exception&){return false;}
}
}
ViewSessions::ViewSessions(PersistenceService& store,std::string_view authority,std::chrono::seconds lifetime):store_(store),binding_(binding(authority)),lifetime_(lifetime){if(lifetime.count()<1||lifetime>std::chrono::hours(8))throw std::invalid_argument("Invalid view session lifetime");}
ViewSession ViewSessions::issue(const std::string& origin){
    check_origin(origin);std::lock_guard lock(mutex_);
    auto credentials=store_.credentials(scope).get();std::size_t active=0;
    for(const auto& metadata:credentials){
        bool keep=false;try{const auto record=Json::parse(store_.information(scope,metadata.id).get());keep=record.at("binding")==binding_ && record.at("expires_unix_ms").get<std::int64_t>()>now();}catch(const NotFound&){}catch(const Json::exception&){}
        if(keep)++active;else store_.delete_credential(scope,metadata.id,metadata.revision).get();
    }
    if(active>=32)throw Conflict("Too many active view sessions");
    const auto id=random_token();auto token=random_token();struct Wipe {std::string& text;~Wipe(){SecureZeroMemory(text.data(),text.size());}} wipe{token};
    const auto expires=now()+std::chrono::duration_cast<std::chrono::milliseconds>(lifetime_).count();
    store_.put_credential(scope,id,binding_,"Browser access session",SecretBytes(std::span(reinterpret_cast<const std::uint8_t*>(token.data()),token.size())),0).get();
    store_.put_information(scope,id,Json{{"origin",origin},{"binding",binding_},{"expires_unix_ms",expires}}.dump()).get();
    return {id+"."+token,expires};
}
bool ViewSessions::accepts(std::string_view credential,const std::string& origin){check_origin(origin);std::lock_guard lock(mutex_);return accepted(store_,binding_,credential,origin);}
void ViewSessions::revoke(std::string_view credential,const std::string& origin){
    check_origin(origin);std::lock_guard lock(mutex_);if(!accepted(store_,binding_,credential,origin))return;
    const auto id=std::string(credential.substr(0,64));for(const auto& metadata:store_.credentials(scope).get())if(metadata.id==id){store_.delete_credential(scope,id,metadata.revision).get();return;}
}
}
