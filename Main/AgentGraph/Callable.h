#pragma once
#include <queue>
#include <vector>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <regex>
#include <fstream>
#include <iostream>
#include <algorithm>
#include "xpackage.h"
#include "Locker.h"

namespace xMind
{
	enum class CallableType
	{
		callable, function, action, agent, compositeAgent
	};
	struct Pin
	{
		std::string name;
		std::vector<std::string> formats;
	};
	class AgentGraph;
	class Callable
	{
		BEGIN_PACKAGE(Callable)
			APISET().AddPropWithType<std::string>("name", &Callable::m_name);
			APISET().AddPropWithType<X::Value>("graph", &Callable::m_varGraph);
			APISET().AddPropWithType<std::string>("description", &Callable::m_description);
			APISET().AddPropL("inputs",
				[](auto* pThis, X::Value v) { pThis->SetInputs(v); },
				[](auto* pThis) { return pThis->GetInputs(); });
			APISET().AddPropL("outputs",
				[](auto* pThis, X::Value v) { pThis->SetOutputs(v); },
				[](auto* pThis) { return pThis->GetOutputs(); });
		END_PACKAGE

	protected:
		//real object such as Xlang func/class
		//or native lib's function
		X::Value m_implObject;
		X::KWARGS m_params;
		X::XRuntime* m_rt;
		int m_index;//index in the agentGraph
		X::Value m_varGraph;
		AgentGraph* m_agentGraph;
		std::string m_name; // Callable Name
		std::string m_instanceName;
		std::string m_description;
		CallableType m_type;

		std::vector<Pin> m_inputs;
		std::vector<Pin> m_outputs;
		Locker m_locker;

		void Copy(Callable* other);
	public:
		Callable() :
			m_type(CallableType::callable),
			m_agentGraph(nullptr),
			m_index(-1)
		{
		}

		virtual ~Callable()
		{
		}
		inline void SetImplObject(X::Value implObject)
		{
			m_implObject = implObject;
		}
		inline void SetName(const std::string& name)
		{
			m_name = name;
		}
		inline X::XRuntime* GetRT()
		{
			return m_rt;
		}
		inline void SetRT(X::XRuntime* rt)
		{
			m_rt = rt;
			if (m_implObject.IsObject())
			{
				m_implObject.GetObj()->SetRT(rt);
			}
		}
		inline std::string GetName()
		{
			return m_name;
		}
		inline void SetParams(X::KWARGS params)
		{
			m_params = params;
		}
		inline void SetInstanceName(const std::string& instanceName)
		{
			m_instanceName = instanceName;
		}
		inline std::string GetInstanceName()
		{
			return m_instanceName.empty()? m_name: m_instanceName;
		}
		inline void SetDescription(const std::string& description)
		{
			m_description = description;
		}
		inline std::string GetDescription()
		{
			return m_description;
		}
		inline void SetIndex(int index)
		{
			m_index = index;
		}
		inline int GetIndex()
		{
			return m_index;
		}
		void PushToOutput(int outputIndex, X::Value data);
		virtual bool ReceiveData(int inputIndex, X::Value& data) = 0;
		virtual bool Run() = 0;
		virtual X::Value Clone() = 0;

		int GetOutputIndex(const std::string& pinName)
		{
			m_locker.Lock();
			for (size_t i = 0; i < m_outputs.size(); ++i)
			{
				if (m_outputs[i].name == pinName)
				{
					m_locker.Unlock();
					return (int)i;
				}
			}
			m_locker.Unlock();
			return -1;
		}
		int GetInputIndex(const std::string& pinName)
		{
			m_locker.Lock();
			for (size_t i = 0; i < m_inputs.size(); ++i)
			{
				if (m_inputs[i].name == pinName)
				{
					m_locker.Unlock();
					return (int)i;
				}
			}
			m_locker.Unlock();
			return -1;
		}
		inline bool InGraph()
		{
			return m_agentGraph != nullptr;
		}
		void SetAgentGraph(AgentGraph* agentGraph);
		inline X::Value GetGraph()
		{
			return m_varGraph;
		}
		inline X::Value GetInputs()
		{
			m_locker.Lock();
			X::List list;
			for (auto& pin : m_inputs)
			{
				X::Dict dict;
				dict["name"] = pin.name;
				X::List formats;
				for (auto& format : pin.formats)
				{
					formats += format;
				}
				dict["formats"] = formats;
				list += dict;
			}
			m_locker.Unlock();
			return list;
		}

		inline void SetInputs(X::Value v)
		{
			m_locker.Lock();
			X::List list(v);
			m_inputs.clear();
			for (size_t i = 0; i < (size_t)list.Size(); ++i)
			{
				auto& item = list[i];
				Pin pin;
				pin.name = item["name"].ToString();
				X::Value varFormats = item["formats"];
				if (varFormats.IsList())
				{
					X::List formats(varFormats);
					for (auto& format : *formats)
					{
						pin.formats.push_back(format.ToString());
					}
				}
				else
				{
					pin.formats.push_back(varFormats.ToString());
				}
				m_inputs.push_back(pin);
			}
			m_locker.Unlock();
		}

		inline X::Value GetOutputs()
		{
			m_locker.Lock();
			X::List list;
			for (auto& pin : m_outputs)
			{
				X::Dict dict;
				dict["name"] = pin.name;
				X::List formats;
				for (auto& format : pin.formats)
				{
					formats += format;
				}
				dict["formats"] = formats;
				list += dict;
			}
			m_locker.Unlock();
			return list;
		}

		inline void SetOutputs(X::Value v)
		{
			m_locker.Lock();
			X::List list(v);
			m_outputs.clear();
			for (auto& item : *list)
			{
				Pin pin;
				pin.name = item["name"].ToString();
				X::Value varFormats = item["formats"];
				if (varFormats.IsList())
				{
					X::List formats(varFormats);
					for (auto& format : *formats)
					{
						pin.formats.push_back(format.ToString());
					}
				}
				else
				{
					pin.formats.push_back(varFormats.ToString());
				}
				m_outputs.push_back(pin);
			}
			m_locker.Unlock();
		}
	};
}
