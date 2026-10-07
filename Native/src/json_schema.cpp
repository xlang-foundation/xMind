#include "agentflow/json_schema.hpp"
#include "agentflow/mcp_wire.hpp"
#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonschema/jsonschema.hpp>

namespace agentflow {
namespace {
using Json=jsoncons::json;
namespace Schema=jsoncons::jsonschema;
Schema::evaluation_options options() {
    Schema::evaluation_options result;
    result.default_version(Schema::schema_version::draft202012());
    result.default_base_uri("https://xmind.invalid/mcp/tool-schema");
    return result;
}
Schema::json_schema<Json> compile(const std::string& source) {
    if(source.empty() || source.size()>256*1024)throw SchemaInvalid("MCP schema exceeds byte limits");
    try {
        const auto compact=mcp_compact_object(source);
        auto value=Json::parse(compact,jsoncons::json_options{}.max_nesting_depth(64));
        if(value.contains("$schema") && (!value["$schema"].is_string() || value["$schema"].as<std::string>()!=Schema::schema_version::draft202012()))throw SchemaInvalid("MCP schema must declare JSON Schema 2020-12");
        static const auto meta=Schema::make_json_schema(Schema::draft202012::schema_draft202012<Json>::get_schema(),options());
        if(!meta.is_valid(value))throw SchemaInvalid("Invalid JSON Schema 2020-12 document");
        // The default resolver contains only the library's built-in metaschemas.
        // Unresolved application refs fail compilation; no I/O resolver exists.
        return Schema::make_json_schema(std::move(value),options());
    }catch(const SchemaInvalid&){throw;}
    catch(const std::exception&){throw SchemaInvalid("MCP schema is invalid or has unresolved references");}
}
}
struct JsonSchema202012::Impl {
    std::string source;
    Schema::json_schema<Json> compiled;
    explicit Impl(std::string value):source(std::move(value)),compiled(compile(source)) {}
};
JsonSchema202012::JsonSchema202012(std::string source):impl_(std::make_unique<Impl>(std::move(source))) {}
JsonSchema202012::~JsonSchema202012()=default;
void JsonSchema202012::validate_object(std::string_view instance) const {
    if(instance.empty() || instance.size()>65536)throw SchemaArgumentsInvalid("MCP arguments exceed byte limits");
    try {
        const auto compact=mcp_compact_object(instance);
        const auto value=Json::parse(compact,jsoncons::json_options{}.max_nesting_depth(64));
        if(!impl_->compiled.is_valid(value))throw SchemaArgumentsInvalid("Arguments do not match the MCP input schema");
    }catch(const SchemaArgumentsInvalid&){throw;}
    catch(const std::exception&){throw SchemaArgumentsInvalid("Invalid MCP arguments or schema evaluation failure");}
}
const std::string& JsonSchema202012::source() const {return impl_->source;}
}
