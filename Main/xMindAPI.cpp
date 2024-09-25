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

#include "xMindAPI.h"
#include "PropScope.h"
#include "Session.h"

namespace xMind
{
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
}