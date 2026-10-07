#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace agentflow {
// Move-only plaintext with explicit byte access; never a public metadata value.
// Wipes its owned bytes on release. Callers must manage their own input copies.
class SecretBytes {
public:
    explicit SecretBytes(std::span<const std::uint8_t> input);
    ~SecretBytes();
    SecretBytes(const SecretBytes&)=delete;
    SecretBytes& operator=(const SecretBytes&)=delete;
    SecretBytes(SecretBytes&& other) noexcept;
    SecretBytes& operator=(SecretBytes&& other) noexcept;
    std::span<const std::uint8_t> view() const;
    void clear() noexcept;
private:
    std::vector<std::uint8_t> bytes_;
};
struct ProtectedSecret {
    std::string protection;
    std::vector<std::uint8_t> ciphertext;
};
ProtectedSecret protect_secret(const SecretBytes& secret,const std::string& context);
SecretBytes reveal_secret(const ProtectedSecret& secret,const std::string& context);
}
