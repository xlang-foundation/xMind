/*
Copyright (C) 2024 The XLang Foundation

This file is part of the xMind Project.

xMind is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

xMind is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with xMind. If not, see <https://www.gnu.org/licenses/>.
*/

#include "Callable.h"
#include "AgentGraph.h"
#include "xMindAPI.h"

namespace xMind
{
	std::atomic<unsigned long long> Callable::s_idCounter{ 0 };
	void Callable::Copy(Callable* other)
	{
		m_implObject = other->m_implObject;
		m_params = other->m_params;
		m_rt = other->m_rt;
		m_agentGraph = other->m_agentGraph;
		if (m_agentGraph)
		{
			auto* pXPack = AgentGraph::APISET().GetProxy(m_agentGraph);
			m_varGraph = X::Value(pXPack);
		}
		m_name = other->m_name;
		m_description = other->m_description;
		m_type = other->m_type;
		m_inputs = other->m_inputs;
		m_outputs = other->m_outputs;
	}
	void Callable::PushEvent(int inputIndex, X::Value data)
	{
		MindAPISet::I().PushEvent(m_ID, inputIndex, data);
	}
	void Callable::SetAgentGraph(AgentGraph* agentGraph)
	{
		m_agentGraph = agentGraph;
		auto* pXPack = AgentGraph::APISET().GetProxy(m_agentGraph);
		m_varGraph = X::Value(pXPack);
	}
	void Callable::PushToOutput(int sessionId,int outputIndex, X::Value data)
	{
		if (m_agentGraph)
		{
			m_agentGraph->PushDataToCallable(this, sessionId,outputIndex, data);
		}
	}
}