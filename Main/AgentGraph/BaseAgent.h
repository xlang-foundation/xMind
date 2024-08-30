#pragma once
#include <queue>
#include <vector>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <regex>
#include <fstream>
#include <iostream>
#include <algorithm>
#include "xpackage.h"
#include "Locker.h"

namespace xMind
{
    class AgentGroup;
    /**
     * @class BaseAgent
     * @brief Represents a base class for agents in the xMind framework.
     *
     * The BaseAgent class provides functionality for managing input and output pins,
     * handling connections between agents, and maintaining input queues for each pin.
     * It includes methods for setting and getting input/output configurations,
     * enqueuing and dequeuing input data, and connecting/disconnecting agents.
     */
    class BaseAgent: public Callable
    {
        BEGIN_PACKAGE(BaseAgent)
        END_PACKAGE

    protected:
        AgentGroup* m_group;
        std::vector<std::queue<X::Value>> m_inputQueues; // Input queues for each pin

    public:
        BaseAgent()
        {
        }

        virtual ~BaseAgent()
        {
        }

        // Enqueue data to the input queue of a specific pin
        inline virtual bool ReceiveData(int inputIndex, X::Value& data) override
        {
            m_locker.Lock();
            m_inputQueues[inputIndex].push(data);
            m_locker.Unlock();
            return true;
        }
#if __TODO__
        // Dequeue data from the input queue of a specific pin
        inline X::Value DequeueInput(const std::string& pinName)
        {
            m_locker.Lock();
            X::Value value;
            auto it = m_inputIndexMap.find(pinName);
            if (it != m_inputIndexMap.end() && !m_inputQueues[it->second].empty())
            {
                value = m_inputQueues[it->second].front();
                m_inputQueues[it->second].pop();
            }
            m_locker.Unlock();
            return value;
        }
#endif
    };
}
