#pragma once
#include "agentflow/run_executor.hpp"
#include "nlohmann/json.hpp"
namespace agentflow::prompt_input {
using Json=nlohmann::json;
inline std::vector<PromptFileAttachment> prompt_file_attachments(const Json& value){
    std::vector<PromptFileAttachment> result;if(!value.contains("files"))return result;
    const auto& files=value.at("files");if(!files.is_array()||files.size()>32)throw std::invalid_argument("Prompt files must be an array of at most 32 workspace files");
    result.reserve(files.size());for(const auto& file:files){
        if(!file.is_object()||file.empty()||file.size()>5)throw std::invalid_argument("Invalid prompt file attachment");
        for(auto it=file.begin();it!=file.end();++it)if(it.key()!="path"&&it.key()!="folder_id"&&it.key()!="name"&&it.key()!="description"&&it.key()!="mention")throw std::invalid_argument("Unknown prompt file attachment field");
        const auto field=[&](const char* key,std::size_t limit){if(!file.contains(key))return std::string{};if(!file.at(key).is_string())throw std::invalid_argument("Invalid prompt file attachment metadata");const auto text=file.at(key).get<std::string>();if(text.size()>limit||text.find('\0')!=std::string::npos)throw std::invalid_argument("Prompt file attachment metadata exceeds limits");return text;};
        PromptFileAttachment parsed;parsed.path=field("path",4096);if(parsed.path.empty())throw std::invalid_argument("Prompt file attachment path is required");
        parsed.folder_id=field("folder_id",256);parsed.name=field("name",512);parsed.description=field("description",4096);parsed.mention=field("mention",512);result.push_back(std::move(parsed));
    }return result;
}
}
