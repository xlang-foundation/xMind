#pragma once
#include <chrono>
#include <stdexcept>
namespace agentflow {
using McpDeadline=std::chrono::steady_clock::time_point;
struct McpTransportError : std::runtime_error {using std::runtime_error::runtime_error;};
struct McpTransportTimeout : McpTransportError {using McpTransportError::McpTransportError;};
struct McpTransportCancelled : McpTransportError {using McpTransportError::McpTransportError;};
}
