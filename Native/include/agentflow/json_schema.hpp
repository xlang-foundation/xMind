#pragma once
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace agentflow {
struct SchemaInvalid : std::invalid_argument {using std::invalid_argument::invalid_argument;};
struct SchemaArgumentsInvalid : std::invalid_argument {using std::invalid_argument::invalid_argument;};
// Native JSON Schema adapter: 2020-12 default, explicit Draft-07 for legacy
// peers. Each dialect retains its own semantics. No network/file fetches, default
// insertion or argument normalization. One owner; execution containment for
// adversarial schema evaluation remains an access-worker responsibility.
class JsonSchema {
public:
    explicit JsonSchema(std::string source);
    ~JsonSchema();
    JsonSchema(const JsonSchema&)=delete;
    JsonSchema& operator=(const JsonSchema&)=delete;
    void validate_object(std::string_view instance) const;
    const std::string& source() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
