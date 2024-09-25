/*
Copyright (C) 2024 The XLang Foundation

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include "singleton.h"
#include "Callable.h"
#include "AgentGraph.h"
#include "Locker.h"
#include "port.h"

namespace xMind
{
	struct ModuleInfo
	{
		std::string moduleName;
		std::string path;
		std::unordered_map<std::string, X::Value> mapCallables;
	};
	class NodeManager : public Singleton<NodeManager>
	{
		Locker m_lock;
	public:
		NodeManager() = default;
		~NodeManager() = default;

		// Add a Callable to the list and map
		bool addCallable(const std::string& moduleName,
			const std::string& moduleFilePath,
			const std::string& name, X::Value& callable)
		{
			AutoLock lock(m_lock);
			//find from m_modules with moduleName
			auto it = std::find_if(m_modules.begin(), m_modules.end(), 
				[moduleName](const ModuleInfo& module) {
				return module.moduleName == moduleName;
				});
			if (it == m_modules.end())
			{
				ModuleInfo moduleInfo;
				moduleInfo.moduleName = moduleName;
				moduleInfo.mapCallables[name] = callable;
				moduleInfo.path = moduleFilePath;
				m_modules.push_back(moduleInfo);
			}
			else
			{
				//check if mapCallables has the same name
				auto it2 = it->mapCallables.find(name);
				if (it2 != it->mapCallables.end())
				{
					return false;
				}
				it->mapCallables[name] = callable;
			}
			return true;
		}

		// Get all Callables
		X::Value queryCallable(const std::string& moduleName,
			const std::string& name)
		{
			AutoLock lock(m_lock);
			auto it = std::find_if(m_modules.begin(), m_modules.end(),
				[moduleName](const ModuleInfo& module) {
					return module.moduleName == moduleName;
				});
			if (it != m_modules.end())
			{
				auto it2 = it->mapCallables.find(name);
				if (it2 != it->mapCallables.end())
				{
					return it2->second;
				}
			}
			return X::Value();
		}

		void AddGraph(X::Value& graph)
		{
			m_lock.Lock();
			m_graphs.push_back(graph);
			m_lock.Unlock();
		}
		// Build JSON representation of the graph
		std::string BuildGraphAsJson()
		{
			const int online_len = 20;
			char convertBuf[online_len];

			X::List listNodes;
			//Collect all callables with module names
			for (const auto& module : m_modules)
			{
				for (const auto& it : module.mapCallables)
				{
					X::XPackageValue<Callable> varCallable(it.second);
					Callable* callable = (Callable*)varCallable.GetRealObj();
					if (!callable)
					{
						continue;
					}
					X::Dict dict;
					dict->Set("moduleName", (std::string)module.moduleName);
					dict->Set("path", (std::string)module.path);
					dict->Set("name", callable->GetName());
					dict->Set("instanceName", callable->GetInstanceName());
					dict->Set("type", static_cast<int>(callable->Type()));
					dict->Set("id", callable->ID());
					dict->Set("inputs", callable->GetInputs());
					dict->Set("outputs", callable->GetOutputs());
					listNodes += dict;
				}
			}

			X::Dict graphDict;
			for (auto& graph : m_graphs)
			{
				if (!graph.IsObject())
				{
					continue;
				}
				X::XPackageValue<AgentGraph> varGraph(graph);
				AgentGraph* pGraph = (AgentGraph*)varGraph.GetRealObj();
				auto graphId = pGraph->ID();
				// Collect all connections
				X::List connList;
				for (const auto& connection : pGraph->GetConnections())
				{
					X::Dict oneConn;
					int idFrom = pGraph->GetCallableByIndex(connection.fromCallableIndex)->ID();
					oneConn->Set("fromCallableId", idFrom);
					oneConn->Set("fromPinIndex",connection.fromPinIndex);
					int idTo = pGraph->GetCallableByIndex(connection.toCallableIndex)->ID();
					oneConn->Set("toCallableId", idTo);
					oneConn->Set("toPinIndex",connection.toPinIndex);
					connList += oneConn;
				}

				SPRINTF(convertBuf, online_len, "%llu", graphId);
				std::string graphIdStr = convertBuf;
				X::Value graphIdValue(graphIdStr);
				graphDict->Set(graphIdValue,connList);
			}
			X::Dict all;
			all->Set("Nodes",listNodes);
			all->Set("Graphs",graphDict);
			std::string graphJson = all->ToString(true);
			return graphJson;
		}
	private:
		std::vector<ModuleInfo> m_modules;
		std::vector<X::Value> m_graphs;
	};
}
