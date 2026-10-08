#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace agentflow {
// Public observations of native context work. Provider windows, responses,
// credentials, authority digests and private source manifests are never DTOs.
struct ContextManualStatus {
    std::string id,state;
};
struct ContextCompactionStatus {
    std::string id;
    std::int64_t provider_elapsed_ms=0;
    std::optional<std::int64_t> preparation_elapsed_ms;
    std::string actual_usage_json="null";
};
struct ContextControlSnapshot {
    std::string session_id,model_id;
    bool enabled=false,automatic=false;
    std::int64_t head_revision=0,source_watermark=0;
    std::optional<ContextManualStatus> manual;
    std::optional<ContextCompactionStatus> checkpoint;
};
}
