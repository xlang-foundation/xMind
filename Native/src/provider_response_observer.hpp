#pragma once
#include "agentflow/model_provider.hpp"
#include "nlohmann/json.hpp"
#include <utility>

namespace agentflow {
inline HttpStreamRequest observed_provider_request(HttpStreamRequest request,ChatCompletionStream::Sink sink){
    request.observe_provider_request=[sink=std::move(sink)](int status,std::string_view id){
        sink({"model.http_response",nlohmann::json{{"status",status},{"request_id",std::string(id)}}.dump()});
    };
    return request;
}
}
