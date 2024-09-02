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
		X::Value data;
	};

	class BufferedProcessor : public Callable
	{
		BEGIN_PACKAGE(BufferedProcessor)
			ADD_BASE(Callable);
			APISET().AddFunc<2>("waitInput", &BufferedProcessor::WaitInput);
			APISET().AddFunc<1>("isRunning", &BufferedProcessor::IsRunning);
			APISET().AddFunc<2>("pushToOutput", &Callable::PushToOutput);
		END_PACKAGE
	protected:
		inline virtual bool ReceiveData(int inputIndex, X::Value& data) override
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			if(inputIndex < 0 || inputIndex >= static_cast<int>(m_inputQueues.size()))
			{
				return false;
			}
			m_inputQueues[inputIndex].push(data);
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
				m_inputQueues.resize(m_inputs.size());
			}
			it = m_params.find("outputs");
			if (it)
			{
				SetOutputs(it->val);
			}

			return true;
		}
		int IsRunning(int timeout = 0)
		{
			std::unique_lock<std::mutex> lock(m_mutex);

			if (timeout == 0)
			{
				return int(m_running ? Status::Running : Status::Stopped);
			}
			else
			{
				bool bRun = m_condVar.wait_for(lock, std::chrono::milliseconds(timeout), [this] {
					return m_running?true:false;
					});
				return int(bRun ? Status::Running : Status::Stopped);
			}
		}
		X::Value WaitInput(int inputIndex = -1, int timeoutMs = -1)
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			InputData inputData{ Status::Ok, inputIndex, X::Value() };

			auto waitCondition = [this, inputIndex] {
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

			bool hasData = false;
			if (timeoutMs == -1)
			{
				m_condVar.wait(lock, waitCondition);
				hasData = true;
			}
			else
			{
				hasData = m_condVar.wait_for(lock, std::chrono::milliseconds(timeoutMs), waitCondition);
			}

			if (hasData)
			{
				if (inputIndex == -1)
				{
					for (size_t i = 0; i < m_inputQueues.size(); ++i)
					{
						if (!m_inputQueues[i].empty())
						{
							inputData.inputIndex = static_cast<int>(i);
							inputData.data = m_inputQueues[i].front();
							m_inputQueues[i].pop();
							break;
						}
					}
				}
				else
				{
					inputData.data = m_inputQueues[inputIndex].front();
					m_inputQueues[inputIndex].pop();
				}
			}
			else
			{
				inputData.status = Status::Timeout;
			}

			X::List list;
			list += (int)inputData.status;
			list += inputData.inputIndex;
			list += inputData.data;
			return list;//CreateXlangStructInputData(inputData);
		}

		inline virtual void Stop() override
		{
			m_running = false;
			m_condVar.notify_all();
			if (m_thread.joinable())
			{
				m_thread.join();
			}
		}

		virtual bool Run() override
		{
			m_running = true;
			m_thread = std::thread(&BufferedProcessor::ThreadRun, this);
			return true;
		}

		void ThreadRun()
		{
			X::KWARGS kwParams;
			X::Value varOwner = GetOwner();
			kwParams.Add("owner", varOwner);

			//while (m_running)
			{
				X::ARGS params(0);
				X::Value retData = m_implObject.ObjCall(params, kwParams);
			}
		}

		inline X::Value DequeueInput(int inputIndex)
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			X::Value retVal;
			auto& q = m_inputQueues[inputIndex];
			retVal = q.front();
			q.pop();
			return retVal;
		}

	protected:
		AgentGroup* m_group;
		std::vector<std::queue<X::Value>> m_inputQueues;
		std::thread m_thread;
		std::atomic<bool> m_running;
		std::mutex m_mutex;
		std::condition_variable m_condVar;
	};
}
