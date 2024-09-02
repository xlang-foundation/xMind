#include "AgentGraph.h"
#include <iostream>
#include "xlang.h"
#include "xMindAPI.h"
#include "BaseAgent.h"

namespace xMind
{
	AgentGraph::~AgentGraph()
	{
		for (auto callable : m_callables)
		{
			delete callable;
		}
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
		AddCallable(callable);
		return true;
	}

	bool AgentGraph::Parse(const std::string& yamlContent)
	{
		try
		{
			AutoLock lock(m_locker); // Lock for thread safety

			// Load the YAML content
			X::Package yaml(MindAPISet::I().RT(), "yaml");
			X::Value configRoot = yaml["loads"](yamlContent);

			// Parse the "name" field
			auto name = configRoot.Query("name");
			if (name.IsValid())
			{
				std::string nameStr = name.ToString();
				std::cout << "Name: " << nameStr << std::endl;
			}

			// Parse the "nodes" list
			auto nodes = configRoot.Query("nodes");
			if (nodes.IsValid())
			{
				X::List nodeList(nodes);
				for (auto& node : *nodeList)
				{
					auto newAgent = new BaseAgent();
					auto nodeName = node.Query("name");
					auto instanceName = node.Query("instanceName");

					if (nodeName.IsValid())
					{
						newAgent->SetName(nodeName.ToString());
					}

					if (instanceName.IsValid())
					{
						newAgent->SetInstanceName(instanceName.ToString());
					}
					else
					{
						newAgent->SetInstanceName(nodeName.ToString());
					}

					auto inputs = node.Query("inputs");
					if (inputs.IsValid())
					{
						newAgent->SetInputs(inputs);
					}

					auto outputs = node.Query("outputs");
					if (outputs.IsValid())
					{
						newAgent->SetOutputs(outputs);
					}

					auto parameters = node.Query("parameters");
					if (parameters.IsValid())
					{
						X::Dict paramDict(parameters);
						for (auto& param : *paramDict)
						{
							//newAgent->AddParameter(param.first.ToString(), param.second.ToString());
						}
					}

					auto group = node.Query("group");
					if (group.IsValid())
					{
						//newAgent->SetGroup(group.ToString());
					}

					int index = AddCallable(newAgent);
					newAgent->SetAgentGraph(this);
				}
			}

			// Parse the "connections" list
			auto connections = configRoot.Query("connections");
			if (connections.IsValid())
			{
				X::List connectionList(connections);
				for (auto& connection : *connectionList)
				{
					auto fromInstanceName = connection.Query("fromInstanceName");
					auto fromPinName = connection.Query("fromPinName");
					auto toInstanceName = connection.Query("toInstanceName");
					auto toPinName = connection.Query("toPinName");

					if (fromInstanceName.IsValid() && fromPinName.IsValid() && toInstanceName.IsValid() && toPinName.IsValid())
					{
						X::ARGS params(4);
						params.push_back(fromInstanceName.ToString());
						params.push_back(fromPinName.ToString());
						params.push_back(toInstanceName.ToString());
						params.push_back(toPinName.ToString());
						X::KWARGS kwParams;
						X::Value retValue;
						AddConnection(nullptr,nullptr,params, kwParams,retValue);
					}
				}
			}

			return true;
		}
		catch (const std::exception& e)
		{
			std::cerr << "Error parsing YAML: " << e.what() << std::endl;
			return false;
		}
	}

	int AgentGraph::AddCallable(Callable* callable)
	{
		AutoLock lock(m_locker); // Lock for thread safety

		int index = (int)m_callables.size();
		m_callableNameToIndex[callable->GetInstanceName()] = index;
		m_callables.push_back(callable);
		callable->SetAgentGraph(this);
		callable->SetIndex(index);
		return index;
	}

	void AgentGraph::RemoveCallable(int index)
	{
		AutoLock lock(m_locker); // Lock for thread safety

		if (index >= 0 && index < m_callables.size())
		{
			delete m_callables[index];
			m_callables[index] = nullptr;
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
				fromPinIndex = m_callables[fromCallableIndex]->GetOutputIndex(fromPinName);
			}
			it = m_callableNameToIndex.find(toInstanceName);
			if (it != m_callableNameToIndex.end())
			{
				toCallableIndex = it->second;
				toPinIndex = m_callables[toCallableIndex]->GetInputIndex(toPinName);
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
		int fromPinIndex = m_callables[fromCallableIndex]->GetOutputIndex(fromPinName);
		int toCallableIndex = m_callableNameToIndex[toInstanceName];
		int toPinIndex = m_callables[toCallableIndex]->GetInputIndex(toPinName);

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

	void AgentGraph::PushDataToCallable(Callable* fromCallable, int outputIndex, X::Value& data)
	{
		AutoLock lock(m_locker);
		if (!m_running)
		{
			return;
		}
		int fromCallableIndex = fromCallable->GetIndex();

		for (const auto& connection : m_connections)
		{
			if (connection.fromCallableIndex == fromCallableIndex && connection.fromPinIndex == outputIndex)
			{
				Callable* toCallable = m_callables[connection.toCallableIndex];
				toCallable->SetRT(fromCallable->GetRT());
				toCallable->ReceiveData(connection.toPinIndex, data);
			}
		}
	}
	bool AgentGraph::Run(X::XRuntime* rt, X::XObj* pContext, 
		X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue)
	{
		m_locker.Lock();
		m_running = true;
		//call run all Callables
		for (auto& callable : m_callables)
		{
			callable->SetRT(rt);
			callable->Run();
		}
		m_locker.Unlock();
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
			auto* pCallable = m_callables[startNode];
			if (pCallable)
			{
				pCallable->SetRT(rt);
				pCallable->ReceiveData(0, dummyData);
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
				auto* pCallable = m_callables[it->second];
				if (pCallable)
				{
					pCallable->SetRT(rt);
					pCallable->ReceiveData(0, dummyData);
				}
			}
		}
		return true;
	}
}