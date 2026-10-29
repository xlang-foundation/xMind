#pragma once
#if defined(_WIN32)
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace agentflow {
// One icon per embedded web owner, never per CLI/IPC client or worker.
class WindowsTray final {
public:
    WindowsTray(std::string web_url,std::string workspace,std::function<void()> quit);
    ~WindowsTray();
    WindowsTray(const WindowsTray&)=delete;
    WindowsTray& operator=(const WindowsTray&)=delete;
    bool visible() const noexcept;
    std::uintptr_t native_window() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
#endif
