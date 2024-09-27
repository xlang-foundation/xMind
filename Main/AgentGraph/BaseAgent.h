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
#include "BufferedProcessor.h"
#include "LlmPool.h"
#include "log.h"
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
			LOG5 << "Agent: " << m_instanceName << " WaitInput" <<LINE_END;
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
				LOG5 << "Agent: "<< m_instanceName << " Have prompts:"<< prompts.ToString() << LINE_END;
				retValue = LlmPool::I().RunTask(m_model, prompts, m_temperature, llmSelections);
				if (retValue.IsObject() && retValue.GetObj()->GetType() == X::ObjType::Error)
				{
					LOG5 << "Agent: " << m_instanceName << "Got Error" << LINE_END;
					return retValue;
				}
				LOG5 << "Agent: " << m_instanceName << " Results:" << retValue.ToString() << LINE_END;
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
			X::List prompts = m_prompts;
			if (params.size() > 0)
			{
				std::string strData = params[0].ToString();
				X::Dict dictPrompt;
				dictPrompt->Set("role", "user");
				dictPrompt->Set("content", strData);
				prompts += dictPrompt;
			}
			retValue = LlmPool::I().RunTask(model, prompts, temperature, llmSelections);
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
