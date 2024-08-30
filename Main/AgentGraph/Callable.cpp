#include "Callable.h"
#include "AgentGraph.h"

namespace xMind
{
	void Callable::PushToOutput(int outputIndex, X::Value data)
	{
		if (m_agentGraph)
		{
			m_agentGraph->PushDataToCallable(this, outputIndex, data);
		}
	}
}