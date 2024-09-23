#include "AgentGraph.h"
#include <iostream>
#include "xlang.h"
#include "xMindAPI.h"
#include "BaseAgent.h"

namespace xMind
{
	std::atomic<unsigned long long> AgentGraph::s_idCounter{ 0 };
	AgentGraph::~AgentGraph()
	{
		for (auto& info : m_callables)
		{
			delete info.callable;
		}
		m_callables.clear();
	}

	bool AgentGraph::AddNode(X::XRuntime* rt, X::XObj* pContext,
		X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue)
	{
		X::Value varObj;
		if (params.size() >= 1)
		{
			varObj = params[0];
		}
		else
		{
			retValue = X::Value(false);
			return false;
		}

		X::XPackageValue<Callable> varCallable(varObj);
		Callable* callable = (Callable*)varCallable.GetRealObj();
		//check if callable exists in Graph, if exists, clone a new one
		if (callable->InGraph())
		{
			X::Value cloneCallable = callable->Clone();
			X::XPackageValue<Callable> varCallableClone(cloneCallable);
			callable = (Callable*)varCallableClone.GetRealObj();
		}
		if (params.size() >= 2)
		{
			callable->SetInstanceName(params[1].ToString());
		}
		AddCallable(varObj, callable);
		retValue = X::Value(true);
		return true;
	}

	int AgentGraph::AddCallable(X::Value& varCallable, Callable* callable)
	{
		if (callable == nullptr)
		{
			X::XPackageValue<Callable> packObj(varCallable);
			callable = packObj.GetRealObj();
		}
		AutoLock lock(m_locker); // Lock for thread safety

		int index = (int)m_callables.size();
		m_callableNameToIndex[callable->GetInstanceName()] = index;
		m_callables.push_back({ callable,varCallable });
		callable->SetAgentGraph(this);
		callable->SetIndex(index);
		return index;
	}

	void AgentGraph::RemoveCallable(int index)
	{
		AutoLock lock(m_locker); // Lock for thread safety

		if (index >= 0 && index < m_callables.size())
		{
			auto& callInfo = m_callables[index];
			delete callInfo.callable;
			callInfo.callable = nullptr;
			callInfo.varCallable.Clear();
		}
	}

	void AgentGraph::AddConnection(X::XRuntime* rt, X::XObj* pContext,
		X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue)
	{
		int fromCallableIndex=-1;
		int fromPinIndex=0;
		int toCallableIndex=-1;
		int toPinIndex=0;
		if (params.size() == 2)
		{
			auto fromInstanceName = params[0].ToString();
			auto it = m_callableNameToIndex.find(fromInstanceName);
			if(it != m_callableNameToIndex.end())
			{
				fromCallableIndex = it->second;
			}
			auto toInstanceName = params[1].ToString();
			it = m_callableNameToIndex.find(toInstanceName);
			if (it != m_callableNameToIndex.end())
			{
				toCallableIndex = it->second;
			}
		}
		else if (params.size() == 4)
		{
			auto fromInstanceName = params[0].ToString();
			auto fromPinName = params[1].ToString();
			auto toInstanceName = params[2].ToString();
			auto toPinName = params[3].ToString();
			auto it = m_callableNameToIndex.find(fromInstanceName);
			if (it != m_callableNameToIndex.end())
			{
				fromCallableIndex = it->second;
				auto& info = m_callables[fromCallableIndex];
				fromPinIndex = info.callable->GetOutputIndex(fromPinName);
			}
			it = m_callableNameToIndex.find(toInstanceName);
			if (it != m_callableNameToIndex.end())
			{
				toCallableIndex = it->second;
				auto& info = m_callables[toCallableIndex];
				toPinIndex = info.callable->GetInputIndex(toPinName);
			}
		}
		if(fromCallableIndex == -1 || toCallableIndex == -1)
		{
			retValue = X::Value(false);
			return;
		}
		AutoLock lock(m_locker);
		Connection newConnection{ fromCallableIndex, fromPinIndex, toCallableIndex, toPinIndex };
		m_connections.push_back(newConnection);
		retValue = X::Value(true);
	}

	void AgentGraph::RemoveConnection(const std::string& fromInstanceName, const std::string& fromPinName, const std::string& toInstanceName, const std::string& toPinName)
	{
		AutoLock lock(m_locker); // Lock for thread safety

		int fromCallableIndex = m_callableNameToIndex[fromInstanceName];
		int fromPinIndex = m_callables[fromCallableIndex].callable->GetOutputIndex(fromPinName);
		int toCallableIndex = m_callableNameToIndex[toInstanceName];
		int toPinIndex = m_callables[toCallableIndex].callable->GetInputIndex(toPinName);

		for (auto it = m_connections.begin(); it != m_connections.end(); ++it)
		{
			if (it->fromCallableIndex == fromCallableIndex &&
				it->fromPinIndex == fromPinIndex &&
				it->toCallableIndex == toCallableIndex &&
				it->toPinIndex == toPinIndex)
			{
				m_connections.erase(it);
				break;
			}
		}
	}

	void AgentGraph::PushDataToCallable(Callable* fromCallable, 
		int sessionId,int outputIndex, X::Value& data)
	{
		AutoLock lock(m_locker);
		if (!m_running)
		{
			return;
		}
		int fromCallableIndex = fromCallable->GetIndex();
		bool hasReceiver = false;
		for (const auto& connection : m_connections)
		{
			if (connection.fromCallableIndex == fromCallableIndex && connection.fromPinIndex == outputIndex)
			{
				Callable* toCallable = m_callables[connection.toCallableIndex].callable;
				toCallable->SetRT(fromCallable->GetRT());
				toCallable->ReceiveData(sessionId,connection.toPinIndex, data);
				hasReceiver = true;
			}
		}
		if (!hasReceiver)
		{//cache to quque for each session
			AddSessionData(sessionId, fromCallable,outputIndex, data);
		}
	}
	bool AgentGraph::Run(X::XRuntime* rt, X::XObj* pContext, 
		X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue)
	{
		RunAllCallables(rt);
		return true;
	}
	bool AgentGraph::StartOnce(X::XRuntime* rt, X::XObj* pContext, 
		X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue)
	{
		AutoLock lock(m_locker);
		m_running = true;
		//find start nodes from m_connections, start node is the node that has no input
		std::vector<int> startNodes;
		for (int i = 0; i < m_callables.size(); i++)
		{
			bool isStartNode = true;
			for (const auto& connection : m_connections)
			{
				if (connection.toCallableIndex == i)
				{
					isStartNode = false;
					break;
				}
			}
			if (isStartNode)
			{
				startNodes.push_back(i);
			}
		}
		//call each with ReceiveData
		for (const auto& startNode : startNodes)
		{
			X::Value dummyData;
			auto* pCallable = m_callables[startNode].callable;
			if (pCallable)
			{
				pCallable->SetRT(rt);
				pCallable->ReceiveData(0,0, dummyData);
			}
		}
		return true;
	}
	bool AgentGraph::StartFrom(X::XRuntime* rt, X::XObj* pContext, 
		X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue)
	{
		AutoLock lock(m_locker);
		m_running = true;
		//for each Callable inside params, call ReceiveData
		for (auto& param : params)
		{
			auto it = m_callableNameToIndex.find(param.ToString());
			if (it != m_callableNameToIndex.end())
			{
				X::Value dummyData;
				auto* pCallable = m_callables[it->second].callable;
				if (pCallable)
				{
					pCallable->SetRT(rt);
					//TODO: sessionId  need to set
					int sessionId = 0;
					pCallable->ReceiveData(sessionId,0, dummyData);
				}
			}
		}
		return true;
	}
	X::Value AgentGraph::RunInputs(int sid, X::Value& inputs)
	{
		m_locker.Lock();
		if (!m_running)
		{
			m_locker.Unlock();
			RunAllCallables();
		}
		else
		{
			m_locker.Unlock();
		}
		//find start nodes from m_connections, start node is the node that has no input
		std::vector<int> startNodes;
		for (int i = 0; i < m_callables.size(); i++)
		{
			bool isStartNode = true;
			for (const auto& connection : m_connections)
			{
				if (connection.toCallableIndex == i)
				{
					isStartNode = false;
					break;
				}
			}
			if (isStartNode)
			{
				startNodes.push_back(i);
			}
		}
		//call each with ReceiveData,if it has multiple inputs, pass for each input from inputs

		for (const auto& startNode : startNodes)
		{
			X::Value dummyData;
			auto* pCallable = m_callables[startNode].callable;
			if (pCallable)
			{
				pCallable->SetRT(xMind::MindAPISet::I().RT());
				pCallable->ReceiveData(sid, 0, inputs);
			}
		}
		//Find all agent output pin without connection, and wait for its outputs
		std::vector<int> endNodes;
		for (int i = 0; i < m_callables.size(); i++)
		{
			bool isEndNode = true;
			for (const auto& connection : m_connections)
			{
				if (connection.fromCallableIndex == i)
				{
					isEndNode = false;
					break;
				}
			}
			if (isEndNode)
			{
				endNodes.push_back(i);
			}
		}
		//check sessionData for this SessionID
		{
			std::unique_lock<std::mutex> lock(m_sessionDataMutex);
			auto it = m_sessionData.find(sid);
			if (it != m_sessionData.end())
			{
				if (it->second.size() > 0)
				{
					X::Value retData = it->second[0].data;
					it->second.erase(it->second.begin());
					return retData;
				}
			}
			m_sessionDataCondVar.wait(lock, [this, sid] {
				auto it = m_sessionData.find(sid);
				if (it != m_sessionData.end())
				{
					return it->second.size() > 0;
				}
				return false;
				});
			it = m_sessionData.find(sid);
			if (it != m_sessionData.end())
			{
				if (it->second.size() > 0)
				{
					X::Value retData = it->second[0].data;
					it->second.erase(it->second.begin());
					return retData;
				}
			}
		}

		return X::Value();
	}
	void AgentGraph::RunAllCallables(X::XRuntime* rt0)
	{
		auto rt = (rt0==nullptr)?xMind::MindAPISet::I().RT():rt0;
		m_locker.Lock();
		m_running = true;
		//call run all Callables
		for (auto& info : m_callables)
		{
			info.callable->SetRT(rt);
			info.callable->Run();
		}
		m_locker.Unlock();
	}
	void AgentGraph::AddSessionData(int sessionId, Callable* pCallable,int outputIndex,X::Value& data)
	{
		std::lock_guard<std::mutex> lock(m_sessionDataMutex);
		m_sessionData[sessionId].push_back(SessionData{ pCallable ,outputIndex ,data});
		m_sessionDataCondVar.notify_all();
	}
}