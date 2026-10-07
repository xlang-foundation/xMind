#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace agentflow {
struct ProcessBeforeDispatchError : std::runtime_error {using std::runtime_error::runtime_error;};
struct ProcessCancelledBeforeDispatch : ProcessBeforeDispatchError {using ProcessBeforeDispatchError::ProcessBeforeDispatchError;};
struct ProcessEffectUncertain : std::runtime_error {using std::runtime_error::runtime_error;};
enum class ProcessTermination {exited,cancelled,timed_out};
struct ProcessConfiguration {
    // Resolved trusted backend configuration, never a model-selected executable.
    std::string executable,working_directory,working_directory_id;
    std::vector<std::string> arguments;
    std::vector<std::pair<std::string,std::string>> environment;
    std::chrono::milliseconds timeout{120000};
    std::size_t output_limit=1024*1024;
};
struct ProcessResult {
    std::uint32_t pid,exit_code;
    ProcessTermination termination;
    // Raw bytes. The caller must explicitly encode invalid text for transport.
    std::string stdout_bytes,stderr_bytes;
    std::uint64_t stdout_count,stderr_count;
    bool truncated;
    std::int64_t elapsed_ms;
};
// Native Windows foreground adapter. The owning executor must have an exact
// durable effect claim before run(). No approval/journal/model logic lives here.
// Job ownership bounds lifetime, not filesystem/network authority. A result
// proves observed process lifecycle only, never arbitrary side-effect validity.
class ForegroundProcess {
public:
    using OutputObserver=std::function<void(bool stderr_channel,std::string_view bytes)>;
    static std::string directory_identity(const std::string& absolute_directory);
    // Observer receives at most output_limit retained raw bytes in total. Its
    // failure after dispatch terminates the job and reports effect uncertainty.
    static ProcessResult run(const ProcessConfiguration& configuration,
        std::stop_token cancel={},OutputObserver observer={});
};
}
