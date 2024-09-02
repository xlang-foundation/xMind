#pragma once
#include "BufferedProcessor.h"
namespace xMind
{
    class BaseAction : public BufferedProcessor
    {
        BEGIN_PACKAGE(BaseAction)
            ADD_BASE(BufferedProcessor);
        END_PACKAGE
        
        inline virtual X::Value GetOwner() override
        {
            auto* pXPack = BaseAction::APISET().GetProxy(this);
            return X::Value(pXPack);
        }
    public:
        BaseAction()
        {
        }

        virtual ~BaseAction()
        {
        }
        virtual X::Value Clone() override
        {
            BaseAction* pAction = new BaseAction();
            pAction->Copy(this);
            auto* pXPack = BaseAction::APISET().GetProxy(pAction);
            X::Value retValue = X::Value(pXPack);
            return retValue;
        }
    };
}
