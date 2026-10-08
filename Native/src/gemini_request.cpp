#include "agentflow/gemini_request.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
std::string quoted(const std::string& value){try{return Json(value).dump();}catch(const Json::exception&){throw std::invalid_argument("Invalid Gemini UTF-8 text");}}
void name(const std::string& value){if(value.empty()||value.size()>128||value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::invalid_argument("Invalid Gemini function name");}
void identity(const std::string& value){if(value.empty()||value.size()>256||value.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid Gemini call identity");}
std::string object(const std::string& source){
    if(source.size()>1024*1024)throw std::invalid_argument("Gemini JSON object exceeds limits");
    std::vector<std::set<std::string>> fields;
    try{
        const auto parsed=Json::parse(source,[&](int depth,Json::parse_event_t event,Json& value){
            if(depth>16)throw std::invalid_argument("Gemini JSON nesting exceeds limits");
            if(event==Json::parse_event_t::object_start)fields.emplace_back();
            else if(event==Json::parse_event_t::object_end)fields.pop_back();
            else if(event==Json::parse_event_t::key&&!fields.back().insert(value.get<std::string>()).second)throw std::invalid_argument("Duplicate Gemini JSON field");
            return true;
        });
        if(parsed.is_object())return source; // Preserve original numeric tokens.
    }catch(const Json::exception&){}
    throw std::invalid_argument("Gemini requires a valid JSON object");
}
}
std::string serialize_gemini_request(const GeminiRequest& request){
    if(request.contents.empty()||request.contents.size()>4096||request.system_instructions.size()>128||request.tools.size()>64)throw std::invalid_argument("Gemini request exceeds limits");
    struct Pending {std::string name;std::optional<std::string> id;};
    std::vector<Pending> pending;std::set<std::string> seen_ids,names;std::size_t bytes=0;
    auto account=[&](std::size_t size){if(size>8*1024*1024-bytes)throw std::invalid_argument("Gemini input exceeds limits");bytes+=size;};
    auto text=[&](const std::string& value){if(value.empty()||value.size()>4*1024*1024)throw std::invalid_argument("Gemini text exceeds limits");account(value.size());return quoted(value);};
    auto functions=[&]{if(request.function_calls!=Capability::supported)throw std::invalid_argument("Gemini function capability is unknown or unsupported");};
    std::string body="{\"contents\":[";bool first_content=true;
    for(const auto& content:request.contents){
        if(content.role!=GeminiRole::user&&content.role!=GeminiRole::model)throw std::invalid_argument("Invalid Gemini content role");
        if(content.parts.empty()||content.parts.size()>128)throw std::invalid_argument("Gemini content parts exceed limits");
        const bool answering=!pending.empty();if(answering&&content.role!=GeminiRole::user)throw std::invalid_argument("Gemini function results must precede model continuation");
        if(!first_content)body+=',';first_content=false;
        body+="{\"role\":"+quoted(content.role==GeminiRole::user?"user":"model")+",\"parts\":[";bool first_part=true;
        for(const auto& part:content.parts){
            if(!first_part)body+=',';first_part=false;std::string encoded;
            if(part.kind==GeminiPartKind::text){
                if(answering||!part.name.empty()||part.call_id||part.object_json!="{}")throw std::invalid_argument("Invalid Gemini text part");
                encoded="{\"text\":"+text(part.text);
            }else{
                functions();name(part.name);account(part.name.size());account(part.object_json.size());const auto payload=object(part.object_json);
                if(!part.text.empty())throw std::invalid_argument("Function part cannot also carry text");
                if(part.call_id){identity(*part.call_id);account(part.call_id->size());}
                if(part.kind==GeminiPartKind::function_call){
                    if(content.role!=GeminiRole::model||answering)throw std::invalid_argument("Gemini function call requires a model turn");
                    if(part.call_id&&!seen_ids.insert(*part.call_id).second)throw std::invalid_argument("Duplicate Gemini call identity");
                    pending.push_back({part.name,part.call_id});encoded="{\"functionCall\":{\"name\":"+quoted(part.name)+",\"args\":"+payload;
                }else if(part.kind==GeminiPartKind::function_response){
                    if(content.role!=GeminiRole::user||!answering||part.thought_signature)throw std::invalid_argument("Gemini function response requires a pending user turn");
                    const auto found=std::find_if(pending.begin(),pending.end(),[&](const auto& call){return call.name==part.name&&call.id==part.call_id;});
                    if(found==pending.end())throw std::invalid_argument("Gemini function response does not match a pending call");
                    pending.erase(found);encoded="{\"functionResponse\":{\"name\":"+quoted(part.name)+",\"response\":"+payload;
                }else throw std::invalid_argument("Invalid Gemini part kind");
                if(part.call_id)encoded+=",\"id\":"+quoted(*part.call_id);encoded+='}';
            }
            if(part.thought_signature){
                if(content.role!=GeminiRole::model||part.thought_signature->empty()||part.thought_signature->size()>65536||part.thought_signature->find('\0')!=std::string::npos)throw std::invalid_argument("Invalid Gemini thought signature");
                account(part.thought_signature->size());encoded+=",\"thoughtSignature\":"+quoted(*part.thought_signature);
            }
            if(part.thought){
                if(content.role!=GeminiRole::model)throw std::invalid_argument("Thought metadata requires a model part");
                encoded+=*part.thought?",\"thought\":true":",\"thought\":false";
            }
            encoded+='}';body+=encoded;
        }
        if(answering&&!pending.empty())throw std::invalid_argument("Gemini continuation requires all function results in one user turn");
        body+="]}";
    }
    if(!pending.empty())throw std::invalid_argument("Gemini continuation requires all function results");body+=']';
    if(!request.system_instructions.empty()){
        body+=",\"systemInstruction\":{\"parts\":[";bool first=true;for(const auto& instruction:request.system_instructions){if(!first)body+=',';first=false;body+="{\"text\":"+text(instruction)+'}';}body+="]}";
    }
    if(!request.tools.empty()){
        functions();body+=",\"tools\":[{\"functionDeclarations\":[";bool first=true;
        for(const auto& tool:request.tools){name(tool.name);if(!names.insert(tool.name).second||tool.description.size()>65536)throw std::invalid_argument("Invalid or duplicate Gemini function declaration");account(tool.name.size());account(tool.description.size());account(tool.input_schema_json.size());if(!first)body+=',';first=false;body+="{\"name\":"+quoted(tool.name)+",\"description\":"+quoted(tool.description)+",\"parametersJsonSchema\":"+object(tool.input_schema_json)+'}';}
        body+="]}]";
    }
    if(request.max_output_tokens){if(*request.max_output_tokens<1||*request.max_output_tokens>2147483647)throw std::invalid_argument("Invalid Gemini output token limit");body+=",\"generationConfig\":{\"maxOutputTokens\":"+std::to_string(*request.max_output_tokens)+'}';}
    body+='}';if(body.size()>8*1024*1024)throw std::invalid_argument("Gemini serialized request exceeds limits");return body;
}
}
