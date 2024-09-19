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
	class NodeManager : public Singleton<NodeManager>
	{
		Locker m_lock;
	public:
		NodeManager() = default;
		~NodeManager() = default;

		// Add a Callable to the list and map
		bool addCallable(const std::string& name, Callable* callable)
		{
			//check same name callable need to release previous one
			auto it = m_callablesMap.find(name);
			if (it != m_callablesMap.end())
			{
				return false;
			}
			m_callables.push_back(callable);
			m_callablesMap[name] = callable;
			return true;
		}

		// Get a Callable by name
		Callable* getCallable(const std::string& name) const
		{
			auto it = m_callablesMap.find(name);
			if (it != m_callablesMap.end())
			{
				return it->second;
			}
			return nullptr;
		}

		// Get all Callables
		const std::vector<Callable*>& getAllCallables() const
		{
			return m_callables;
		}

		void AddGraph(AgentGraph* pGraph)
		{
			m_lock.Lock();
			m_graphs.push_back(pGraph);
			m_lock.Unlock();
		}
		// Build JSON representation of the graph
		std::string BuildGraphAsJson()
		{
			const int online_len = 20;
			char convertBuf[online_len];

			X::List listNodes;
			// Collect all callables
			for (const auto& callable : m_callables)
			{
				X::Dict dict;
				dict->Set("id",callable->ID());
				dict->Set("name",callable->GetName());
				dict->Set("type",static_cast<int>(callable->Type()));
				listNodes += dict;
			}
			X::Dict graphDict;
			for (auto* pGraph : m_graphs)
			{
				auto graphId = pGraph->ID();
				// Collect all connections
				X::List connList;
				for (const auto& connection : pGraph->GetConnections())
				{
					X::Dict oneConn;
					oneConn->Set("fromCallableIndex",connection.fromCallableIndex);
					oneConn->Set("fromPinIndex",connection.fromPinIndex);
					oneConn->Set("toCallableIndex",connection.toCallableIndex);
					oneConn->Set("toPinIndex",connection.toPinIndex);
					connList += oneConn;
				}

				SPRINTF(convertBuf, online_len, "%llu", graphId);
				graphDict->Set(convertBuf,connList);
			}
			X::Dict all;
			all->Set("Nodes",listNodes);
			all->Set("Graphs",graphDict);
			std::string graphJson = all->ToString(true);
			return graphJson;
		}
	private:
		std::vector<Callable*> m_callables;
		std::unordered_map<std::string, Callable*> m_callablesMap;
		std::vector<AgentGraph*> m_graphs;
	};
}
