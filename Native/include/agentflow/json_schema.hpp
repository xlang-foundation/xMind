#pragma once
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace agentflow {
struct SchemaInvalid : std::invalid_argument {using std::invalid_argument::invalid_argument;};
struct SchemaArgumentsInvalid : std::invalid_argument {using std::invalid_argument::invalid_argument;};
// Native JSON Schema 2020-12 adapter. No schema network/file fetches, no default
// insertion or argument normalization. One owner; execution containment for
// adversarial schema evaluation remains an access-worker responsibility.
class JsonSchema202012 {
public:
    explicit JsonSchema202012(std::string source);
    ~JsonSchema202012();
    JsonSchema202012(const JsonSchema202012&)=delete;
    JsonSchema202012& operator=(const JsonSchema202012&)=delete;
    void validate_object(std::string_view instance) const;
    const std::string& source() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
