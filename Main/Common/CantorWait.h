#pragma once
#include <condition_variable>
#include <mutex>
#include <chrono>

class CantorWait 
{
public:
    CantorWait(bool autoReset = false) : m_autoReset(autoReset), m_signaled(false) {}

    ~CantorWait() = default;

    bool Wait(int timeoutMS) {
        std::unique_lock<std::mutex> lock(m_mutex);
        bool result;
        if (timeoutMS < 0) {
            m_condition.wait(lock, [this] { return m_signaled; });
            result = true;
        }
        else {
            result = m_condition.wait_for(lock, std::chrono::milliseconds(timeoutMS), [this] { return m_signaled; });
        }
        if (m_autoReset && result) {
            m_signaled = false;
        }
        return result;
    }

    void Release() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_signaled = true;
        m_condition.notify_one();
    }

    void Reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_signaled = false;
    }

private:
    bool m_autoReset;
    bool m_signaled;
    std::mutex m_mutex;
    std::condition_variable m_condition;
};
