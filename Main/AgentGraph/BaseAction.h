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
