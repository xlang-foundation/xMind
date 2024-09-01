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
			APISET().AddVarFunc("addNode", &AgentGraph::AddNode);
			APISET().AddFunc<1>("removeNode", &AgentGraph::RemoveCallable);
			APISET().AddVarFunc("connect", &AgentGraph::AddConnection);
			APISET().AddFunc<4>("disconnect", &AgentGraph::RemoveConnection);
			APISET().AddVarFunc("run", &AgentGraph::Run);
			APISET().AddVarFunc("startOnce", &AgentGraph::StartOnce);
			APISET().AddVarFunc("startFrom", &AgentGraph::StartFrom);
			APISET().AddFunc<0>("stop", &AgentGraph::Stop);
		END_PACKAGE
	public:
		AgentGraph() = default;
		~AgentGraph();

		bool AddNode(X::XRuntime* rt, X::XObj* pContext,
			X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue);
		bool Parse(const std::string& yamlContent);
		int AddCallable(Callable* callable);
		void RemoveCallable(int index);
		void AddConnection(X::XRuntime* rt, X::XObj* pContext,
			X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue);
		void RemoveConnection(const std::string& fromInstanceName, const std::string& fromPinName, const std::string& toInstanceName, const std::string& toPinName);
		void PushDataToCallable(Callable* fromCallable, int outputIndex, X::Value& data);
		bool Run(X::XRuntime* rt, X::XObj* pContext,
			X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue);
		bool StartOnce(X::XRuntime* rt, X::XObj* pContext,
			X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue);
		bool StartFrom(X::XRuntime* rt, X::XObj* pContext,
			X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue);
		void Stop()
		{
			m_locker.Lock();
			m_running = false;
			m_locker.Unlock();
		}
	private:
		bool m_running = false;
		std::vector<Callable*> m_callables;
		std::vector<Connection> m_connections;
		std::unordered_map<std::string, int> m_callableNameToIndex;
		Locker m_locker;
	};
}
