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

#include "xMindAPI.h"
#include "PropScope.h"
#include "Session.h"
#include "utility.h"
#include "start.h"

namespace xMind
{
    bool MindAPISet::AddRootAgent(std::string agentFile)
    {
		return  Starter::I().AddRootAgent(agentFile);
    }
    bool MindAPISet::RemoveRootAgent(std::string aggentFile)
    {
		return Starter::I().RemoveRootAgent(aggentFile);
    }
    bool MindAPISet::AddGraph(X::XRuntime* rt, X::XObj* pContext,
        X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue)
    {
        if (params.size() < 2)
        {
            retValue = false;
            return true;
        }
        int moduleId = (int)params[0];
        X::Value graph = params[1];
        if (!graph.IsObject())
        {
            retValue = false;
            return true;
        }
        if (params.size() > 2)
        {
            std::string graphName = params[2].ToString();
            //TODO::
        }
		NodeManager::I().AddGraph(moduleId,graph);
		retValue = true;
        return true;
    }

	//Call as xMind.AddNode(moduleName, callableName, callable)
    bool MindAPISet::AddNode(X::XRuntime* rt, X::XObj* pContext, 
        X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue)
    {
		if (params.size() < 3)
		{
			retValue = false;
			return true;
		}
		std::string moduleName = params[0].ToString();
		std::string callableName = params[1].ToString();
		X::Value callable = params[2];
        if (!callable.IsObject())
        {
            retValue = false;
            return true;
        }
        X::Value node;
        if (callable.GetObj()->GetType() == X::ObjType::RemoteObject)
        {
            X::Value nativeObj;
            if (X::g_pXHost->ExtractNativeObjectFromRemoteObject(callable, nativeObj))
            {
                node = nativeObj;
            }
        }
        if (node.IsInvalid())
        {
            node = callable;
        }
        std::string moduleFilePath;
		auto it = kwParams.find("moduleFilePath");
		if (it)
		{
			moduleFilePath = it->val.ToString();
		}
		NodeManager::I().addCallable(moduleName, moduleFilePath,callableName, node);
        return true;
    }
    bool MindAPISet::AddNodeWithModuleId(X::XRuntime* rt, X::XObj* pContext, 
        X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue)
    {
        if (params.size() < 3)
        {
            retValue = false;
            return true;
        }
        int  moduleId = (int)params[0];
        std::string callableName = params[1].ToString();
        X::Value callable = params[2];
        if (!callable.IsObject())
        {
            retValue = false;
            return true;
        }
        X::Value node;
        if (callable.GetObj()->GetType() == X::ObjType::RemoteObject)
        {
            X::Value nativeObj;
            if (X::g_pXHost->ExtractNativeObjectFromRemoteObject(callable, nativeObj))
            {
                node = nativeObj;
            }
        }
        if (node.IsInvalid())
        {
            node = callable;
        }
        NodeManager::I().addCallable(moduleId, callableName, node);
        return true;

    }
    bool MindAPISet::ChatRequest(X::XRuntime* rt, X::XObj* pContext,
        X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue)
    {
        if (params.size() < 1)
        {
            retValue = false;
			return false;
        }
        retValue = SessionManager::I().HandleChatRequest(params[0]);
        return true;
    }
    bool MindAPISet::CompletionRequest(X::XRuntime* rt, X::XObj* pContext, 
        X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue)
    {
        if (params.size() < 1)
        {
            retValue = false;
            return false;
        }
		retValue = SessionManager::I().HandleCompletionRequest(params[0]);
		return true;
    }
    bool MindAPISet::Start()
    {
        m_defaultRuntime = new X::Runtime();
        return true;
    }
    void MindAPISet::Shutdown()
    {
        if (m_defaultRuntime)
        {
            delete m_defaultRuntime;
        }
    }
    void MindAPISet::CalcDecoratorParameter(X::ARGS& expres, X::KWARGS* pKwParams)
    {
        PropScope scope(pKwParams);
        X::g_pXHost->CreateScopeWrapper(&scope);
        for(auto& varExpr : expres)
		{
            X::g_pXHost->SetExpressionScope(&scope, varExpr);
            X::Value result;
            if (X::g_pXHost->RunExpression(varExpr, result))
			{
			}
		}
    }
    X::Value MindAPISet::QueryRootAgentFlowStatus(std::string rootAgentName, 
        std::string sessionId)
    {
        std::string blueprintName = Starter::I().GetRootAgentBlueprintFileName(rootAgentName);
        if (blueprintName == "")
        {
            return X::Value();
        }
        //just make sure load it
        X::Value graph = Starter::I().GetOrCreateRunningGraph(rootAgentName);
		if (graph.IsInvalid())
		{
            return X::Value();
		}
        SESSION_ID sid= SessionManager::I().getSessionId(sessionId);
        sid = GetShortSessionID(sid);
		X::XPackageValue<AgentGraph> packGraph(graph);
        AgentGraph* pGraph = packGraph.GetRealObj();
		X::Value status = pGraph->GetStatus(sid);
        return status;
    }
}