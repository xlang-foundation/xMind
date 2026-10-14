#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace agentflow {
struct SessionModelSelection {
    std::string model_id;
    std::string provider_profile_id;
    std::string provider_route_id;
};
struct SessionModelState {
    std::optional<SessionModelSelection> selected;
    std::int64_t revision=0;
    bool editable=false;
};
}
