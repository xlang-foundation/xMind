#pragma once

#include "Callable.h"
#include "Locker.h"
#include "xpackage.h"
#include <unordered_map>
#include <vector>
#include <memory>

namespace xMind
{
	struct Connection
	{
		int fromCallableIndex;
		int fromPinIndex;
		int toCallableIndex;
		int toPinIndex;
	};

	class AgentGraph
	{
		BEGIN_PACKAGE(AgentGraph)
			APISET().AddFunc<4>("connect", &AgentGraph::AddConnection);
			APISET().AddFunc<4>("disconnect", &AgentGraph::RemoveConnection);
		END_PACKAGE
	public:
		AgentGraph() = default;
		~AgentGraph();

		bool Parse(const std::string& yamlContent);
		int AddCallable(Callable* callable);
		void RemoveCallable(int index);
		void AddConnection(const std::string& fromInstanceName, const std::string& fromPinName, const std::string& toInstanceName, const std::string& toPinName);
		void RemoveConnection(const std::string& fromInstanceName, const std::string& fromPinName, const std::string& toInstanceName, const std::string& toPinName);
		void PushDataToCallable(Callable* fromCallable, int outputIndex, X::Value& data);

	private:
		std::vector<Callable*> m_callables;
		std::vector<Connection> m_connections;
		std::unordered_map<std::string, int> m_callableNameToIndex;
		Locker m_locker;
	};
}
