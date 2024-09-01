#include "xMindAPI.h"
#include "PropScope.h"

namespace xMind
{
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