#pragma once
#include "agentflow/provider_profiles.hpp"
#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace agentflow {
enum class ProviderYamlErrorCode {file_unavailable,exceeds_limits,invalid_utf8,invalid_yaml,
    unsupported_yaml,duplicate_field,invalid_schema,route_unavailable};
// Fixed diagnostics only. No parser exception, path, key, scalar or YAML
// source is incorporated in what() or the public error-code observation.
struct ProviderYamlError : std::runtime_error {
    explicit ProviderYamlError(ProviderYamlErrorCode code)
        : std::runtime_error("Provider YAML configuration rejected"), code_(code) {}
    ProviderYamlErrorCode code() const noexcept { return code_; }
private:
    ProviderYamlErrorCode code_;
};
struct OpenAICompatibleConfig {
    std::string base_url;
    Capability tools=Capability::unknown,stream_usage=Capability::unknown;
};
struct ProviderYamlConfig {
    std::vector<ProviderProfileConfigEntry> profiles;
    std::optional<std::string> active_profile;
    std::optional<OpenAICompatibleConfig> openai_compatible;
};
// Backend-only input; model/UI protocol cannot provide endpoints or policies.
// Trusted local configuration can register the OpenAI-compatible route.
// Plaintext is never serialized as a DTO. Caller source buffers remain caller
// owned; read() wipes its bounded input buffer on every exit.
ProviderYamlConfig parse_provider_yaml_config(std::string_view source,
    const std::vector<std::string>& allowed_routes);
ProviderYamlConfig read_provider_yaml_config(const std::filesystem::path& absolute_path,
    const std::vector<std::string>& allowed_routes);
}
