#pragma once

#include "Callable.h"
#include "Locker.h"
#include "xpackage.h"
#include <unordered_map>
#include <vector>
#include <memory>
#include <condition_variable> 
#include "type_def.h"

namespace xMind
{
	struct Connection
	{
		int fromCallableIndex;
		int fromPinIndex;
		int toCallableIndex;
		int toPinIndex;
	};
	struct CallableInfo
	{
		Callable* callable;
		X::Value varCallable;
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
			APISET().AddVarFunc("isRunning", &AgentGraph::IsRunning);
			APISET().AddFunc<0>("stop", &AgentGraph::Stop);
			APISET().AddFunc<0>("waitToStop", &AgentGraph::WaitToStop);
		END_PACKAGE
	public:
		AgentGraph() = default;
		~AgentGraph();

		bool AddNode(X::XRuntime* rt, X::XObj* pContext,
			X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue);
		bool Parse(const std::string& yamlContent);
		int AddCallable(Callable* callable,X::Value& varCallable);
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
			std::unique_lock<std::mutex> lock(m_mutex);
			m_running = false;
			m_condVar.notify_all(); // Notify all waiting threads
		}
		bool IsRunning(X::XRuntime* rt, X::XObj* pContext,
			X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue)
		{
			int timeout = 0;
			if (params.size() > 0)
			{
				timeout = (int)params[0];
			}
			else
			{
				auto it = kwParams.find("timeout");
				if (it)
				{
					timeout = (int)it->val;
				}
			}
			std::unique_lock<std::mutex> lock(m_mutex);

			if (timeout == 0)
			{
				retValue = int(m_running ? Status::Running : Status::Stopped);
			}
			else
			{
				bool bRun = m_condVar.wait_for(lock, std::chrono::milliseconds(timeout), [this] {
					return m_running ? true : false;
					});
				retValue = int(bRun ? Status::Running : Status::Stopped);
			}
			return true;
		}
		void WaitToStop()
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_condVar.wait(lock, [this] { return !m_running; });
			for(auto& info : m_callables)
			{
				info.callable->Stop();
			}
		}
	private:
		bool m_running = false;
		std::vector<CallableInfo> m_callables;
		std::vector<Connection> m_connections;
		std::unordered_map<std::string, int> m_callableNameToIndex;
		Locker m_locker;
		std::mutex m_mutex; // Mutex for condition variable
		std::condition_variable m_condVar; // Condition variable
	};
}
