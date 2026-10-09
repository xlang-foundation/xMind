#pragma once
#include <memory>
#include <stdexcept>
#include <string>

namespace agentflow {
struct RuntimeGenerationDenied : std::runtime_error {using std::runtime_error::runtime_error;};
struct RuntimeGenerationBinding {
    std::string root,root_identity,manifest_sha256,native_revision,sdk_revision,
        source_manifest_sha256,server_sha256;
    bool operator==(const RuntimeGenerationBinding&)const=default;
};
// Owner-controlled deployment input, never a model tool. Windows file handles
// pin the exact verified inventory against writes/deletion for this lifetime.
// Integrity does not establish that a package passed its separate native gate;
// the operator must supply the independently accepted manifest digest.
class VerifiedRuntimeGeneration {
public:
    VerifiedRuntimeGeneration(const std::string& root,const std::string& manifest_sha256,
        const std::string& excluded_workspace_root={});
    ~VerifiedRuntimeGeneration();
    VerifiedRuntimeGeneration(VerifiedRuntimeGeneration&&) noexcept;
    VerifiedRuntimeGeneration& operator=(VerifiedRuntimeGeneration&&) noexcept;
    VerifiedRuntimeGeneration(const VerifiedRuntimeGeneration&)=delete;
    VerifiedRuntimeGeneration& operator=(const VerifiedRuntimeGeneration&)=delete;
    const RuntimeGenerationBinding& binding()const;
    void revalidate()const;
    // Require the loaded process image to be this inventory's xmind.exe,
    // using actual OS file identity rather than a caller-provided executable name.
    void require_current_server()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
