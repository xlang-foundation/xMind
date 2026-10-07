#include "agentflow/secret_protection.hpp"
#include <stdexcept>
#include <system_error>
#include <utility>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <dpapi.h>
#endif

namespace agentflow {
SecretBytes::SecretBytes(std::span<const std::uint8_t> input):bytes_(input.begin(),input.end()) {}
SecretBytes::~SecretBytes() {clear();}
SecretBytes::SecretBytes(SecretBytes&& other) noexcept:bytes_(std::move(other.bytes_)) {}
SecretBytes& SecretBytes::operator=(SecretBytes&& other) noexcept {
    if(this!=&other) {clear();bytes_=std::move(other.bytes_);}return *this;
}
std::span<const std::uint8_t> SecretBytes::view() const {return bytes_;}
void SecretBytes::clear() noexcept {
    volatile std::uint8_t* bytes=bytes_.data();
    for(std::size_t i=0;i<bytes_.size();++i) bytes[i]=0;
    bytes_.clear();
}
#if defined(_WIN32)
namespace {
constexpr std::size_t max_secret_size=1024*1024;
void valid_context(const std::string& context) {
    if(context.empty() || context.size()>65536) throw std::invalid_argument("Invalid secret context");
}
struct NativeOutput {
    DATA_BLOB blob{};
    ~NativeOutput() {
        if(blob.pbData) {SecureZeroMemory(blob.pbData,blob.cbData);LocalFree(blob.pbData);}
    }
};
DATA_BLOB input_blob(std::span<const std::uint8_t> value,std::size_t limit=max_secret_size) {
    if(value.size()>limit) throw std::length_error("Secret payload exceeds its size limit");
    return {static_cast<DWORD>(value.size()),const_cast<BYTE*>(value.data())};
}
DATA_BLOB entropy_blob(const std::string& context) {
    return {static_cast<DWORD>(context.size()),reinterpret_cast<BYTE*>(const_cast<char*>(context.data()))};
}
}
#endif
ProtectedSecret protect_secret(const SecretBytes& secret,const std::string& context) {
#if defined(_WIN32)
    valid_context(context);
    if(secret.view().empty()) throw std::invalid_argument("Secret must not be empty");
    auto input=input_blob(secret.view());auto entropy=entropy_blob(context);NativeOutput output;
    if(!CryptProtectData(&input,L"xMind credential v1",&entropy,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output.blob))
        throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"Credential protection failed");
    return {"windows-dpapi-user-v1",{output.blob.pbData,output.blob.pbData+output.blob.cbData}};
#else
    (void)secret;(void)context;
    throw std::runtime_error("A secret-protection provider is required for this OS");
#endif
}
SecretBytes reveal_secret(const ProtectedSecret& secret,const std::string& context) {
#if defined(_WIN32)
    valid_context(context);
    if(secret.protection!="windows-dpapi-user-v1") throw std::invalid_argument("Unsupported secret protection");
    if(secret.ciphertext.empty()) throw std::invalid_argument("Encrypted secret must not be empty");
    auto input=input_blob(secret.ciphertext,max_secret_size+65536);auto entropy=entropy_blob(context);NativeOutput output;
    if(!CryptUnprotectData(&input,nullptr,&entropy,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output.blob))
        throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"Credential decryption failed");
    if(output.blob.cbData>max_secret_size) throw std::length_error("Decrypted secret exceeds its size limit");
    return SecretBytes({output.blob.pbData,output.blob.cbData});
#else
    (void)secret;(void)context;
    throw std::runtime_error("A secret-protection provider is required for this OS");
#endif
}
}
