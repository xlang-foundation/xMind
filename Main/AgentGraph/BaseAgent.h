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
#include "LlmPool.h"
namespace xMind
{
    class BaseAgent: public BufferedProcessor
    {
        BEGIN_PACKAGE(BaseAgent)
            ADD_BASE(BufferedProcessor);
            APISET().AddProp0("prompts", &BaseAgent::m_prompts);
            APISET().AddFunc<2>("addPrompt", &BaseAgent::AddPrompt);
            APISET().AddVarFunc("llm", &BaseAgent::RunLlm);
        END_PACKAGE

        inline virtual X::Value GetOwner() override
        {
            auto* pXPack = BaseAgent::APISET().GetProxy(this);
            return X::Value(pXPack);
        }
    public:
        BaseAgent():
			BufferedProcessor()
        {
			m_RunInThread = true;
        }

        virtual ~BaseAgent()
        {
        }
		virtual X::Value RunOnce() override
		{
            if (m_implObject.IsValid())
            {
				return BufferedProcessor::RunOnce();
            }
			//we are agent, if no process function, we still use input as prompts,
            //combine other prompts to llm
			//and return result from llm inference
			std::vector<std::string> llmSelections;
			for (auto selection : *m_selections)
			{
				llmSelections.push_back(selection.ToString());
			}
			X::ARGS params(0);
			X::KWARGS kwParams;
			X::Value varData;

			X::Value retValue;
			int data_SessionId = 0;
			if (WaitInput(nullptr, nullptr, params, kwParams, varData))
			{
				X::List prompts;
				for (auto prompt : *m_prompts)
				{
					prompts += prompt;
				}
				if (varData.IsList() && varData.Size()==4)
				{
					Status status = (Status)(int)varData.GetItemValue(0);
					if (status == Status::Ok)
					{
						X::Value data = varData[3];
						data_SessionId = (int)varData[1];
						if (data.IsList())
						{
							X::List listData = data;
							for (auto prompt : *listData)
							{
								prompts += prompt;
							}
						}
						else if (data.IsDict())
						{
							prompts += data;
						}
						else
						{
							std::string strData = data.ToString();
							X::Dict dictPrompt;
							dictPrompt->Set("role", "user");
							dictPrompt->Set("content", strData);
							prompts += dictPrompt;
						}
					}
				}

				retValue = LlmPool::I().RunTask(m_model, prompts, m_temperature, llmSelections);
				if (retValue.IsObject() && retValue.GetObj()->GetType() == X::ObjType::Error)
				{
					return retValue;
				}
				//Need parse the result
				//then push to next node
				PushToOutput(data_SessionId,0, retValue);
			}
			return retValue;
		}
        virtual X::Value Clone() override
        {
			BaseAgent* pAgent = new BaseAgent();
			pAgent->Copy(this);
			auto* pXPack = BaseAgent::APISET().GetProxy(pAgent);
			X::Value retValue = X::Value(pXPack);
			return retValue;
		}
        void AddPrompt(const std::string& role, const std::string& content)
        {
			X::Dict prompt;
            prompt->Set("role", (std::string&)role);
			prompt->Set("content", (std::string&) content);
			m_prompts += prompt;
        }
        bool RunLlm(X::XRuntime* rt, X::XObj* pContext,
            X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue)
        {
            std::vector<std::string> llmSelections;
			std::string model = m_model;
            double temperature = m_temperature;
			auto it = kwParams.find("model");
			if (it)
			{
				model = it->val.ToString();
			}
			it = kwParams.find("selections");
			X::List selections(it?it->val:m_selections);
			for (auto selection : *selections)
			{
				llmSelections.push_back(selection.ToString());
			}
			it = kwParams.find("temperature");
			if (it)
			{
				temperature = (double)it->val;
			}
			retValue = LlmPool::I().RunTask(model, m_prompts, temperature, llmSelections);
            return true;
        }
		inline void SetTemperature(double temperature)
		{
			m_temperature = temperature;
		}
		inline double GetTemperature()
		{
			return m_temperature;
		}
		inline void SetSelections(const X::List& selections)
		{
			m_selections = selections;
		}
		inline X::List GetSelections()
		{
			return m_selections;
		}
		inline void SetModel(const std::string& model)
		{
			m_model = model;
		}
		inline std::string GetModel()
		{
			return m_model;
		}
	private:
		X::List m_prompts;
		double m_temperature = 0.7;
		X::List m_selections;
		std::string m_model;
    };
}
