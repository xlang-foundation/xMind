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

#pragma once
#include "BaseAgent.h"
#include <vector>
#include <string>

namespace xMind
{
    class PlanningAgent : public BaseAgent
    {
        BEGIN_PACKAGE(PlanningAgent)
            ADD_BASE(BaseAgent);
        END_PACKAGE

    public:
        PlanningAgent() : BaseAgent()
        {
        }

        virtual ~PlanningAgent()
        {
        }

        // Override RunOnce to call base class's RunOnce
        virtual X::Value RunOnce() override;

    private:
        X::Value IterateGraph(const X::Value& varTaskInputs, 
            SESSION_ID& data_SessionId, std::vector<std::string>& llmSelections);
    };
}
