#include "Callable.h"
#include "AgentGraph.h"

namespace xMind
{
	void Callable::Copy(Callable* other)
	{
		m_implObject = other->m_implObject;
		m_params = other->m_params;
		m_rt = other->m_rt;
		m_agentGraph = other->m_agentGraph;
		auto* pXPack = AgentGraph::APISET().GetProxy(m_agentGraph);
		m_varGraph = X::Value(pXPack);
		m_name = other->m_name;
		m_description = other->m_description;
		m_type = other->m_type;
		m_inputs = other->m_inputs;
		m_outputs = other->m_outputs;
	}
	void Callable::SetAgentGraph(AgentGraph* agentGraph)
	{
		m_agentGraph = agentGraph;
		auto* pXPack = AgentGraph::APISET().GetProxy(m_agentGraph);
		m_varGraph = X::Value(pXPack);
	}
	void Callable::PushToOutput(int outputIndex, X::Value data)
	{
		if (m_agentGraph)
		{
			m_agentGraph->PushDataToCallable(this, outputIndex, data);
		}
	}
}