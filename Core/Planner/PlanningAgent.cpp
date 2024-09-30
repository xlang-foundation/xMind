/*
Copyright (C) 2024 The XLang Foundation

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "PlanningAgent.h"
#include "AgentGraphParser.h"

namespace xMind
{
    X::Value PlanningAgent::RunOnce()
    {
        std::vector<std::string> llmSelections;
        for (auto selection : *m_selections)
        {
            llmSelections.push_back(selection.ToString());
        }
        X::ARGS params(0);
        X::KWARGS kwParams;
        X::Value varData;
        X::Value retValue;
        SESSION_ID data_SessionId = 0;
        LOG5 << "PlanningAgent: " << m_instanceName << " WaitInputs" << LINE_END;
        if (WaitInputs(nullptr, nullptr, params, kwParams, varData))
        {
			retValue = IterateGraph(varData, data_SessionId,llmSelections);
            std::string strRet = retValue.ToString();
            ProcessLLMResult(data_SessionId, strRet);
            retValue = X::Value(strRet);
        }
        return retValue;
    }
    X::Value PlanningAgent::IterateGraph(const X::Value& varTaskInputs, 
        SESSION_ID& data_SessionId, std::vector<std::string>& llmSelections)
    {
		X::List prevPrompts;
		unsigned long iterationNumber = 0;
        while (iterationNumber < m_iterationLimit)
        {
            X::List prompts = prevPrompts;
            data_SessionId = BuildPrompts((X::Value&)varTaskInputs, prompts);
            LOG5 << "Agent: " << m_instanceName << " Have prompts:" << prompts.ToString() << LINE_END;
            X::Value retLLM = LlmPool::I().RunTask(m_model, prompts, m_temperature, llmSelections);
            if (retLLM.IsObject() && retLLM.GetObj()->GetType() == X::ObjType::Error)
            {
                LOG5 << "PlanningAgent: " << m_instanceName << " Got Error" << LINE_END;
                return retLLM;
            }
            std::string strGraphDesc = retLLM.ToString();
            std::string strModuleName = "PlanningAgent";
            Parser parser;
            X::Value graph;
            AgentGraph* pGraph = nullptr;
            bool bValid = parser.ParseAgentGraphDescFromString(graph, strGraphDesc,
                strModuleName, strModuleName);
            if (bValid)
            {
                X::XPackageValue<AgentGraph> packGraph(graph);
                pGraph = packGraph.GetRealObj();
                if (pGraph != nullptr)
                {
                    bValid = pGraph->AreAllNodesConnected();
                    if (bValid)
                    {
                        //check if at least one input pin is not connected
                        //then can get data injected from graph
                        auto graphInputPins = pGraph->GetNodesWithUnconnectedInputPins();
                        bValid = graphInputPins.size() > 0;
                        if (bValid)
                        {
                            //then have to be has at least one terminal node with outputs
                            //then we can fetch the data from this node
                            bValid = pGraph->HasTerminalNodeWithOutputs();
                        }
                    }
                }
                else
                {
                    bValid = false;
                }
            }

            if (bValid && pGraph)
            {
                X::Value varInputs;//TODO: get inputs from varTaskInputs
                X::Value retMsg = pGraph->RunInputs(data_SessionId, varInputs);
				if (retMsg.IsObject() && retMsg.GetObj()->GetType() != X::ObjType::Error)
				{
					LOG5 << "PlanningAgent: " << m_instanceName << " Got Error" << LINE_END;
					return retMsg;
				}
            }
            else
            {
				//Add more prompts to constraint the LLM to make change for a valid graph
                //into prevPrompts
                iterationNumber++;
            }
		}//End of while
		return X::Value();
	}//End of IterateGraph
}