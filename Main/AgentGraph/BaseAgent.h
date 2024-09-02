#pragma once
#include "BufferedProcessor.h"
namespace xMind
{
    class BaseAgent: public BufferedProcessor
    {
        BEGIN_PACKAGE(BaseAgent)
            ADD_BASE(BufferedProcessor);
        END_PACKAGE

        inline virtual X::Value GetOwner() override
        {
            auto* pXPack = BaseAgent::APISET().GetProxy(this);
            return X::Value(pXPack);
        }
    public:
        BaseAgent()
        {
        }

        virtual ~BaseAgent()
        {
        }
        virtual X::Value Clone() override
        {
			BaseAgent* pAgent = new BaseAgent();
			pAgent->Copy(this);
			auto* pXPack = BaseAgent::APISET().GetProxy(pAgent);
			X::Value retValue = X::Value(pXPack);
			return retValue;
		}
    };
}
