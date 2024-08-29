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
    struct AgentPin
    {
        std::string name;
        std::vector<std::string> formats;
    };

    struct AgentConnection
    {
        std::string acceptPinName; // my side pin name
        std::string fromAgentName; // another side agent name
        std::string fromPinName;   // another side pin name
    };

    /**
     * @class BaseAgent
     * @brief Represents a base class for agents in the xMind framework.
     *
     * The BaseAgent class provides functionality for managing input and output pins,
     * handling connections between agents, and maintaining input queues for each pin.
     * It includes methods for setting and getting input/output configurations,
     * enqueuing and dequeuing input data, and connecting/disconnecting agents.
     */
    class BaseAgent
    {
        BEGIN_PACKAGE(BaseAgent)
            APISET().AddPropWithType<std::string>("name", &BaseAgent::m_name);
            APISET().AddPropL("inputs",
                [](auto* pThis, X::Value v) { pThis->SetInputs(v); },
                [](auto* pThis) { return pThis->GetInputs(); });
            APISET().AddPropL("outputs",
                [](auto* pThis, X::Value v) { pThis->SetOutputs(v); },
                [](auto* pThis) { return pThis->GetOutputs(); });
            APISET().AddFunc<3>("connect", &BaseAgent::Connect);
            APISET().AddVarFunc("disconnect", &BaseAgent::Disconnect);
            APISET().AddFunc<2>("pushToOutput", &BaseAgent::PushToOutput);
        END_PACKAGE

    protected:
        std::string m_name; // Agent Name
        std::vector<AgentPin> m_inputs;
        std::vector<AgentPin> m_outputs;
        std::vector<AgentConnection> m_connections;
        std::vector<std::queue<X::Value>> m_inputQueues; // Input queues for each pin
        std::unordered_map<std::string, size_t> m_inputIndexMap; // Map pin names to indices
        Locker m_locker;

    public:
        BaseAgent()
        {
        }

        virtual ~BaseAgent()
        {
        }

        inline X::Value GetInputs()
        {
            m_locker.Lock();
            X::List list;
            for (auto& pin : m_inputs)
            {
                X::Dict dict;
                dict["name"] = pin.name;
                dict["formats"] = pin.formats;
                list += dict;
            }
            m_locker.Unlock();
            return list;
        }

        inline void SetInputs(X::Value v)
        {
            m_locker.Lock();
            X::List list(v);
            m_inputs.clear();
            m_inputQueues.clear(); // Clear existing queues
            m_inputIndexMap.clear(); // Clear existing index map
            for (size_t i = 0; i < list.Size(); ++i)
            {
                auto& item = list[i];
                AgentPin pin;
                pin.name = item["name"];
                X::Value varFormats = item["formats"];
                if (varFormats.IsList())
                {
                    X::List formats(varFormats);
                    for (auto& format : *formats)
                    {
                        pin.formats.push_back(format.ToString());
                    }
                }
                else
                {
                    pin.formats.push_back(varFormats.ToString());
                }
                m_inputs.push_back(pin);
                m_inputQueues.emplace_back(); // Initialize a queue for each input pin
                m_inputIndexMap[pin.name] = i; // Map pin name to index
            }
            m_locker.Unlock();
        }

        inline X::Value GetOutputs()
        {
            m_locker.Lock();
            X::List list;
            for (auto& pin : m_outputs)
            {
                X::Dict dict;
                dict["name"] = pin.name;
                dict["formats"] = pin.formats;
                list += dict;
            }
            m_locker.Unlock();
            return list;
        }

        inline void SetOutputs(X::Value v)
        {
            m_locker.Lock();
            X::List list(v);
            m_outputs.clear();
            for (auto& item : *list)
            {
                AgentPin pin;
                pin.name = item["name"];
                X::Value varFormats = item["formats"];
                if (varFormats.IsList())
                {
                    X::List formats(varFormats);
                    for (auto& format : *formats)
                    {
                        pin.formats.push_back(format.ToString());
                    }
                }
                else
                {
                    pin.formats.push_back(varFormats.ToString());
                }
                m_outputs.push_back(pin);
            }
            m_locker.Unlock();
        }

        // Enqueue data to the input queue of a specific pin
        inline void EnqueueInput(const std::string& pinName, const X::Value& value)
        {
            m_locker.Lock();
            auto it = m_inputIndexMap.find(pinName);
            if (it != m_inputIndexMap.end())
            {
                m_inputQueues[it->second].push(value);
            }
            m_locker.Unlock();
        }

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

        // Only call from the agent with this output pin
        inline void Connect(std::string fromPinName,
            std::string agentNameConnectTo,
            std::string agentPinNameConnectTo)
        {
            AutoLock lock(m_locker);
            // check if this combination exists
            for (auto& connection : m_connections)
            {
                if (connection.acceptPinName == fromPinName &&
                    connection.fromAgentName == agentNameConnectTo &&
                    connection.fromPinName == agentPinNameConnectTo)
                {
                    return;
                }
            }
            // check if the fromPinName exists in my agent
            bool bFound = false;
            for (auto& pin : m_outputs)
            {
                if (pin.name == fromPinName)
                {
                    bFound = true;
                    break;
                }
            }
            if (!bFound)
            {
                return;
            }
            // then add it
            AgentConnection connection;
            connection.acceptPinName = fromPinName;
            connection.fromAgentName = agentNameConnectTo;
            connection.fromPinName = agentPinNameConnectTo;
            m_connections.push_back(connection);
        }

        inline void Disconnect(std::string fromPinName,
            std::string agentNameConnectTo,
            std::string agentPinNameConnectTo)
        {
            AutoLock lock(m_locker);
            for (auto it = m_connections.begin(); it != m_connections.end();)
            {
                if (it->acceptPinName == fromPinName &&
                    it->fromAgentName == agentNameConnectTo &&
                    it->fromPinName == agentPinNameConnectTo)
                {
                    it = m_connections.erase(it);
                    break;
                }
                else
                {
                    ++it;
                }
            }
        }
        // Push data to the output pin and deliver it to the connected agent's input queue
        inline void PushToOutput(const std::string& pinName, const X::Value& value)
        {
            m_locker.Lock();
            for (auto& connection : m_connections)
            {
                if (connection.acceptPinName == pinName)
                {
                    // Find the connected agent and push data to its input queue
                    // Assuming we have a way to get the agent by name
                    BaseAgent* connectedAgent = GetAgentByName(connection.fromAgentName);
                    if (connectedAgent)
                    {
                        connectedAgent->EnqueueInput(connection.fromPinName, value);
                    }
                }
            }
            m_locker.Unlock();
        }

        // Placeholder for getting an agent by name
        // This should be implemented according to your framework's agent management
        BaseAgent* GetAgentByName(const std::string& name)
        {
            // Implement this method to return the agent instance by name
            return nullptr;
        }
    };
}
