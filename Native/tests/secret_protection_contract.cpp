#include "agentflow/secret_protection.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <type_traits>

using namespace agentflow;
static_assert(!std::is_copy_constructible_v<SecretBytes>);
static_assert(!std::is_copy_assignable_v<SecretBytes>);
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class Function> void rejects(Function function) {
    try {function();} catch(const std::exception&) {return;} throw std::runtime_error("Expected secret rejection did not occur");
}
int main() {
    try {
        // Synthetic bytes only. No real credentials are read or printed.
        const std::vector<std::uint8_t> sample{0,1,2,255,'d','u','m','m','y',0};
        SecretBytes secret(sample);
        auto protected_value=protect_secret(secret,"xMind/provider-a/credential-1");
        require(protected_value.protection=="windows-dpapi-user-v1","Protection version missing");
        require(protected_value.ciphertext!=sample,"Ciphertext must not equal plaintext");
        auto revealed=reveal_secret(protected_value,"xMind/provider-a/credential-1");
        require(std::ranges::equal(revealed.view(),sample),"Secret round trip failed");
        rejects([&]{reveal_secret(protected_value,"xMind/provider-b/credential-1");});
        auto changed=protected_value;changed.ciphertext.back()^=1;
        rejects([&]{reveal_secret(changed,"xMind/provider-a/credential-1");});
        auto unknown=protected_value;unknown.protection="unknown";
        rejects([&]{reveal_secret(unknown,"xMind/provider-a/credential-1");});
        rejects([&]{protect_secret(secret,"");});
        SecretBytes empty(std::span<const std::uint8_t>{});
        rejects([&]{protect_secret(empty,"context");});
        revealed.clear();require(revealed.view().empty(),"Clear must remove plaintext access");
        SecretBytes moved(std::move(secret));require(std::ranges::equal(moved.view(),sample),"Move must preserve ownership");
        std::cout<<"Windows native secret-protection contracts passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
