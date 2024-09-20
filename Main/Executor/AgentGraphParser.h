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


//use this for agent no module name
#define NO_MODULE_NAME "module"

namespace xMind
{
    struct GroupInfo {
        std::string name;
        std::vector<std::string> agents;
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
        bool ParseImports(const X::Value& importsValue)
        {
            if (!importsValue.IsList())
            {
                return false;
            }
            X::List list(importsValue);
            for (const auto item : *list)
            {
                X::Dict importItem(item);
                auto fileName = importItem["file"].ToString();
                auto alias = importItem["alias"].ToString();
				if (alias.empty())
				{
					//use the file name exclude the extension as the alias
					size_t pos = fileName.find_last_of('.');
					if (pos != std::string::npos)
					{
						alias = fileName.substr(0, pos);
					}
                    else
                    {
                        alias = fileName;
                    }
				}
				// Parse the file
				ParseAgentGraphDesc(alias, fileName);
            }
            return true;
        }

        X::Value QueyNode(const std::string& curModuleName,std::string combineName)
        {
			std::string moduleName;
			std::string callableName;
            size_t pos = combineName.find('.');
            if (pos != std::string::npos)
            {
                moduleName = combineName.substr(0, pos);
                callableName = combineName.substr(pos + 1);
            }
            else
            {//if no module prefix use current module name
                moduleName = curModuleName;
                callableName = combineName;
            }
            //check from NodeManager to find the callable with the name
            return NodeManager::I().queryCallable(moduleName, callableName);
        }
        // Function to parse agents/actions/functions
        bool ParseNodes(const X::Value& nodesValue,
            const std::string& moduleName, const std::string& fileName);
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
        bool ParseAgentGraphDesc(const std::string& moduleName = "", const std::string& fileName = "");
        bool ParseAgentGraphDescFromString(const std::string& desc);
		bool ParseAgentGraphDescFromRoot(X::Value& root,
            const std::string& moduleName = "", const std::string& fileName = "")
        {
            // Check if root is a Dict
            if (!root.IsDict())
            {
                std::cerr << "Error: Invalid yaml format." << std::endl;
                return false;
            }
			std::string strModuleName = moduleName;

            // Extract basic information
            std::string name = root["name"].ToString();
            if (strModuleName.empty())
            {
                strModuleName = root["module"].ToString();
				// if no module name is provided,
				// use a default module name
				if (strModuleName.empty())
				{
					strModuleName = NO_MODULE_NAME;
				}
            }

            std::string type = root["type"].ToString();
            std::string version = root["version"].ToString();
            std::string description = root["description"].ToString();

            // Process 'prompts' if any (assuming it's a list or dict)
            X::Value prompts = root["prompts"];
            // TODO: Process prompts as needed

            // Parse imports
            X::Value importsValue = root["imports"];
            ParseImports(importsValue);

            // Parse agents/actions/functions
            X::Value nodesValue = root["nodes"];
            ParseNodes(nodesValue, moduleName,fileName);

            // Parse connections
            X::Value connectionsValue = root["connections"];
            std::vector<ConnectionInfo> connections = ParseConnections(connectionsValue);
			AgentGraph* graph = new AgentGraph();
			for (auto& connection : connections)
			{
				//Add into Graph
                X::Value CallableFrom = QueyNode(moduleName,connection.fromAgentName);
                X::Value CallableTo = QueyNode(moduleName,connection.toAgentName);
                if (!CallableFrom.IsObject() || !CallableTo.IsObject())
                {
                    continue;
                }
                graph->AddCallable(CallableFrom);
                graph->AddCallable(CallableTo);

                X::ARGS params(4);
                params.push_back(connection.fromAgentName);
                params.push_back(connection.fromPinName);
                params.push_back(connection.toAgentName);
                params.push_back(connection.toPinName);
                X::KWARGS kwParams;
                X::Value retValue;
                graph->AddConnection(nullptr, nullptr, params, kwParams, retValue);
            }
            NodeManager::I().AddGraph(graph);
            // Parse groups
            X::Value groupsValue = root["groups"];
            std::vector<Group> groups = ParseGroups(groupsValue);


            std::cout << "Parsed Callables:" << std::endl;
            return true;

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
