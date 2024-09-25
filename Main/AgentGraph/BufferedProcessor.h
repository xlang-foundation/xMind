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
#include <queue>
#include <thread>
#include <vector>
#include <condition_variable>
#include "Callable.h"
#include "type_def.h"

namespace xMind
{
	class AgentGroup;

	struct InputData
	{
		Status status;
		int inputIndex;
		int sessionId;
		X::Value data;
	};
	using SessionValue = std::pair<int, X::Value>;

	class BufferedProcessor : public Callable
	{
		BEGIN_PACKAGE(BufferedProcessor)
			ADD_BASE(Callable);
			APISET().AddVarFunc("waitInput", &BufferedProcessor::WaitInput);
			APISET().AddVarFunc("isRunning", &BufferedProcessor::IsRunning);
			APISET().AddFunc<3>("pushToOutput", &Callable::PushToOutput);
		END_PACKAGE
	protected:
		inline virtual bool ReceiveData(int sessionId, int inputIndex, X::Value& data) override
		{
			PushEvent(inputIndex, X::Value());//just notify if there is subscriber
			std::unique_lock<std::mutex> lock(m_mutex);
			if(inputIndex < 0 || inputIndex >= static_cast<int>(m_inputQueues.size()))
			{
				return false;
			}
			m_inputQueues[inputIndex].push(std::pair(sessionId,data));
			m_condVar.notify_all();
			return true;
		}

		virtual X::Value GetOwner() = 0;

		X::Value CreateXlangStructInputData(InputData& inputData)
		{
			X::Struct xStruct;
			xStruct->addField("status", "int");
			xStruct->addField("inputIndex", "int");
			xStruct->addField("data", "xvalue");
			xStruct->Build();

			InputData* data = (InputData*)xStruct->Data();
			data->status = inputData.status;
			data->inputIndex = inputData.inputIndex;
			data->data = inputData.data;

			return X::Value(xStruct);
		}

	public:
		BufferedProcessor() : m_running(false) {}

		virtual ~BufferedProcessor()
		{
			Stop();
		}
		inline bool Create()
		{
			auto it = m_params.find("inputs");
			if (it)
			{
				SetInputs(it->val);
			}
			it = m_params.find("outputs");
			if (it)
			{
				SetOutputs(it->val);
			}

			return true;
		}
		inline virtual void SetInputs(X::Value v) override
		{
			Callable::SetInputs(v);
			m_inputQueues.resize(m_inputs.size());
		}
		bool IsRunning(X::XRuntime* rt, X::XObj* pContext,
			X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue)
		{
			int timeout = 0;
			if (params.size() > 0)
			{
				timeout = (int)params[0];
			}
			else
			{
				auto it = kwParams.find("timeout");
				if (it)
				{
					timeout = (int)it->val;
				}
			}
			std::unique_lock<std::mutex> lock(m_mutex);

			if (timeout == 0)
			{
				retValue = int(m_running ? Status::Running : Status::Stopped);
			}
			else
			{
				bool bRun = m_condVar.wait_for(lock, std::chrono::milliseconds(timeout), [this] {
					return m_running?true:false;
					});
				retValue = int(bRun ? Status::Running : Status::Stopped);
			}
			return true;
		}
		bool WaitInput(X::XRuntime* rt, X::XObj* pContext,
			X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue)
		{
			int inputIndex = -1;
			int timeoutMs = -1;
			if (params.size() > 0)
			{
				inputIndex = (int)params[0];
			}
			else if (params.size() > 1)
			{
				timeoutMs = (int)params[1];
			}
			else
			{
				auto it = kwParams.find("inputIndex");
				if (it)
				{
					inputIndex = (int)it->val;
				}
				it = kwParams.find("timeout");
				if (it)
				{
					timeoutMs = (int)it->val;
				}
			}
			std::unique_lock<std::mutex> lock(m_mutex);
			InputData inputData{ Status::Ok, inputIndex,0, X::Value() };

			auto waitCondition = [this, inputIndex] {
				if (!m_running)
				{
					return true;
				}
				if (inputIndex == -1)
				{
					for (const auto& queue : m_inputQueues)
					{
						if (!queue.empty())
						{
							return true;
						}
					}
				}
				else if (inputIndex >= 0 && inputIndex < static_cast<int>(m_inputQueues.size()))
				{
					return !m_inputQueues[inputIndex].empty();
				}
				return false;
				};

			bool isNotTimeout = true;
			if (timeoutMs == -1)
			{
				m_condVar.wait(lock, waitCondition);
			}
			else
			{
				//timeout will set isTimeout to false
				isNotTimeout = m_condVar.wait_for(lock, std::chrono::milliseconds(timeoutMs), waitCondition);
			}
			//if still running,whatever timeout or not,we try to get the data
			if (m_running)
			{
				if (inputIndex == -1)
				{
					for (size_t i = 0; i < m_inputQueues.size(); ++i)
					{
						if (!m_inputQueues[i].empty())
						{
							inputData.inputIndex = static_cast<int>(i);
							auto pair = m_inputQueues[i].front();
							inputData.sessionId = pair.first;
							inputData.data = pair.second;
							m_inputQueues[i].pop();
							isNotTimeout = true;
							break;
						}
					}
				}
				else
				{
					auto& q = m_inputQueues[inputIndex];
					if (!q.empty())
					{
						auto pair = m_inputQueues[inputIndex].front();
						inputData.sessionId = pair.first;
						inputData.data = pair.second;
						m_inputQueues[inputIndex].pop();
						isNotTimeout = true;
					}
				}
				inputData.status = isNotTimeout ? Status::Ok : Status::Timeout;
			}
			else
			{
				inputData.status = Status::Stopped;
			}

			X::List list;
			list += (int)inputData.status;
			list += inputData.sessionId;
			list += inputData.inputIndex;
			list += inputData.data;
			//xlang struct has issues to call destructor for members like X::Value
			//so here use list to return the data
			retValue = list;//CreateXlangStructInputData(inputData);
			return true;
		}

		inline virtual void Stop() override
		{
			m_running = false;
			m_condVar.notify_all();
			if (m_implObject.IsObject() && m_thread.joinable())
			{
				m_thread.join();
			}
		}

		virtual bool Run() override
		{
			m_running = true;
			//for python non-threading mode,we don't run in thread
			//it will not pass in m_implObject
			if (m_RunInThread || m_implObject.IsObject())
			{
				m_thread = std::thread(&BufferedProcessor::ThreadRun, this);
			}
			return true;
		}

		void ThreadRun()
		{
			while (m_running)
			{
				X::Value retData = RunOnce();
				if (retData.IsObject() && retData.GetObj()->GetType() == X::ObjType::Error)
				{
					//error happened
					break;
				}
			}
		}
		virtual X::Value RunOnce()
		{
			X::Value retData;
			if (m_implObject.IsValid())
			{
				X::KWARGS kwParams;
				X::Value varOwner = GetOwner();
				kwParams.Add("owner", varOwner);
				X::ARGS params(0);
				retData = m_implObject.ObjCall(params, kwParams);
			}
			return retData;
		}
		inline SessionValue DequeueInput(int inputIndex)
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			SessionValue retVal;
			auto& q = m_inputQueues[inputIndex];
			retVal = q.front();
			q.pop();
			return retVal;
		}

	protected:
		bool m_RunInThread = false;
		AgentGroup* m_group;
		std::vector<std::queue<SessionValue>> m_inputQueues;
		std::thread m_thread;
		std::atomic<bool> m_running;
		std::mutex m_mutex;
		std::condition_variable m_condVar;
	};
}
