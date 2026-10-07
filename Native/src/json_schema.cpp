#include "agentflow/json_schema.hpp"
#include "agentflow/mcp_wire.hpp"
#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonschema/jsonschema.hpp>
#include <regex>

namespace agentflow {
namespace {
using Json=jsoncons::json;
namespace Schema=jsoncons::jsonschema;
bool resource_failure(const std::regex_error& error) {
    return error.code()==std::regex_constants::error_space || error.code()==std::regex_constants::error_stack || error.code()==std::regex_constants::error_complexity;
}
Schema::evaluation_options options(const std::string& version=Schema::schema_version::draft202012()) {
    Schema::evaluation_options result;
    result.default_version(version);
    result.default_base_uri("https://xmind.invalid/mcp/tool-schema");
    return result;
}
Schema::json_schema<Json> compile(const std::string& source) {
    if(source.empty() || source.size()>256*1024)throw SchemaInvalid("MCP schema exceeds byte limits");
    try {
        const auto compact=mcp_compact_object(source);
        auto value=Json::parse(compact,jsoncons::json_options{}.max_nesting_depth(64));
        auto version=Schema::schema_version::draft202012();
        if(value.contains("$schema")){
            if(!value["$schema"].is_string())throw SchemaInvalid("Invalid MCP schema dialect");
            version=value["$schema"].as<std::string>();
            if(version!=Schema::schema_version::draft202012() && version!=Schema::schema_version::draft7())throw SchemaInvalid("Unsupported MCP schema dialect");
        }
        static const auto meta2020=Schema::make_json_schema(Schema::draft202012::schema_draft202012<Json>::get_schema(),options());
        static const auto meta7=Schema::make_json_schema(Schema::draft7::schema_draft7<Json>::get_schema(),options(Schema::schema_version::draft7()));
        if(!(version==Schema::schema_version::draft7()?meta7:meta2020).is_valid(value))throw SchemaInvalid("Invalid JSON Schema document for its declared dialect");
        // The default resolver contains only the library's built-in metaschemas.
        // Unresolved application refs fail compilation; no I/O resolver exists.
        return Schema::make_json_schema(std::move(value),options(version));
    }catch(const SchemaInvalid&){throw;}
    catch(const McpProtocolError&){throw SchemaInvalid("Invalid MCP schema JSON");}
    catch(const Schema::schema_error&){throw SchemaInvalid("MCP schema is invalid or has unresolved references");}
    catch(const jsoncons::ser_error&){throw SchemaInvalid("Invalid MCP schema JSON");}
    catch(const std::regex_error& error){if(resource_failure(error))throw;throw SchemaInvalid("Invalid MCP schema regular expression");}
    // Allocation/unexpected evaluation faults must reach the isolated worker
    // failure boundary; they do not prove that a schema is invalid.
}
}
struct JsonSchema::Impl {
    std::string source;
    Schema::json_schema<Json> compiled;
    explicit Impl(std::string value):source(std::move(value)),compiled(compile(source)) {}
};
JsonSchema::JsonSchema(std::string source):impl_(std::make_unique<Impl>(std::move(source))) {}
JsonSchema::~JsonSchema()=default;
void JsonSchema::validate_object(std::string_view instance) const {
    if(instance.empty() || instance.size()>65536)throw SchemaArgumentsInvalid("MCP arguments exceed byte limits");
    try {
        const auto compact=mcp_compact_object(instance);
        const auto value=Json::parse(compact,jsoncons::json_options{}.max_nesting_depth(64));
        if(!impl_->compiled.is_valid(value))throw SchemaArgumentsInvalid("Arguments do not match the MCP input schema");
    }catch(const SchemaArgumentsInvalid&){throw;}
    catch(const McpProtocolError&){throw SchemaArgumentsInvalid("Invalid MCP argument JSON");}
    catch(const jsoncons::ser_error&){throw SchemaArgumentsInvalid("Invalid MCP argument JSON");}
}
const std::string& JsonSchema::source() const {return impl_->source;}
}
