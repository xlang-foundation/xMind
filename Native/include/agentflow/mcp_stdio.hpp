#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace agentflow {
struct McpTransportError : std::runtime_error {using std::runtime_error::runtime_error;};
struct McpTransportTimeout : McpTransportError {using McpTransportError::McpTransportError;};
struct McpTransportCancelled : McpTransportError {using McpTransportError::McpTransportError;};
struct McpStdioConfiguration {
    std::string executable,working_directory;
    std::vector<std::string> arguments;
    std::vector<std::pair<std::string,std::string>> environment;
    std::size_t stdout_buffer_limit=4*1024*1024;
};
struct McpStdioStatus {
    std::uint32_t pid;
    std::optional<std::uint32_t> exit_code;
    std::size_t buffered_stdout;
    std::uint64_t discarded_stderr;
    bool faulted,closed;
};
// Windows native access adapter. Trusted backend configuration only, never a
// model/view-supplied command. Single owner for write/read/status/shutdown.
// Pumps own pipe reads; protocol parsing stays on the calling owner thread.
// A job owns process lifetime, not filesystem/network sandbox permissions.
class McpStdioProcess {
public:
    using Deadline=std::chrono::steady_clock::time_point;
    explicit McpStdioProcess(const McpStdioConfiguration& configuration);
    ~McpStdioProcess();
    McpStdioProcess(const McpStdioProcess&)=delete;
    McpStdioProcess& operator=(const McpStdioProcess&)=delete;
    void write(std::string_view frame,Deadline deadline,std::stop_token cancel={});
    std::optional<std::string> read(Deadline deadline,std::stop_token cancel={}); // nullopt = stdout EOF
    McpStdioStatus status() const;
    void shutdown() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
