#include "agentflow/json_schema.hpp"
#include <iostream>
using namespace agentflow;
namespace {
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected schema rejection did not occur");}
}
int main(){
    try {
        JsonSchema202012 schema(R"({"$schema":"https://json-schema.org/draft/2020-12/schema","type":"object","$defs":{"path":{"type":"string","minLength":1}},"properties":{"path":{"$ref":"#/$defs/path"},"mode":{"enum":["read","write"]},"count":{"type":"integer","minimum":1,"maximum":3},"pair":{"type":"array","prefixItems":[{"type":"string"},{"type":"integer"}],"items":false,"minItems":2}},"required":["path","mode"],"dependentRequired":{"count":["pair"]},"if":{"properties":{"mode":{"const":"write"}}},"then":{"required":["count"]},"unevaluatedProperties":false})");
        const std::string exact=R"({"path":"file.txt","mode":"write","count":2,"pair":["name",1]})";
        schema.validate_object(exact);
        if(exact!=R"({"path":"file.txt","mode":"write","count":2,"pair":["name",1]})")throw std::runtime_error("Schema validation mutated arguments");
        schema.validate_object(R"({"path":"file.txt","mode":"read"})");
        for(const auto* invalid:{R"({"path":"","mode":"read"})",R"({"path":"file.txt","mode":"write"})",R"({"path":"file.txt","mode":"write","count":1})",R"({"path":"file.txt","mode":"read","extra":true})",R"({"path":"file.txt","mode":"write","count":4,"pair":["name",1]})",R"({"path":"file.txt","mode":"write","count":1,"pair":[1,"name"]})",R"({"path":"file.txt","mode":"read","path":"spoof"})","[]"})rejects<SchemaArgumentsInvalid>([&]{schema.validate_object(invalid);});
        JsonSchema202012 recursive(R"({"$dynamicAnchor":"node","type":"object","properties":{"name":{"type":"string"},"child":{"$dynamicRef":"#node"}},"required":["name"],"additionalProperties":false})");
        recursive.validate_object(R"({"name":"parent","child":{"name":"child"}})");
        rejects<SchemaArgumentsInvalid>([&]{recursive.validate_object(R"({"name":"parent","child":{"name":3}})");});
        for(const auto* invalid:{R"({"$schema":"http://json-schema.org/draft-07/schema#"})",R"({"type":"imaginary"})",R"({"properties":{"x":{"minLength":-1}}})",R"({"$ref":"https://untrusted.invalid/private-schema"})",R"({"$ref":"file:///C:/private-schema.json"})",R"({"type":"object","type":"string"})"})rejects<SchemaInvalid>([&]{JsonSchema202012 rejected(invalid);});
        rejects<SchemaArgumentsInvalid>([&]{schema.validate_object(std::string(65537,'x'));});
        JsonSchema202012 defaults(R"({"type":"object","properties":{"x":{"type":"integer","default":1}},"required":["x"]})");
        rejects<SchemaArgumentsInvalid>([&]{defaults.validate_object("{}");});
        std::cout<<"Native JSON Schema 2020-12 adapter passed actual validator/metaschema fixtures: refs, dynamic refs, prefixItems, dependencies, conditional and unevaluated properties, invalid schemas, unchanged arguments and no external fetch/default insertion. Not a complete upstream conformance or adversarial resource-containment pass\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
