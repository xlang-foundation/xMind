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
						AddConnection(fromInstanceName.ToString(), fromPinName.ToString(), toInstanceName.ToString(), toPinName.ToString());
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

	void AgentGraph::AddConnection(const std::string& fromInstanceName, const std::string& fromPinName, const std::string& toInstanceName, const std::string& toPinName)
	{
		AutoLock lock(m_locker); // Lock for thread safety

		int fromCallableIndex = m_callableNameToIndex[fromInstanceName];
		int fromPinIndex = m_callables[fromCallableIndex]->GetOutputIndex(fromPinName);
		int toCallableIndex = m_callableNameToIndex[toInstanceName];
		int toPinIndex = m_callables[toCallableIndex]->GetInputIndex(toPinName);

		Connection newConnection{ fromCallableIndex, fromPinIndex, toCallableIndex, toPinIndex };
		m_connections.push_back(newConnection);
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

		int fromCallableIndex = fromCallable->GetIndex();

		for (const auto& connection : m_connections)
		{
			if (connection.fromCallableIndex == fromCallableIndex && connection.fromPinIndex == outputIndex)
			{
				Callable* toCallable = m_callables[connection.toCallableIndex];
				toCallable->ReceiveData(connection.toPinIndex, data);
			}
		}
	}
}