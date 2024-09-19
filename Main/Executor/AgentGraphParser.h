#pragma once
#include <queue>
#include <vector>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <regex>
#include <atomic>
#include <fstream>
#include <iostream>
#include <algorithm>
#include "xpackage.h"
#include "Locker.h"
#include "Callable.h"
#include "BaseAction.h"
#include "BaseAgent.h"
#include "NodeManager.h"


namespace xMind
{
    struct GroupInfo {
        std::string name;
        std::vector<std::string> agents;
    };

    struct ImportInfo {
        std::string file;
        std::string alias;
    };
    struct ConnectionInfo {
        std::string fromAgentName;
        std::string fromPinName;
        std::string toAgentName;
        std::string toPinName;
    };
    struct Group {
        std::string name;
        std::vector<std::string> agents;
    };
    // Parser class definition
    class Parser
    {
    public:
        // Function to parse imports
        std::vector<ImportInfo> ParseImports(const X::Value& importsValue)
        {
            std::vector<ImportInfo> imports;
            if (importsValue.IsList())
            {
				X::List list(importsValue);
                for (const auto item : *list)
                {
					X::Dict importItem(item);
                    ImportInfo imp;
                    imp.file = importItem["file"].ToString();
                    imp.alias = importItem["alias"].ToString();
                    imports.push_back(imp);
                }
            }
            return imports;
        }

        // Function to parse agents/actions/functions
        bool ParseNodes(const X::Value& nodesValue)
        {
            if (!nodesValue.IsList())
            {
				return false;
            }
			X::List list(nodesValue);
            for (const auto item : *list)
            {
				X::Dict nodeItem(item);
                std::string typeStr = nodeItem["type"].ToString();
                CallableType callableType = CallableType::callable;

                if (typeStr == "agent")
                {
                    callableType = CallableType::agent;
                }
                else if (typeStr == "action")
                {
                    callableType = CallableType::action;
                }
                else if (typeStr == "function")
                {
                    callableType = CallableType::function;
                }
                else
                {
					std::cerr << "Error: Unknown node type: " << typeStr << std::endl;
					continue;
                }

                Callable* callable = nullptr;

                switch (callableType)
                {
                case CallableType::agent:
                    callable = new BaseAgent();
                    break;
                case CallableType::action:
                    callable = new BaseAction();
                    break;
                case CallableType::function:
                    callable = new Function();
                    break;
                default:
                    break;
                }
				std::string name = nodeItem["name"].ToString();
                callable->SetName(name);
                callable->SetDescription(nodeItem["description"].ToString());
                //TODO:
                std::string instanceName = name;
                callable->SetInstanceName(instanceName);

                // Parse inputs
                callable->SetInputs(nodeItem["inputs"]);

                // Parse outputs
                callable->SetOutputs(nodeItem["outputs"]);

                // Parse source
                X::Value sourceValue = nodeItem["source"];
                if (sourceValue.IsDict())
                {
                    // Assuming we have a method to set the source in Callable
                    // callable->SetSource(sourceValue);
                }

                // Parse parameters
                X::Value parametersValue = nodeItem["parameters"];
                if (parametersValue.IsDict())
                {
					X::Dict parameters(parametersValue);
                    X::KWARGS params;
                    parameters->Enum([&](X::Value& key, X::Value& value) {
                        params.Add(key.ToString().c_str(), value);
                        });
                    callable->SetParams(params);
                }

                // Parse group
                //TODO:
                // callable->SetGroup(nodeItem["group"].ToString());

				NodeManager::I().addCallable(name, callable);
            }
     
            return true;
        }

        // Function to parse connections
        std::vector<ConnectionInfo> ParseConnections(const X::Value& connectionsValue)
        {
            std::vector<ConnectionInfo> connections;
            if (connectionsValue.IsList())
            {
				X::List list(connectionsValue);
                for (const auto item : *list)
                {
					X::Dict connectionItem(item);
                    ConnectionInfo connection;
                    connection.fromAgentName = connectionItem["fromAgentName"].ToString();
                    connection.fromPinName = connectionItem["fromPinName"].ToString();
                    connection.toAgentName = connectionItem["toAgentName"].ToString();
                    connection.toPinName = connectionItem["toPinName"].ToString();
                    connections.push_back(connection);
                }
            }
            return connections;
        }

        // Function to parse groups
        std::vector<Group> ParseGroups(const X::Value& groupsValue)
        {
            std::vector<Group> groups;
            if (groupsValue.IsList())
            {
				X::List list(groupsValue);
                for (const auto item : *list)
                {
					X::Dict groupItem(item);
                    Group group;
                    group.name = groupItem["name"].ToString();

                    X::Value agentsValue = groupItem["agents"];
                    if (agentsValue.IsList())
                    {
						X::List agent_lists(agentsValue);
                        for (auto agentNameValue : *agent_lists)
                        {
							std::string name = agentNameValue.ToString();
                            group.agents.push_back(name);
                        }
                    }
                    groups.push_back(group);
                }
            }
            return groups;
        }

        // Main function to parse the agent graph description
        void ParseAgentGraphDesc(X::Value& root)
        {
            // Check if root is a Dict
            if (!root.IsDict())
            {
                std::cerr << "Error: root is not a dictionary." << std::endl;
                return;
            }

            // Extract basic information
            std::string name = root["name"].ToString();
            std::string type = root["type"].ToString();
            std::string version = root["version"].ToString();
            std::string description = root["description"].ToString();

            // Process 'prompts' if any (assuming it's a list or dict)
            X::Value prompts = root["prompts"];
            // TODO: Process prompts as needed

            // Parse imports
            X::Value importsValue = root["imports"];
            std::vector<ImportInfo> imports = ParseImports(importsValue);

            // Parse agents/actions/functions
            X::Value nodesValue = root["nodes"];
            ParseNodes(nodesValue);

            // Parse connections
            X::Value connectionsValue = root["connections"];
            std::vector<ConnectionInfo> connections = ParseConnections(connectionsValue);
			AgentGraph* graph = new AgentGraph();
			for (auto& connection : connections)
			{
				//Add into Graph
                X::Value varCallable;
				Callable* pCallableFrom = NodeManager::I().getCallable(connection.fromAgentName);
                Callable* pCallableTo = NodeManager::I().getCallable(connection.toAgentName);
                if (pCallableFrom == nullptr || pCallableTo == nullptr)
                {
                    continue;
                }
                graph->AddCallable(pCallableFrom, varCallable);
                graph->AddCallable(pCallableTo, varCallable);

                X::ARGS params(4);
                params.push_back(connection.fromAgentName);
                params.push_back(connection.fromPinName);
                params.push_back(connection.toAgentName);
                params.push_back(connection.toPinName);
                X::KWARGS kwParams;
                X::Value retValue;
                graph->AddConnection(nullptr, nullptr, params, kwParams, retValue);
            }
            // Parse groups
            X::Value groupsValue = root["groups"];
            std::vector<Group> groups = ParseGroups(groupsValue);


            std::cout << "Parsed Callables:" << std::endl;
        }

    private:
        // Helper function to convert CallableType to string
        std::string CallableTypeToString(CallableType type)
        {
            switch (type)
            {
            case CallableType::callable:
                return "callable";
            case CallableType::function:
                return "function";
            case CallableType::action:
                return "action";
            case CallableType::agent:
                return "agent";
            case CallableType::compositeAgent:
                return "compositeAgent";
            default:
                return "unknown";
            }
        }
    };
}
